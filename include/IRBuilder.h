/**
 * @file IRBuilder.h
 * @brief Factory for IR instructions: Create<T> and convenience methods for common instructions.
 *
 * All definitions are in the header so that template Create<InstType, Args...> can be
 * instantiated at call sites. Non-template helpers are inline here for simplicity.
 */
#ifndef IR_BUILDER_H
#define IR_BUILDER_H

#include "ir/BasicBlock.h"
#include "ir/Instruction.h"
#include "ir/Type.h"
#include <AST.h>
#include <vector>

namespace ir {

    /**
     * @brief Helper to build and insert instructions into the current basic block.
     *
     * SetInsertPoint() sets the block; Create* methods insert into that block and return
     * the new instruction (raw pointer; ownership is held by the BasicBlock).
     */
    class IRBuilder {
    public:
        IRBuilder() = default;

        // -------------------------------------------------------------------------
        // Insertion point
        // -------------------------------------------------------------------------

        /** @brief Set the basic block where subsequent instructions will be inserted. */
        void SetInsertPoint(BasicBlock* bb) {
            insert_point_ = bb;
        }

        /** @brief Get the current insertion block, or nullptr if none set. */
        BasicBlock* GetInsertBlock() const {
            return insert_point_;
        }

        // -------------------------------------------------------------------------
        // Generic instruction creation (template)
        // -------------------------------------------------------------------------

        /**
         * @brief Create an instruction of type InstType and append it to the current block.
         * @return Pointer to the new instruction, or nullptr if insert_point_ is null.
         */
        template <typename InstType, typename... Args>
        InstType* Create(Args&&... args) {
            if (insert_point_ == nullptr) {
                return nullptr;
            }
            auto inst = std::make_unique<InstType>(std::forward<Args>(args)...);
            InstType* inst_ptr = inst.get();
            insert_point_->AddInstruction(std::move(inst));
            return inst_ptr;
        }

        // -------------------------------------------------------------------------
        // Memory
        // -------------------------------------------------------------------------

        /** @brief Create alloca: allocate a stack slot. Type is the allocated type (e.g. i32, [N x
         * i32]). */
        Instruction* CreateAlloca(Type* type) {
            BasicBlock* bb = GetInsertBlock();
            if (!bb || !type) {
                return nullptr;
            }
            return Create<AllocaInst>("", type, bb);
        }

        /**
         * @brief Create load from pointer. Pointee type is taken from ptr's type (must be
         * PointerType).
         * @return The new LoadInst, or nullptr on error.
         */
        Instruction* CreateLoad(Value* ptr) {
            BasicBlock* bb = GetInsertBlock();
            if (!bb || !ptr || !ptr->GetType()) {
                return nullptr;
            }
            auto* ptr_ty = dynamic_cast<PointerType*>(ptr->GetType());
            if (!ptr_ty) {
                return nullptr;
            }
            Type* elem_ty = ptr_ty->GetPointeeType();
            return Create<LoadInst>("", elem_ty, bb, ptr);
        }

        /** @brief Create store: store value into ptr. */
        Instruction* CreateStore(Value* value, Value* ptr) {
            BasicBlock* bb = GetInsertBlock();
            if (!bb || !value || !ptr) {
                return nullptr;
            }
            return Create<StoreInst>("", value->GetType(), bb, value, ptr);
        }

        // -------------------------------------------------------------------------
        // Binary arithmetic
        // -------------------------------------------------------------------------

        /** @brief Create binary op: %res = op type %lhs, %rhs (add, sub, mul, sdiv, srem). */
        Instruction* CreateBinary(OpType op, Value* lhs, Value* rhs) {
            BasicBlock* bb = GetInsertBlock();
            if (!bb || !lhs || !rhs) {
                return nullptr;
            }
            return Create<BinaryInst>("", lhs->GetType(), bb, op, lhs, rhs);
        }

        // -------------------------------------------------------------------------
        // Control flow
        // -------------------------------------------------------------------------

