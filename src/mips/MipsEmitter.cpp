#include "mips/MipsEmitter.h"
#include "ir/Constant.h"
#include "ir/GlobalVar.h"
#include "ir/Type.h"
#include "mips/FunctionEmitter.h"
#include <algorithm>

namespace mips {

    namespace {
        const char* k_indent = "    "; // 4 spaces for instructions under a label

        std::string GlobalLabel(std::string name) {
            // 对于if.true.x等标签直接删除所有.为_
            // 特别的，LLVM IR中字符串全局变量是.str.0等，删除开头的.
            std::replace(name.begin(), name.end(), '.', '_');
            return (name[0] == '_' ? "global" : "global_") + name;
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
                ir::ArrayType* arr_type = global->GetArrayType();
                assert(arr_type != nullptr && "GlobalVar is not an array");

                unsigned arr_size = arr_type->GetNumElements();
                ir::Type* elem_ty = arr_type->GetElementType();
                ir::IntegerType* it = dynamic_cast<ir::IntegerType*>(elem_ty);
                bool is_i8_array = (it && it->GetBits() == 8);

                ir::Constant* init = global->GetInitializer();
                ir::ConstantArray* ca = dynamic_cast<ir::ConstantArray*>(init);
                if (is_i8_array) {
                    // 字符串/char 数组：.asciiz "..." 或 .space n
                    if (ca) {
                        const auto& elts = ca->GetElements();
                        assert(elts.size() == arr_size &&
                               "global array initializer must be zero-padded");
                        std::string s;
                        for (size_t i = 0; i < elts.size(); ++i) {
                            ir::ConstantInt* ci = dynamic_cast<ir::ConstantInt*>(elts[i]);
                            int64_t v = ci ? ci->GetValue() : 0;
                            if (v == 0) {
                                break;
                            }
                            char c = static_cast<char>(v);
                            if (c == '\\') {
                                s += "\\\\";
                            } else if (c == '"') {
                                s += "\\\"";
                            } else if (c == '\n') {
                                s += "\\n";
                            } else {
                                s += c;
                            }
                        }
                        os_ << ".asciiz \"" << s << "\"\n";
                    } else {
                        os_ << ".space " << arr_size << "\n";
                    }
                } else {
                    if (ca) {
                        const auto& elts = ca->GetElements();
                        assert(elts.size() == arr_size &&
                               "global array initializer must be zero-padded");
                        os_ << ".word ";
                        for (unsigned i = 0; i < arr_size; ++i) {
                            if (i != 0) {
                                os_ << ", ";
                            }
                            ir::ConstantInt* ci = dynamic_cast<ir::ConstantInt*>(elts[i]);
                            os_ << (ci ? ci->GetValue() : 0);
                        }
                        os_ << "\n";
                    } else {
                        os_ << ".space " << (4 * arr_size) << "\n";
                    }
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
        os_ << "__start:\n";
        os_ << k_indent << "jal   main\n";
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
