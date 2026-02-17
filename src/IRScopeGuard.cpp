
#include "IRScopeGuard.h"
#include "IRGenVisitor.h"

IRScopeGuard::IRScopeGuard(IRGenVisitor& visitor) : visitor_(&visitor) {
    visitor_->PushScope();
}

IRScopeGuard::~IRScopeGuard() {
    visitor_->PopScope();
}
