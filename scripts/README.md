# scripts/

LLVM IR 验证脚本，用于在本地通过 `lli` 解释执行来验证编译器输出的正确性。

## 前置依赖

需要 LLVM 20 工具链（`clang-20`, `lli-20`, `llvm-link-20`, `llvm-as-20`, `opt-20`）。
如果本地命令名不同，可通过环境变量覆盖，例如 `CLANG=clang LLI=lli ./scripts/test_llvm.sh`。

## 脚本说明

| 脚本 | 用途 |
|------|------|
| `test_llvm.sh` | 运行本项目编译器，将生成的 `llvm_ir.txt` 链接运行时后通过 `lli` 执行 |
| `run_gt_llvm.sh` | 用标准 clang 编译 SysY 源文件并执行，作为 ground truth 对比 |
| `run_gt_llvm_mem2reg.sh` | 同上，但额外运行 `opt -passes=mem2reg`，用于对比 SSA 形式 |
| `runtime_io.c` | SysY 运行时 IO 函数（`getint`, `getchar`, `putint`, `putch`, `putstr`），链接时使用 |

## 用法

```bash
# 所有脚本从项目根目录运行，testfile.txt 和 in.txt 需放在根目录

# 验证本项目编译器的 IR 输出
./scripts/test_llvm.sh              # stdin 从 in.txt 读取
./scripts/test_llvm.sh input.txt    # 指定输入文件

# 用标准 clang 编译执行，作为参考答案
./scripts/run_gt_llvm.sh testfile.txt
./scripts/run_gt_llvm.sh testfile.txt input.txt

# 对比 mem2reg 后的 SSA 形式
./scripts/run_gt_llvm_mem2reg.sh testfile.txt
```
