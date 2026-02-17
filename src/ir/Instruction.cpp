/**
 * @file Instruction.cpp
 * @brief All instruction implementations; header is declaration-only.
 */
#include "ir/Instruction.h"
#include "ir/BasicBlock.h"
#include "ir/Constant.h"
#include "ir/Type.h"
#include <ostream>

namespace ir {

    namespace {
        const char* BinaryOpToMnemonic(BinaryOp op) {
            switch (op) {
                case BinaryOp::ADD: return "add";
                case BinaryOp::SUB: return "sub";
                case BinaryOp::MUL: return "mul";
                case BinaryOp::DIV: return "sdiv";
                case BinaryOp::REM: return "srem";
                default: return "add";
            }
        }
        bool BinaryOpHasNsw(BinaryOp op) {
            return op == BinaryOp::ADD || op == BinaryOp::SUB || op == BinaryOp::MUL;
        }
        const char* IcmpPredToMnemonic(IcmpPred pred) {
            switch (pred) {
                case IcmpPred::SLT: return "slt";
                case IcmpPred::SGT: return "sgt";
                case IcmpPred::SLE: return "sle";
                case IcmpPred::SGE: return "sge";
                case IcmpPred::EQ: return "eq";
                case IcmpPred::NE: return "ne";
                default: return "eq";
            }
        }
    } // namespace

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

    void AllocaInst::Print(std::ostream& os) const {
        os << "  %" << GetName() << " = alloca ";
        if (type_) {
            PointerType* pt = dynamic_cast<PointerType*>(type_);
            if (pt && pt->GetPointeeType()) {
                pt->GetPointeeType()->Print(os);
            } else {
                type_->Print(os);
            }
        }
        os << ", align 4";
    }

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

