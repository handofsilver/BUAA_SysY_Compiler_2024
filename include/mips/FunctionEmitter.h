#pragma once

#include "ir/Function.h"
#include "ir/Instruction.h"
#include <unordered_map>

namespace mips {

    class FunctionEmitter {
    public:
        FunctionEmitter(std::ostream& os, const ir::Function& func) : os_(os), func_(func) {}

        void BuildStackFrame();
        void EmitPrologue();
        void EmitBody();
        void EmitEpilogue();

        void EmitBinaryInst(const ir::BinaryInst* inst);
        void EmitLoadInst(const ir::LoadInst* inst);
        void EmitStoreInst(const ir::StoreInst* inst);
        void EmitBranchInst(const ir::BranchInst* inst);
        void EmitReturnInst(const ir::ReturnInst* inst);
        void EmitGetElementPtrInst(const ir::GetElementPtrInst* inst);
        void EmitIcmpInst(const ir::IcmpInst* inst);
        void EmitZextInst(const ir::ZextInst* inst);
        void EmitTruncInst(const ir::TruncInst* inst);
        void EmitCallInst(const ir::CallInst* inst);

        void LoadValueToReg(const ir::Value* val, const std::string& reg);

    private:
        std::ostream& os_;
        const ir::Function& func_;
        int frame_size_ = 0;
        std::unordered_map<const ir::Value*, int> value_offset_;
    };
} // namespace mips
