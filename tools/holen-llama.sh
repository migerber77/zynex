#!/bin/sh
# Holt llama.cpp in der festgelegten Version nach third_party/llama.cpp.
set -e
VERSION=b12414
ZIEL="$(dirname "$0")/../third_party/llama.cpp"
[ -d "$ZIEL/.git" ] && exit 0
git clone --depth 1 --branch "$VERSION" https://github.com/ggml-org/llama.cpp "$ZIEL"
