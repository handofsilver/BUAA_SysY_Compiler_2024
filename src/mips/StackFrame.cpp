#include "mips/StackFrame.h"
#include "ir/Type.h"
#include <cassert>

namespace mips {

    StackFrame::StackFrame(const ir::Function& func) : func_(func) {}

    void StackFrame::Build() {
        frame_size_ = 0;
        value_offset_.clear();

        // $ra save slot at offset 0 (bottom of frame).
        frame_size_ += 4;

        AllocateAllocas();
        AllocateInstructionSlots();
        AllocateArgumentSlots();
    }

    int StackFrame::GetOffset(const ir::Value* val) const {
        auto it = value_offset_.find(val);
        assert(it != value_offset_.end() && "Value has no stack slot");
        return it->second;
    }

    bool StackFrame::HasSlot(const ir::Value* val) const {
        return value_offset_.count(val) != 0;
    }

    // -------------------------------------------------------------------------
    // Private allocation helpers
    // -------------------------------------------------------------------------

    void StackFrame::AllocateAllocas() {
        // Convention: all AllocaInsts are at the front of the entry block.
        const auto& entry_block = func_.GetBlocks()[0];
        for (const auto& inst : entry_block->GetInstructions()) {
            auto* alloca = dynamic_cast<const ir::AllocaInst*>(inst.get());
            if (!alloca) {
                break;
            }

            auto* ptr_ty = dynamic_cast<const ir::PointerType*>(alloca->GetType());
            ir::Type* pointee = ptr_ty->GetPointeeType();

            int size = 4; // Scalar alloca: 4 bytes.
            if (auto* arr_ty = dynamic_cast<const ir::ArrayType*>(pointee)) {
                auto* int_ty = dynamic_cast<const ir::IntegerType*>(arr_ty->GetElementType());
                assert(int_ty && "Array element type must be integer");
                int elem_bytes = static_cast<int>(int_ty->GetBits() / 8);
                size = static_cast<int>(arr_ty->GetNumElements()) * elem_bytes;
                // Round up to 4-byte alignment for subsequent value slots.
                if (size > 0 && size % 4 != 0) {
                    size = (size + 3) & ~3;
                }
            }

            value_offset_[alloca] = frame_size_;
            frame_size_ += size;
        }
    }

    void StackFrame::AllocateInstructionSlots() {
        // Each value-producing instruction gets a 4-byte result slot.
        for (const auto& block : func_.GetBlocks()) {
            for (const auto& inst : block->GetInstructions()) {
                if (ProducesValue(inst.get())) {
                    value_offset_[inst.get()] = frame_size_;
                    frame_size_ += 4;
                }
            }
        }
    }

    void StackFrame::AllocateArgumentSlots() {
        const size_t kNumArgs = func_.GetArguments().size();

        // First 4 args get local stack slots (spilled from $a0-$a3 in prologue).
        for (size_t i = 0; i < kNumArgs && i < 4u; ++i) {
            value_offset_[func_.GetArgument(i)] = frame_size_;
            frame_size_ += 4;
        }

        // Args 5+ live in the caller's frame at frame_size + (i-4)*4.
        for (size_t i = 4; i < kNumArgs; ++i) {
            value_offset_[func_.GetArgument(i)] = frame_size_ + static_cast<int>((i - 4) * 4);
        }
    }

    bool StackFrame::ProducesValue(const ir::Instruction* inst) {
        if (dynamic_cast<const ir::BinaryInst*>(inst) || dynamic_cast<const ir::LoadInst*>(inst) ||
            dynamic_cast<const ir::GetElementPtrInst*>(inst) ||
            dynamic_cast<const ir::IcmpInst*>(inst) || dynamic_cast<const ir::ZextInst*>(inst) ||
            dynamic_cast<const ir::TruncInst*>(inst) || dynamic_cast<const ir::PhiInst*>(inst)) {
            return true;
        }
        if (auto* call = dynamic_cast<const ir::CallInst*>(inst)) {
            return !dynamic_cast<const ir::VoidType*>(call->GetType());
        }
        return false;
    }

} // namespace mips
