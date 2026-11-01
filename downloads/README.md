# Aurora para Windows

[Baixar Aurora 0.3.0 x64](https://github.com/Aspxexe/CHAT-GPT-CRIACOES/raw/refs/heads/main/downloads/Aurora-0.3.0-windows-x64.zip)

Extraia o ZIP inteiro, abra `Aurora.exe` e clique em **Jogar Aurora Coleta**.

- Setas ou WASD: mover.
- Enter ou R: reiniciar.
- Colete os cinco cristais amarelos e evite o inimigo vermelho.
- O jogo pausa ao sair da janela ou minimizá-la.

O jogo caseiro é integrado ao executável. `aurora-coleta.iso` e
`aurora-coleta.ps-exe` também estão incluídos para carregamento manual.

Requer Windows 10/11 x64 e driver Vulkan 1.1. O pacote foi compilado no Linux
para Windows; a nova interface e as teclas ainda precisam de validação no Windows.
Os testes de CPU, IO, vitória, derrota e reinício passaram no núcleo Linux.
Não há suporte a Resident Evil 2 ou outros jogos comerciais nesta versão.

O arquivo `.sha256` contém o checksum do ZIP. No PowerShell:

```powershell
Get-FileHash .\Aurora-0.3.0-windows-x64.zip -Algorithm SHA256
```

O código-fonte está na raiz do repositório. Os binários desta pasta são arquivos
de distribuição; não são necessários para compilar o projeto.
