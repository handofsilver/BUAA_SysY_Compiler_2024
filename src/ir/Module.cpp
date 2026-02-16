/**
 * @file Module.cpp
 * @brief Module: type and constant pool; out-of-line destructor.
 *
 * Lib I/O (getint, getchar, putint, putch, putstr): only declare on demand.
 * GetFunction(name) calls EnsureDeclaredLibFunction(name), which adds a declaration for
 * that name only if it is one of the course-defined lib functions and not yet present.
 * Single source of truth: the 5 lib signatures live in GetLibFunctionDescriptor(); only
 * declarations for actually used lib functions appear in the module.
 */
#include "ir/Module.h"

namespace ir {

    Module::~Module() = default;

    PointerType* Module::GetPointerType(Type* pointee_type) {
        if (!pointee_type) {
            return nullptr;
        }
        auto it = ptr_type_cache_.find(pointee_type);
        if (it != ptr_type_cache_.end()) {
            return it->second;
        }
        pointer_types_.push_back(std::make_unique<PointerType>(pointee_type));
        PointerType* p = pointer_types_.back().get();
        ptr_type_cache_[pointee_type] = p;
        return p;
    }

    IntegerType* Module::GetI32Type() {
        if (integer_types_.empty()) {
            integer_types_.push_back(std::make_unique<IntegerType>(32));
        }
        return integer_types_[0].get();
    }

    IntegerType* Module::GetI8Type() {
        if (integer_types_.size() < 2u) {
            integer_types_.push_back(std::make_unique<IntegerType>(8));
        }
        return integer_types_[1].get();
    }

    IntegerType* Module::GetI1Type() {
        if (integer_types_.size() < 3u) {
            integer_types_.push_back(std::make_unique<IntegerType>(1));
        }
        return integer_types_[2].get();
    }

    ArrayType* Module::GetArrayType(Type* element_type, unsigned num_elements) {
        if (!element_type) {
            return nullptr;
        }
        array_types_.push_back(std::make_unique<ArrayType>(element_type, num_elements));
        return array_types_.back().get();
    }

    ConstantArray* Module::CreateConstantArray(ArrayType* type,
                                               const std::vector<Constant*>& elements) {
        if (!type) {
            return nullptr;
        }
        auto c = std::make_unique<ConstantArray>("", type, elements);
        ConstantArray* p = c.get();
        other_constants_.push_back(std::move(c));
        return p;
    }

    ConstantInt* Module::GetInt32Constant(int64_t value) {
        auto it = const_i32_cache_.find(value);
        if (it != const_i32_cache_.end()) {
            return it->second;
        }
        IntegerType* i32 = GetI32Type();
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
        IntegerType* i8 = GetI8Type();
        auto c = std::make_unique<ConstantInt>("", i8, value);
        ConstantInt* p = c.get();
        constants_.push_back(std::move(c));
        const_i8_cache_[value] = p;
        return p;
    }

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
        // Course-defined lib I/O: getint, getchar, putint, putch, putstr (declare only, no define).
        if (name == "getint") {
            return LibFuncDescriptor{GetI32Type(), {}};
        }
        if (name == "getchar") {
            return LibFuncDescriptor{GetI32Type(), {}};
        }
        if (name == "putint") {
            return LibFuncDescriptor{GetVoidType(), {GetI32Type()}};
        }
        if (name == "putch") {
            return LibFuncDescriptor{GetVoidType(), {GetI32Type()}};
        }
        if (name == "putstr") {
            return LibFuncDescriptor{GetVoidType(), {GetPointerType(GetI8Type())}};
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
            func->AddArgument(std::make_unique<Argument>("", param_types[i]));
        }
        Function* ptr = func.get();
        functions_.push_back(std::move(func));
        return ptr;
    }

    GlobalVar* Module::CreateGlobalVar(const std::string& name, Type* type, Constant* init,
                                       bool is_constant) {
        global_vars_.push_back(std::make_unique<GlobalVar>(name, type));
        GlobalVar* g = global_vars_.back().get();
        g->SetInitializer(init);
        g->SetConstant(is_constant);
        return g;
    }
} // namespace ir
