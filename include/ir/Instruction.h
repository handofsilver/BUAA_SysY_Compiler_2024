/**
 * @file Instruction.h
 * @brief Instruction hierarchy: base Instruction and concrete opcodes.
 *
 * All instructions live inside a BasicBlock. They are Users (have operands)
 * and hold a pointer to their parent block.
 */

#ifndef IR_INSTRUCTION_H
#define IR_INSTRUCTION_H

#include "ir/User.h"

namespace ir {

    class BasicBlock;

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

    /** @brief load: load from pointer. */
    class LoadInst : public Instruction {
    public:
        LoadInst() = default;
        LoadInst(const std::string& name, Type* type, BasicBlock* parent) :
        Instruction(name, type, parent) {}
    };

    /** @brief store: store to pointer. */
    class StoreInst : public Instruction {
    public:
        StoreInst() = default;
        StoreInst(const std::string& name, Type* type, BasicBlock* parent) :
        Instruction(name, type, parent) {}
    };

    /** @brief Binary op: add, sub, mul, sdiv, srem, etc. */
    class BinaryInst : public Instruction {
    public:
        BinaryInst() = default;
        BinaryInst(const std::string& name, Type* type, BasicBlock* parent) :
        Instruction(name, type, parent) {}
    };

    /** @brief br: conditional or unconditional branch. */
    class BranchInst : public Instruction {
    public:
        BranchInst() = default;
        BranchInst(const std::string& name, Type* type, BasicBlock* parent) :
        Instruction(name, type, parent) {}
    };

    /** @brief call: function call. */
    class CallInst : public Instruction {
    public:
        CallInst() = default;
        CallInst(const std::string& name, Type* type, BasicBlock* parent) :
        Instruction(name, type, parent) {}
    };

    /** @brief ret: return from function. */
    class ReturnInst : public Instruction {
    public:
        ReturnInst() = default;
        ReturnInst(const std::string& name, Type* type, BasicBlock* parent) :
        Instruction(name, type, parent) {}
    };

    /** @brief getelementptr: address computation. */
    class GetElementPtrInst : public Instruction {
    public:
        GetElementPtrInst() = default;
        GetElementPtrInst(const std::string& name, Type* type, BasicBlock* parent) :
        Instruction(name, type, parent) {}
    };

} // namespace ir

#endif // IR_INSTRUCTION_H