    void LoadInst::Print(std::ostream& os) const {
        os << "  %" << GetName() << " = load ";
        if (type_) {
            type_->Print(os);
        }
        os << ", ptr ";
        if (Value* ptr = GetPointerOperand()) {
            ptr->PrintAsOperand(os);
        }
        os << ", align 4";
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

    void StoreInst::Print(std::ostream& os) const {
        Value* val = GetValueOperand();
        Value* ptr = GetPointerOperand();
        os << "  store ";
        if (type_) {
            type_->Print(os);
        }
        os << " ";
        if (val) {
            if (ConstantInt* c = dynamic_cast<ConstantInt*>(val)) {
                os << c->GetValue();
            } else {
                val->PrintAsOperand(os);
            }
        }
        os << ", ptr ";
        if (ptr) {
            ptr->PrintAsOperand(os);
        }
        os << ", align 4";
    }

    // -----------------------------------------------------------------------------
    // BinaryInst
    // -----------------------------------------------------------------------------
    BinaryInst::BinaryInst() = default;

    BinaryInst::BinaryInst(const std::string& name, Type* type, BasicBlock* parent, BinaryOp op,
                           Value* lhs, Value* rhs) :
    Instruction(name, type, parent),
    op_(op) {
        ResizeOperands(2);
        SetOperand(0, lhs);
        SetOperand(1, rhs);
    }

    BinaryOp BinaryInst::GetOp() const {
        return op_;
    }

    Value* BinaryInst::GetLhs() const {
        return GetOperand(0);
    }

    Value* BinaryInst::GetRhs() const {
        return GetOperand(1);
    }

    void BinaryInst::Print(std::ostream& os) const {
        Value* lhs = GetLhs();
        Value* rhs = GetRhs();
        const char* mnemonic = BinaryOpToMnemonic(op_);
        os << "  %" << GetName() << " = " << mnemonic;
        if (BinaryOpHasNsw(op_)) {
            os << " nsw ";
        } else {
            os << " ";
        }
        if (type_) {
            type_->Print(os);
        }
        os << " ";
        if (lhs) {
            if (ConstantInt* c = dynamic_cast<ConstantInt*>(lhs)) {
                os << c->GetValue();
            } else {
                lhs->PrintAsOperand(os);
            }
        }
        os << ", ";
        if (rhs) {
            if (ConstantInt* c = dynamic_cast<ConstantInt*>(rhs)) {
                os << c->GetValue();
            } else {
                rhs->PrintAsOperand(os);
            }
        }
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

    void BranchInst::Print(std::ostream& os) const {
        os << "  br ";
        if (is_conditional_) {
            os << "i1 ";
            if (Value* cond = GetCond()) {
                cond->PrintAsOperand(os);
            }
            os << ", ";
            if (BasicBlock* t = GetIfTrue()) {
                os << "label %" << t->GetName();
            }
            os << ", ";
            if (BasicBlock* f = GetIfFalse()) {
                os << "label %" << f->GetName();
            }
        } else {
            os << "label ";
            if (BasicBlock* dest = GetDest()) {
                os << "%" << dest->GetName();
            }
        }
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

    void CallInst::Print(std::ostream& os) const {
        Type* ty = GetType();
        bool is_void = ty && ty->GetTypeId() == TypeID::VOID_TY_ID;
        if (!is_void) {
            os << "  %" << GetName() << " = ";
        } else {
            os << "  ";
        }
        os << "call ";
        if (ty) {
            ty->Print(os);
        }
        os << " ";
        if (Value* callee = GetCallee()) {
            callee->PrintAsOperand(os);
        }
        os << "(";
        for (size_t i = 0; i < GetNumArgs(); ++i) {
            if (i != 0) {
                os << ", ";
            }
            Value* arg = GetArg(static_cast<int>(i));
            if (arg) {
                if (arg->GetType()) {
                    arg->GetType()->Print(os);
                }
                os << " ";
                if (ConstantInt* c = dynamic_cast<ConstantInt*>(arg)) {
                    os << c->GetValue();
                } else {
                    arg->PrintAsOperand(os);
                }
            }
        }
        os << ")";
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

    void ReturnInst::Print(std::ostream& os) const {
        os << "  ret ";
        Value* ret_val = GetRetVal();
        if (ret_val && ret_val->GetType()) {
            ret_val->GetType()->Print(os);
            os << " ";
            if (ConstantInt* c = dynamic_cast<ConstantInt*>(ret_val)) {
                os << c->GetValue();
            } else {
                ret_val->PrintAsOperand(os);
            }
        } else {
            os << "void";
        }
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

    void GetElementPtrInst::Print(std::ostream& os) const {
        Value* base = GetPointerOperand();
        if (!base || !base->GetType()) {
            return;
        }
        Type* base_ty = base->GetType();
        PointerType* ptr_ty = base_ty ? dynamic_cast<PointerType*>(base_ty) : nullptr;
        Type* pointee = ptr_ty ? ptr_ty->GetPointeeType() : nullptr;
        os << "  %" << GetName() << " = getelementptr inbounds ";
        if (pointee) {
            pointee->Print(os);
        } else {
            os << "i32";
        }
        os << ", ptr ";
        base->PrintAsOperand(os);
        Value* idx0 = GetIndex(0);
        Value* idx1 = GetIndex(1);
        if (idx0) {
            os << ", i32 ";
            if (ConstantInt* c = dynamic_cast<ConstantInt*>(idx0)) {
                os << c->GetValue();
            } else {
                idx0->PrintAsOperand(os);
            }
        }
        if (idx1) {
            os << ", i32 ";
            if (ConstantInt* c = dynamic_cast<ConstantInt*>(idx1)) {
                os << c->GetValue();
            } else {
                idx1->PrintAsOperand(os);
            }
        }
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

    void IcmpInst::Print(std::ostream& os) const {
        Value* lhs = GetLhs();
        Value* rhs = GetRhs();
        os << "  %" << GetName() << " = icmp " << IcmpPredToMnemonic(pred_) << " ";
        if (lhs && lhs->GetType()) {
            lhs->GetType()->Print(os);
            os << " ";
            if (ConstantInt* c = dynamic_cast<ConstantInt*>(lhs)) {
                os << c->GetValue();
            } else {
                lhs->PrintAsOperand(os);
            }
        }
        os << ", ";
        if (rhs) {
            if (ConstantInt* c = dynamic_cast<ConstantInt*>(rhs)) {
                os << c->GetValue();
            } else {
                rhs->PrintAsOperand(os);
            }
        }
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

    void ZextInst::Print(std::ostream& os) const {
        Value* op = GetOperandValue();
        os << "  %" << GetName() << " = zext ";
        if (op && op->GetType()) {
            op->GetType()->Print(os);
        } else {
            os << "i1";
        }
        os << " ";
        if (op) {
            if (ConstantInt* c = dynamic_cast<ConstantInt*>(op)) {
                os << c->GetValue();
            } else {
                op->PrintAsOperand(os);
            }
        }
        os << " to ";
        if (type_) {
            type_->Print(os);
        } else {
            os << "i32";
        }
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

    void TruncInst::Print(std::ostream& os) const {
        Value* op = GetOperandValue();
        os << "  %" << GetName() << " = trunc ";
        if (op && op->GetType()) {
            op->GetType()->Print(os);
        } else {
            os << "i32";
        }
        os << " ";
        if (op) {
            if (ConstantInt* c = dynamic_cast<ConstantInt*>(op)) {
                os << c->GetValue();
            } else {
                op->PrintAsOperand(os);
            }
        }
        os << " to ";
        if (type_) {
            type_->Print(os);
        } else {
            os << "i8";
        }
    }

} // namespace ir
