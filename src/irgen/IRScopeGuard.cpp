#include "irgen/IRScopeGuard.h"
#include "irgen/IRGenContext.h"

IRScopeGuard::IRScopeGuard(IRGenContext& ctx) : ctx_(&ctx) {
    ctx_->PushScope();
}

IRScopeGuard::~IRScopeGuard() {
    ctx_->PopScope();
}
