# Gaya-apps — regras para sessões do Claude Code

- **Fonte única:** a branch `main` é o que roda no servidor (`serverhipersul`, pasta
  `~/Historico_Cimento_Gaucho`, PM2 `principal`). Toda sessão parte da `main` atual.
  Ao terminar, a mudança volta para a `main` (pull request ou fast-forward combinado com o
  usuário). Nunca partir de cópias antigas de `server.js` enviadas em sessões anteriores.
- **Arquivo enviado pelo usuário vindo do servidor:** antes de alterar, compare com a `main`
  (`diff`). Diferenças são alterações feitas no servidor: incorpore-as, não as sobrescreva.
- **Deploy (modo atual: cópia manual):** o usuário copia para o servidor os arquivos que a
  sessão envia. Toda entrega: (1) é gerada a partir da `main` atual, já com o commit na `main`;
  (2) lista o `sha256sum` de cada arquivo enviado e onde ele vai (`public/` para os `.html`);
  (3) inclui o comando de conferência `sha256sum <arquivos>` e `pm2 restart principal`.
  O `./deploy.sh` (ver `DEPLOY.md`) é a alternativa automática, opcional.
- **Dados de produção** (ver `.gitignore`) nunca entram no repositório.
- **Alterações no `server.js`:** mínimas e cirúrgicas; o processo `pa` não é deste repositório
  e não deve ser tocado. Acesso ao CLP só pela fila de `receitas.js` (sem concorrência no socket).
- **Validação:** `node --check server.js receitas.js` e `node --test test/*.test.js` (precisa de
  `express` instalado).
