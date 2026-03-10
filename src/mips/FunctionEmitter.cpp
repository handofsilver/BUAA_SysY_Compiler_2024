#include "mips/FunctionEmitter.h"
#include "ir/Constant.h"
#include "ir/GlobalVar.h"
#include "ir/Type.h"
#include "mips/MipsCommon.h"
#include <algorithm>
#include <cassert>
#include <vector>

namespace mips {

    namespace {
        /// Compute the element size (bytes) of the pointee type for a GEP result.
        /// Returns 1 for i8, 4 for i32 (default).
        int GepElementSizeBytes(const ir::GetElementPtrInst* inst) {
            auto* ptr_ty = dynamic_cast<const ir::PointerType*>(inst->GetType());
            if (!ptr_ty) {
                return 4;
            }
            ir::Type* pointee = ptr_ty->GetPointeeType();
            if (!pointee) {
                return 4;
            }
            ir::Type* elem_ty = pointee;
            if (auto* arr = dynamic_cast<const ir::ArrayType*>(pointee)) {
                elem_ty = arr->GetElementType();
            }
            if (auto* int_ty = dynamic_cast<const ir::IntegerType*>(elem_ty)) {
                return static_cast<int>(int_ty->GetBits() / 8);
            }
            return 4;
        }
    } // namespace

    // =========================================================================
    // Construction & top-level entry
    // =========================================================================

    FunctionEmitter::FunctionEmitter(std::ostream& os, const ir::Function& func) :
    os_(os),
    func_(func),
    frame_(func) {}

    void FunctionEmitter::Emit() {
        frame_.Build();
        EmitPrologue();
        EmitBody();
    }

    // =========================================================================
    // Prologue / Epilogue
    // =========================================================================

    void FunctionEmitter::EmitPrologue() {
        os_ << func_.GetName() << ":\n";
        os_ << kIndent << "addiu $sp, $sp, -" << frame_.GetFrameSize() << "\n";
        os_ << kIndent << "sw    $ra, 0($sp)\n";

        // Spill register-passed arguments ($a0-$a3) into their stack slots.
        const size_t kNumArgs = func_.GetArguments().size();
        for (size_t i = 0; i < kNumArgs && i < 4u; ++i) {
            os_ << kIndent << "sw    $a" << i << ", " << frame_.GetOffset(func_.GetArgument(i))
                << "($sp)\n";
        }
    }

    void FunctionEmitter::EmitEpilogue() {
        os_ << kIndent << "lw    $ra, 0($sp)\n";
        os_ << kIndent << "addiu $sp, $sp, " << frame_.GetFrameSize() << "\n";
        os_ << kIndent << "jr    $ra\n";
    }

    // =========================================================================
    // Body: iterate blocks and dispatch each instruction
    // =========================================================================

    void FunctionEmitter::EmitBody() {
        for (const auto& block : func_.GetBlocks()) {
            os_ << BlockLabel(func_.GetName(), block->GetName()) << ":\n";
            for (const auto& inst : block->GetInstructions()) {
                EmitInstruction(inst.get(), block.get());
            }
        }
    }

    void FunctionEmitter::EmitInstruction(const ir::Instruction* inst,
                                          const ir::BasicBlock* block) {
        if (auto* bin = dynamic_cast<const ir::BinaryInst*>(inst)) {
            EmitBinaryInst(bin);
        } else if (auto* ret = dynamic_cast<const ir::ReturnInst*>(inst)) {
            EmitReturnInst(ret);
            EmitEpilogue();
        } else if (auto* load = dynamic_cast<const ir::LoadInst*>(inst)) {
            EmitLoadInst(load);
        } else if (auto* store = dynamic_cast<const ir::StoreInst*>(inst)) {
            EmitStoreInst(store);
        } else if (auto* gep = dynamic_cast<const ir::GetElementPtrInst*>(inst)) {
            EmitGetElementPtrInst(gep);
        } else if (auto* icmp = dynamic_cast<const ir::IcmpInst*>(inst)) {
            EmitIcmpInst(icmp);
        } else if (auto* branch = dynamic_cast<const ir::BranchInst*>(inst)) {
            EmitPhiMovesBeforeBranch(block, branch);
            EmitBranchInst(branch);
        } else if (auto* zext = dynamic_cast<const ir::ZextInst*>(inst)) {
            EmitZextInst(zext);
        } else if (auto* trunc = dynamic_cast<const ir::TruncInst*>(inst)) {
            EmitTruncInst(trunc);
        } else if (auto* call = dynamic_cast<const ir::CallInst*>(inst)) {
            EmitCallInst(call);
        }
        // PhiInst: handled by EmitPhiMovesBeforeBranch at predecessor's branch.
        // AllocaInst: space allocated in StackFrame::Build(); no runtime code needed.
    }

