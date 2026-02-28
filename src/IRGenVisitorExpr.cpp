/**
 * @file IRGenVisitorExpr.cpp
 * @brief IRGenVisitor: expressions (LVal, number, character, binary/unary, func call, const exp).
 */
#include "AST.h"
#include "IRGenVisitor.h"
#include "ir/Instruction.h"
#include "ir/Type.h"
#include "irgen/TypeMapping.h"
#include <optional>

// -----------------------------------------------------------------------------
// Expressions (result in temp_value_)
// -----------------------------------------------------------------------------

void IRGenVisitor::VisitLVal(LVal& lval) {
    ir::Value* value = LookupVariable(lval.ident);
    if (!value) {
        return;
    }

    // scalar lval or array name (no index)
    if (!lval.index.has_value() || !*lval.index) {
        if (is_lval_mode_ || func_arg_want_pointer_) {
            // function argument needs pointer (array/pointer parameter): pass address, not load
            temp_value_ = value;
        } else {
            ir::Type* pointee = GetPointeeType(value);
            if (pointee && dynamic_cast<ir::ArrayType*>(pointee)) {
                temp_value_ = value;
            } else {
                ir::Instruction* load = builder_->CreateLoad(value);
                temp_value_ = load ? load : value;
            }
        }
        return;
    }

    // array lval
    // set is_lval_mode_ to false as index should not be used as lval
    bool is_lval_mode_backup = is_lval_mode_;
    is_lval_mode_ = false;
    (*lval.index)->Accept(*this);
    is_lval_mode_ = is_lval_mode_backup;

    ir::Value* index_val = temp_value_;
    if (!index_val || !builder_->GetInsertBlock()) {
        return;
    }
    index_val = PromoteToI32(index_val);
    auto* ptr_ty = dynamic_cast<ir::PointerType*>(value->GetType());
    if (!ptr_ty) {
        temp_value_ = value;
        return;
    }
    ir::Type* pointee = ptr_ty->GetPointeeType();
    ir::Type* elem_ty = nullptr;
    ir::Instruction* gep = nullptr;
    if (auto* arr_ty = dynamic_cast<ir::ArrayType*>(pointee)) {
        elem_ty = arr_ty->GetElementType();
        gep = builder_->CreateGEP(types_.GetPointerType(elem_ty), value,
                                  module_->GetInt32Constant(0), index_val);
    } else {
        elem_ty = pointee;
        gep = builder_->CreateGEP(types_.GetPointerType(elem_ty), value, index_val);
    }
    if (!gep) {
        temp_value_ = value;
        return;
    }
    if (is_lval_mode_) {
        temp_value_ = gep;
    } else {
        ir::Instruction* load = builder_->CreateLoad(gep);
        temp_value_ = load ? load : gep;
    }
}

void IRGenVisitor::VisitNumber(Number& number) {
    temp_value_ = module_->GetInt32Constant(number.int_const);
}

void IRGenVisitor::VisitCharacter(Character& character) {
    temp_value_ = module_->GetInt8Constant(character.char_const);
}

void IRGenVisitor::VisitBinaryExp(BinaryExp& binary_exp) {
    OpType op = binary_exp.op;

    if (op == OpType::AND) {
        EmitShortCircuitAND(binary_exp.lhs.get(), binary_exp.rhs.get());
        return;
    }
    if (op == OpType::OR) {
        EmitShortCircuitOR(binary_exp.lhs.get(), binary_exp.rhs.get());
        return;
    }

    binary_exp.lhs->Accept(*this);
    ir::Value* lhs = temp_value_;
    binary_exp.rhs->Accept(*this);
    ir::Value* rhs = temp_value_;
    if (!lhs || !rhs || !builder_->GetInsertBlock()) {
        return;
    }
    std::optional<ir::BinaryOp> bop = irgen::OpTypeToBinaryOp(op);
    if (bop) {
        lhs = PromoteToI32(lhs);
        rhs = PromoteToI32(rhs);
        ir::Instruction* inst = builder_->CreateBinary(*bop, lhs, rhs);
        temp_value_ = inst ? inst : temp_value_;
        return;
    }
    std::optional<ir::IcmpPred> pred = irgen::OpTypeToIcmpPred(op);
    if (pred) {
        lhs = PromoteToI32(lhs);
        rhs = PromoteToI32(rhs);
        ir::Instruction* cmp = builder_->CreateIcmp(types_.GetI1Type(), *pred, lhs, rhs);
        if (cmp) {
            ir::Instruction* zext = builder_->CreateZext(cmp, types_.GetI32Type());
            temp_value_ = zext ? zext : cmp;
        }
        return;
    }
}

