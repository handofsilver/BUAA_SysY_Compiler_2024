#include "mips/FunctionEmitter.h"
#include "mips/MipsCommon.h"
#include "mips/RegAlloc.h"

namespace mips {

    FunctionEmitter::FunctionEmitter(AsmWriter& writer, const ir::Function& func,
                                     const MipsOptions& options) :
    writer_(writer),
    func_(func),
    options_(options),
    frame_(func),
    inst_emitter_(writer, frame_, func, options_) {}

    void FunctionEmitter::Emit() {
        frame_.Build();
        // Buffer mode is needed by both peephole (O4) and register allocation
        // (O7).  Enable it if either optimization is active.
        const bool kUseBuffer = options_.enable_peephole || options_.enable_reg_alloc;
        if (kUseBuffer) {
            writer_.BeginBuffer();
        }
        EmitPrologue();
        EmitBody();
        if (options_.enable_reg_alloc) {
            RegAllocator::Run(writer_.GetBuffer());
        }
        if (kUseBuffer) {
            writer_.FlushBuffer();
        }
    }

    // =========================================================================
    // Prologue
    // =========================================================================

    void FunctionEmitter::EmitPrologue() {
        writer_.EmitLabel(func_.GetName());
        writer_.EmitAddiu("$sp", "$sp", -frame_.GetFrameSize());
        writer_.EmitSwSp("$ra", 0);

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
        const auto& blocks = func_.GetBlocks();
        for (size_t idx = 0; idx < blocks.size(); ++idx) {
            const ir::BasicBlock* cur_block = blocks[idx].get();
            // next_block is used by O5 to detect fall-through opportunities.
            // It is nullptr for the last block (no successor in emission order).
            const ir::BasicBlock* next_block =
                (idx + 1 < blocks.size()) ? blocks[idx + 1].get() : nullptr;

            writer_.EmitLabel(BlockLabel(func_.GetName(), cur_block->GetName()));
            for (const auto& inst : cur_block->GetInstructions()) {
                inst_emitter_.Emit(inst.get(), cur_block, next_block);
            }
        }
    }

} // namespace mips