    // =========================================================================
    // Instruction emission
    // =========================================================================

    void FunctionEmitter::EmitBinaryInst(const ir::BinaryInst* inst) {
        LoadValueToReg(inst->GetLhs(), "$t0");
        LoadValueToReg(inst->GetRhs(), "$t1");
        switch (inst->GetOp()) {
            case ir::BinaryOp::ADD: os_ << kIndent << "addu  $t2, $t0, $t1\n"; break;
            case ir::BinaryOp::SUB: os_ << kIndent << "subu  $t2, $t0, $t1\n"; break;
            case ir::BinaryOp::MUL: os_ << kIndent << "mul   $t2, $t0, $t1\n"; break;
            case ir::BinaryOp::DIV:
                os_ << kIndent << "div   $t0, $t1\n";
                os_ << kIndent << "mflo  $t2\n";
                break;
            case ir::BinaryOp::REM:
                os_ << kIndent << "div   $t0, $t1\n";
                os_ << kIndent << "mfhi  $t2\n";
                break;
            default: assert(false && "Unknown binary op");
        }
        os_ << kIndent << "sw    $t2, " << frame_.GetOffset(inst) << "($sp)\n";
    }

    void FunctionEmitter::EmitLoadInst(const ir::LoadInst* inst) {
        LoadValueToReg(inst->GetPointerOperand(), "$t0");
        auto* int_ty = dynamic_cast<const ir::IntegerType*>(inst->GetType());
        if (int_ty && int_ty->GetBits() == 8) {
            os_ << kIndent << "lbu   $t0, 0($t0)\n"; // i8: zero-extend byte to 32-bit
        } else {
            os_ << kIndent << "lw    $t0, 0($t0)\n";
        }
        os_ << kIndent << "sw    $t0, " << frame_.GetOffset(inst) << "($sp)\n";
    }

    void FunctionEmitter::EmitStoreInst(const ir::StoreInst* inst) {
        LoadValueToReg(inst->GetValueOperand(), "$t0");
        LoadValueToReg(inst->GetPointerOperand(), "$t1");
        auto* int_ty = dynamic_cast<const ir::IntegerType*>(inst->GetValueOperand()->GetType());
        if (int_ty && int_ty->GetBits() == 8) {
            os_ << kIndent << "sb    $t0, 0($t1)\n"; // i8: store low byte only
        } else {
            os_ << kIndent << "sw    $t0, 0($t1)\n";
        }
    }

    void FunctionEmitter::EmitGetElementPtrInst(const ir::GetElementPtrInst* inst) {
        LoadValueToReg(inst->GetPointerOperand(), "$t0");
        // Two-index GEP: base[0][idx1]; single-index GEP: base[idx0].
        const ir::Value* elem_index =
            inst->GetIndex(1) != nullptr ? inst->GetIndex(1) : inst->GetIndex(0);
        LoadValueToReg(elem_index, "$t1");
        int elem_size = GepElementSizeBytes(inst);
        if (elem_size == 4) {
            os_ << kIndent << "sll   $t2, $t1, 2\n";
            os_ << kIndent << "addu  $t2, $t0, $t2\n";
        } else {
            // i8 elements: offset = index * 1, just add directly.
            os_ << kIndent << "addu  $t2, $t0, $t1\n";
        }
        os_ << kIndent << "sw    $t2, " << frame_.GetOffset(inst) << "($sp)\n";
    }

    void FunctionEmitter::EmitIcmpInst(const ir::IcmpInst* inst) {
        LoadValueToReg(inst->GetLhs(), "$t0");
        LoadValueToReg(inst->GetRhs(), "$t1");
        switch (inst->GetPredicate()) {
            case ir::IcmpPred::SLT: os_ << kIndent << "slt   $t2, $t0, $t1\n"; break;
            case ir::IcmpPred::SGT: os_ << kIndent << "sgt   $t2, $t0, $t1\n"; break;
            case ir::IcmpPred::SLE: os_ << kIndent << "sle   $t2, $t0, $t1\n"; break;
            case ir::IcmpPred::SGE: os_ << kIndent << "sge   $t2, $t0, $t1\n"; break;
            case ir::IcmpPred::EQ: os_ << kIndent << "seq   $t2, $t0, $t1\n"; break;
            case ir::IcmpPred::NE: os_ << kIndent << "sne   $t2, $t0, $t1\n"; break;
        }
        os_ << kIndent << "sw    $t2, " << frame_.GetOffset(inst) << "($sp)\n";
    }

