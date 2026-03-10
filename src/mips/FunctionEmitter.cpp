#include "mips/FunctionEmitter.h"
#include "mips/MipsCommon.h"

namespace mips {

    FunctionEmitter::FunctionEmitter(AsmWriter& writer, const ir::Function& func,
                                     const MipsOptions& options) :
    writer_(writer),
    func_(func),
    options_(options),
    frame_(func),
    inst_emitter_(writer, frame_, func) {}

    void FunctionEmitter::Emit() {
        frame_.Build();
        EmitPrologue();
        EmitBody();
    }

    // =========================================================================
    // Prologue
    // =========================================================================

    void FunctionEmitter::EmitPrologue() {
        writer_.EmitLabel(func_.GetName());
        writer_.EmitAddiu("$sp", "$sp", -frame_.GetFrameSize());
        writer_.EmitInsn("sw    $ra, 0($sp)");

        // Spill register-passed arguments ($a0-$a3) into their stack slots.
        const size_t kNumArgs = func_.GetArguments().size();
        for (size_t i = 0; i < kNumArgs && i < 4u; ++i) {
            writer_.EmitSwSp("$a" + std::to_string(i), frame_.GetOffset(func_.GetArgument(i)));
        }
    }

    // =========================================================================
    // Body: iterate blocks and dispatch each instruction
    // =========================================================================

    void FunctionEmitter::EmitBody() {
        for (const auto& block : func_.GetBlocks()) {
            writer_.EmitLabel(BlockLabel(func_.GetName(), block->GetName()));
            for (const auto& inst : block->GetInstructions()) {
                inst_emitter_.Emit(inst.get(), block.get());
            }
        }
    }

} // namespace mips
