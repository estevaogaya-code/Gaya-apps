#!/usr/bin/env bash
# Atualiza o dashboard a partir da branch main do GitHub.
# Uso (no servidor, na pasta do dashboard):  ./deploy.sh
# Não apaga nem altera arquivos de dados (ver .gitignore).
set -euo pipefail
cd "$(dirname "$0")"
APP=principal
BRANCH=main

# 1. Nada pode ter sido editado direto no servidor
if ! git diff --quiet HEAD --; then
  echo "PAREI: há arquivos alterados direto no servidor (não estão no GitHub):"
  git status --short --untracked-files=no
  echo
  echo "Mande essas mudanças para a sessão do Code levar ao repositório:"
  echo "  git diff > ~/alteracoes-servidor.diff"
  echo "Ou, se puderem ser descartadas:  git checkout -- <arquivo>"
  exit 1
fi

# 2. Busca a main
git fetch --quiet origin "$BRANCH"
ANTES=$(git rev-parse --short HEAD)
DEPOIS=$(git rev-parse --short "origin/$BRANCH")
if [ "$ANTES" = "$DEPOIS" ]; then
  echo "Já está na versão $ANTES — nada a fazer."
  exit 0
fi
echo "Atualizando $ANTES → $DEPOIS:"
git log --oneline "HEAD..origin/$BRANCH"
git merge --ff-only --quiet "origin/$BRANCH"

# 3. Sintaxe antes de reiniciar (se falhar, volta)
if ! node --check server.js || ! node --check receitas.js; then
  echo "ERRO de sintaxe — voltando para $ANTES (nada foi reiniciado)."
  git reset --hard --quiet "$ANTES"
  exit 1
fi

# 4. Reinicia (mantém as variáveis do PM2, como RECEITAS_PIN) e confere
pm2 restart "$APP" >/dev/null
sleep 8
CODE=$(curl -s -o /dev/null -w '%{http_code}' -X POST localhost:3000/api/receitas/auth || true)
if [ "$CODE" = "401" ] || [ "$CODE" = "503" ]; then
  echo "OK: versão $DEPOIS rodando (auth respondeu $CODE)."
  sha256sum server.js receitas.js
else
  echo "ATENÇÃO: /api/receitas/auth respondeu '$CODE' (esperado 401)."
  echo "Para voltar à versão anterior:  git reset --hard $ANTES && pm2 restart $APP"
  exit 1
fi