    void FunctionEmitter::EmitBranchInst(const ir::BranchInst* inst) {
        if (inst->IsConditional()) {
            LoadValueToReg(inst->GetCond(), "$t0");
            os_ << kIndent << "bnez  $t0, "
                << BlockLabel(func_.GetName(), inst->GetIfTrue()->GetName()) << "\n";
            os_ << kIndent << "j     " << BlockLabel(func_.GetName(), inst->GetIfFalse()->GetName())
                << "\n";
        } else {
            os_ << kIndent << "j     " << BlockLabel(func_.GetName(), inst->GetDest()->GetName())
                << "\n";
        }
    }

    void FunctionEmitter::EmitZextInst(const ir::ZextInst* inst) {
        LoadValueToReg(inst->GetOperandValue(), "$t0");
        os_ << kIndent << "sw    $t0, " << frame_.GetOffset(inst) << "($sp)\n";
    }

    void FunctionEmitter::EmitTruncInst(const ir::TruncInst* inst) {
        LoadValueToReg(inst->GetOperandValue(), "$t0");
        os_ << kIndent << "andi  $t0, $t0, 0xFF\n"; // i32 -> i8: keep low 8 bits
        os_ << kIndent << "sw    $t0, " << frame_.GetOffset(inst) << "($sp)\n";
    }

    void FunctionEmitter::EmitReturnInst(const ir::ReturnInst* inst) {
        if (auto* ret_val = inst->GetRetVal()) {
            LoadValueToReg(ret_val, "$v0");
        }
    }

    // =========================================================================
    // Function calls
    // =========================================================================

    void FunctionEmitter::EmitCallInst(const ir::CallInst* inst) {
        const std::string& name = inst->GetCallee()->GetName();
        if (IsLibraryFunction(name)) {
            EmitLibraryCall(inst);
            return;
        }

        const size_t kNumArgs = inst->GetNumArgs();
        const size_t kExtraArgs = (kNumArgs > 4u) ? (kNumArgs - 4u) : 0u;
        const int kExtraSize = static_cast<int>(kExtraArgs * 4);

        // All argument loads must happen before $sp is adjusted;
        // otherwise lw offset($sp) would read from wrong slots.

        // 1) Load args 5+ into temporaries (still using current $sp).
        if (kExtraSize > 0) {
            for (size_t i = 4; i < kNumArgs; ++i) {
                LoadValueToReg(inst->GetArg(static_cast<int>(i)),
                               "$t" + std::to_string(static_cast<int>(i - 4)));
            }
        }
        // 2) Load first 4 args into $a0-$a3 (still using current $sp).
        for (size_t i = 0; i < kNumArgs && i < 4u; ++i) {
            LoadValueToReg(inst->GetArg(static_cast<int>(i)), "$a" + std::to_string(i));
        }
        // 3) Push stack space for args 5+ and write them, then jal.
        if (kExtraSize > 0) {
            os_ << kIndent << "addiu $sp, $sp, -" << kExtraSize << "\n";
            for (size_t i = 4; i < kNumArgs; ++i) {
                os_ << kIndent << "sw    $t" << (i - 4) << ", " << static_cast<int>((i - 4) * 4)
                    << "($sp)\n";
            }
        }

        os_ << kIndent << "jal   " << name << "\n";

        if (kExtraSize > 0) {
            os_ << kIndent << "addiu $sp, $sp, " << kExtraSize << "\n";
        }

        // Store return value if non-void.
        if (!dynamic_cast<const ir::VoidType*>(inst->GetType())) {
            if (frame_.HasSlot(inst)) {
                os_ << kIndent << "sw    $v0, " << frame_.GetOffset(inst) << "($sp)\n";
            }
        }
    }

    void FunctionEmitter::EmitLibraryCall(const ir::CallInst* inst) {
        const std::string& name = inst->GetCallee()->GetName();
        if (name == "getint") {
            os_ << kIndent << "li    $v0, 5\n";
            os_ << kIndent << "syscall\n";
            os_ << kIndent << "sw    $v0, " << frame_.GetOffset(inst) << "($sp)\n";
        } else if (name == "getchar") {
            os_ << kIndent << "li    $v0, 12\n";
            os_ << kIndent << "syscall\n";
            os_ << kIndent << "sw    $v0, " << frame_.GetOffset(inst) << "($sp)\n";
            // MARS syscall 12 reads one char but leaves trailing '\n' in the buffer;
            // consume it so that a subsequent getint reads the next line correctly.
            os_ << kIndent << "li    $v0, 12\n";
            os_ << kIndent << "syscall\n";
        } else if (name == "putint") {
            LoadValueToReg(inst->GetArg(0), "$a0");
            os_ << kIndent << "li    $v0, 1\n";
            os_ << kIndent << "syscall\n";
        } else if (name == "putch") {
            LoadValueToReg(inst->GetArg(0), "$a0");
            os_ << kIndent << "li    $v0, 11\n";
            os_ << kIndent << "syscall\n";
        } else if (name == "putstr") {
            LoadValueToReg(inst->GetArg(0), "$a0");
            os_ << kIndent << "li    $v0, 4\n";
            os_ << kIndent << "syscall\n";
        }
    }

