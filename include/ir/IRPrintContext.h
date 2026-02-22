/**
 * @file IRPrintContext.h
 * @brief Print-time SSA renumbering: layout-order names for .ll output.
 *
 * Built once per function before printing. Maps each SSA Value and each
 * BasicBlock to a print name so that output has monotonically increasing
 * %N and stable block labels (e.g. for.cond.1, if.then.2).
 */
#pragma once

#include <string>
#include <unordered_map>

namespace ir {

    class Function;
    class Value;
    class BasicBlock;

    class IRPrintContext {
    public:
        /** @brief Build value and block name maps in layout order (args, then blocks, then insts).
         */
        void Build(const Function* func);

        /** @brief Get print SSA name for a value (e.g. "3"); returns false if not in this function.
         */
        bool GetSSAName(const Value* val, std::string& out) const;

        /** @brief Get print label for a block (e.g. "entry", "for.cond.1"); returns false if not in
         * this function. */
        bool GetBlockLabel(const BasicBlock* bb, std::string& out) const;

    private:
        std::unordered_map<const Value*, std::string> value_to_name_;
        std::unordered_map<const BasicBlock*, std::string> block_to_label_;
    };

} // namespace ir
