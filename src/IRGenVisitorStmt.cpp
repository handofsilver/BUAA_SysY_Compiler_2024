/**
 * @file IRGenVisitorStmt.cpp
 * @brief IRGenVisitor: block, statements (assign, if, for, break, continue, return, getint/getchar,
 * printf).
 */
#include "AST.h"
#include "IRGenVisitor.h"
#include "IRScopeGuard.h"

// -----------------------------------------------------------------------------
// Block and statements
// -----------------------------------------------------------------------------

void IRGenVisitor::VisitBlock(Block& block) {
    IRScopeGuard scope_guard(*this);
    for (auto& item : block.block_items) {
        item->Accept(*this);
    }
}

void IRGenVisitor::VisitBlockStmt(BlockStmt& block_stmt) {
    if (block_stmt.block) {
        block_stmt.block->Accept(*this);
    }
}

void IRGenVisitor::VisitAssignStmt(AssignStmt& assign_stmt) {
    is_lval_mode_ = true;
    assign_stmt.lval->Accept(*this);
    ir::Value* addr = temp_value_;
    is_lval_mode_ = false;
    assign_stmt.exp->Accept(*this);
    ir::Value* val = temp_value_;

    assert(addr != nullptr && val != nullptr && builder_->GetInsertBlock() != nullptr);
    ir::Type* target_ty = GetPointeeType(addr);

    assert(target_ty != nullptr);
    val = ConvertToTargetType(val, target_ty);
    builder_->CreateStore(val, addr);
}

void IRGenVisitor::VisitExpStmt(ExpStmt& exp_stmt) {
    if (exp_stmt.exp.has_value() && *exp_stmt.exp) {
        (*exp_stmt.exp)->Accept(*this);
    }
}

void IRGenVisitor::VisitIfStmt(IfStmt& if_stmt) {
    ir::BasicBlock* true_block = CreateBasicBlock("if.then");
    ir::BasicBlock* next_block = CreateBasicBlock("if.next");
    ir::BasicBlock* false_block = (if_stmt.else_stmt.has_value() && if_stmt.else_stmt->get()) ?
                                      CreateBasicBlock("if.else") :
                                      next_block;

    if_stmt.cond->Accept(*this);
    ir::Value* cond_i1 = CoerceToI1(temp_value_);
    if (!cond_i1 || !builder_->GetInsertBlock()) {
        return;
    }
    builder_->CreateCondBr(cond_i1, true_block, false_block);

    builder_->SetInsertPoint(true_block);
    if_stmt.then_stmt->Accept(*this);
    if (!IsBlockTerminated()) {
        builder_->CreateBr(next_block);
    }

    if (if_stmt.else_stmt.has_value() && if_stmt.else_stmt->get()) {
        builder_->SetInsertPoint(false_block);
        (*if_stmt.else_stmt)->Accept(*this);
        if (!IsBlockTerminated()) {
            builder_->CreateBr(next_block);
        }
    }

    builder_->SetInsertPoint(next_block);
}

void IRGenVisitor::VisitForStmt(ForStmt& for_stmt) {
    IRScopeGuard scope_guard(*this);

    if (for_stmt.init.has_value() && for_stmt.init->get()) {
        (*for_stmt.init)->Accept(*this);
    }

    ir::BasicBlock* cond_block = CreateBasicBlock("for.cond");
    ir::BasicBlock* body_block = CreateBasicBlock("for.body");
    ir::BasicBlock* step_block = CreateBasicBlock("for.step");
    ir::BasicBlock* after_block = CreateBasicBlock("for.after");

    if (!IsBlockTerminated()) {
        builder_->CreateBr(cond_block);
    }

    builder_->SetInsertPoint(cond_block);
    if (for_stmt.cond.has_value() && for_stmt.cond->get()) {
        (*for_stmt.cond)->Accept(*this);
        ir::Value* cond_val = temp_value_;
        if (cond_val && builder_->GetInsertBlock()) {
            ir::Value* cond_i1 = CoerceToI1(cond_val);
            builder_->CreateCondBr(cond_i1, body_block, after_block);
        } else {
            builder_->CreateBr(body_block);
        }
    } else {
        builder_->CreateBr(body_block);
    }

    break_targets_.push_back(after_block);
    continue_targets_.push_back(step_block);

    builder_->SetInsertPoint(body_block);
    for_stmt.body->Accept(*this);
    if (!IsBlockTerminated()) {
        builder_->CreateBr(step_block);
    }

    builder_->SetInsertPoint(step_block);
    if (for_stmt.step.has_value() && for_stmt.step->get()) {
        (*for_stmt.step)->Accept(*this);
    }
    builder_->CreateBr(cond_block);

    break_targets_.pop_back();
    continue_targets_.pop_back();
    builder_->SetInsertPoint(after_block);
}

void IRGenVisitor::VisitForInitOrStep(ForInitOrStep& for_init_or_step) {
    is_lval_mode_ = true;
    if (for_init_or_step.lval) {
        for_init_or_step.lval->Accept(*this);
    }
    is_lval_mode_ = false;
    ir::Value* addr = temp_value_;
    if (for_init_or_step.exp) {
        for_init_or_step.exp->Accept(*this);
    }
    ir::Value* val = temp_value_;
    if (addr && val && builder_->GetInsertBlock()) {
        ir::Type* target_ty = GetPointeeType(addr);
        if (target_ty) {
            val = ConvertToTargetType(val, target_ty);
        }
        builder_->CreateStore(val, addr);
    }
}

