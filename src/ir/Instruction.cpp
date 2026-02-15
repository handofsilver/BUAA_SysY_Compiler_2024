/**
 * @file Instruction.cpp
 * @brief All instruction implementations; header is declaration-only.
 */
#include "ir/Instruction.h"
#include "ir/BasicBlock.h"

namespace ir {

    // -----------------------------------------------------------------------------
    // Instruction (base)
    // -----------------------------------------------------------------------------
    Instruction::Instruction() : parent_(nullptr) {}

    Instruction::Instruction(const std::string& name, Type* type, BasicBlock* parent) :
    User(name, type),
    parent_(parent) {}

    BasicBlock* Instruction::GetParent() const {
        return parent_;
    }

    void Instruction::SetParent(BasicBlock* bb) {
        parent_ = bb;
    }

    // -----------------------------------------------------------------------------
    // AllocaInst
    // -----------------------------------------------------------------------------
    AllocaInst::AllocaInst() = default;

    AllocaInst::AllocaInst(const std::string& name, Type* type, BasicBlock* parent) :
    Instruction(name, type, parent) {}

    // -----------------------------------------------------------------------------
    // LoadInst
    // -----------------------------------------------------------------------------
    LoadInst::LoadInst() = default;

    LoadInst::LoadInst(const std::string& name, Type* type, BasicBlock* parent, Value* ptr) :
    Instruction(name, type, parent) {
        ResizeOperands(1);
        SetOperand(0, ptr);
    }

    Value* LoadInst::GetPointerOperand() const {
        return GetOperand(0);
    }

    // -----------------------------------------------------------------------------
    // StoreInst
    // -----------------------------------------------------------------------------
    StoreInst::StoreInst() = default;

    StoreInst::StoreInst(const std::string& name, Type* type, BasicBlock* parent, Value* value,
                         Value* ptr) :
    Instruction(name, type, parent) {
        ResizeOperands(2);
        SetOperand(0, value);
        SetOperand(1, ptr);
    }

    Value* StoreInst::GetValueOperand() const {
        return GetOperand(0);
    }

    Value* StoreInst::GetPointerOperand() const {
        return GetOperand(1);
    }

    // -----------------------------------------------------------------------------
    // BinaryInst
    // -----------------------------------------------------------------------------
    BinaryInst::BinaryInst() = default;

    BinaryInst::BinaryInst(const std::string& name, Type* type, BasicBlock* parent, OpType op,
                           Value* lhs, Value* rhs) :
    Instruction(name, type, parent),
    op_(op) {
        ResizeOperands(2);
        SetOperand(0, lhs);
        SetOperand(1, rhs);
    }

    OpType BinaryInst::GetOp() const {
        return op_;
    }

    Value* BinaryInst::GetLhs() const {
        return GetOperand(0);
    }

    Value* BinaryInst::GetRhs() const {
        return GetOperand(1);
    }

    // -----------------------------------------------------------------------------
    // BranchInst
    // -----------------------------------------------------------------------------
    BranchInst::BranchInst() : is_conditional_(false) {}

    BranchInst::BranchInst(const std::string& name, Type* type, BasicBlock* parent,
                           BasicBlock* dest) :
    Instruction(name, type, parent),
    is_conditional_(false) {
        ResizeOperands(1);
        SetOperand(0, dest);
    }

    BranchInst::BranchInst(const std::string& name, Type* type, BasicBlock* parent, Value* cond,
                           BasicBlock* if_true, BasicBlock* if_false) :
    Instruction(name, type, parent),
    is_conditional_(true) {
        ResizeOperands(3);
        SetOperand(0, cond);
        SetOperand(1, if_true);
        SetOperand(2, if_false);
    }

    BasicBlock* BranchInst::GetDest() const {
        return is_conditional_ ? nullptr : static_cast<BasicBlock*>(GetOperand(0));
    }

    Value* BranchInst::GetCond() const {
        return is_conditional_ ? GetOperand(0) : nullptr;
    }

    BasicBlock* BranchInst::GetIfTrue() const {
        return is_conditional_ ? static_cast<BasicBlock*>(GetOperand(1)) : nullptr;
    }

    BasicBlock* BranchInst::GetIfFalse() const {
        return is_conditional_ ? static_cast<BasicBlock*>(GetOperand(2)) : nullptr;
    }

