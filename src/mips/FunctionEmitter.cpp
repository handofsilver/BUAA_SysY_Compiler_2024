#include "mips/FunctionEmitter.h"
#include "ir/Constant.h"
#include "ir/GlobalVar.h"
#include "ir/Type.h"
#include <algorithm>
#include <cassert>
#include <set>
#include <vector>

namespace mips {

    namespace {
        const char* k_indent = "    ";

        /** GEP 结果指针的 pointee 元素大小（字节）。i8 -> 1，i32 -> 4。 */
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
            if (auto* it = dynamic_cast<const ir::IntegerType*>(elem_ty)) {
                return static_cast<int>(it->GetBits() / 8);
            }
            return 4;
        }

        std::string GlobalLabel(std::string name) {
            // 对于if.true.x等标签直接删除所有.为_
            // 特别的，LLVM IR中字符串全局变量是.str.0等，删除开头的.
            std::replace(name.begin(), name.end(), '.', '_');
            return (name[0] == '_' ? "global" : "global_") + name;
        }

        std::string BlockLabel(const std::string& func_name, std::string label_name) {
            // 按值传递 label_name，得到可修改的副本，便于将 '.' 替换为 '_'
            std::replace(label_name.begin(), label_name.end(), '.', '_');
            return func_name + "_" + label_name;
        }
    } // namespace

    void FunctionEmitter::BuildStackFrame() {
        frame_size_ = 0;
        frame_size_ += 4; // $ra 保存槽
        // 先为 AllocaInst 分配“空间”（不占结果槽）：标量 4 字节，数组 N×4 字节

        const auto& entry_block = func_.GetBlocks()[0];
        for (const auto& inst : entry_block->GetInstructions()) {
            if (auto* alloca = dynamic_cast<const ir::AllocaInst*>(inst.get())) {
                ir::Type* pointee =
                    dynamic_cast<const ir::PointerType*>(alloca->GetType())->GetPointeeType();
                int size = 4;
                if (auto* arr = dynamic_cast<const ir::ArrayType*>(pointee)) {
                    ir::Type* elem = arr->GetElementType();
                    int elem_bytes = 4;
                    if (auto* it = dynamic_cast<const ir::IntegerType*>(elem)) {
                        elem_bytes = static_cast<int>(it->GetBits() / 8);
                    }
                    size = static_cast<int>(arr->GetNumElements()) * elem_bytes;
                    // 至少 4 字节对齐，便于后续 value 槽对齐
                    if (size > 0 && size % 4 != 0) {
                        size = (size + 3) & ~3;
                    }
                }
                value_offset_[alloca] = frame_size_;
                frame_size_ += size;
            } else {
                break; // 约定：所有alloca都在entry块最前面
            }
        }
        // 再为其他产生结果的指令各预留 4 字节结果槽
        for (const auto& block : func_.GetBlocks()) {
            for (const auto& inst : block->GetInstructions()) {
                if (dynamic_cast<const ir::BinaryInst*>(inst.get()) ||
                    dynamic_cast<const ir::LoadInst*>(inst.get()) ||
                    dynamic_cast<const ir::GetElementPtrInst*>(inst.get()) ||
                    dynamic_cast<const ir::IcmpInst*>(inst.get()) ||
                    dynamic_cast<const ir::ZextInst*>(inst.get()) ||
                    dynamic_cast<const ir::TruncInst*>(inst.get()) ||
                    dynamic_cast<const ir::PhiInst*>(inst.get())) {
                    value_offset_[inst.get()] = frame_size_;
                    frame_size_ += 4;
                } else if (auto* call = dynamic_cast<const ir::CallInst*>(inst.get())) {
                    if (!dynamic_cast<const ir::VoidType*>(call->GetType())) {
                        value_offset_[inst.get()] = frame_size_;
                        frame_size_ += 4;
                    }
                }
            }
        }
        // 形参：前 4 个占栈槽（prologue 中从 $a0–$a3 写入），第 5 个起在 caller 传入的栈区，偏移
        // frame_size + (i-4)*4
        const size_t kNumArgs = func_.GetArguments().size();
        for (size_t i = 0; i < kNumArgs && i < 4u; ++i) {
            value_offset_[func_.GetArgument(i)] = frame_size_;
            frame_size_ += 4;
        }
        for (size_t i = 4; i < kNumArgs; ++i) {
            value_offset_[func_.GetArgument(i)] = frame_size_ + static_cast<int>((i - 4) * 4);
        }
    }

    void FunctionEmitter::EmitPrologue() {
        os_ << func_.GetName() << ":\n";
        os_ << k_indent << "addiu $sp, $sp, -" << frame_size_ << "\n";
        os_ << k_indent << "sw    $ra, 0($sp)\n";
        const size_t kNumArgs = func_.GetArguments().size();
        for (size_t i = 0; i < kNumArgs && i < 4u; ++i) {
            os_ << k_indent << "sw    $a" << i << ", " << value_offset_.at(func_.GetArgument(i))
                << "($sp)\n";
        }
    }

    void FunctionEmitter::EmitBody() {
        for (const auto& block : func_.GetBlocks()) {
            os_ << BlockLabel(func_.GetName(), block->GetName()) << ":\n";
            for (const auto& inst : block->GetInstructions()) {
                if (auto* bin = dynamic_cast<const ir::BinaryInst*>(inst.get())) {
                    EmitBinaryInst(bin);
                }
                if (auto* ret = dynamic_cast<const ir::ReturnInst*>(inst.get())) {
                    EmitReturnInst(ret);
                    EmitEpilogue(); // 多块时每条 return 路径都必须立即 epilogue，不能统一在最后
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
                if (auto* icmp = dynamic_cast<const ir::IcmpInst*>(inst.get())) {
                    EmitIcmpInst(icmp);
                }
                if (auto* branch = dynamic_cast<const ir::BranchInst*>(inst.get())) {
                    EmitPhiMovesBeforeBranch(block.get(), branch);
                    EmitBranchInst(branch);
                }
                if (auto* zext = dynamic_cast<const ir::ZextInst*>(inst.get())) {
                    EmitZextInst(zext);
                }
                if (auto* trunc = dynamic_cast<const ir::TruncInst*>(inst.get())) {
                    EmitTruncInst(trunc);
                }
                if (auto* call = dynamic_cast<const ir::CallInst*>(inst.get())) {
                    EmitCallInst(call);
                }
                // PhiInst：在本块不发射；只在前驱块末尾通过 EmitPhiMovesBeforeBranch 写入
            }
        }
    }

    void FunctionEmitter::EmitEpilogue() {
        os_ << k_indent << "lw    $ra, 0($sp)\n";
        os_ << k_indent << "addiu $sp, $sp, " << frame_size_ << "\n";
        os_ << k_indent << "jr    $ra\n";
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
        auto* it = dynamic_cast<const ir::IntegerType*>(inst->GetType());
        if (it && it->GetBits() == 8) {
            os_ << k_indent << "lbu   $t0, 0($t0)\n"; // i8：按字节取，零扩展为 32 位
        } else {
            os_ << k_indent << "lw    $t0, 0($t0)\n";
        }
        os_ << k_indent << "sw    $t0, " << value_offset_[inst] << "($sp)\n";
    }

    void FunctionEmitter::EmitStoreInst(const ir::StoreInst* inst) {
        LoadValueToReg(inst->GetValueOperand(), "$t0");
        LoadValueToReg(inst->GetPointerOperand(), "$t1");
        auto* it = dynamic_cast<const ir::IntegerType*>(inst->GetValueOperand()->GetType());
        if (it && it->GetBits() == 8) {
            os_ << k_indent << "sb    $t0, 0($t1)\n"; // i8：只存低 8 位
        } else {
            os_ << k_indent << "sw    $t0, 0($t1)\n";
        }
    }

    void FunctionEmitter::EmitGetElementPtrInst(const ir::GetElementPtrInst* inst) {
        LoadValueToReg(inst->GetPointerOperand(), "$t0");
        // GEP 可能有两下标 (0, elem_idx)：取元素用最后一个下标；单下标则用 GetIndex(0)
        const ir::Value* elem_index =
            inst->GetIndex(1) != nullptr ? inst->GetIndex(1) : inst->GetIndex(0);
        LoadValueToReg(elem_index, "$t1");
        int elem_size = GepElementSizeBytes(inst);
        if (elem_size == 4) {
            os_ << k_indent << "sll   $t2, $t1, 2\n";
            os_ << k_indent << "addu  $t2, $t0, $t2\n";
        } else {
            // i8 等单字节：偏移 = index * 1，直接加
            os_ << k_indent << "addu  $t2, $t0, $t1\n";
        }
        os_ << k_indent << "sw    $t2, " << value_offset_[inst] << "($sp)\n";
    }

    void FunctionEmitter::EmitIcmpInst(const ir::IcmpInst* inst) {
        LoadValueToReg(inst->GetLhs(), "$t0");
        LoadValueToReg(inst->GetRhs(), "$t1");
        switch (inst->GetPredicate()) {
            case ir::IcmpPred::SLT: os_ << k_indent << "slt   $t2, $t0, $t1\n"; break;
            case ir::IcmpPred::SGT: os_ << k_indent << "sgt   $t2, $t0, $t1\n"; break;
            case ir::IcmpPred::SLE: os_ << k_indent << "sle   $t2, $t0, $t1\n"; break;
            case ir::IcmpPred::SGE: os_ << k_indent << "sge   $t2, $t0, $t1\n"; break;
            case ir::IcmpPred::EQ: os_ << k_indent << "seq   $t2, $t0, $t1\n"; break;
            case ir::IcmpPred::NE: os_ << k_indent << "sne   $t2, $t0, $t1\n"; break;
        }
        os_ << k_indent << "sw    $t2, " << value_offset_[inst] << "($sp)\n";
    }

    void FunctionEmitter::EmitBranchInst(const ir::BranchInst* inst) {
        if (inst->IsConditional()) {
            LoadValueToReg(inst->GetCond(), "$t0");
            os_ << k_indent << "bnez  $t0, "
                << BlockLabel(func_.GetName(), inst->GetIfTrue()->GetName()) << "\n";
            os_ << k_indent << "j     "
                << BlockLabel(func_.GetName(), inst->GetIfFalse()->GetName()) << "\n";
        } else {
            os_ << k_indent << "j     " << BlockLabel(func_.GetName(), inst->GetDest()->GetName())
                << "\n";
        }
    }

    void FunctionEmitter::EmitZextInst(const ir::ZextInst* inst) {
        LoadValueToReg(inst->GetOperandValue(), "$t0");
        os_ << k_indent << "sw    $t0, " << value_offset_[inst] << "($sp)\n";
    }

    void FunctionEmitter::EmitTruncInst(const ir::TruncInst* inst) {
        LoadValueToReg(inst->GetOperandValue(), "$t0");
        os_ << k_indent << "andi  $t0, $t0, 0xFF\n"; // i32 -> i8 取低 8 位
        os_ << k_indent << "sw    $t0, " << value_offset_[inst] << "($sp)\n";
    }

    void FunctionEmitter::LoadValueToReg(const ir::Value* val, const std::string& reg) {
        if (auto* const_int = dynamic_cast<const ir::ConstantInt*>(val)) {
            os_ << k_indent << "li    " << reg << ", " << const_int->GetValue() << "\n";
        } else if (auto* gv = dynamic_cast<const ir::GlobalVar*>(val)) {
            os_ << k_indent << "la    " << reg << ", " << GlobalLabel(gv->GetName()) << "\n";
        } else if (auto* alloca = dynamic_cast<const ir::AllocaInst*>(val)) {
            os_ << k_indent << "addiu " << reg << ", $sp, " << value_offset_[alloca] << "\n";
        } else {
            os_ << k_indent << "lw    " << reg << ", " << value_offset_[val] << "($sp)\n";
        }
    }

    void FunctionEmitter::EmitReturnInst(const ir::ReturnInst* inst) {
        if (auto* ret_val = inst->GetRetVal()) {
            LoadValueToReg(ret_val, "$v0");
        }
    }

    void FunctionEmitter::EmitCallInst(const ir::CallInst* inst) {
        const std::string& name = inst->GetCallee()->GetName();
        if (name == "getint" || name == "getchar" || name == "putint" || name == "putch" ||
            name == "putstr") {
            EmitLibraryFunctionCall(inst);
            return;
        }
        const size_t kNumArgs = inst->GetNumArgs();
        // 第 5 个及以后的参数通过栈传递：caller 在 jal 前预留空间并写入，callee 从 frame_size($sp)
        // 起取。 必须先在本帧的 $sp 下把所有实参加载到寄存器，再调整 $sp 并写栈传参；否则 lw
        // offset($sp) 会错位。
        const size_t kExtraArgs = (kNumArgs > 4u) ? (kNumArgs - 4u) : 0u;
        const int kExtraSize = static_cast<int>(kExtraArgs * 4);

        // 1) 栈上传参加载到 $t0,$t1,...（仍用当前 $sp）
        if (kExtraSize > 0) {
            for (size_t i = 4; i < kNumArgs; ++i) {
                LoadValueToReg(inst->GetArg(static_cast<int>(i)),
                               "$t" + std::to_string(static_cast<int>(i - 4)));
            }
        }
        // 2) 前 4 个实参加载到 $a0–$a3（仍用当前 $sp）
        for (size_t i = 0; i < kNumArgs && i < 4u; ++i) {
            LoadValueToReg(inst->GetArg(static_cast<int>(i)), "$a" + std::to_string(i));
        }
        // 3) 压栈并写入第 5+ 实参，再 jal
        if (kExtraSize > 0) {
            os_ << k_indent << "addiu $sp, $sp, -" << kExtraSize << "\n";
            for (size_t i = 4; i < kNumArgs; ++i) {
                os_ << k_indent << "sw    $t" << (i - 4) << ", " << static_cast<int>((i - 4) * 4)
                    << "($sp)\n";
            }
        }

        os_ << k_indent << "jal   " << name << "\n";

        if (kExtraSize > 0) {
            os_ << k_indent << "addiu $sp, $sp, " << kExtraSize << "\n";
        }

        if (!dynamic_cast<const ir::VoidType*>(inst->GetType())) {
            auto it = value_offset_.find(inst);
            if (it != value_offset_.end()) {
                os_ << k_indent << "sw    $v0, " << it->second << "($sp)\n";
            }
        }
    }

    void FunctionEmitter::EmitLibraryFunctionCall(const ir::CallInst* inst) {
        const std::string& name = inst->GetCallee()->GetName();
        if (name == "getint") {
            os_ << k_indent << "li    $v0, 5\n";
            os_ << k_indent << "syscall\n";
            os_ << k_indent << "sw    $v0, " << value_offset_.at(inst) << "($sp)\n";
        } else if (name == "getchar") {
            os_ << k_indent << "li    $v0, 12\n";
            os_ << k_indent << "syscall\n";
            os_ << k_indent << "sw    $v0, " << value_offset_.at(inst) << "($sp)\n";
            // MARS syscall 12 reads one char but leaves the trailing '\n' in the buffer;
            // consume it so that a subsequent getint() reads the next line correctly.
            os_ << k_indent << "li    $v0, 12\n";
            os_ << k_indent << "syscall\n";
        } else if (name == "putint") {
            LoadValueToReg(inst->GetArg(0), "$a0");
            os_ << k_indent << "li    $v0, 1\n";
            os_ << k_indent << "syscall\n";
        } else if (name == "putch") {
            LoadValueToReg(inst->GetArg(0), "$a0");
            os_ << k_indent << "li    $v0, 11\n";
            os_ << k_indent << "syscall\n";
        } else if (name == "putstr") {
            LoadValueToReg(inst->GetArg(0), "$a0");
            os_ << k_indent << "li    $v0, 4\n";
            os_ << k_indent << "syscall\n";
        }
    }

    void FunctionEmitter::EmitPhiMovesBeforeBranch(const ir::BasicBlock* pred_block,
                                                   const ir::BranchInst* branch) {
        // 收集 P 的后继块
        std::vector<const ir::BasicBlock*> successors;
        if (branch->IsConditional()) {
            successors.push_back(branch->GetIfTrue());
            successors.push_back(branch->GetIfFalse());
        } else {
            successors.push_back(branch->GetDest());
        }

        for (const ir::BasicBlock* succ : successors) {
            // 收集 succ 中“来自 pred_block”的 (phi, incoming_val) 对
            std::vector<std::pair<const ir::PhiInst*, const ir::Value*>> edge_phis;
            for (const auto& inst : succ->GetInstructions()) {
                auto* phi = dynamic_cast<const ir::PhiInst*>(inst.get());
                if (!phi) {
                    break; // phi 只在块顶
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

            // 按依赖拓扑序：若 phi_A 的 incoming 是 phi_B（同块），必须先写 A 再写 B，否则读 B
            // 时已被覆盖。 “生产者”p.first 可加入 sorted 仅当所有“接收者”(q 满足 q.second==p.first)
            // 已入 sorted。
            std::vector<std::pair<const ir::PhiInst*, const ir::Value*>> sorted;
            std::set<const ir::PhiInst*> phis_in_succ;
            for (const auto& p : edge_phis) {
                phis_in_succ.insert(p.first);
            }

            while (sorted.size() < edge_phis.size()) {
                bool added = false;
                for (const auto& p : edge_phis) {
                    if (std::find_if(sorted.begin(), sorted.end(), [&p](const auto& x) {
                            return x.first == p.first;
                        }) != sorted.end()) {
                        continue;
                    }
                    // 若存在 (phi_A, p.first)：即有人从 p.first 接收，则必须先写 phi_A 再写 p.first
                    bool all_receivers_of_me_ready = true;
                    for (const auto& q : edge_phis) {
                        if (q.second != p.first) {
                            continue;
                        }
                        if (std::find_if(sorted.begin(), sorted.end(), [&q](const auto& x) {
                                return x.first == q.first;
                            }) == sorted.end()) {
                            all_receivers_of_me_ready = false;
                            break;
                        }
                    }
                    if (!all_receivers_of_me_ready) {
                        continue;
                    }
                    sorted.push_back(p);
                    added = true;
                }
                assert(added && "phi cycle in same block");
            }

            for (const auto& p : sorted) {
                LoadValueToReg(p.second, "$t0");
                os_ << k_indent << "sw    $t0, " << value_offset_.at(p.first) << "($sp)\n";
            }
        }
    }
} // namespace mips
