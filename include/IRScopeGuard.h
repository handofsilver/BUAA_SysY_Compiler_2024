#pragma once

class IRGenVisitor;

class IRScopeGuard {
public:
    explicit IRScopeGuard(IRGenVisitor& visitor);
    ~IRScopeGuard();
    IRScopeGuard(const IRScopeGuard&) = delete;
    IRScopeGuard& operator=(const IRScopeGuard&) = delete;

private:
    IRGenVisitor* visitor_;
};