    // -----------------------------------------------------------------------------
    // CallInst
    // -----------------------------------------------------------------------------
    CallInst::CallInst() = default;

    CallInst::CallInst(const std::string& name, Type* type, BasicBlock* parent, Value* callee,
                       const std::vector<Value*>& args) :
    Instruction(name, type, parent) {
        ResizeOperands(1 + static_cast<int>(args.size()));
        SetOperand(0, callee);
        for (size_t i = 0; i < args.size(); ++i) {
            SetOperand(static_cast<int>(i + 1), args[i]);
        }
    }

    Value* CallInst::GetCallee() const {
        return GetOperand(0);
    }

    Value* CallInst::GetArg(int i) const {
        return GetOperand(i + 1);
    }

    size_t CallInst::GetNumArgs() const {
        return GetNumOperands() > 0 ? static_cast<size_t>(GetNumOperands()) - 1 : 0;
    }

    // -----------------------------------------------------------------------------
    // ReturnInst
    // -----------------------------------------------------------------------------
    ReturnInst::ReturnInst() = default;

    ReturnInst::ReturnInst(const std::string& name, Type* type, BasicBlock* parent,
                           Value* ret_val) :
    Instruction(name, type, parent) {
        ResizeOperands(1);
        SetOperand(0, ret_val);
    }

    ReturnInst::ReturnInst(const std::string& name, Type* type, BasicBlock* parent) :
    Instruction(name, type, parent) {}

    Value* ReturnInst::GetRetVal() const {
        return GetNumOperands() > 0 ? GetOperand(0) : nullptr;
    }

    // -----------------------------------------------------------------------------
    // GetElementPtrInst
    // -----------------------------------------------------------------------------
    GetElementPtrInst::GetElementPtrInst() = default;

    GetElementPtrInst::GetElementPtrInst(const std::string& name, Type* type, BasicBlock* parent,
                                         Value* base, Value* index) :
    Instruction(name, type, parent) {
        ResizeOperands(2);
        SetOperand(0, base);
        SetOperand(1, index);
    }

    GetElementPtrInst::GetElementPtrInst(const std::string& name, Type* type, BasicBlock* parent,
                                         Value* base, Value* index0, Value* index1) :
    Instruction(name, type, parent) {
        ResizeOperands(3);
        SetOperand(0, base);
        SetOperand(1, index0);
        SetOperand(2, index1);
    }

    Value* GetElementPtrInst::GetPointerOperand() const {
        return GetOperand(0);
    }

    Value* GetElementPtrInst::GetIndex(int i) const {
        size_t idx = static_cast<size_t>(i);
        return GetNumOperands() > 1u + idx ? GetOperand(1 + i) : nullptr;
    }

    // -----------------------------------------------------------------------------
    // IcmpInst
    // -----------------------------------------------------------------------------
    IcmpInst::IcmpInst() = default;

    IcmpInst::IcmpInst(const std::string& name, Type* type, BasicBlock* parent, IcmpPred pred,
                       Value* lhs, Value* rhs) :
    Instruction(name, type, parent),
    pred_(pred) {
        ResizeOperands(2);
        SetOperand(0, lhs);
        SetOperand(1, rhs);
    }

    IcmpPred IcmpInst::GetPredicate() const {
        return pred_;
    }

    Value* IcmpInst::GetLhs() const {
        return GetOperand(0);
    }

    Value* IcmpInst::GetRhs() const {
        return GetOperand(1);
    }

    // -----------------------------------------------------------------------------
    // ZextInst
    // -----------------------------------------------------------------------------
    ZextInst::ZextInst() = default;

    ZextInst::ZextInst(const std::string& name, Type* dest_type, BasicBlock* parent, Value* value) :
    Instruction(name, dest_type, parent) {
        ResizeOperands(1);
        SetOperand(0, value);
    }

    Value* ZextInst::GetOperandValue() const {
        return GetOperand(0);
    }

    // -----------------------------------------------------------------------------
    // TruncInst
    // -----------------------------------------------------------------------------
    TruncInst::TruncInst() = default;

    TruncInst::TruncInst(const std::string& name, Type* dest_type, BasicBlock* parent,
                         Value* value) :
    Instruction(name, dest_type, parent) {
        ResizeOperands(1);
        SetOperand(0, value);
    }

    Value* TruncInst::GetOperandValue() const {
        return GetOperand(0);
    }

} // namespace ir
