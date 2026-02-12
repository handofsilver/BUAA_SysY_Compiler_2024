#pragma once

class SymbolTable;

/**
 * RAII: push scope on construction, pop on destruction.
 * Use in VisitBlock / VisitFuncDef so that early return or exception still pops scope.
 */
class ScopeGuard {
public:
    explicit ScopeGuard(SymbolTable& table);
    ~ScopeGuard();
    ScopeGuard(const ScopeGuard&) = delete;
    ScopeGuard& operator=(const ScopeGuard&) = delete;

private:
    SymbolTable* table_;
};
