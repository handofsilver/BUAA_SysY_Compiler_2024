/**
 * @file Module.h
 * @brief Module: top-level container for global variables and functions.
 *
 * The Module owns all GlobalVars and Functions. It is the root of the
 * IR ownership tree.
 */
#pragma once

#include "ir/Constant.h"
#include "ir/Function.h"
#include "ir/GlobalVar.h"
#include "ir/Type.h"

#include <vector>

#include <memory>
#include <optional>
#include <ostream>
#include <string>
#include <unordered_map>
#include <vector>

namespace ir {

    /**
     * @brief Top-level IR container: globals, functions, and constant pool.
     *
     * Ownership: Module owns all GlobalVar, Function, and ConstantInt objects.
     */
    class Module {
    public:
        Module() = default;

        /** @brief Get global variables (ownership held here). */
        std::vector<std::unique_ptr<GlobalVar>>& GetGlobalVars() {
            return global_vars_;
        }
        const std::vector<std::unique_ptr<GlobalVar>>& GetGlobalVars() const {
            return global_vars_;
        }

        /** @brief Get functions (ownership held here). */
        std::vector<std::unique_ptr<Function>>& GetFunctions() {
            return functions_;
        }
        const std::vector<std::unique_ptr<Function>>& GetFunctions() const {
            return functions_;
        }

        /** @brief Common types (owned here). */
        IntegerType* GetI32Type();
        IntegerType* GetI8Type();
        IntegerType* GetI1Type();
        VoidType* GetVoidType();
        LabelType* GetLabelType();

        /**
         * @brief Get the pointer type for the given pointee type.
         */
        PointerType* GetPointerType(Type* pointee_type);

        /**
         * @brief Get or create an array type [N x element_type]. Owned by module.
         */
        ArrayType* GetArrayType(Type* element_type, unsigned num_elements);

        /**
         * @brief Get or create an i32 constant. Returned pointer is valid for module lifetime.
         */
        ConstantInt* GetInt32Constant(int64_t value);

        /**
         * @brief Get or create an i8 constant. Returned pointer is valid for module lifetime.
         */
        ConstantInt* GetInt8Constant(int64_t value);

        /**
         * @brief Create a constant array (owned by module). Used as global initializer.
         */
        ConstantArray* CreateConstantArray(ArrayType* type, const std::vector<Constant*>& elements);

        /**
         * @brief Get a function by name. Known lib I/O (getint, getchar, putint, putch, putstr)
         * are created as declarations on first use; user-defined functions are found in functions_.
         * Only lib functions that are actually requested are declared (no hardcoded list in
         * output).
         * @return Function* or nullptr if not found.
         */
        Function* GetFunction(const std::string& name);

        /**
         * @brief Create a user-defined function and add it to the module.
         * Allocates FunctionType and Argument values; ownership held by Module/Function.
         * @return The new Function* (never nullptr).
         */
        Function* CreateFunction(const std::string& name, Type* return_type,
                                 const std::vector<Type*>& param_types);

        /**
         * @brief Create a global variable (or constant) with optional initializer.
         * @param init If non-null, must be ConstantInt (scalar) or ConstantArray (array); module
         *        does not take ownership of init (caller must ensure it lives, e.g. from
         *        GetInt32Constant or CreateConstantArray). If null, global has zeroinitializer.
         * @param is_constant True for const globals (ConstDef).
         */
        GlobalVar* CreateGlobalVar(const std::string& name, Type* type, Constant* init,
                                   bool is_constant = false);

        /** Out-of-line destructor so TUs that only see Module do not need to destroy Instruction.
         */
        ~Module();

        /** @brief Print entire module to LLVM IR (globals, declares, defines). */
        void Print(std::ostream& os) const;

    private:
        /** Ensure the given lib I/O function is declared (idempotent). Only declares if \p name
         * is one of the course-defined lib functions and not yet in functions_. */
        void EnsureDeclaredLibFunction(const std::string& name);

        /** Descriptor for a lib function: return type + param types. */
        struct LibFuncDescriptor {
            Type* return_type;
            std::vector<Type*> param_types;
        };
        /** If \p name is a known lib function, returns its signature (using this module's types).
         */
        std::optional<LibFuncDescriptor> GetLibFunctionDescriptor(const std::string& name);

        /** Find a function in functions_ by name; returns nullptr if not found. */
        Function* FindFunctionByName(const std::string& name) const;

        std::vector<std::unique_ptr<GlobalVar>> global_vars_;
        std::vector<std::unique_ptr<Function>> functions_;
        /** Owning storage for FunctionTypes of lib declarations (getint, putint, ...). */
        std::vector<std::unique_ptr<FunctionType>> function_types_;
        std::vector<std::unique_ptr<IntegerType>> integer_types_;
        std::vector<std::unique_ptr<PointerType>> pointer_types_;
        std::unordered_map<Type*, PointerType*> ptr_type_cache_;
        std::vector<std::unique_ptr<ConstantInt>> constants_;
        std::unordered_map<int64_t, ConstantInt*> const_i32_cache_;
        std::unordered_map<int64_t, ConstantInt*> const_i8_cache_;
        std::vector<std::unique_ptr<ArrayType>> array_types_;
        std::vector<std::unique_ptr<Constant>> other_constants_;
        std::unique_ptr<VoidType> void_type_;
        std::unique_ptr<LabelType> label_type_;
    };

} // namespace ir
