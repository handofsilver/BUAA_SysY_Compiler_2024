/**
 * @file IRGenVisitorExpr.cpp
 * @brief IRGenVisitor: expressions (LVal, number, character, binary/unary, func call, const exp).
 */
#include "AST.h"
#include "ir/Instruction.h"
#include "ir/Type.h"
#include "irgen/IRGenVisitor.h"
#include "irgen/TypeMapping.h"
#include <optional>

// -----------------------------------------------------------------------------
// Expressions (result in temp_value_)
// -----------------------------------------------------------------------------

void IRGenVisitor::VisitLVal(LVal& lval) {
    ir::Value* value = ctx_.LookupVariable(lval.ident);
    assert(value);

    // scalar lval or array name (no index)
    if (!lval.index.has_value() || !*lval.index) {
        if (is_lval_mode_ || func_arg_want_pointer_) {
            temp_value_ = value;
        } else {
            ir::Type* pointee = GetPointeeType(value);
            if (pointee && dynamic_cast<ir::ArrayType*>(pointee)) {
                temp_value_ = value;
            } else {
                ir::Instruction* load = ctx_.builder->CreateLoad(value);
                assert(load);
                temp_value_ = load;
            }
        }
        return;
    }

    // array lval
    bool is_lval_mode_backup = is_lval_mode_;
    is_lval_mode_ = false;
    (*lval.index)->Accept(*this);
    is_lval_mode_ = is_lval_mode_backup;

    ir::Value* index_val = temp_value_;
    assert(index_val && ctx_.builder->GetInsertBlock());

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
        gep = ctx_.builder->CreateGEP(ctx_.types.GetPointerType(elem_ty), value,
                                      ctx_.module->GetInt32Constant(0), index_val);
    } else {
        elem_ty = pointee;
        gep = ctx_.builder->CreateGEP(ctx_.types.GetPointerType(elem_ty), value, index_val);
    }
    assert(gep);

    if (is_lval_mode_) {
        temp_value_ = gep;
    } else {
        ir::Instruction* load = ctx_.builder->CreateLoad(gep);
        assert(load);
        temp_value_ = load;
    }
}

void IRGenVisitor::VisitNumber(Number& number) {
    temp_value_ = ctx_.module->GetInt32Constant(number.int_const);
}

void IRGenVisitor::VisitCharacter(Character& character) {
    temp_value_ = ctx_.module->GetInt8Constant(character.char_const);
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
    assert(lhs && rhs && ctx_.builder->GetInsertBlock());

    std::optional<ir::BinaryOp> bop = irgen::OpTypeToBinaryOp(op);
    if (bop) {
        lhs = PromoteToI32(lhs);
        rhs = PromoteToI32(rhs);
        ir::Instruction* inst = ctx_.builder->CreateBinary(*bop, lhs, rhs);
        assert(inst);
        temp_value_ = inst;
        return;
    }
    std::optional<ir::IcmpPred> pred = irgen::OpTypeToIcmpPred(op);
    if (pred) {
        lhs = PromoteToI32(lhs);
        rhs = PromoteToI32(rhs);
        ir::Instruction* cmp = ctx_.builder->CreateIcmp(ctx_.types.GetI1Type(), *pred, lhs, rhs);
        assert(cmp);
        ir::Instruction* zext = ctx_.builder->CreateZext(cmp, ctx_.types.GetI32Type());
        assert(zext);
        temp_value_ = zext;
        return;
    }
}

void IRGenVisitor::VisitUnaryExp(UnaryExp& unary_exp) {
    unary_exp.operand->Accept(*this);

    ir::Value* operand = temp_value_;
    assert(operand && ctx_.builder->GetInsertBlock());

    OpType op = unary_exp.op;
    ir::ConstantInt* zero = ctx_.module->GetInt32Constant(0);

    if (op == OpType::MINU) {
        operand = PromoteToI32(operand);
        ir::Instruction* inst = ctx_.builder->CreateBinary(ir::BinaryOp::SUB, zero, operand);
        assert(inst);
        temp_value_ = inst;
        return;
    }
    if (op == OpType::NOT) {
        operand = PromoteToI32(operand);
        ir::Instruction* cmp =
            ctx_.builder->CreateIcmp(ctx_.types.GetI1Type(), ir::IcmpPred::EQ, operand, zero);
        assert(cmp);
        ir::Instruction* zext = ctx_.builder->CreateZext(cmp, ctx_.types.GetI32Type());
        assert(zext);
        temp_value_ = zext;
        return;
    }
}

void IRGenVisitor::VisitFuncCall(FuncCall& func_call) {
    ir::Function* callee = ctx_.module->GetFunction(func_call.ident);

    assert(callee != nullptr && ctx_.builder->GetInsertBlock() != nullptr);

    ir::Type* ret_type = nullptr;
    std::vector<ir::Type*> param_types;
    if (ir::Type* ft = callee->GetType()) {
        if (auto* fty = dynamic_cast<ir::FunctionType*>(ft)) {
            ret_type = fty->GetReturnType();
            param_types = fty->GetParamTypes();
        }
    }

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

    assert(call_args_.size() == param_types.size());
    std::vector<ir::Value*> converted_args;
    for (size_t i = 0; i < call_args_.size(); ++i) {
        ir::Value* arg = call_args_[i];
        converted_args.push_back(ConvertToTargetType(arg, param_types[i]));
    }
    ir::Instruction* call = ctx_.builder->CreateCall(ret_type, callee, converted_args);
    if (call && ret_type && ret_type != ctx_.types.GetVoidType()) {
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
        temp_value_ = ctx_.module->GetInt32Constant(const_exp.const_value.value());
    } else if (const_exp.inner) {
        const_exp.inner->Accept(*this);
    }
}
