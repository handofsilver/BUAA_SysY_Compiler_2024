#!/bin/bash
# 与 run_gt_llvm.sh 相同，但在链接后额外运行 mem2reg pass，用于对比 SSA 形式输出。
# 用法: ./scripts/run_gt_llvm_mem2reg.sh <source.c> [input_file]

set -euo pipefail

CLANG=${CLANG:-clang-20}
LLI=${LLI:-lli-20}
LLVM_LINK=${LLVM_LINK:-llvm-link-20}
OPT=${OPT:-opt-20}

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SCRIPTS="$ROOT/scripts"
SOURCE="${1:?用法: $0 <source.c> [input_file]}"
INPUT="${2:-$ROOT/in.txt}"
TMPDIR=$(mktemp -d)
trap 'rm -rf "$TMPDIR"' EXIT

# 1. 编译源码和运行时（禁用 optnone 以允许 pass 生效）
$CLANG -emit-llvm -Xclang -disable-O0-optnone -c "$SOURCE" -o "$TMPDIR/source.bc" -O0 -Wno-implicit-function-declaration
$CLANG -emit-llvm -Xclang -disable-O0-optnone -c "$SCRIPTS/runtime_io.c" -o "$TMPDIR/runtime_io.bc" -O0

# 2. 链接
$LLVM_LINK "$TMPDIR/source.bc" "$TMPDIR/runtime_io.bc" -S -o "$TMPDIR/merged.ll"

# 3. 仅运行 mem2reg pass
$OPT -passes=mem2reg "$TMPDIR/merged.ll" -S -o "$TMPDIR/mem2reg.ll"

# 4. 执行
$LLI "$TMPDIR/mem2reg.ll" < "$INPUT"
