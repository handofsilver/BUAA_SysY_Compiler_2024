/**
 * @file IRGenVisitor.cpp
 * @brief Implementation of IRGenVisitor: AST traversal and IR construction.
 *
 * Visit* bodies are left as stubs with brief guidance; implement according to
 * the grammar and docs/ai_collab_notes/llvm_ir_instructions_summary.md.
 */
#include "IRGenVisitor.h"
#include "AST.h"

namespace {

    // Optional: include IR instruction headers if you need concrete types in this file.
    // #include "ir/Instruction.h"
    // #include "ir/Type.h"

} // namespace

// -----------------------------------------------------------------------------
// Constructor and module access
// -----------------------------------------------------------------------------

IRGenVisitor::IRGenVisitor() {
    module_ = std::make_unique<ir::Module>();
    builder_ = std::make_unique<ir::IRBuilder>();
}

std::unique_ptr<ir::Module> IRGenVisitor::GetModule() {
    return std::move(module_);
}

// -----------------------------------------------------------------------------
// Scope helpers (symbol table)
// -----------------------------------------------------------------------------

ir::Value* IRGenVisitor::LookupVariable(const std::string& name) {
    for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
        auto i = it->map.find(name);
        if (i != it->map.end()) {
            return i->second;
        }
    }
    return nullptr;
}

void IRGenVisitor::PushScope() {
    current_scope_id_ = next_scope_id_++;
    scopes_.push_back(Scope{current_scope_id_, {}});
}

void IRGenVisitor::PopScope() {
    if (scopes_.empty()) {
        return;
    }
    scopes_.pop_back();
    current_scope_id_ = scopes_.empty() ? 0 : scopes_.back().id;
}

// -----------------------------------------------------------------------------
// CompUnit and declarations (global vs local)
// -----------------------------------------------------------------------------

void IRGenVisitor::VisitCompUnit(CompUnit& comp_unit) {
    // Implement (exercise): Set is_global_ = true, push global scope. Iterate decls and func defs,
    // call Accept() on each. Optionally declare I/O (getint, getchar, putint, putch, putstr).
}

void IRGenVisitor::VisitConstDecl(ConstDecl& const_decl) {
    // Implement (exercise): For each ConstDef, create global constant or local alloca + store
    // depending on is_global_. Use ConstInitVal for initializer.
}

void IRGenVisitor::VisitVarDecl(VarDecl& var_decl) {
    // Implement (exercise): For each VarDef, create global variable or local alloca (and store if
    // InitVal present). Respect is_global_.
}

void IRGenVisitor::VisitConstDef(ConstDef& const_def) {
    // Implement (exercise): Create constant/alloc, bind name in current scope. Evaluate
    // ConstInitVal.
}

void IRGenVisitor::VisitVarDef(VarDef& var_def) {
    // Implement (exercise): Create global or alloca, optional init from InitVal, bind name in
    // scope.
}

void IRGenVisitor::VisitConstInitVal(ConstInitVal& const_init_val) {
    // Implement (exercise): Evaluate constant initializer (ConstExp or list); result used by
    // ConstDef.
}

void IRGenVisitor::VisitInitVal(InitVal& init_val) {
    // Implement (exercise): Evaluate initializer (Exp or list); result stored by VarDef.
}

// -----------------------------------------------------------------------------
// Functions
// -----------------------------------------------------------------------------

void IRGenVisitor::VisitFuncDef(FuncDef& func_def) {
    // Implement (exercise): Create Function, add entry BasicBlock, set current_function_ and
    // builder_ insert point. Alloca for each FuncFParam and store args. Visit Block.
}

void IRGenVisitor::VisitMainFuncDef(MainFuncDef& main_func_def) {
    // Implement (exercise): Same as FuncDef for main: define i32 @main(), then visit body.
}

void IRGenVisitor::VisitFuncFParam(FuncFParam& func_f_param) {
    // Implement (exercise): Create alloca for parameter (or register), bind name in scope.
}

// -----------------------------------------------------------------------------
// Block and statements
// -----------------------------------------------------------------------------

void IRGenVisitor::VisitBlock(Block& block) {
    // Implement (exercise): PushScope(). For each BlockItem, Accept(). PopScope().
}

