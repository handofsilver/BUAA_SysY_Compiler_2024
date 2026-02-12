# 语义分析重构指南：从 Java One-Pass 到 C++ 现代架构

> **生成说明**：本篇笔记由 Google Gemini 3 Pro 生成。
> **生成日期**：2026-02-12
> **生成方式**：基于**“苏格拉底式”引导对话（Guided Socratic Method）**，由 LLM 与人类开发者深度协作，探讨 SysY 编译器从 Java 重构至 Modern C++ 的语义分析架构设计，并最终由 LLM 总结提炼主要 QA 成果。

**背景**：

在原有的 Java 实现中，我们采用了 Parse-Time Analysis（边解析边分析）的策略，导致 Parser 极其臃肿，符号表依赖全局静态变量，且语义信息与 AST 节点高度耦合。

**目标**：

利用 C++17 特性（OOP、RAII、Variant）重构语义分析层，实现高内聚、低耦合的编译器架构。

## 第一章：架构解耦 —— 访问者模式 (Visitor Pattern)

### Q1: 如何将“解析(Parsing)”与“分析(Analyzing)”彻底分开？

**旧设计痛点**：`parseFuncDef` 方法里既要识别语法，又要注册符号，还要检查重定义。想加一个代码生成功能，还得去改 Parser。

**新设计思路**：**访问者模式 (Visitor Pattern)**。

我们将 AST 看作一个稳定的数据结构（“死”的树），将语义分析看作是一个在树上游走的外部操作（“活”的检查员）。

### Q2: 什么是“双分派 (Double Dispatch)”，为什么要握两次手？

在 C++ 中，为了让 Visitor 正确地调用处理具体节点的方法（如 `VisitFuncDef`），我们需要两次分派过程：

1. **第一次分派（运行时多态）**：`node->Accept(visitor)`
   - **目的**：确定 **Node 的真实类型**。
   - **机制**：通过虚函数表，程序在运行时跳到具体子类（如 `FuncDefNode`）的 `Accept` 实现中。
2. **第二次分派（静态重载）**：`visitor.VisitFuncDef(*this)`
   - **目的**：确定 **Visitor 的处理方法**。
   - **机制**：在 `FuncDefNode` 内部，编译器明确知道 `*this` 是 `FuncDefNode` 类型，因此自动匹配 `Visitor::VisitFuncDef` 重载版本。

**代码直观演示**：

```
// 1. 抽象节点
class AstNode {
public:
    virtual void Accept(ASTVisitor& visitor) = 0; // 第一次分派入口
};

// 2. 具体节点
class FuncDefNode : public AstNode {
public:
    void Accept(ASTVisitor& visitor) override {
        // 第二次分派：现在 visitor 确切知道我是 FuncDefNode
        visitor.VisitFuncDef(*this);
    }
};

// 3. 访问者
class SemanticAnalyzer : public ASTVisitor {
public:
    void VisitFuncDef(FuncDefNode& node) override {
        // 具体的语义分析逻辑写在这里
    }
};
```

### Q3: 谁来控制递归？Node 驱动 vs Visitor 驱动？

**结论**：必须是 **Visitor 驱动**。

- **Node 驱动 (Anti-Pattern)**：Node 的 `Accept` 自动调用子节点的 `Accept`。
  - *缺点*：Visitor 变成被动监听器，无法控制遍历顺序。
- **Visitor 驱动 (Best Practice)**：Node 的 `Accept` 只做双分派，遍历子节点的逻辑写在 `Visitor::VisitXxx` 里。
  - *优势*：可以在访问子节点**之前**或**之后**插入逻辑（如推入作用域），或者根据条件决定**是否**访问（如死代码消除）。

## 第二章：状态管理 —— RAII 与 作用域 (Scope)

### Q4: 没有了 Java 的 `static` 全局符号表，信息如何在递归中传递？

**旧设计痛点**：`SymbolManager` 是静态单例。手动 `pushScope()` 和 `popScope()` 容易漏写，且无法支持并发编译。

