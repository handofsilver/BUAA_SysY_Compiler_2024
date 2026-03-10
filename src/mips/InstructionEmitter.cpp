#include "mips/InstructionEmitter.h"
#include "ir/Constant.h"
#include "ir/GlobalVar.h"
#include "ir/Type.h"
#include "mips/MipsCommon.h"
#include <algorithm>
#include <cassert>
#include <vector>

namespace mips {

    namespace {
        /// Compute element size (bytes) for the pointee type of a GEP result.
        /// Returns 1 for i8 elements, 4 for i32 elements (default).
        int GepElementSizeBytes(const ir::GetElementPtrInst* inst) {
            auto* ptr_ty = dynamic_cast<const ir::PointerType*>(inst->GetType());
            if (!ptr_ty) {
                return 4;
            }
            ir::Type* pointee = ptr_ty->GetPointeeType();
            assert(pointee && "Pointee type must be non-null");

            ir::Type* elem_ty = pointee;
            // SysY has only 1D arrays; our IR never produces GEP with result type ptr-to-array.
            // If that changes (e.g. multi-dim), handle here and remove this assert.
            if (dynamic_cast<const ir::ArrayType*>(pointee) != nullptr) {
                assert(false && "GEP result pointee is ArrayType (unexpected under SysY)");
            }

            auto* int_ty = dynamic_cast<const ir::IntegerType*>(elem_ty);
            assert(int_ty && "Element type must be integer");
            return static_cast<int>(int_ty->GetBits() / 8);
        }
    } // namespace

    // =========================================================================
    // Construction
    // =========================================================================

    InstructionEmitter::InstructionEmitter(AsmWriter& writer, const StackFrame& frame,
                                           const ir::Function& func) :
    writer_(writer),
    frame_(frame),
    func_(func) {}

    // =========================================================================
    // Public dispatch
    // =========================================================================

