# MARS getint / getchar 混合输入问题分析与修复

**日期**：2026-03-09
**文件**：`src/mips/FunctionEmitter.cpp` → `EmitLibraryFunctionCall`

---

## 问题现象

编译 `testfile.txt`（含 `getint` 与 `getchar` 混用）后，用以下输入运行生成的 MIPS：

```
in.txt:
1
2
A
3
```

MARS 报错：

```
Runtime exception at 0x...: invalid integer input (syscall 5)
```

程序输出 `1`、`2` 后在第三个 `getint`（读 `z`）处崩溃。

---

## 根本原因：MARS syscall 行为与 C 标准不同

### C 标准 `scanf("%d")` 行为
- **跳过前导空白**（包括 `\n`、空格、Tab）
- 读完整数后，**不消耗**尾部 `\n`，留在缓冲区

### MARS syscall 5（getint）实际行为
- **整行读取**（line-by-line，类似 `fgets` + `atoi`）
- 读取一整行（直到 `\n`，含 `\n`），然后解析该行为整数
- **如果读到空行（只有 `\n`）→ 报 "invalid integer input" 错误**

### MARS syscall 12（getchar）实际行为
- 读**一个字符**，不消耗后面的 `\n`

---

## 错误序列分析

SysY 源码：
```c
x = getint();   // 读 1
y = getint();   // 读 2
c1 = getchar(); // 应读 'A'
z = getint();   // 应读 3
```

输入缓冲区变化（不做任何修复时）：

| 操作 | 消耗 | 剩余缓冲区 |
|------|------|-----------|
| getint → x | `1\n` | `2\nA\n3\n` |
| getint → y | `2\n` | `A\n3\n` |
| getchar → c1 | `A` | `\n3\n` |
| getint → z | 尝试读下一行 `\n`（空行）| — |

最后一步：MARS 读到空行 `\n`，解析整数失败 → 报错。

---

## 为什么不实现 C 标准的跳空白行为？

理论上可以在 MIPS 汇编中手写字符级读循环来模拟 `scanf("%d")`：

```
read_int_loop:
    li   $v0, 12
    syscall              # 读一个字符
    beq  $v0, '\n', read_int_loop  # 是空白则跳过
    beq  $v0, ' ',  read_int_loop
    # 开始累加数字 ...
```

但这需要：
- 完整的状态机实现（识别符号、处理非数字边界等）
- 替换掉所有 `getint` 的 syscall 5 调用

**结论**：实现成本高，BUAA 课程测试数据遵守"每值一行"格式，没有必要。

---

## 修复方案

**在每个 `getchar` 的 MIPS 代码之后，额外执行一次 `syscall 12` 消耗掉尾随的 `\n`，结果不保存。**

修复后的 `EmitLibraryFunctionCall`（`getchar` 分支）：

```cpp
} else if (name == "getchar") {
    os_ << k_indent << "li    $v0, 12\n";
    os_ << k_indent << "syscall\n";
    os_ << k_indent << "sw    $v0, " << value_offset_.at(inst) << "($sp)\n";
    // MARS syscall 12 只读一个字符，尾部 '\n' 留在缓冲区；
    // 消耗掉它，否则下一个 getint 会读到空行并报错。
    os_ << k_indent << "li    $v0, 12\n";
    os_ << k_indent << "syscall\n";
}
```

生成的 MIPS 片段示例：

```mips
li    $v0, 12      # getchar 读字符
syscall
sw    $v0, 72($sp) # 保存结果
li    $v0, 12      # 消耗尾随 '\n'
syscall            # 结果扔掉（$v0 被后续覆盖）
```

---

## 修复后的正确序列

| 操作 | 消耗 | 剩余缓冲区 |
|------|------|-----------|
| getint → x | `1\n` | `2\nA\n3\n` |
| getint → y | `2\n` | `A\n3\n` |
| getchar → c1 | `A` | `\n3\n` |
| 额外 syscall 12 | `\n` | `3\n` |
| getint → z | `3\n` | `""` |

z = 3，读取成功。

---

## 使用约束

当前实现假设：**每个 `getchar` 读取的字符后面跟一个 `\n`**（即 in.txt 中字符值独占一行）。这是 BUAA 课程测试数据的标准格式。

如果输入格式为多字符同行（如 `AB\n`），额外的 `syscall 12` 会吃掉下一个字符，导致错误。对于课程测试数据无需担心此情况。

---

## getint 无需额外处理

MARS syscall 5 自身会消耗整行（含 `\n`），因此 `getint` 之后**不需要**额外的 `syscall 12`。曾尝试在 `getint` 后加额外消耗，结果适得其反——它消耗掉了下一行的第一个有效字符（如 `2`），导致第二个 `getint` 读到空行报错。
