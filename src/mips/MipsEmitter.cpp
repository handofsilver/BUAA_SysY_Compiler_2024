#include "mips/MipsEmitter.h"
#include "ir/Constant.h"
#include "ir/GlobalVar.h"
#include "ir/Type.h"
#include "mips/FunctionEmitter.h"
#include "mips/MipsCommon.h"

namespace mips {

    MipsEmitter::MipsEmitter(std::ostream& os, const ir::Module& module,
                             const MipsOptions& options) :
    writer_(os),
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
        writer_.EmitDirective(".data");

        for (const auto& global : module_.GetGlobalVars()) {
            // Emit the label with indentation inline (label is part of data line).
            // We use the raw stream via a local lambda to match the original format:
            //   "    label: .word ...\n"
            // AsmWriter::EmitLabel() emits "label:\n" (no indent, block labels).
            // Data labels are different: they appear indented on the same line as
            // the initializer, so we write them directly here.
            const std::string kLabel = GlobalLabel(global->GetName());

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
                    // Char array: emit .asciiz for initialized strings, .space for zero-only.
                    if (ca) {
                        const auto& elts = ca->GetElements();
                        assert(elts.size() == arr_size &&
                               "Global array initializer must be zero-padded");
                        std::string s;
                        for (size_t i = 0; i < elts.size(); ++i) {
                            auto* ci = dynamic_cast<ir::ConstantInt*>(elts[i]);
                            int64_t v = ci ? ci->GetValue() : 0;
                            if (v == 0) {
                                break; // Null terminator; stop here.
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
                        writer_.EmitInsn(kLabel + ": .asciiz \"" + s + "\"");
                    } else {
                        writer_.EmitInsn(kLabel + ": .space " + std::to_string(arr_size));
                    }
                } else {
                    // Int array: emit .word list or .space.
                    if (ca) {
                        const auto& elts = ca->GetElements();
                        assert(elts.size() == arr_size &&
                               "Global array initializer must be zero-padded");
                        std::string line = kLabel + ": .word ";
                        for (unsigned i = 0; i < arr_size; ++i) {
                            if (i != 0) {
                                line += ", ";
                            }
                            auto* ci = dynamic_cast<ir::ConstantInt*>(elts[i]);
                            line += std::to_string(ci ? ci->GetValue() : 0);
                        }
                        writer_.EmitInsn(line);
                    } else {
                        writer_.EmitInsn(kLabel + ": .space " + std::to_string(4 * arr_size));
                    }
                }
            } else {
                // Scalar global.
                ir::Constant* init = global->GetInitializer();
                if (auto* ci = dynamic_cast<ir::ConstantInt*>(init)) {
                    writer_.EmitInsn(kLabel + ": .word " + std::to_string(ci->GetValue()));
                } else {
                    writer_.EmitInsn(kLabel + ": .word 0");
                }
            }
        }

        writer_.EmitBlankLine();
    }

    // =========================================================================
    // .text segment
    // =========================================================================

    void MipsEmitter::EmitTextSegment() {
        writer_.EmitDirective(".text");
        writer_.EmitLabel("__start");
        writer_.EmitJal("main");
        writer_.EmitLi("$v0", 10);
        writer_.EmitSyscall();
        writer_.EmitBlankLine();

        for (const auto& func : module_.GetFunctions()) {
            if (func->GetBlocks().empty()) {
                continue; // Skip external declarations (getint, putint, etc.).
            }
            FunctionEmitter fe(writer_, *func, options_);
            fe.Emit();
        }
    }

} // namespace mips
