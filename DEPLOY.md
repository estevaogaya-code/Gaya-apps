# Deploy do dashboard (server.js) — fonte única: branch `main`

**Regra:** o código que roda no servidor é sempre o da branch `main` do GitHub
(`estevaogaya-code/Gaya-apps`). Ninguém edita `server.js`, `receitas.js` ou `public/` direto
no servidor, nem copia arquivo à mão. Para atualizar: `./deploy.sh`.

Os arquivos de **dados** (`producao.json`, `historico.json`, `turno_corrente.json`,
`backups_payload/`, `receitas_log.jsonl` etc.) ficam só no servidor. Estão no `.gitignore`
e o Git nunca os altera.

## Configuração inicial (uma vez só)

Pasta: `/home/serverhipersul/Historico_Cimento_Gaucho` (processo PM2 `principal`).

### 1. Backup da pasta inteira
```bash
cd ~
cp -a Historico_Cimento_Gaucho Historico_Cimento_Gaucho.bak-$(date +%F)
```

### 2. Acesso de leitura ao GitHub (chave de deploy, só leitura)
```bash
ssh-keygen -t ed25519 -f ~/.ssh/gaya_deploy -N "" -C "deploy serverhipersul"
cat ~/.ssh/gaya_deploy.pub
cat >> ~/.ssh/config <<'CFG'
Host github-gaya
  HostName github.com
  User git
  IdentityFile ~/.ssh/gaya_deploy
  IdentitiesOnly yes
CFG
```
No GitHub: **Settings → Deploy keys → Add deploy key**, cole o conteúdo do `.pub`,
**sem** marcar "Allow write access". Teste: `ssh -T github-gaya` (deve citar o repositório).

(Se o repositório for público, dá para usar `https://github.com/estevaogaya-code/Gaya-apps.git`
e pular este passo.)

### 3. Transformar a pasta em cópia do repositório — sem mexer nos arquivos
```bash
cd ~/Historico_Cimento_Gaucho
git init -b main
git remote add origin github-gaya:estevaogaya-code/Gaya-apps.git
git fetch origin main
git reset origin/main      # aponta para a main SEM alterar nenhum arquivo da pasta
git status
```
O `git status` mostra a diferença entre o que está na pasta e a `main`:
- `modified: server.js` (ou outro): o arquivo do servidor é **diferente** da `main`. **Pare** e
  mande `git diff > ~/diferencas.diff` para a sessão do Code antes de seguir.
- `deleted: RECEITAS.md`, `deploy.sh`, `test/...`: só não existem ainda na pasta — normal.
- `Untracked`: `package.json`, `package-lock.json`, `node_modules/` e os arquivos de dados
  (`producao.json`, `historico.json`, `backups_payload/`…) — normal: o `.gitignore` ainda não
  está na pasta. Depois do `git checkout` abaixo, os dados somem da lista.

Se só houver `deleted`/`Untracked` (nenhum `modified` de código), alinhe:
```bash
git checkout -- .          # traz os arquivos que faltam; NÃO toca nos dados
git status                 # deve mostrar só Untracked (package.json etc.)
pm2 restart principal
```

> Nunca rode `git clean` nesta pasta.

## Atualizar (sempre que a `main` mudar)
```bash
cd ~/Historico_Cimento_Gaucho && ./deploy.sh
```
O script:
1. **para** se algum arquivo de código foi editado direto no servidor (mostra quais);
2. baixa a `main` e mostra os commits novos;
3. confere a sintaxe (`node --check`) — se falhar, volta sozinho e não reinicia;
4. reinicia o `principal` (o `RECEITAS_PIN` do PM2 é mantido) e testa `/api/receitas/auth`;
5. mostra o `sha256sum` dos arquivos e o comando para voltar à versão anterior.

## Emergência: precisei editar direto no servidor
Pode, mas depois:
```bash
git diff > ~/alteracoes-servidor.diff
```
Mande o `.diff` para a sessão do Code, que leva a mudança para a `main`. Enquanto isso o
`./deploy.sh` se recusa a rodar — de propósito, para nada se perder.

## Voltar uma versão
```bash
git log --oneline -5                 # escolha o commit
git reset --hard <commit> && pm2 restart principal
```
(Só arquivos de código mudam; dados ficam.) No próximo `./deploy.sh` volta para a `main`.
