/**
 * @file SSANameAllocator.h
 * @brief Central SSA name allocation for IR generation (decoupled from IRBuilder/Visitor).
 *
 * Single responsibility: assign unique, monotonically increasing SSA names (e.g. "0", "1", ...).
 * Reset at function entry so each function gets its own numbering.
 */
#pragma once

#include <string>

namespace ir {

    class SSANameAllocator {
    public:
        SSANameAllocator() = default;

        /** @brief Return next SSA name and advance (e.g. "0", "1", "2", ...). */
        std::string Next() {
            return std::to_string(counter_++);
        }

        /** @brief Reset counter so next Next() returns \p start. Call at function entry. */
        void Reset(int start = 0) {
            counter_ = start;
        }

    private:
        int counter_ = 0;
    };

} // namespace ir
