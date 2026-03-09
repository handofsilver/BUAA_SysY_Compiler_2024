#include "mips/FunctionEmitter.h"
#include "ir/Constant.h"
#include "ir/GlobalVar.h"

namespace mips {

    namespace {
        const char* k_indent = "    ";

        std::string GlobalLabel(const std::string& name) {
            return "global_" + name;
        }
    } // namespace

    void FunctionEmitter::BuildStackFrame() {
        frame_size_ = 0;
        // 见 step_M1a_substeps.md：先为 $ra 预留 4 字节，再为每个“产生结果的指令”预留 4 字节
        frame_size_ += 4; // $ra 保存槽
        for (const auto& block : func_.GetBlocks()) {
            for (const auto& inst : block->GetInstructions()) {
                if (dynamic_cast<const ir::BinaryInst*>(inst.get()) ||
                    dynamic_cast<const ir::LoadInst*>(inst.get()) ||
                    dynamic_cast<const ir::GetElementPtrInst*>(inst.get())) {
                    value_offset_[inst.get()] = frame_size_;
                    frame_size_ += 4;
                }
            }
        }
    }

    void FunctionEmitter::EmitPrologue() {
        os_ << func_.GetName() << ":\n";
        os_ << k_indent << "addiu $sp, $sp, -" << frame_size_ << "\n";
        os_ << k_indent << "sw    $ra, 0($sp)\n";
    }

    void FunctionEmitter::EmitBody() {
        for (const auto& block : func_.GetBlocks()) {
            for (const auto& inst : block->GetInstructions()) {
                if (auto* bin = dynamic_cast<const ir::BinaryInst*>(inst.get())) {
                    EmitBinaryInst(bin);
                }
                if (auto* ret = dynamic_cast<const ir::ReturnInst*>(inst.get())) {
                    EmitReturnInst(ret);
                }
                if (auto* load = dynamic_cast<const ir::LoadInst*>(inst.get())) {
                    EmitLoadInst(load);
                }
                if (auto* store = dynamic_cast<const ir::StoreInst*>(inst.get())) {
                    EmitStoreInst(store);
                }
                if (auto* gep = dynamic_cast<const ir::GetElementPtrInst*>(inst.get())) {
                    EmitGetElementPtrInst(gep);
                }
            }
        }
    }

    void FunctionEmitter::EmitEpilogue() {
        os_ << k_indent << "lw    $ra, 0($sp)\n";
        os_ << k_indent << "addiu $sp, $sp, " << frame_size_ << "\n";
        os_ << k_indent << "jr    $ra\n\n";
    }

    void FunctionEmitter::EmitBinaryInst(const ir::BinaryInst* inst) {
        // 子步骤 M1e 再实现：需要 value_offset_ 与 loadValueToReg
        LoadValueToReg(inst->GetLhs(), "$t0");
        LoadValueToReg(inst->GetRhs(), "$t1");
        switch (inst->GetOp()) {
            case ir::BinaryOp::ADD: os_ << k_indent << "addu  $t2, $t0, $t1\n"; break;
            case ir::BinaryOp::SUB: os_ << k_indent << "subu  $t2, $t0, $t1\n"; break;
            case ir::BinaryOp::MUL: os_ << k_indent << "mul   $t2, $t0, $t1\n"; break;
            case ir::BinaryOp::DIV:
                os_ << k_indent << "div   $t0, $t1\n";
                os_ << k_indent << "mflo  $t2\n";
                break;
            case ir::BinaryOp::REM:
                os_ << k_indent << "div   $t0, $t1\n";
                os_ << k_indent << "mfhi  $t2\n";
                break;
            default: assert(false);
        }
        os_ << k_indent << "sw    $t2, " << value_offset_[inst] << "($sp)\n";
    }

    void FunctionEmitter::EmitLoadInst(const ir::LoadInst* inst) {
        LoadValueToReg(inst->GetPointerOperand(), "$t0");
        os_ << k_indent << "lw    $t0, 0($t0)\n";
        os_ << k_indent << "sw    $t0, " << value_offset_[inst] << "($sp)\n";
    }

    void FunctionEmitter::EmitStoreInst(const ir::StoreInst* inst) {
        LoadValueToReg(inst->GetValueOperand(), "$t0");
        LoadValueToReg(inst->GetPointerOperand(), "$t1");

        os_ << k_indent << "sw    $t0, 0($t1)\n";
    }

    void FunctionEmitter::EmitGetElementPtrInst(const ir::GetElementPtrInst* inst) {
        LoadValueToReg(inst->GetPointerOperand(), "$t0");
        // GEP 可能有两下标 (0, elem_idx)：取元素用最后一个下标；单下标则用 GetIndex(0)
        const ir::Value* elem_index =
            inst->GetIndex(1) != nullptr ? inst->GetIndex(1) : inst->GetIndex(0);
        LoadValueToReg(elem_index, "$t1");
        os_ << k_indent << "sll   $t2, $t1, 2\n";
        os_ << k_indent << "addu  $t2, $t0, $t2\n";
        os_ << k_indent << "sw    $t2, " << value_offset_[inst] << "($sp)\n";
    }

    void FunctionEmitter::LoadValueToReg(const ir::Value* val, const std::string& reg) {
        if (auto* const_int = dynamic_cast<const ir::ConstantInt*>(val)) {
            os_ << k_indent << "li    " << reg << ", " << const_int->GetValue() << "\n";
        } else if (auto* gv = dynamic_cast<const ir::GlobalVar*>(val)) {
            os_ << k_indent << "la    " << reg << ", " << GlobalLabel(gv->GetName()) << "\n";
        } else {
            os_ << k_indent << "lw    " << reg << ", " << value_offset_[val] << "($sp)\n";
        }
    }

    void FunctionEmitter::EmitReturnInst(const ir::ReturnInst* inst) {
        if (auto* ret_val = inst->GetRetVal()) {
            LoadValueToReg(ret_val, "$v0");
        }
    }
} // namespace mips