    void InstructionEmitter::Emit(const ir::Instruction* inst, const ir::BasicBlock* block) {
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
            EmitPhiMovesForEdge(block, branch);
            EmitBranchInst(branch);
        } else if (auto* zext = dynamic_cast<const ir::ZextInst*>(inst)) {
            EmitZextInst(zext);
        } else if (auto* trunc = dynamic_cast<const ir::TruncInst*>(inst)) {
            EmitTruncInst(trunc);
        } else if (auto* call = dynamic_cast<const ir::CallInst*>(inst)) {
            EmitCallInst(call);
        }
        // PhiInst: handled by EmitPhiMovesForEdge at predecessor's branch.
        // AllocaInst: space allocated in StackFrame::Build(); no runtime code needed.
    }

    // =========================================================================
    // Instruction emission
    // =========================================================================

    void InstructionEmitter::EmitBinaryInst(const ir::BinaryInst* inst) {
        LoadValueToReg(inst->GetLhs(), "$t0");
        LoadValueToReg(inst->GetRhs(), "$t1");
        switch (inst->GetOp()) {
            case ir::BinaryOp::ADD: writer_.EmitInsn("addu  $t2, $t0, $t1"); break;
            case ir::BinaryOp::SUB: writer_.EmitInsn("subu  $t2, $t0, $t1"); break;
            case ir::BinaryOp::MUL: writer_.EmitInsn("mul   $t2, $t0, $t1"); break;
            case ir::BinaryOp::DIV:
                writer_.EmitInsn("div   $t0, $t1");
                writer_.EmitInsn("mflo  $t2");
                break;
            case ir::BinaryOp::REM:
                writer_.EmitInsn("div   $t0, $t1");
                writer_.EmitInsn("mfhi  $t2");
                break;
            default: assert(false && "Unknown binary op");
        }
        writer_.EmitSwSp("$t2", frame_.GetOffset(inst));
    }

    void InstructionEmitter::EmitLoadInst(const ir::LoadInst* inst) {
        LoadValueToReg(inst->GetPointerOperand(), "$t0");
        auto* int_ty = dynamic_cast<const ir::IntegerType*>(inst->GetType());
        if (int_ty && int_ty->GetBits() == 8) {
            writer_.EmitInsn("lbu   $t0, 0($t0)"); // i8: zero-extend byte to 32-bit
        } else {
            writer_.EmitInsn("lw    $t0, 0($t0)");
        }
        writer_.EmitSwSp("$t0", frame_.GetOffset(inst));
    }

    void InstructionEmitter::EmitStoreInst(const ir::StoreInst* inst) {
        LoadValueToReg(inst->GetValueOperand(), "$t0");
        LoadValueToReg(inst->GetPointerOperand(), "$t1");
        auto* int_ty = dynamic_cast<const ir::IntegerType*>(inst->GetValueOperand()->GetType());
        if (int_ty && int_ty->GetBits() == 8) {
            writer_.EmitInsn("sb    $t0, 0($t1)"); // i8: store low byte only
        } else {
            writer_.EmitInsn("sw    $t0, 0($t1)");
        }
    }

    void InstructionEmitter::EmitGetElementPtrInst(const ir::GetElementPtrInst* inst) {
        LoadValueToReg(inst->GetPointerOperand(), "$t0");
        // ptr-to-array: GEP has two indices (first 0, second = element index);
        // ptr-to-scalar (e.g. param): one index.
        const ir::Value* elem_index =
            inst->GetIndex(1) != nullptr ? inst->GetIndex(1) : inst->GetIndex(0);
        LoadValueToReg(elem_index, "$t1");
        int elem_size = GepElementSizeBytes(inst);
        if (elem_size == 4) {
            writer_.EmitInsn("sll   $t2, $t1, 2");
            writer_.EmitInsn("addu  $t2, $t0, $t2");
        } else {
            // i8 elements: offset = index * 1, add directly.
            writer_.EmitInsn("addu  $t2, $t0, $t1");
        }
        writer_.EmitSwSp("$t2", frame_.GetOffset(inst));
    }

    void InstructionEmitter::EmitIcmpInst(const ir::IcmpInst* inst) {
        LoadValueToReg(inst->GetLhs(), "$t0");
        LoadValueToReg(inst->GetRhs(), "$t1");
        switch (inst->GetPredicate()) {
            case ir::IcmpPred::SLT: writer_.EmitInsn("slt   $t2, $t0, $t1"); break;
            case ir::IcmpPred::SGT: writer_.EmitInsn("sgt   $t2, $t0, $t1"); break;
            case ir::IcmpPred::SLE: writer_.EmitInsn("sle   $t2, $t0, $t1"); break;
            case ir::IcmpPred::SGE: writer_.EmitInsn("sge   $t2, $t0, $t1"); break;
            case ir::IcmpPred::EQ: writer_.EmitInsn("seq   $t2, $t0, $t1"); break;
            case ir::IcmpPred::NE: writer_.EmitInsn("sne   $t2, $t0, $t1"); break;
        }
        writer_.EmitSwSp("$t2", frame_.GetOffset(inst));
    }

    void InstructionEmitter::EmitBranchInst(const ir::BranchInst* inst) {
        if (inst->IsConditional()) {
            LoadValueToReg(inst->GetCond(), "$t0");
            writer_.EmitInsn("bnez  $t0, " +
                             BlockLabel(func_.GetName(), inst->GetIfTrue()->GetName()));
            writer_.EmitInsn("j     " + BlockLabel(func_.GetName(), inst->GetIfFalse()->GetName()));
        } else {
            writer_.EmitInsn("j     " + BlockLabel(func_.GetName(), inst->GetDest()->GetName()));
        }
    }

    void InstructionEmitter::EmitZextInst(const ir::ZextInst* inst) {
        LoadValueToReg(inst->GetOperandValue(), "$t0");
        writer_.EmitSwSp("$t0", frame_.GetOffset(inst));
    }

    void InstructionEmitter::EmitTruncInst(const ir::TruncInst* inst) {
        LoadValueToReg(inst->GetOperandValue(), "$t0");
        writer_.EmitInsn("andi  $t0, $t0, 0xFF"); // i32 -> i8: keep low 8 bits
        writer_.EmitSwSp("$t0", frame_.GetOffset(inst));
    }

    void InstructionEmitter::EmitReturnInst(const ir::ReturnInst* inst) {
        if (auto* ret_val = inst->GetRetVal()) {
            LoadValueToReg(ret_val, "$v0");
        }
    }

    void InstructionEmitter::EmitEpilogue() {
        writer_.EmitLwSp("$ra", 0);
        writer_.EmitAddiu("$sp", "$sp", frame_.GetFrameSize());
        writer_.EmitInsn("jr    $ra");
    }

    // =========================================================================
    // Function calls
    // =========================================================================

    void InstructionEmitter::EmitCallInst(const ir::CallInst* inst) {
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

        // 1) Store args 5+ one at a time to their final positions below
        //    current $sp. Each arg is loaded into $t0 and immediately
        //    written, avoiding the need for N temporary registers.
        //    Target offset from current $sp: -kExtraSize + (i-4)*4.
        //    After $sp adjustment in step 3, these become (i-4)*4($sp).
        if (kExtraSize > 0) {
            for (size_t i = 4; i < kNumArgs; ++i) {
                LoadValueToReg(inst->GetArg(static_cast<int>(i)), "$t0");
                writer_.EmitSwSp("$t0", -kExtraSize + static_cast<int>((i - 4) * 4));
            }
        }
        // 2) Load first 4 args into $a0-$a3 (still using current $sp).
        for (size_t i = 0; i < kNumArgs && i < 4u; ++i) {
            LoadValueToReg(inst->GetArg(static_cast<int>(i)), "$a" + std::to_string(i));
        }
        // 3) Adjust $sp to cover the pre-written arg area.
        if (kExtraSize > 0) {
            writer_.EmitAddiu("$sp", "$sp", -kExtraSize);
        }

        writer_.EmitInsn("jal   " + name);

        if (kExtraSize > 0) {
            writer_.EmitAddiu("$sp", "$sp", kExtraSize);
        }

        // Store return value if non-void.
        if (!dynamic_cast<const ir::VoidType*>(inst->GetType())) {
            if (frame_.HasSlot(inst)) {
                writer_.EmitSwSp("$v0", frame_.GetOffset(inst));
            }
        }
    }

    void InstructionEmitter::EmitLibraryCall(const ir::CallInst* inst) {
        const std::string& name = inst->GetCallee()->GetName();
        if (name == "getint") {
            writer_.EmitLi("$v0", 5);
            writer_.EmitSyscall();
            writer_.EmitSwSp("$v0", frame_.GetOffset(inst));
        } else if (name == "getchar") {
            writer_.EmitLi("$v0", 12);
            writer_.EmitSyscall();
            writer_.EmitSwSp("$v0", frame_.GetOffset(inst));
            // MARS syscall 12 reads one char but leaves trailing '\n' in the buffer;
            // consume it so that a subsequent getint reads the next line correctly.
            writer_.EmitLi("$v0", 12);
            writer_.EmitSyscall();
        } else if (name == "putint") {
            LoadValueToReg(inst->GetArg(0), "$a0");
            writer_.EmitLi("$v0", 1);
            writer_.EmitSyscall();
        } else if (name == "putch") {
            LoadValueToReg(inst->GetArg(0), "$a0");
            writer_.EmitLi("$v0", 11);
            writer_.EmitSyscall();
        } else if (name == "putstr") {
            LoadValueToReg(inst->GetArg(0), "$a0");
            writer_.EmitLi("$v0", 4);
            writer_.EmitSyscall();
        }
    }

    // =========================================================================
    // Value loading helper
    // =========================================================================

    void InstructionEmitter::LoadValueToReg(const ir::Value* val, const std::string& reg) {
        // Phi incoming can be nullptr (undef) from Mem2Reg; no stack slot exists.
        if (val == nullptr) {
            writer_.EmitLi(reg, 0);
            return;
        }
        if (auto* ci = dynamic_cast<const ir::ConstantInt*>(val)) {
            writer_.EmitLi(reg, ci->GetValue());
        } else if (auto* gv = dynamic_cast<const ir::GlobalVar*>(val)) {
            writer_.EmitLa(reg, GlobalLabel(gv->GetName()));
        } else if (auto* alloca = dynamic_cast<const ir::AllocaInst*>(val)) {
            // Alloca address = $sp + slot offset; no memory load needed.
            writer_.EmitAddiu(reg, "$sp", frame_.GetOffset(alloca));
        } else {
            // General stack slot: dispatch through ValueLocation for future
            // register-allocation support (currently always STACK).
            ValueLocation loc = frame_.GetLocation(val);
            if (loc.kind == ValueLocation::REGISTER) {
                if (loc.reg_name != reg) {
                    writer_.EmitMove(reg, loc.reg_name);
                }
            } else {
                writer_.EmitLwSp(reg, loc.stack_offset);
            }
        }
    }

    // =========================================================================
    // Phi lowering
    // =========================================================================

    void InstructionEmitter::EmitPhiMovesForEdge(const ir::BasicBlock* pred_block,
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
                    break; // Convention: phis are always at the top of the block.
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
                writer_.EmitSwSp("$t0", frame_.GetOffset(phi));
            }
        }
    }

} // namespace mips