void IRGenVisitor::VisitBreakStmt(BreakStmt& break_stmt) {
    (void)break_stmt;
    if (!break_targets_.empty()) {
        builder_->CreateBr(break_targets_.back());
    }
}

void IRGenVisitor::VisitContinueStmt(ContinueStmt& continue_stmt) {
    (void)continue_stmt;
    if (!continue_targets_.empty()) {
        builder_->CreateBr(continue_targets_.back());
    }
}

void IRGenVisitor::VisitReturnStmt(ReturnStmt& return_stmt) {
    if (return_stmt.exp.has_value() && *return_stmt.exp) {
        (*return_stmt.exp)->Accept(*this);
        ir::Value* val = temp_value_;
        if (val && builder_->GetInsertBlock() && current_function_) {
            ir::Type* ft = current_function_->GetType();
            if (auto* fty = dynamic_cast<ir::FunctionType*>(ft)) {
                ir::Type* ret_ty = fty->GetReturnType();
                if (ret_ty && ret_ty != types_.GetVoidType()) {
                    val = ConvertToTargetType(val, ret_ty);
                }
            }
            builder_->CreateRet(val);
        }
    } else {
        builder_->CreateRetVoid();
    }
}

void IRGenVisitor::VisitGetintStmt(GetintStmt& getint_stmt) {
    is_lval_mode_ = true;
    getint_stmt.lval->Accept(*this);
    is_lval_mode_ = false;
    ir::Value* addr = temp_value_;
    ir::Instruction* call =
        builder_->CreateCall(types_.GetI32Type(), module_->GetFunction("getint"), {});
    if (addr && call && builder_->GetInsertBlock()) {
        ir::Type* target_ty = GetPointeeType(addr);
        ir::Value* to_store = target_ty ? ConvertToTargetType(call, target_ty) : call;
        builder_->CreateStore(to_store, addr);
    }
}

void IRGenVisitor::VisitGetcharStmt(GetcharStmt& getchar_stmt) {
    is_lval_mode_ = true;
    getchar_stmt.lval->Accept(*this);
    is_lval_mode_ = false;
    ir::Value* addr = temp_value_;
    ir::Instruction* call =
        builder_->CreateCall(types_.GetI32Type(), module_->GetFunction("getchar"), {});
    if (addr && call && builder_->GetInsertBlock()) {
        ir::Type* target_ty = GetPointeeType(addr);
        ir::Value* to_store = target_ty ? ConvertToTargetType(call, target_ty) : call;
        builder_->CreateStore(to_store, addr);
    }
}

void IRGenVisitor::VisitPrintfStmt(PrintfStmt& printf_stmt) {
    if (!builder_->GetInsertBlock()) {
        return;
    }
    const std::string& fmt = printf_stmt.format_string;
    size_t exp_idx = 0;
    std::string literal;

    for (size_t i = 0; i < fmt.size(); ++i) {
        if (fmt[i] == '%' && i + 1 < fmt.size()) {
            if (!literal.empty()) {
                ir::Value* str_ptr = EmitGlobalStringLiteral(literal);
                if (str_ptr) {
                    ir::Function* putstr_fn = module_->GetFunction("putstr");
                    if (putstr_fn) {
                        builder_->CreateCall(types_.GetVoidType(), putstr_fn, {str_ptr});
                    }
                }
                literal.clear();
            }
            if (fmt[i + 1] == 'd') {
                if (exp_idx < printf_stmt.exp_list.size()) {
                    printf_stmt.exp_list[exp_idx]->Accept(*this);
                    ir::Value* val = PromoteToI32(temp_value_);
                    if (val) {
                        ir::Function* putint_fn = module_->GetFunction("putint");
                        if (putint_fn) {
                            builder_->CreateCall(types_.GetVoidType(), putint_fn, {val});
                        }
                    }
                    ++exp_idx;
                }
                ++i;
            } else if (fmt[i + 1] == 'c') {
                if (exp_idx < printf_stmt.exp_list.size()) {
                    printf_stmt.exp_list[exp_idx]->Accept(*this);
                    ir::Value* val = PromoteToI32(temp_value_);
                    if (val) {
                        ir::Function* putch_fn = module_->GetFunction("putch");
                        if (putch_fn) {
                            builder_->CreateCall(types_.GetVoidType(), putch_fn, {val});
                        }
                    }
                    ++exp_idx;
                }
                ++i;
            } else if (fmt[i + 1] == '%') {
                literal.push_back('%');
                ++i;
            }
        } else {
            literal.push_back(fmt[i]);
        }
    }
    if (!literal.empty()) {
        ir::Value* str_ptr = EmitGlobalStringLiteral(literal);
        if (str_ptr) {
            ir::Function* putstr_fn = module_->GetFunction("putstr");
            if (putstr_fn) {
                builder_->CreateCall(types_.GetVoidType(), putstr_fn, {str_ptr});
            }
        }
    }
}
