# 如何得到标准 MIPS 目标代码样例

当已有**符合课程要求的 SysY 源文件**，或**已生成的合法 LLVM IR 文件**（含本项目采用的 LLVM 20 格式）时，可用以下方式得到可参考的 MIPS 目标代码。

---

## 一、从 SysY 源文件得到 MIPS

### 1.1 使用本仓库（当前能力）

本仓库当前流程为：

```text
testfile.txt  →  Compiler  →  llvm_ir.txt
```

MIPS 后端尚未实现，因此**无法**直接从本编译器得到 `mips.txt`。

做法：先用本编译器从 SysY 得到 IR，再按下一节「从 LLVM IR 得到 MIPS」处理。

- 将 SysY 源码放入 `testfile.txt`（或复制到可执行文件同目录）。
- 运行：`./build/Compiler`，得到 `llvm_ir.txt`。
- 再对 `llvm_ir.txt` 使用下文中的 `llc` 或其它工具生成 MIPS。

### 1.2 若有课程/参考 MIPS 编译器

若手头有能生成 MIPS 的参考编译器（如课程提供的或原 Java 版）：

- 输入：`testfile.txt`（同一份 SysY 源码）。
- 输出：该编译器生成的 `mips.txt` 即为**课程意义上的标准 MIPS 样例**（与 MARS 4.5、课程组规范一致）。

---

## 二、从 LLVM IR 得到 MIPS（推荐：llc）

LLVM 自带 MIPS 后端，可用 **llc** 将 LLVM IR 编译为 MIPS 汇编，作为**指令选择与结构**的参考。

### 2.1 安装带 MIPS 的 LLVM

需安装包含 MIPS 目标的 LLVM（版本建议与生成 IR 的保持一致，如 LLVM 20）：

- **Ubuntu/Debian**：
  `sudo apt install llvm-20 llvm-20-dev`（若该包未启用 MIPS，需从源码编译 LLVM 并启用 `MIPS` target）。
- **从源码构建**：在 CMake 中打开 `LLVM_TARGETS_TO_BUILD` 中的 `MIPS`。

安装后确认：

```bash
llc --version
# 在 “Registered Targets” 中应包含 mips
```

### 2.2 基本命令

```bash
# 将 llvm_ir.txt 生成为 MIPS 汇编，输出到 mips.txt
llc -march=mips -mattr=+mips32r2 -filetype=asm -o mips.txt llvm_ir.txt
```

常用选项简要说明：

| 选项 | 含义 |
|------|------|
| `-march=mips` | 目标架构为 MIPS |
| `-mattr=+mips32r2` | 使用 MIPS32 Release 2（可按需改为 `+mips32` 等） |
| `-filetype=asm` | 输出汇编文本（默认也可能是 asm） |
| `-o mips.txt` | 输出文件名 |

若 IR 文件扩展名不是 `.ll`，部分环境可能对文件类型有假设，可先复制为 `xxx.ll` 再执行 llc。

### 2.3 与课程/MARS 的差异（重要）

- **运行时/库函数**：课程要求使用 MARS 的 syscall（如 getint→5、getchar→12、putint→1、putch→11、putstr→4）。llc 生成的是「裸」汇编，**不会**自动插入这些库函数实现；若 IR 中有 `getint`/`putint` 等声明，llc 会生成对同名符号的调用，需要在 MARS 中自行提供实现（例如在汇编里写 syscall 包装）。
- **指令集约束**：课程要求 MARS 4.5（课程组修改版）、**基础指令 + 伪指令，不可用宏指令**。llc 生成的指令集可能与之一致，也可能使用到 MARS 不支持的指令或伪指令，需要对照课程文档做替换或删减。
- **调用约定/数据布局**：llc 默认按 LLVM 的 MIPS ABI 生成，与课程约定的「参数 $a0–$a3、返回值 $v0」等可能一致，但栈帧、对齐等细节可能不同，若要做成与课程完全一致的样例，可能需局部手改或后处理。

因此：**llc 输出最适合作为「如何把 IR 翻成 MIPS 指令」的参考**；要得到**可直接在 MARS 上按课程要求运行的标准样例**，通常还需：

1. 保证 IR 中的库函数声明与课程一致（getint/getchar/putint/putch/putstr）；
2. 在 MIPS 中提供上述库函数的 syscall 实现；
3. 检查并去掉或改写不满足「基础+伪指令、无宏指令」的指令。

---

## 三、推荐流程小结

| 你已有的 | 推荐做法 |
|----------|----------|
| 仅 SysY 源文件 | 用本编译器生成 `llvm_ir.txt`，再对 `llvm_ir.txt` 用 llc 生成 MIPS；或若有参考 MIPS 编译器，直接用其编译同一 SysY 得到标准 mips.txt。 |
| 已有 LLVM IR 文件 | 直接用 `llc -march=mips ... -o mips.txt llvm_ir.txt` 得到 MIPS；再按课程要求补库函数、检查指令集。 |

本仓库 MIPS 后端开发完成后，可直接：
`testfile.txt → Compiler → mips.txt`，届时本编译器生成的即为符合课程规范的 MIPS 样例。

---

## 四、用 MARS.jar 运行 mips.txt

课程要求使用**课程组修改版 MARS 4.5**，对应你手头的 `Mars.jar`。运行方式有两种。

### 4.1 图形界面（推荐日常调试）

1. 启动 MARS：
   ```bash
   java -jar Mars.jar
   ```
   若 `Mars.jar` 不在当前目录，请写完整路径，例如：
   ```bash
   java -jar /path/to/BUAA_Compiler_2026_Refactored/Mars.jar
   ```

2. 在 MARS 中：**File → Open**，选择项目根目录下的 `mips.txt`。

3. 菜单 **Run → Run**（或工具栏运行按钮），程序从默认入口开始执行；若希望从 `main` 开始，可在 **Run → Run** 前在 **Run → Go** 的对话框中设置起始标签为 `main`，或使用下方命令行方式并加 `sm` 选项。

4. 程序结束后，可在 Run 界面查看寄存器、内存；若程序用 syscall 退出，可看到退出码等。

### 4.2 命令行（适合脚本/批量验证）

在项目根目录（或保证 `Mars.jar` 与 `mips.txt` 路径正确）下执行：

```bash
# 基本：汇编并运行 mips.txt
java -jar Mars.jar mips.txt

# 从 main 标签开始执行（课程程序通常有 main）
java -jar Mars.jar sm mips.txt

# 不显示版权信息，便于重定向输出（如自动化测试）
java -jar Mars.jar nc sm mips.txt
```

常用选项简要说明：

| 选项 | 含义 |
|------|------|
| `sm` | 从 `main` 标签开始执行（若存在） |
| `nc` | 不显示版权信息，便于脚本中捕获输出 |
| `a` | 仅汇编，不模拟执行 |
| `h` | 显示帮助 |

**注意**：课程组修改版 MARS 可能与官方 MARS 在选项上略有差异，若上述命令报错，请以课程资料中的「竞速排序及仿真器使用说明2024」为准。确保本机已安装 **Java**（通常 JRE 8 或以上即可）：`java -version`。
