/**
 * @file Instruction.h
 * @brief Instruction hierarchy: base Instruction and concrete opcodes.
 *
 * All instructions live inside a BasicBlock. They are Users (have operands)
 * and hold a pointer to their parent block.
 */

#ifndef IR_INSTRUCTION_H
#define IR_INSTRUCTION_H

#include "ir/BasicBlock.h" // BasicBlock extends Value; needed for BranchInst operands
#include "ir/User.h"
#include <AST.h>
#include <vector>

namespace ir {

    /**
     * @brief Base class for all IR instructions.
     *
     * Ownership: Instruction is owned by BasicBlock (unique_ptr in block).
     * parent_ is a non-owning pointer to the containing block.
     */
    class Instruction : public User {
    public:
        Instruction() : parent_(nullptr) {}
        Instruction(const std::string& name, Type* type, BasicBlock* parent) :
        User(name, type),
        parent_(parent) {}
        virtual ~Instruction() = default;

        BasicBlock* GetParent() const {
            return parent_;
        }
        void SetParent(BasicBlock* bb) {
            parent_ = bb;
        }

    protected:
        BasicBlock* parent_; /**< The basic block that contains this instruction. */
    };

    /** @brief alloca: allocate stack slot. */
    class AllocaInst : public Instruction {
    public:
        AllocaInst() = default;
        AllocaInst(const std::string& name, Type* type, BasicBlock* parent) :
        Instruction(name, type, parent) {}
    };

    /** @brief load: load from pointer. operand(0) = pointer. */
    class LoadInst : public Instruction {
    public:
        LoadInst() = default;
        LoadInst(const std::string& name, Type* type, BasicBlock* parent, Value* ptr) :
        Instruction(name, type, parent) {
            ResizeOperands(1);
            SetOperand(0, ptr);
        }
        Value* GetPointerOperand() const {
            return GetOperand(0);
        }
    };

    /** @brief store: store to pointer. operand(0)=value, operand(1)=pointer. */
    class StoreInst : public Instruction {
    public:
        StoreInst() = default;
        StoreInst(const std::string& name, Type* type, BasicBlock* parent, Value* value,
                  Value* ptr) :
        Instruction(name, type, parent) {
            ResizeOperands(2);
            SetOperand(0, value);
            SetOperand(1, ptr);
        }
        Value* GetValueOperand() const {
            return GetOperand(0);
        }
        Value* GetPointerOperand() const {
            return GetOperand(1);
        }
    };

    /** @brief Binary op: add, sub, mul, sdiv, srem, etc. */
    class BinaryInst : public Instruction {
    public:
        BinaryInst() = default;
        BinaryInst(const std::string& name, Type* type, BasicBlock* parent, OpType op, Value* lhs,
                   Value* rhs) :
        Instruction(name, type, parent),
        op_(op) {
            ResizeOperands(2);
            SetOperand(0, lhs);
            SetOperand(1, rhs);
        }

        OpType GetOp() const {
            return op_;
        }

    private:
        OpType op_;
    };

    /** @brief br: conditional or unconditional branch. Single source of truth: operands_. */
    class BranchInst : public Instruction {
    public:
        BranchInst() : is_conditional_(false) {}
        /** Unconditional: br label %dest. operands_[0] = dest. */
        BranchInst(const std::string& name, Type* type, BasicBlock* parent, BasicBlock* dest) :
        Instruction(name, type, parent),
        is_conditional_(false) {
            ResizeOperands(1);
            SetOperand(0, dest);
        }
        /** Conditional: br i1 %cond, label %if_true, label %if_false. operands_[0,1,2] = cond,
         * if_true, if_false. */
        BranchInst(const std::string& name, Type* type, BasicBlock* parent, Value* cond,
                   BasicBlock* if_true, BasicBlock* if_false) :
        Instruction(name, type, parent),
        is_conditional_(true) {
            ResizeOperands(3);
            SetOperand(0, cond);
            SetOperand(1, if_true);
            SetOperand(2, if_false);
        }

        bool IsConditional() const {
            return is_conditional_;
        }
        BasicBlock* GetDest() const {
            return is_conditional_ ? nullptr : static_cast<BasicBlock*>(GetOperand(0));
        }
        Value* GetCond() const {
            return is_conditional_ ? GetOperand(0) : nullptr;
        }
        BasicBlock* GetIfTrue() const {
            return is_conditional_ ? static_cast<BasicBlock*>(GetOperand(1)) : nullptr;
        }
        BasicBlock* GetIfFalse() const {
            return is_conditional_ ? static_cast<BasicBlock*>(GetOperand(2)) : nullptr;
        }

    private:
        bool is_conditional_;
    };
    /** @brief call: function call. operand(0)=callee (Function*), operands(1..n)=args. */
    class CallInst : public Instruction {
    public:
        CallInst() = default;
        CallInst(const std::string& name, Type* type, BasicBlock* parent, Value* callee,
                 const std::vector<Value*>& args) :
        Instruction(name, type, parent) {
            ResizeOperands(1 + args.size());
            SetOperand(0, callee);
            for (size_t i = 0; i < args.size(); ++i) {
                SetOperand(static_cast<int>(i + 1), args[i]);
            }
        }
        Value* GetCallee() const {
            return GetOperand(0);
        }
        Value* GetArg(int i) const {
            return GetOperand(i + 1);
        }
        size_t GetNumArgs() const {
            return GetNumOperands() > 0 ? GetNumOperands() - 1 : 0;
        }
    };

    /** @brief ret: return from function. operand(0) = return value (or no operand for ret void). */
    class ReturnInst : public Instruction {
    public:
        ReturnInst() = default;
        ReturnInst(const std::string& name, Type* type, BasicBlock* parent, Value* ret_val) :
        Instruction(name, type, parent) {
            ResizeOperands(1);
            SetOperand(0, ret_val);
        }
        Value* GetRetVal() const {
            return GetNumOperands() > 0 ? GetOperand(0) : nullptr;
        }
    };

    /** @brief getelementptr: address computation. operand(0)=base ptr, operand(1..n)=indices. */
    class GetElementPtrInst : public Instruction {
    public:
        GetElementPtrInst() = default;
        GetElementPtrInst(const std::string& name, Type* type, BasicBlock* parent, Value* base,
                          Value* index) :
        Instruction(name, type, parent) {
            ResizeOperands(2);
            SetOperand(0, base);
            SetOperand(1, index);
        }
        Value* GetPointerOperand() const {
            return GetOperand(0);
        }
        Value* GetIndex() const {
            return GetOperand(1);
        }
    };

} // namespace ir

#endif // IR_INSTRUCTION_H
