#include "SymbolTable.h"

void SymbolTable::PushScope() {
    current_scope_id_ = next_scope_id_++;
    scopes_.push_back(Scope{current_scope_id_, {}});
}

void SymbolTable::PopScope() {
    if (scopes_.empty())
        return;
    scopes_.pop_back();
    current_scope_id_ = scopes_.empty() ? 0 : scopes_.back().id;
}

const Symbol* SymbolTable::Lookup(const std::string& name) const {
    for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
        auto i = it->map.find(name);
        if (i != it->map.end())
            return &i->second;
    }
    return nullptr;
}

bool SymbolTable::Register(const std::string& name, Symbol symbol) {
    if (scopes_.empty())
        return false;
    auto& m = scopes_.back().map;
    if (m.count(name))
        return false;
    symbol.scope_id = current_scope_id_;
    m.emplace(name, std::move(symbol));
    return true;
}
