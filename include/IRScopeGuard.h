#pragma once

class IRGenContext;

class IRScopeGuard {
public:
    explicit IRScopeGuard(IRGenContext& ctx);
    ~IRScopeGuard();
    IRScopeGuard(const IRScopeGuard&) = delete;
    IRScopeGuard& operator=(const IRScopeGuard&) = delete;

private:
    IRGenContext* ctx_;
};
