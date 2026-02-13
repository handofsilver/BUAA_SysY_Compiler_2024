#include "ScopeGuard.h"
#include "SymbolTable.h"

ScopeGuard::ScopeGuard(SymbolTable& table) : table_(&table) {
    table_->PushScope();
}

ScopeGuard::~ScopeGuard() {
    if (table_) {
        table_->PopScope();
    }
}