void IRGenVisitor::VisitBlockStmt(BlockStmt& block_stmt) {
    // Implement (exercise): Visit the inner Block (it will push/pop scope).
}

void IRGenVisitor::VisitAssignStmt(AssignStmt& assign_stmt) {
    // Implement (exercise): Set is_lval_mode_ = true, visit LVal (get address). Set is_lval_mode_ =
    // false, visit Exp (get value). Create store value -> address.
}

void IRGenVisitor::VisitExpStmt(ExpStmt& exp_stmt) {
    // Implement (exercise): If expression present, visit it (result in temp_value_; may be unused).
}

void IRGenVisitor::VisitIfStmt(IfStmt& if_stmt) {
    // Implement (exercise): Visit Cond (temp_value_ = i1). Create cond_br, then/else/merge blocks,
    // set insert point and visit Stmt(s).
}

void IRGenVisitor::VisitForStmt(ForStmt& for_stmt) {
    // Implement (exercise): Create cond/body/step/end blocks. Push break_targets_.push_back(end),
    // continue_targets_.push_back(step). Visit init, cond, body, step; wire br.
    // Pop break/continue targets.
}

void IRGenVisitor::VisitForInitOrStep(ForInitOrStep& for_init_or_step) {
    // Implement (exercise): Treat as assignment: LVal = Exp; generate store in current block.
}

void IRGenVisitor::VisitBreakStmt(BreakStmt& break_stmt) {
    // Implement (exercise): Create br to break_targets_.back() (must be inside a loop).
}

void IRGenVisitor::VisitContinueStmt(ContinueStmt& continue_stmt) {
    // Implement (exercise): Create br to continue_targets_.back() (must be inside a loop).
}

void IRGenVisitor::VisitReturnStmt(ReturnStmt& return_stmt) {
    // Implement (exercise): If return value present, visit Exp then CreateRet(temp_value_);
    // else CreateRetVoid(). Then set insert point to unreachable or omit further code.
}

void IRGenVisitor::VisitGetintStmt(GetintStmt& getint_stmt) {
    // Implement (exercise): Create call @getint(), then store result to LVal (is_lval_mode_ = true,
    // visit LVal).
}

void IRGenVisitor::VisitGetcharStmt(GetcharStmt& getchar_stmt) {
    // Implement (exercise): Create call @getchar(), then store result to LVal.
}

void IRGenVisitor::VisitPrintfStmt(PrintfStmt& printf_stmt) {
    // Implement (exercise): Parse format string, emit putint/putch/putstr calls for each format
    // specifier and Exp.
}

// -----------------------------------------------------------------------------
// Expressions (result in temp_value_)
// -----------------------------------------------------------------------------

void IRGenVisitor::VisitLVal(LVal& lval) {
    // Implement (exercise): LookupVariable(ident). If array element, evaluate index, CreateGEP,
    // then if is_lval_mode_ leave address in temp_value_, else CreateLoad and set temp_value_.
}

void IRGenVisitor::VisitNumber(Number& number) {
    // Implement (exercise): Create constant (or use pre-built i32 constant), set temp_value_.
}

void IRGenVisitor::VisitCharacter(Character& character) {
    // Implement (exercise): Create constant (i8 or i32 as required), set temp_value_.
}

void IRGenVisitor::VisitBinaryExp(BinaryExp& binary_exp) {
    // Implement (exercise): Visit lhs and rhs (recursively or via sub-expressions), then
    // CreateBinary(op, lhs, rhs); set temp_value_ to result. For comparison, use icmp and possibly
    // zext i1 to i32.
}

void IRGenVisitor::VisitUnaryExp(UnaryExp& unary_exp) {
    // Implement (exercise): Handle PrimaryExp (delegate to child), UnaryOp '-' (sub 0, x), '!'
    // (icmp eq x, 0).
}

void IRGenVisitor::VisitFuncCall(FuncCall& func_call) {
    // Implement (exercise): Look up function, visit FuncRParams for args, CreateCall; set
    // temp_value_ if non-void.
}

void IRGenVisitor::VisitFuncRParams(FuncRParams& func_r_params) {
    // Implement (exercise): Visit each Exp, collect Value* args for the current call.
}

void IRGenVisitor::VisitConstExp(ConstExp& const_exp) {
    // Implement (exercise): Evaluate as constant expression (e.g. for array size or constant init).
}