**新设计思路**：**上下文传递 + RAII**。

符号表 (`SymbolTable`) 是 `SemanticAnalyzer` 的一个成员变量。它随着 Visitor 在树上爬行，作为“随身携带的记录簿”。

### Q5: 什么是 RAII？它是如何“降维打击”作用域管理的？

**概念**：**R**esource **A**cquisition **I**s **I**nitialization（资源获取即初始化）。

**核心**：利用 C++ 局部变量的生命周期（构造 -> 析构）来自动管理资源。

**ScopeGuard 实现**：

```
// 作用域守护者
class ScopeGuard {
public:
    ScopeGuard(SymbolTable& table) : table_(table) {
        table_.PushScope(); // 构造时：自动进门刷卡
    }
    ~ScopeGuard() {
        table_.PopScope();  // 析构时：出门自动销毁记录
    }
private:
    SymbolTable& table_;
};

// 使用示例
void SemanticAnalyzer::VisitBlock(BlockNode& node) {
    ScopeGuard guard(this->symbol_table_); // <--- 自动 Push
    // ... 访问语句 ...
} // <--- 函数结束，guard 销毁，自动 Pop。哪怕中间 return 也不怕。
```

### Q6: 遇到“特殊作用域”怎么办？（标志位法 vs 拆分法）

**场景**：SysY 中函数定义的形参和函数体 Block 属于同一作用域。如果 `VisitFuncDef` 推了一次栈，`VisitBlock` 又推了一次，就会导致分层错误。

- **解法 A（标志位法 - 不推荐）**：传 `boolean needPush` 给 `VisitBlock`。
  - *缺点*：污染了通用逻辑，导致控制耦合。
- **解法 B（拆分法 - 推荐）**：
  - 将 `BlockNode` 视为“大括号”+“内容列表”。
  - `VisitBlock` 负责标准行为（推栈+访问内容）。
  - `VisitFuncDef` 手动接管控制权：推栈 -> 访问形参 -> **直接访问 Block 的内容列表**（跳过 `VisitBlock` 的推栈逻辑）。

## 第三章：数据解耦 —— 符号纯粹性 (Symbol Purity)

### Q7: 语义分析后的 Symbol 应该存什么？

**旧设计痛点**：`Symbol` 对象里直接存了 `InitVal` 等 AST 节点。导致语义层依赖语法层，想知道一个常量的值还得去翻树。

**新设计思路**：**Symbol = 纯粹的语义信息**。

Symbol 不应持有 AST 节点，而应持有“计算后”的属性：

1. **类型** (`DataType`: INT, VOID...)
2. **种类** (`Kind`: CONST, VAR, FUNC)
3. **值** (对于常量，直接存 `int` 数值)

### Q8: 怎么处理常量表达式？(Constant Folding)

不要在 Symbol 里存 `ExpNode`。在访问 AST 时，直接计算出结果。

**实现技巧**：

Visitor 内部维护一个成员变量 `last_value_`（或者使用 `std::any` 返回值）。

```
void SemanticAnalyzer::VisitBinaryExp(BinaryExpNode& node) {
    node.lhs_->Accept(*this);
    int left = this->last_value_; // 获取左子树计算结果

    node.rhs_->Accept(*this);
    int right = this->last_value_; // 获取右子树计算结果

    // 直接计算并存储，不生成新的 AST 节点
    this->last_value_ = left + right;
}
```

这样，当遇到 `const int a = 1 + 2;` 时，存入符号表的 `a` 的值直接就是 `3`。

### Q9: 总结一下 C++ 重构的核心收益？

1. **安全性**：RAII 杜绝了作用域泄漏。
2. **解耦**：AST 只是数据，Symbol 只是信息，Visitor 负责逻辑。三者互不干扰。
3. **扩展性**：新增中间代码生成 (`IrGenVisitor`) 或 优化器 (`OptimizeVisitor`) 时，只需新增 Visitor 类，无需修改现有代码。
