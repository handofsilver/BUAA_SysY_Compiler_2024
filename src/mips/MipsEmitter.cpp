#include "mips/MipsEmitter.h"
#include "ir/Constant.h"
#include "ir/GlobalVar.h"
#include "ir/Type.h"
#include "mips/FunctionEmitter.h"
#include "mips/MipsCommon.h"

namespace mips {

    MipsEmitter::MipsEmitter(std::ostream& os, const ir::Module& module,
                             const MipsOptions& options) :
    os_(os),
    module_(module),
    options_(options) {}

    void MipsEmitter::Emit() {
        EmitDataSegment();
        EmitTextSegment();
    }

    // =========================================================================
    // .data segment
    // =========================================================================

    void MipsEmitter::EmitDataSegment() {
        os_ << ".data\n";

        for (const auto& global : module_.GetGlobalVars()) {
            os_ << kIndent << GlobalLabel(global->GetName()) << ": ";

            if (global->IsArray()) {
                ir::ArrayType* arr_type = global->GetArrayType();
                assert(arr_type != nullptr && "GlobalVar is not an array");

                unsigned arr_size = arr_type->GetNumElements();
                ir::Type* elem_ty = arr_type->GetElementType();
                auto* int_ty = dynamic_cast<ir::IntegerType*>(elem_ty);
                bool is_i8 = (int_ty && int_ty->GetBits() == 8);

                ir::Constant* init = global->GetInitializer();
                auto* ca = dynamic_cast<ir::ConstantArray*>(init);

                if (is_i8) {
                    // Char array: emit .asciiz or .space.
                    if (ca) {
                        const auto& elts = ca->GetElements();
                        assert(elts.size() == arr_size &&
                               "Global array initializer must be zero-padded");
                        std::string s;
                        for (size_t i = 0; i < elts.size(); ++i) {
                            auto* ci = dynamic_cast<ir::ConstantInt*>(elts[i]);
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
                    // Int array: emit .word list or .space.
                    if (ca) {
                        const auto& elts = ca->GetElements();
                        assert(elts.size() == arr_size &&
                               "Global array initializer must be zero-padded");
                        os_ << ".word ";
                        for (unsigned i = 0; i < arr_size; ++i) {
                            if (i != 0) {
                                os_ << ", ";
                            }
                            auto* ci = dynamic_cast<ir::ConstantInt*>(elts[i]);
                            os_ << (ci ? ci->GetValue() : 0);
                        }
                        os_ << "\n";
                    } else {
                        os_ << ".space " << (4 * arr_size) << "\n";
                    }
                }
            } else {
                // Scalar global.
                ir::Constant* init = global->GetInitializer();
                if (auto* ci = dynamic_cast<ir::ConstantInt*>(init)) {
                    os_ << ".word " << ci->GetValue() << "\n";
                } else {
                    os_ << ".word 0\n";
                }
            }
        }

        os_ << "\n";
    }

    // =========================================================================
    // .text segment
    // =========================================================================

    void MipsEmitter::EmitTextSegment() {
        os_ << ".text\n";
        os_ << "__start:\n";
        os_ << kIndent << "jal   main\n";
        os_ << kIndent << "li    $v0, 10\n";
        os_ << kIndent << "syscall\n\n";

        for (const auto& func : module_.GetFunctions()) {
            if (func->GetBlocks().empty()) {
                continue; // Skip external declarations. i.e. getint, putint, etc.
            }
            FunctionEmitter fe(os_, *func);
            fe.Emit();
        }
    }

} // namespace mips