    // =========================================================================
    // Value loading helper
    // =========================================================================

    void FunctionEmitter::LoadValueToReg(const ir::Value* val, const std::string& reg) {
        // Phi incoming can be nullptr (undef) from Mem2Reg; no stack slot exists.
        if (val == nullptr) {
            os_ << kIndent << "li    " << reg << ", 0\n";
            return;
        }
        if (auto* ci = dynamic_cast<const ir::ConstantInt*>(val)) {
            os_ << kIndent << "li    " << reg << ", " << ci->GetValue() << "\n";
        } else if (auto* gv = dynamic_cast<const ir::GlobalVar*>(val)) {
            os_ << kIndent << "la    " << reg << ", " << GlobalLabel(gv->GetName()) << "\n";
        } else if (auto* alloca = dynamic_cast<const ir::AllocaInst*>(val)) {
            os_ << kIndent << "addiu " << reg << ", $sp, " << frame_.GetOffset(alloca) << "\n";
        } else {
            os_ << kIndent << "lw    " << reg << ", " << frame_.GetOffset(val) << "($sp)\n";
        }
    }

    // =========================================================================
    // Phi lowering
    // =========================================================================

    void FunctionEmitter::EmitPhiMovesBeforeBranch(const ir::BasicBlock* pred_block,
                                                   const ir::BranchInst* branch) {
        std::vector<const ir::BasicBlock*> successors;
        if (branch->IsConditional()) {
            successors.push_back(branch->GetIfTrue());
            successors.push_back(branch->GetIfFalse());
        } else {
            successors.push_back(branch->GetDest());
        }

        for (const ir::BasicBlock* succ : successors) {
            // Collect (phi, incoming_value) pairs for the pred->succ edge.
            std::vector<std::pair<const ir::PhiInst*, const ir::Value*>> edge_phis;
            for (const auto& inst : succ->GetInstructions()) {
                auto* phi = dynamic_cast<const ir::PhiInst*>(inst.get());
                if (!phi) {
                    break; // Phis are always at block top.
                }
                for (int i = 0; i < phi->GetNumIncoming(); ++i) {
                    if (phi->GetIncomingBlock(i) == pred_block) {
                        edge_phis.push_back({phi, phi->GetIncomingValue(i)});
                        break;
                    }
                }
            }
            if (edge_phis.empty()) {
                continue;
            }

            // Topological sort to resolve phi dependencies:
            // If phi_A's incoming is phi_B (same block), we must write A's slot
            // before B's slot; otherwise writing B first would clobber the value
            // that A needs to read from B's slot.
            //
            // Constraint: producer phi P can enter `sorted` only when all its
            // receivers (edges whose incoming == P) are already in `sorted`.
            std::vector<std::pair<const ir::PhiInst*, const ir::Value*>> sorted;
            sorted.reserve(edge_phis.size());

            while (sorted.size() < edge_phis.size()) {
                bool added = false;
                for (const auto& p : edge_phis) {
                    // Skip if already in sorted.
                    if (std::find_if(sorted.begin(), sorted.end(), [&p](const auto& x) {
                            return x.first == p.first;
                        }) != sorted.end()) {
                        continue;
                    }
                    // Check: all edges that read from p.first must already be in sorted.
                    bool receivers_ready = true;
                    for (const auto& q : edge_phis) {
                        if (q.second != p.first) {
                            continue;
                        }
                        if (std::find_if(sorted.begin(), sorted.end(), [&q](const auto& x) {
                                return x.first == q.first;
                            }) == sorted.end()) {
                            receivers_ready = false;
                            break;
                        }
                    }
                    if (!receivers_ready) {
                        continue;
                    }
                    sorted.push_back(p);
                    added = true;
                }
                assert(added && "Phi cycle detected in same block");
            }

            for (const auto& [phi, incoming] : sorted) {
                LoadValueToReg(incoming, "$t0");
                os_ << kIndent << "sw    $t0, " << frame_.GetOffset(phi) << "($sp)\n";
            }
        }
    }

} // namespace mips
