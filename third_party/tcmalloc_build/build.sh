#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
if [ ! -f ../tcmalloc/MODULE.bazel ]; then
  git -C "$(git rev-parse --show-toplevel)" submodule update --init --checkout third_party/tcmalloc
fi
bazel build //:tcmalloc_google
mkdir -p lib
cp -f bazel-bin/libtcmalloc_google.a lib/
chmod u+w lib/libtcmalloc_google.a
echo "built $(pwd)/lib/libtcmalloc_google.a"
