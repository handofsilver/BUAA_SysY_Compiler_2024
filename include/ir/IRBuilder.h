/**
 * @file IRBuilder.h
 * @brief Factory for IR instructions: Create<T> and convenience methods for common instructions.
 *
 * All definitions are in the header so that template Create<InstType, Args...> can be
 * instantiated at call sites. Non-template helpers are inline here for simplicity.
 */
#pragma once

#include "ir/BasicBlock.h"
#include "ir/Instruction.h"
#include "ir/Type.h"
#include "ir/TypeManager.h"
#include "irgen/SSANameAllocator.h"
#include <cassert>
#include <string>
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

        /** @brief Reset SSA counter (call at function entry). Next GetNextSSAName() will return \p
         * start. Use param count so params stay %0,%1,... and first alloca/inst is %param_count. */
        void ResetSSACounter(int start = 0) {
            ssa_allocator_.Reset(start);
        }

        /** @brief Return next SSA name (e.g. "0", "1", ...) for value-producing instructions. */
        std::string GetNextSSAName() {
            return ssa_allocator_.Next();
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
            assert(insert_point_ != nullptr);
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
            assert(bb != nullptr && type != nullptr);
            return Create<AllocaInst>(GetNextSSAName(), type, bb);
        }

        /**
         * @brief Create load from pointer. Pointee type is taken from ptr's type (must be
         * PointerType).
         * @return The new LoadInst, or nullptr on error.
         */
        Instruction* CreateLoad(Value* ptr) {
            BasicBlock* bb = GetInsertBlock();
            assert(bb != nullptr && ptr != nullptr && ptr->GetType() != nullptr);
            auto* ptr_ty = dynamic_cast<PointerType*>(ptr->GetType());
            assert(ptr_ty != nullptr);
            Type* elem_ty = ptr_ty->GetPointeeType();
            return Create<LoadInst>(GetNextSSAName(), elem_ty, bb, ptr);
        }

        /** @brief Create store: store value into ptr. */
        Instruction* CreateStore(Value* value, Value* ptr) {
            BasicBlock* bb = GetInsertBlock();
            assert(bb != nullptr && value != nullptr && ptr != nullptr);
            return Create<StoreInst>("", value->GetType(), bb, value, ptr);
        }

        // -------------------------------------------------------------------------
        // Binary arithmetic
        // -------------------------------------------------------------------------

        /** @brief Create binary op: %res = op type %lhs, %rhs (add, sub, mul, sdiv, srem). */
        Instruction* CreateBinary(BinaryOp op, Value* lhs, Value* rhs) {
            BasicBlock* bb = GetInsertBlock();
            assert(bb != nullptr && lhs != nullptr && rhs != nullptr);
            return Create<BinaryInst>(GetNextSSAName(), lhs->GetType(), bb, op, lhs, rhs);
        }

        // -------------------------------------------------------------------------
        // Control flow
        // -------------------------------------------------------------------------

        /** @brief Create unconditional branch: br label %dest. */
        Instruction* CreateBr(BasicBlock* dest) {
            BasicBlock* bb = GetInsertBlock();
            assert(bb != nullptr && dest != nullptr);
            return Create<BranchInst>("", dest->GetType(), bb, dest);
        }

        /** @brief Create conditional branch: br i1 %cond, label %if_true, label %if_false. */
        Instruction* CreateCondBr(Value* cond, BasicBlock* if_true, BasicBlock* if_false) {
            BasicBlock* bb = GetInsertBlock();
            assert(bb != nullptr && cond != nullptr && if_true != nullptr && if_false != nullptr);
            return Create<BranchInst>("", cond->GetType(), bb, cond, if_true, if_false);
        }

        /** @brief Create return with value: ret type %val. */
        Instruction* CreateRet(Value* val) {
            BasicBlock* bb = GetInsertBlock();
            assert(bb != nullptr && val != nullptr);
            return Create<ReturnInst>("", val->GetType(), bb, val);
        }

        /** @brief Create return without value: ret void. */
        Instruction* CreateRetVoid() {
            BasicBlock* bb = GetInsertBlock();
            assert(bb != nullptr);
            return Create<ReturnInst>("", TypeManager::Get().GetVoidType(), bb);
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
            assert(bb != nullptr && callee != nullptr);
            return Create<CallInst>(GetNextSSAName(), ret_type, bb, callee, args);
        }

        /** @brief Create getelementptr with one index: base + index. */
        Instruction* CreateGEP(Type* result_ptr_type, Value* base, Value* index) {
            BasicBlock* bb = GetInsertBlock();
            assert(bb != nullptr && base != nullptr && index != nullptr);
            return Create<GetElementPtrInst>(GetNextSSAName(), result_ptr_type, bb, base, index);
        }

        /**
         * @brief Create getelementptr with two indices (e.g. [N x T]: base, i32 0, i32 %idx).
         */
        Instruction* CreateGEP(Type* result_ptr_type, Value* base, Value* index0, Value* index1) {
            BasicBlock* bb = GetInsertBlock();
            assert(bb != nullptr && base != nullptr && index0 != nullptr && index1 != nullptr);
            return Create<GetElementPtrInst>(GetNextSSAName(), result_ptr_type, bb, base, index0,
                                             index1);
        }

        /** @brief Create icmp: result is i1. Pass GetI1Type() from Module as result_type. */
        Instruction* CreateIcmp(Type* result_type, IcmpPred pred, Value* lhs, Value* rhs) {
            BasicBlock* bb = GetInsertBlock();
            assert(bb != nullptr && result_type != nullptr && lhs != nullptr && rhs != nullptr);
            return Create<IcmpInst>(GetNextSSAName(), result_type, bb, pred, lhs, rhs);
        }

        /** @brief Create zext from value to dest_type (e.g. i1 to i32). */
        Instruction* CreateZext(Value* value, Type* dest_type) {
            BasicBlock* bb = GetInsertBlock();
            assert(bb != nullptr && value != nullptr && dest_type != nullptr);
            return Create<ZextInst>(GetNextSSAName(), dest_type, bb, value);
        }

        /** @brief Create trunc from value to dest_type (e.g. i32 to i8). */
        Instruction* CreateTrunc(Value* value, Type* dest_type) {
            BasicBlock* bb = GetInsertBlock();
            assert(bb != nullptr && value != nullptr && dest_type != nullptr);
            return Create<TruncInst>(GetNextSSAName(), dest_type, bb, value);
        }

    private:
        BasicBlock* insert_point_ = nullptr;
        SSANameAllocator ssa_allocator_;
    };

} // namespace ir
