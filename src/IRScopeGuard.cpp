#include "IRScopeGuard.h"
#include "IRGenContext.h"

IRScopeGuard::IRScopeGuard(IRGenContext& ctx) : ctx_(&ctx) {
    ctx_->PushScope();
}

IRScopeGuard::~IRScopeGuard() {
    ctx_->PopScope();
}
