#!/usr/bin/env bash
set -euo pipefail
project_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
toolchain_prefix=/workspace/toolchains/windows/usr
export LD_LIBRARY_PATH="$toolchain_prefix/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
cmake_bin="$toolchain_prefix/bin/cmake"
ctest_bin="$toolchain_prefix/bin/ctest"
"$cmake_bin" -S "$project_root" -B "$project_root/build-native" -DCMAKE_BUILD_TYPE=Debug
"$cmake_bin" --build "$project_root/build-native" --parallel 4
"$ctest_bin" --test-dir "$project_root/build-native" --output-on-failure
export MINGW_ROOT="$toolchain_prefix"
"$cmake_bin" -S "$project_root" -B "$project_root/build-windows" \
  -DCMAKE_TOOLCHAIN_FILE="$project_root/tools/mingw-toolchain.cmake" \
  -DVULKAN_HEADERS_DIR="$toolchain_prefix/include" -DBUILD_TESTING=OFF -DCMAKE_BUILD_TYPE=Release
"$cmake_bin" --build "$project_root/build-windows" --parallel 4
"$project_root/build-native/aurora-cli" --make-demo "$project_root/build-native/demo.ps-exe"
"$project_root/build-native/aurora-cli" --run "$project_root/build-native/demo.ps-exe" \
  --ppm "$project_root/build-native/demo.ppm"
rm -f "$project_root/build-native/demo.iso"
python3 "$project_root/tools/make-demo-iso.py" "$project_root/build-native/demo.ps-exe" \
  "$project_root/build-native/demo.iso"
"$project_root/build-native/aurora-cli" --probe "$project_root/build-native/demo.iso" \
  --report "$project_root/build-native/demo-disc-diagnostico.txt" > /dev/null
"$project_root/build-native/aurora-cli" --run "$project_root/build-native/demo.iso" \
  --ppm "$project_root/build-native/demo-disc.ppm"
cmp "$project_root/build-native/demo.ppm" "$project_root/build-native/demo-disc.ppm"
"$project_root/build-native/aurora-cli" --make-homebrew "$project_root/build-native/aurora-coleta.ps-exe"
rm -f "$project_root/build-native/aurora-coleta.iso"
python3 "$project_root/tools/make-demo-iso.py" "$project_root/build-native/aurora-coleta.ps-exe" \
  "$project_root/build-native/aurora-coleta.iso" --volume AURORA_COLETA --boot COLETA.PSX
"$project_root/build-native/aurora-cli" --run "$project_root/build-native/aurora-coleta.ps-exe" \
  --ppm "$project_root/build-native/homebrew.ppm"
"$project_root/build-native/aurora-cli" --run "$project_root/build-native/aurora-coleta.iso" \
  --ppm "$project_root/build-native/homebrew-disc.ppm"
cmp "$project_root/build-native/homebrew.ppm" "$project_root/build-native/homebrew-disc.ppm"