        /** @brief Create unconditional branch: br label %dest. */
        Instruction* CreateBr(BasicBlock* dest) {
            BasicBlock* bb = GetInsertBlock();
            if (!bb || !dest) {
                return nullptr;
            }
            return Create<BranchInst>("", dest->GetType(), bb, dest);
        }

        /** @brief Create conditional branch: br i1 %cond, label %if_true, label %if_false. */
        Instruction* CreateCondBr(Value* cond, BasicBlock* if_true, BasicBlock* if_false) {
            BasicBlock* bb = GetInsertBlock();
            if (!bb || !cond || !if_true || !if_false) {
                return nullptr;
            }
            return Create<BranchInst>("", cond->GetType(), bb, cond, if_true, if_false);
        }

        /** @brief Create return with value: ret type %val. */
        Instruction* CreateRet(Value* val) {
            BasicBlock* bb = GetInsertBlock();
            if (!bb || !val) {
                return nullptr;
            }
            return Create<ReturnInst>("", val->GetType(), bb, val);
        }

        /** @brief Create return without value: ret void. */
        Instruction* CreateRetVoid() {
            BasicBlock* bb = GetInsertBlock();
            if (!bb) {
                return nullptr;
            }
            return Create<ReturnInst>("", GetVoidType(), bb);
        }

        // -------------------------------------------------------------------------
        // Call and GEP
        // -------------------------------------------------------------------------

        /**
         * @brief Create function call. Type is the return type of the call (void for void
         * functions).
         */
        Instruction* CreateCall(Type* ret_type, Value* callee, const std::vector<Value*>& args) {
            BasicBlock* bb = GetInsertBlock();
            if (!bb || !callee) {
                return nullptr;
            }
            return Create<CallInst>("", ret_type, bb, callee, args);
        }

        /** @brief Create getelementptr with one index: base + index. */
        Instruction* CreateGEP(Type* result_ptr_type, Value* base, Value* index) {
            BasicBlock* bb = GetInsertBlock();
            if (!bb || !base || !index) {
                return nullptr;
            }
            return Create<GetElementPtrInst>("", result_ptr_type, bb, base, index);
        }

        /**
         * @brief Create getelementptr with two indices (e.g. [N x T]: base, i32 0, i32 %idx).
         */
        Instruction* CreateGEP(Type* result_ptr_type, Value* base, Value* index0, Value* index1) {
            BasicBlock* bb = GetInsertBlock();
            if (!bb || !base || !index0 || !index1) {
                return nullptr;
            }
            return Create<GetElementPtrInst>("", result_ptr_type, bb, base, index0, index1);
        }

        /** @brief Create icmp: result is i1. Pass GetI1Type() from Module as result_type. */
        Instruction* CreateIcmp(Type* result_type, IcmpPred pred, Value* lhs, Value* rhs) {
            BasicBlock* bb = GetInsertBlock();
            if (!bb || !result_type || !lhs || !rhs) {
                return nullptr;
            }
            return Create<IcmpInst>("", result_type, bb, pred, lhs, rhs);
        }

        /** @brief Create zext from value to dest_type (e.g. i1 to i32). */
        Instruction* CreateZext(Value* value, Type* dest_type) {
            BasicBlock* bb = GetInsertBlock();
            if (!bb || !value || !dest_type) {
                return nullptr;
            }
            return Create<ZextInst>("", dest_type, bb, value);
        }

        /** @brief Create trunc from value to dest_type (e.g. i32 to i8). */
        Instruction* CreateTrunc(Value* value, Type* dest_type) {
            BasicBlock* bb = GetInsertBlock();
            if (!bb || !value || !dest_type) {
                return nullptr;
            }
            return Create<TruncInst>("", dest_type, bb, value);
        }

    private:
        BasicBlock* insert_point_ = nullptr;
    };

} // namespace ir

#endif // IR_BUILDER_H
