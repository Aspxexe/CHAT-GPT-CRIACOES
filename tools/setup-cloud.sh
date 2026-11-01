#!/usr/bin/env bash
set -euo pipefail

toolchain_root=/workspace/toolchains
mkdir -p "$toolchain_root/apt/lists/partial" "$toolchain_root/apt/cache/archives/partial" \
  "$toolchain_root/downloads" "$toolchain_root/windows"
printf '%s\n' 'deb [signed-by=/usr/share/keyrings/debian-archive-keyring.gpg] https://deb.debian.org/debian trixie main' \
  > "$toolchain_root/apt/sources.list"
apt_options=(
  -o "Dir::State::Lists=$toolchain_root/apt/lists"
  -o "Dir::Cache=$toolchain_root/apt/cache"
  -o "Dir::Etc::sourcelist=$toolchain_root/apt/sources.list"
  -o Dir::Etc::sourceparts=-
  -o Dir::Etc::parts=-
)
apt-get "${apt_options[@]}" update
(
  cd "$toolchain_root/downloads"
  apt-get "${apt_options[@]}" download \
    g++-mingw-w64-x86-64-posix gcc-mingw-w64-x86-64-posix \
    gcc-mingw-w64-x86-64-posix-runtime gcc-mingw-w64-base \
    binutils-mingw-w64-x86-64 mingw-w64-x86-64-dev mingw-w64-common \
    libvulkan-dev cmake cmake-data librhash1 libuv1t64
  for package in ./*.deb; do dpkg-deb -x "$package" "$toolchain_root/windows"; done
)
"$toolchain_root/windows/usr/bin/x86_64-w64-mingw32-g++-posix" --version
LD_LIBRARY_PATH="$toolchain_root/windows/usr/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
  "$toolchain_root/windows/usr/bin/cmake" --version
