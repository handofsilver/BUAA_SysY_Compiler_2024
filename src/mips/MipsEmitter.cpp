#include "mips/MipsEmitter.h"
#include "ir/Constant.h"
#include "ir/GlobalVar.h"
#include "ir/Type.h"
#include "mips/FunctionEmitter.h"

namespace mips {

    namespace {
        const char* k_indent = "    "; // 4 spaces for instructions under a label

        // 临时：在控制台打印 main 的返回值（便于 MARS 仿真验证），设为 false 可关闭
        const bool kEmitDebugPrintMainRet = true;

        std::string GlobalLabel(const std::string& name) {
            return "global_" + name;
        }
    } // namespace

    void MipsEmitter::Emit() {
        EmitDataSegment();
        EmitTextSegment();
    }

    void MipsEmitter::EmitDataSegment() {
        os_ << ".data\n";

        for (const auto& global : module_.GetGlobalVars()) {
            os_ << k_indent << GlobalLabel(global->GetName()) << ": ";

            if (global->IsArray()) {
                ir::ArrayType* at = global->GetArrayType();
                assert(at != nullptr && "GlobalVar is not an array");

                unsigned n = at->GetNumElements();
                ir::Constant* init = global->GetInitializer();
                ir::ConstantArray* ca = dynamic_cast<ir::ConstantArray*>(init);
                if (ca) {
                    const auto& elts = ca->GetElements();
                    // IRDeclEmitter::EmitGlobal zero-pads init to full size; initializer is never
                    // short.
                    assert(elts.size() == n && "global array initializer must be zero-padded to "
                                               "full size (IRDeclEmitter::EmitGlobal)");
                    os_ << ".word ";
                    for (unsigned i = 0; i < n; ++i) {
                        if (i != 0) {
                            os_ << ", ";
                        }
                        ir::ConstantInt* ci = dynamic_cast<ir::ConstantInt*>(elts[i]);
                        os_ << (ci ? ci->GetValue() : 0);
                    }
                    os_ << "\n";
                } else {
                    os_ << ".space " << (4 * n) << "\n";
                }
            } else {
                ir::Constant* init = global->GetInitializer();
                if (ir::ConstantInt* ci = dynamic_cast<ir::ConstantInt*>(init)) {
                    os_ << ".word " << ci->GetValue() << "\n";
                } else {
                    os_ << ".word 0\n";
                }
            }
        }

        os_ << "\n";
    }

    void MipsEmitter::EmitTextSegment() {
        os_ << ".text\n";
        os_ << "# 入口：调用 main，再用 syscall 10 退出\n";
        os_ << "__start:\n";
        os_ << k_indent << "jal   main\n";

        if (kEmitDebugPrintMainRet) {
            os_ << "    # 临时：在控制台打印 main 的返回值（可关闭 "
                   "k_emit_debug_print_main_ret）\n";
            os_ << k_indent << "move  $a0, $v0\n";
            os_ << k_indent << "li    $v0, 1\n";
            os_ << k_indent << "syscall\n";
        }

        os_ << k_indent << "li    $v0, 10\n";
        os_ << k_indent << "syscall\n\n";

        for (const auto& func : module_.GetFunctions()) {
            if (func->GetBlocks().empty()) {
                continue; // 跳过外部声明（如 getint）
            }
            FunctionEmitter fe(os_, *func);
            fe.BuildStackFrame();
            fe.EmitPrologue();
            fe.EmitBody();
            // Epilogue 已在 EmitBody 中每条 ReturnInst 后输出，此处不再调用
        }
    }

} // namespace mips