void IRGenVisitor::VisitUnaryExp(UnaryExp& unary_exp) {
    unary_exp.operand->Accept(*this);
    ir::Value* operand = temp_value_;
    if (!operand || !builder_->GetInsertBlock()) {
        return;
    }

    OpType op = unary_exp.op;
    ir::ConstantInt* zero = module_->GetInt32Constant(0);

    if (op == OpType::MINU) {
        operand = PromoteToI32(operand);
        ir::Instruction* inst = builder_->CreateBinary(ir::BinaryOp::SUB, zero, operand);
        temp_value_ = inst ? inst : temp_value_;
        return;
    }
    if (op == OpType::NOT) {
        operand = PromoteToI32(operand);
        ir::Instruction* cmp =
            builder_->CreateIcmp(types_.GetI1Type(), ir::IcmpPred::EQ, operand, zero);
        if (cmp) {
            ir::Instruction* zext = builder_->CreateZext(cmp, types_.GetI32Type());
            temp_value_ = zext ? zext : cmp;
        }
        return;
    }
}

void IRGenVisitor::VisitFuncCall(FuncCall& func_call) {
    ir::Function* callee = module_->GetFunction(func_call.ident);

    assert(callee != nullptr && builder_->GetInsertBlock() != nullptr);

    // get return type and parameter types
    ir::Type* ret_type = nullptr;
    std::vector<ir::Type*> param_types;
    if (ir::Type* ft = callee->GetType()) {
        if (auto* fty = dynamic_cast<ir::FunctionType*>(ft)) {
            ret_type = fty->GetReturnType();
            param_types = fty->GetParamTypes();
        }
    }

    // collect call arguments
    call_args_.clear();
    if (func_call.func_r_params) {
        const auto& exps = func_call.func_r_params->exp_list;
        assert(exps.size() == param_types.size());
        for (size_t i = 0; i < exps.size(); ++i) {
            func_arg_want_pointer_ = (dynamic_cast<ir::PointerType*>(param_types[i]) != nullptr);
            exps[i]->Accept(*this);
            func_arg_want_pointer_ = false;
            if (temp_value_) {
                call_args_.push_back(temp_value_);
            }
        }
    }

    // convert arguments to target types
    assert(call_args_.size() == param_types.size());
    std::vector<ir::Value*> converted_args;
    for (size_t i = 0; i < call_args_.size(); ++i) {
        ir::Value* arg = call_args_[i];
        converted_args.push_back(ConvertToTargetType(arg, param_types[i]));
    }
    ir::Instruction* call = builder_->CreateCall(ret_type, callee, converted_args);
    if (call && ret_type && ret_type != types_.GetVoidType()) {
        temp_value_ = call;
    }
}

void IRGenVisitor::VisitFuncRParams(FuncRParams& func_r_params) {
    for (auto& exp : func_r_params.exp_list) {
        exp->Accept(*this);
        if (temp_value_) {
            call_args_.push_back(temp_value_);
        }
    }
}

void IRGenVisitor::VisitConstExp(ConstExp& const_exp) {
    if (const_exp.const_value.has_value()) {
        temp_value_ = module_->GetInt32Constant(const_exp.const_value.value());
    } else if (const_exp.inner) {
        const_exp.inner->Accept(*this);
    }
}
