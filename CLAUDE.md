# Gaya-apps — regras para sessões do Claude Code

- **Fonte única:** a branch `main` é o que roda no servidor (`serverhipersul`, pasta
  `~/Historico_Cimento_Gaucho`, PM2 `principal`). Toda sessão parte da `main` atual.
  Ao terminar, a mudança volta para a `main` (pull request ou fast-forward combinado com o
  usuário). Nunca partir de cópias antigas de `server.js` enviadas em sessões anteriores.
- **Arquivo enviado pelo usuário vindo do servidor:** antes de alterar, compare com a `main`
  (`diff`). Diferenças são alterações feitas no servidor: incorpore-as, não as sobrescreva.
- **Deploy:** só por `./deploy.sh` no servidor (ver `DEPLOY.md`). Não instruir cópia manual.
- **Dados de produção** (ver `.gitignore`) nunca entram no repositório.
- **Alterações no `server.js`:** mínimas e cirúrgicas; o processo `pa` não é deste repositório
  e não deve ser tocado. Acesso ao CLP só pela fila de `receitas.js` (sem concorrência no socket).
- **Validação:** `node --check server.js receitas.js` e `node --test test/` (precisa de
  `express` instalado).
