/**
 * @file IRBuilder.h
 * @brief Factory for IR instructions: Create<T> + convenience CreateBinary/CreateBr/CreateRet.
 *
 * 为何全部放在 .h：Create<InstType, Args...> 是模板，必须在头文件中定义以便在各调用点实例化；
 * CreateBinary/CreateBr 等是单行转发，放在 .h 是常见实践，无需单独 .cpp。
 */
#include "ir/BasicBlock.h"
#include "ir/Instruction.h"
#include <AST.h>

namespace ir {

    class IRBuilder {
    public:
        IRBuilder() = default;

        // --- 1. 定位机制 ---
        // 告诉 Builder，接下来的指令都插到这个 bb 里面去
        void SetInsertPoint(BasicBlock* bb) {
            insert_point_ = bb;
        }

        BasicBlock* GetInsertBlock() const {
            return insert_point_;
        }

        // --- 2. 核心生产线 ---
        // 这是一个辅助模板，用于创建指令并自动插入到当前块
        // InstType 是具体的指令类型（如 BinaryInst）
        // Args 是透传给指令构造函数的参数
        template <typename InstType, typename... Args>
        InstType* Create(Args&&... args) {
            // 1. 检查 insert_point_ 是否为空 (防止段错误)
            // 2. new 一个 InstType (使用 std::make_unique)
            // 3. 将它 push_back 到 insert_point_ 的指令列表中
            // 4. 返回原生指针给调用者使用 (Instruction* 或 InstType*)
            if (insert_point_ == nullptr) {
                return nullptr;
            }

            auto inst = std::make_unique<InstType>(std::forward<Args>(args)...);

            InstType* inst_ptr = inst.get(); // 获取原生指针

            insert_point_->AddInstruction(std::move(inst));

            return inst_ptr;
        }

        // --- 3. 便捷接口 (语法糖) ---
        // 这些接口内部调用上面的 Create 模板

        // 生成二元运算: %res = op type %lhs, %rhs
        Instruction* CreateBinary(OpType op, Value* lhs, Value* rhs) {
            BasicBlock* bb = GetInsertBlock();
            if (!bb || !lhs || !rhs) {
                return nullptr;
            }
            return Create<BinaryInst>("", lhs->GetType(), bb, op, lhs, rhs);
        }

        // 生成跳转: br label %dest
        Instruction* CreateBr(BasicBlock* dest) {
            BasicBlock* bb = GetInsertBlock();
            if (!bb) {
                return nullptr;
            }
            return Create<BranchInst>("", dest->GetType(), bb, dest);
        }

        // 生成条件跳转: br i1 %cond, label %if_true, label %if_false
        Instruction* CreateCondBr(Value* cond, BasicBlock* if_true, BasicBlock* if_false) {
            BasicBlock* bb = GetInsertBlock();
            if (!bb || !cond || !if_true || !if_false) {
                return nullptr;
            }
            return Create<BranchInst>("", cond->GetType(), bb, cond, if_true, if_false);
        }

        // 生成返回: ret i32 %val
        Instruction* CreateRet(Value* val) {
            BasicBlock* bb = GetInsertBlock();
            if (!bb || !val) {
                return nullptr;
            }
            return Create<ReturnInst>("", val->GetType(), bb, val);
        }

    private:
        BasicBlock* insert_point_ = nullptr; // 当前正在写入的基本块
    };

} // namespace ir
