#!/usr/bin/env python3
"""Empacota somente os binarios e a demo produzidos pela compilacao local."""
from pathlib import Path
import hashlib
import re
import shutil
import subprocess
import zipfile

root = Path(__file__).resolve().parents[1]
destination = root / "dist"
match = re.search(r"project\(AuroraPrototype VERSION (\d+\.\d+\.\d+)", (root / "CMakeLists.txt").read_text())
if not match:
    raise SystemExit("Versao do projeto nao encontrada no CMakeLists.txt")
version = match.group(1)
package = destination / f"Aurora-{version}-windows-x64"
package.mkdir(parents=True, exist_ok=True)
for name in ("Aurora.exe", "aurora-cli.exe"):
    origin = root / "build-windows" / name
    if origin.read_bytes()[:2] != b"MZ":
        raise SystemExit(f"Nao e um executavel PE: {origin}")
    shutil.copy2(origin, package / name)
    strip = Path("/workspace/toolchains/windows/usr/bin/x86_64-w64-mingw32-strip")
    if strip.is_file():
        subprocess.run([str(strip), "--strip-debug", str(package / name)], check=True)
shutil.copy2(root / "build-native" / "demo.ps-exe", package / "demo.ps-exe")
shutil.copy2(root / "build-native" / "demo.iso", package / "demo.iso")
shutil.copy2(root / "build-native" / "aurora-coleta.ps-exe", package / "aurora-coleta.ps-exe")
shutil.copy2(root / "build-native" / "aurora-coleta.iso", package / "aurora-coleta.iso")
shutil.copy2(root / "README.md", package / "LEIA-ME.md")
(package / "docs").mkdir(exist_ok=True)
for name in ("homebrew-design.md", "aurora-coleta.png"):
    shutil.copy2(root / "docs" / name, package / "docs" / name)
notes = (
    f"Aurora {version} - Aurora Coleta, jogo caseiro PS1 integrado\n\n"
    "Abra Aurora.exe e clique em Jogar Aurora Coleta.\n"
    "Setas ou WASD: mover. Enter ou R: reiniciar.\n"
    "Objetivo: colete 5 cristais e evite o inimigo vermelho.\n"
    "O jogo pausa quando voce sai da janela.\n"
    "Requer Windows 10/11 x64 e driver Vulkan 1.1.\n"
    "Esta versao NAO roda jogos comerciais nem outros consoles.\n"
    "Tambem e possivel adicionar aurora-coleta.iso e usar Executar selecionado.\n"
    "demo.iso e a demonstracao grafica anterior.\n"
    "CUE/BIN/ISO PS1: carregamento direto de SYSTEM.CNF -> PS-X EXE.\n"
    "Isso ainda NAO implementa BIOS/HLE, runtime CD-ROM ou suporte a jogos comerciais.\n"
    "ZIP nao e disco: extraia seu conteudo e selecione o CUE, mantendo os BINs nas pastas indicadas.\n"
    "Volume maior que os setores presentes gera aviso; arquivos necessarios ausentes continuam bloqueados.\n"
    "Se parar, use Copiar ultimo diagnostico. O relatorio tambem fica em LocalAppData/AuroraPrototype/diagnostico.txt.\n"
    "PKG: apenas inspecao. Outros consoles ainda ausentes.\n"
    "Demo 0.1 confirmada no Windows pelo usuario; esta atualizacao ainda requer novo teste.\n"
    "Leia LEIA-ME.md para limites e detalhes da validacao.\n"
)
(package / "COMECE-AQUI.txt").write_text(notes, encoding="utf-8")
archive = destination / f"{package.name}.zip"
with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as output:
    for name in ("Aurora.exe", "aurora-cli.exe", "demo.ps-exe", "demo.iso", "aurora-coleta.ps-exe", "aurora-coleta.iso", "LEIA-ME.md", "COMECE-AQUI.txt", "docs/homebrew-design.md", "docs/aurora-coleta.png"):
        output.write(package / name, arcname=f"{package.name}/{name}")
digest = hashlib.sha256(archive.read_bytes()).hexdigest()
(destination / f"{archive.name}.sha256").write_text(f"{digest}  {archive.name}\n", encoding="ascii")
source = destination / f"Aurora-{version}-source.zip"
with zipfile.ZipFile(source, "w", zipfile.ZIP_DEFLATED) as output:
    for name in ("include", "src", "tests", "tools", "docs"):
        for entry in sorted((root / name).rglob("*")):
            if entry.is_file() and "__pycache__" not in entry.parts:
                output.write(entry, arcname=f"Aurora-source/{entry.relative_to(root)}")
    for name in ("CMakeLists.txt", "README.md", ".gitignore"):
        output.write(root / name, arcname=f"Aurora-source/{name}")
print(f"Pacote Windows: {archive}")
print(f"Codigo-fonte: {source}")
print(f"SHA256: {digest}")
