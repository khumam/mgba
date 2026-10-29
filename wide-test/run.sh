#!/bin/sh
# Builds and runs the host renderer tests: engine/wide-test/run.sh [--write-golden]
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/.." && pwd)
cmake_bin=${CMAKE:-$HOME/Library/Android/sdk/cmake/3.22.1/bin}
build="$root/build-wide-test"
"$cmake_bin/cmake" -S "$here" -B "$build" -G Ninja -DCMAKE_MAKE_PROGRAM="$cmake_bin/ninja" -DCMAKE_BUILD_TYPE=Release >/dev/null
"$cmake_bin/cmake" --build "$build" --target wide_render_test
"$build/wide_render_test" "$@"
