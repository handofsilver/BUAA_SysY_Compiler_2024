/**
 * @file IRGenContext.h
 * @brief Global IR generation environment: resource pointers and scope (symbol table).
 *
 * Extracted from IRGenVisitor to decouple environment state from AST traversal logic.
 * Traversal-specific flags (temp_value_, is_lval_mode_, etc.) remain in IRGenVisitor.
 */
#pragma once

#include "ir/IRBuilder.h"
#include "ir/Module.h"
#include "ir/TypeManager.h"
#include <map>
#include <string>
#include <vector>

namespace ir {
    class Function;
    class Value;
} // namespace ir

class IRGenContext {
public:
    IRGenContext(ir::Module* module, ir::IRBuilder* builder, ir::TypeManager& types);

    // --- Resource pointers (non-owning) ---
    ir::Module* module;
    ir::IRBuilder* builder;
    ir::TypeManager& types;

    /** Currently generated function. Set by VisitFuncDef / VisitMainFuncDef. */
    ir::Function* current_function = nullptr;

    // --- Scope management ---

    void PushScope();
    void PopScope();
    void RegisterVariable(const std::string& name, ir::Value* value);
    ir::Value* LookupVariable(const std::string& name) const;

private:
    struct Scope {
        int id;
        std::map<std::string, ir::Value*> map;
    };
    std::vector<Scope> scopes_;
    int next_scope_id_ = 1;
    int current_scope_id_ = 0;
};
