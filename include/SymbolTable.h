#pragma once

#include "Symbol.h"
#include <map>
#include <string>
#include <vector>

/**
 * Symbol table with scope stack. Scope id = "number of scopes entered so far" + 1
 * (global = 1). Lookup walks from current scope outward; Register only in current.
 */
class SymbolTable {
public:
    SymbolTable() = default;
    SymbolTable(const SymbolTable&) = delete;
    SymbolTable& operator=(const SymbolTable&) = delete;

    /** Push a new scope; next Register() will use this scope and a new scope_id. */
    void PushScope();

    /** Pop current scope (must match a prior PushScope). */
    void PopScope();

    /** Lookup from current scope outward. Returns nullptr if not found. */
    const Symbol* Lookup(const std::string& name) const;

    /**
     * Register in current scope. Returns false if name already exists in current scope
     * (redefinition); caller should record error and not use the symbol.
     */
    bool Register(const std::string& name, Symbol symbol);

    /** Current scope id (1-based). */
    int GetCurrentScopeId() const {
        return current_scope_id_;
    }

    /**
     * Ordered list for symbol.txt: (scope_id, symbol) in "scope order then declaration order".
     * Call after analysis; append in Register() when implementing semantic analysis.
     */
    using OrderedSymbolList = std::vector<std::pair<int, Symbol>>;

private:
    struct Scope {
        int id;
        std::map<std::string, Symbol> map;
    };
    std::vector<Scope> scopes_;
    int next_scope_id_ = 1;
    int current_scope_id_ = 0; // 0 before first PushScope
};
