/**
 * @file Instruction.h
 * @brief Instruction hierarchy: declarations only. Implementations in Instruction.cpp.
 *
 * All instructions live inside a BasicBlock. They are Users (have operands)
 * and hold a pointer to their parent block.
 */
#pragma once

#include "ir/User.h"
#include <ostream>
#include <string>
#include <vector>

namespace ir {

    class BasicBlock; // forward declaration; full definition in BasicBlock.h
    class IRPrintContext;

    /**
     * @brief Base class for all IR instructions.
     *
     * Ownership: Instruction is owned by BasicBlock (unique_ptr in block).
     * parent_ is a non-owning pointer to the containing block.
     */
    class Instruction : public User {
    public:
        Instruction();
        Instruction(const std::string& name, Type* type, BasicBlock* parent);
        virtual ~Instruction() = default;

        BasicBlock* GetParent() const;
        void SetParent(BasicBlock* bb);

        /** @brief Print this instruction to LLVM IR text (one line). \p context for print-time SSA
         * names. */
        virtual void Print(std::ostream& os, const IRPrintContext* context = nullptr) const = 0;

    protected:
        BasicBlock* parent_;
    };

    /** @brief alloca: allocate stack slot. */
    class AllocaInst : public Instruction {
    public:
        AllocaInst();
        AllocaInst(const std::string& name, Type* type, BasicBlock* parent);
        void Print(std::ostream& os, const IRPrintContext* context = nullptr) const override;
    };

    /** @brief load: load from pointer. operand(0) = pointer. */
    class LoadInst : public Instruction {
    public:
        LoadInst();
        LoadInst(const std::string& name, Type* type, BasicBlock* parent, Value* ptr);
        Value* GetPointerOperand() const;
        void Print(std::ostream& os, const IRPrintContext* context = nullptr) const override;
    };

    /** @brief store: store to pointer. operand(0)=value, operand(1)=pointer. */
    class StoreInst : public Instruction {
    public:
        StoreInst();
        StoreInst(const std::string& name, Type* type, BasicBlock* parent, Value* value,
                  Value* ptr);
        Value* GetValueOperand() const;
        Value* GetPointerOperand() const;
        void Print(std::ostream& os, const IRPrintContext* context = nullptr) const override;
    };

    /** @brief IR-level binary arithmetic op (decoupled from AST OpType). */
    enum class BinaryOp { ADD, SUB, MUL, DIV, REM };

    /** @brief Binary op: add, sub, mul, sdiv, srem, etc. */
    class BinaryInst : public Instruction {
    public:
        BinaryInst();
        BinaryInst(const std::string& name, Type* type, BasicBlock* parent, BinaryOp op, Value* lhs,
                   Value* rhs);
        BinaryOp GetOp() const;
        Value* GetLhs() const;
        Value* GetRhs() const;
        void Print(std::ostream& os, const IRPrintContext* context = nullptr) const override;

    private:
        BinaryOp op_;
    };

    /** @brief br: conditional or unconditional branch. */
    class BranchInst : public Instruction {
    public:
        BranchInst();
        BranchInst(const std::string& name, Type* type, BasicBlock* parent, BasicBlock* dest);
        BranchInst(const std::string& name, Type* type, BasicBlock* parent, Value* cond,
                   BasicBlock* if_true, BasicBlock* if_false);

        bool IsConditional() const {
            return is_conditional_;
        }
        BasicBlock* GetDest() const;
        Value* GetCond() const;
        BasicBlock* GetIfTrue() const;
        BasicBlock* GetIfFalse() const;
        void Print(std::ostream& os, const IRPrintContext* context = nullptr) const override;

    private:
        bool is_conditional_;
    };

    /** @brief call: function call. operand(0)=callee, operands(1..n)=args. */
    class CallInst : public Instruction {
    public:
        CallInst();
        CallInst(const std::string& name, Type* type, BasicBlock* parent, Value* callee,
                 const std::vector<Value*>& args);
        Value* GetCallee() const;
        Value* GetArg(int i) const;
        size_t GetNumArgs() const;
        void Print(std::ostream& os, const IRPrintContext* context = nullptr) const override;
    };

    /** @brief ret: return from function. operand(0) = return value (or none for ret void). */
    class ReturnInst : public Instruction {
    public:
        ReturnInst();
        ReturnInst(const std::string& name, Type* type, BasicBlock* parent, Value* ret_val);
        ReturnInst(const std::string& name, Type* type, BasicBlock* parent);
        Value* GetRetVal() const;
        void Print(std::ostream& os, const IRPrintContext* context = nullptr) const override;
    };

    /** @brief getelementptr: address computation. operand(0)=base, operand(1..n)=indices. */
    class GetElementPtrInst : public Instruction {
    public:
        GetElementPtrInst();
        GetElementPtrInst(const std::string& name, Type* type, BasicBlock* parent, Value* base,
                          Value* index);
        GetElementPtrInst(const std::string& name, Type* type, BasicBlock* parent, Value* base,
                          Value* index0, Value* index1);
        Value* GetPointerOperand() const;
        Value* GetIndex(int i = 0) const;
        void Print(std::ostream& os, const IRPrintContext* context = nullptr) const override;
    };

    /** @brief icmp: integer comparison. Result is i1. operand(0)=lhs, operand(1)=rhs. */
    enum class IcmpPred { SLT, SGT, SLE, SGE, EQ, NE };

    class IcmpInst : public Instruction {
    public:
        IcmpInst();
        IcmpInst(const std::string& name, Type* type, BasicBlock* parent, IcmpPred pred, Value* lhs,
                 Value* rhs);
        IcmpPred GetPredicate() const;
        Value* GetLhs() const;
        Value* GetRhs() const;
        void Print(std::ostream& os, const IRPrintContext* context = nullptr) const override;

    private:
        IcmpPred pred_;
    };

    /** @brief zext: zero-extend (e.g. i1 to i32). operand(0)=value. */
    class ZextInst : public Instruction {
    public:
        ZextInst();
        ZextInst(const std::string& name, Type* dest_type, BasicBlock* parent, Value* value);
        Value* GetOperandValue() const;
        void Print(std::ostream& os, const IRPrintContext* context = nullptr) const override;
    };

    /** @brief trunc: truncate (e.g. i32 to i8). operand(0)=value. */
    class TruncInst : public Instruction {
    public:
        TruncInst();
        TruncInst(const std::string& name, Type* dest_type, BasicBlock* parent, Value* value);
        Value* GetOperandValue() const;
        void Print(std::ostream& os, const IRPrintContext* context = nullptr) const override;
    };

} // namespace ir
