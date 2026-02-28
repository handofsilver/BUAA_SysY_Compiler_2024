/**
 * @file Module.cpp
 * @brief Module: constant pool, function/global management; out-of-line destructor.
 *
 * Type management lives in TypeManager (see TypeManager.h/cpp).
 *
 * Lib I/O (getint, getchar, putint, putch, putstr): only declare on demand.
 * GetFunction(name) calls EnsureDeclaredLibFunction(name), which adds a declaration for
 * that name only if it is one of the course-defined lib functions and not yet present.
 */
#include "ir/Module.h"

#include "ir/TypeManager.h"

#include <cassert>

namespace ir {

    Module::~Module() = default;

    // =========================================================================
    // Constant pool
    // =========================================================================

    ConstantInt* Module::GetInt32Constant(int64_t value) {
        auto it = const_i32_cache_.find(value);
        if (it != const_i32_cache_.end()) {
            return it->second;
        }
        IntegerType* i32 = TypeManager::Get().GetI32Type();
        auto c = std::make_unique<ConstantInt>("", i32, value);
        ConstantInt* p = c.get();
        constants_.push_back(std::move(c));
        const_i32_cache_[value] = p;
        return p;
    }

    ConstantInt* Module::GetInt8Constant(int64_t value) {
        auto it = const_i8_cache_.find(value);
        if (it != const_i8_cache_.end()) {
            return it->second;
        }
        IntegerType* i8 = TypeManager::Get().GetI8Type();
        auto c = std::make_unique<ConstantInt>("", i8, value);
        ConstantInt* p = c.get();
        constants_.push_back(std::move(c));
        const_i8_cache_[value] = p;
        return p;
    }

    ConstantArray* Module::CreateConstantArray(ArrayType* type,
                                               const std::vector<Constant*>& elements) {
        assert(type && !elements.empty());
        auto c = std::make_unique<ConstantArray>("", type, elements);
        ConstantArray* p = c.get();
        other_constants_.push_back(std::move(c));
        return p;
    }

    // =========================================================================
    // Function management
    // =========================================================================

    Function* Module::FindFunctionByName(const std::string& name) const {
        for (const auto& f : functions_) {
            if (f->GetName() == name) {
                return f.get();
            }
        }
        return nullptr;
    }

    std::optional<Module::LibFuncDescriptor>
    Module::GetLibFunctionDescriptor(const std::string& name) {
        auto& tm = TypeManager::Get();
        if (name == "getint") {
            return LibFuncDescriptor{tm.GetI32Type(), {}};
        }
        if (name == "getchar") {
            return LibFuncDescriptor{tm.GetI32Type(), {}};
        }
        if (name == "putint") {
            return LibFuncDescriptor{tm.GetVoidType(), {tm.GetI32Type()}};
        }
        if (name == "putch") {
            return LibFuncDescriptor{tm.GetVoidType(), {tm.GetI32Type()}};
        }
        if (name == "putstr") {
            return LibFuncDescriptor{tm.GetVoidType(), {tm.GetPointerType(tm.GetI8Type())}};
        }
        return std::nullopt;
    }

    void Module::EnsureDeclaredLibFunction(const std::string& name) {
        auto desc = GetLibFunctionDescriptor(name);
        if (!desc.has_value() || FindFunctionByName(name) != nullptr) {
            return;
        }
        function_types_.push_back(
            std::make_unique<FunctionType>(desc->return_type, desc->param_types));
        functions_.push_back(std::make_unique<Function>(name, function_types_.back().get()));
    }

    Function* Module::GetFunction(const std::string& name) {
        EnsureDeclaredLibFunction(name);
        return FindFunctionByName(name);
    }

    Function* Module::CreateFunction(const std::string& name, Type* return_type,
                                     const std::vector<Type*>& param_types) {
        function_types_.push_back(std::make_unique<FunctionType>(return_type, param_types));
        auto func = std::make_unique<Function>(name, function_types_.back().get());
        for (size_t i = 0; i < param_types.size(); ++i) {
            func->AddArgument(std::make_unique<Argument>(std::to_string(i), param_types[i]));
        }
        Function* ptr = func.get();
        functions_.push_back(std::move(func));
        return ptr;
    }

    // =========================================================================
    // Global variable management
    // =========================================================================

    GlobalVar* Module::CreateGlobalVar(const std::string& name, Type* type, Constant* init,
                                       bool is_constant) {
        global_vars_.push_back(std::make_unique<GlobalVar>(name, type));
        GlobalVar* g = global_vars_.back().get();
        g->SetInitializer(init);
        g->SetConstant(is_constant);
        return g;
    }

    // =========================================================================
    // Print
    // =========================================================================

    void Module::Print(std::ostream& os) const {
        for (const auto& g : global_vars_) {
            if (g) {
                g->Print(os);
            }
        }
        for (const auto& f : functions_) {
            if (f) {
                f->Print(os);
            }
        }
    }

} // namespace ir
