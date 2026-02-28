/**
 * @file IRGenContext.cpp
 * @brief Implementation of IRGenContext: scope chain management.
 */
#include "irgen/IRGenContext.h"

IRGenContext::IRGenContext(ir::Module* module, ir::IRBuilder* builder, ir::TypeManager& types) :
module(module),
builder(builder),
types(types) {}

void IRGenContext::PushScope() {
    current_scope_id_ = next_scope_id_++;
    scopes_.push_back(Scope{current_scope_id_, {}});
}

void IRGenContext::PopScope() {
    if (scopes_.empty()) {
        return;
    }
    scopes_.pop_back();
    current_scope_id_ = scopes_.empty() ? 0 : scopes_.back().id;
}

void IRGenContext::RegisterVariable(const std::string& name, ir::Value* value) {
    scopes_.back().map[name] = value;
}

ir::Value* IRGenContext::LookupVariable(const std::string& name) const {
    for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
        auto i = it->map.find(name);
        if (i != it->map.end()) {
            return i->second;
        }
    }
    return nullptr;
}
