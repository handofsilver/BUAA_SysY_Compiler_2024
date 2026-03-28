#!/bin/bash
# 用标准 clang 编译 SysY 源码并通过 lli 执行，作为 ground truth 对比基准。
# 用法: ./scripts/run_gt_llvm.sh <source.c> [input_file]
#   source.c:   SysY 源文件
#   input_file: 可选，stdin 输入文件，默认为 in.txt

set -euo pipefail

CLANG=${CLANG:-clang-20}
LLI=${LLI:-lli-20}
LLVM_LINK=${LLVM_LINK:-llvm-link-20}

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SCRIPTS="$ROOT/scripts"
SOURCE="${1:?用法: $0 <source.c> [input_file]}"
INPUT="${2:-$ROOT/in.txt}"
TMPDIR=$(mktemp -d)
trap 'rm -rf "$TMPDIR"' EXIT

# 1. 编译源码和运行时为位码
$CLANG -emit-llvm -c "$SOURCE" -o "$TMPDIR/source.bc" -O0 -Wno-implicit-function-declaration
$CLANG -emit-llvm -c "$SCRIPTS/runtime_io.c" -o "$TMPDIR/runtime_io.bc" -O0

# 2. 链接并运行
$LLVM_LINK "$TMPDIR/source.bc" "$TMPDIR/runtime_io.bc" -S -o "$TMPDIR/merged.ll"
$LLI "$TMPDIR/merged.ll" < "$INPUT"
