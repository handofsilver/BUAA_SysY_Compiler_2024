#!/bin/bash
# 运行本项目编译器生成的 LLVM IR，通过 lli 解释执行验证正确性。
# 用法: ./scripts/test_llvm.sh [input_file]
#   input_file: 可选，stdin 输入文件，默认为 in.txt

set -euo pipefail

CLANG=${CLANG:-clang-20}
LLI=${LLI:-lli-20}
LLVM_LINK=${LLVM_LINK:-llvm-link-20}
LLVM_AS=${LLVM_AS:-llvm-as-20}

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SCRIPTS="$ROOT/scripts"
COMPILER="$ROOT/build/Compiler"
INPUT="${1:-$ROOT/in.txt}"
TMPDIR=$(mktemp -d)
trap 'rm -rf "$TMPDIR"' EXIT

# 1. 运行编译器生成 llvm_ir.txt
"$COMPILER"

# 2. 编译 runtime_io.c 为位码
$CLANG -emit-llvm -c "$SCRIPTS/runtime_io.c" -o "$TMPDIR/runtime_io.bc" -O0

# 3. 将 llvm_ir.txt 转为位码
$LLVM_AS "$ROOT/llvm_ir.txt" -o "$TMPDIR/main.bc"

# 4. 链接并运行
$LLVM_LINK "$TMPDIR/main.bc" "$TMPDIR/runtime_io.bc" -S -o "$TMPDIR/merged.ll"
$LLI "$TMPDIR/merged.ll" < "$INPUT"
