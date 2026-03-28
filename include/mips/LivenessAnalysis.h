/**
 * @file LivenessAnalysis.h
 * @brief MIPS-level liveness analysis and interference graph construction.
 *
 * Operates on the flat MipsInst buffer produced by FunctionEmitter.
 * This is the "Build" phase of Chaitin-Briggs graph coloring register
 * allocation: compute live ranges for every register, then construct an
 * interference graph where two nodes are connected iff their live ranges
 * overlap.
 *
 * The analysis is read-only — it never modifies the instruction buffer.
 */
#pragma once

#include "mips/MipsInst.h"

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace mips {

    // =====================================================================
    // Def/Use extraction
    // =====================================================================

    /// Return the registers defined by @p inst.
    std::vector<std::string> GetDefs(const MipsInst& inst);

    /// Return the registers used (read) by @p inst.
    std::vector<std::string> GetUses(const MipsInst& inst);

    // =====================================================================
    // Register ID mapping (string name <-> integer)
    // =====================================================================

    class RegIdMap {
    public:
        /// Get or create an integer ID for the given register name.
        int GetOrCreate(const std::string& name);

        /// Look up the ID for a name that is known to exist.
        int Get(const std::string& name) const;

        /// @return id if present, otherwise -1.
        int TryGet(const std::string& name) const;

        /// Reverse map: ID -> name.
        const std::string& GetName(int id) const;

        /// Total number of mapped registers.
        int Size() const {
            return static_cast<int>(id_to_name_.size());
        }

        /// True if @p name participates in graph coloring ($vr*, $t0-$t9, $s0-$s7).
        static bool IsAllocatable(const std::string& name);

        /// True for virtual registers emitted before RA rewrite (prefix "$vr").
        static bool IsVirtual(const std::string& name);

        /// Palette index 0..17 for fixed $t0-$t9 / $s0-$s7; -1 for virtual regs and others.
        static int PaletteIndexOf(const std::string& name);

    private:
        std::unordered_map<std::string, int> name_to_id_;
        std::vector<std::string> id_to_name_;
    };

    // =====================================================================
    // MIPS-level basic block (partitioned from the flat instruction buffer)
    // =====================================================================

    struct MipsBlock {
        int start;              ///< Buffer index of the LABEL (inclusive).
        int end;                ///< Buffer index past the last instruction (exclusive).
        std::string label;      ///< Label name.
        std::vector<int> succs; ///< Successor block indices.
        std::vector<int> preds; ///< Predecessor block indices.
    };

    // =====================================================================
    // Interference graph
    // =====================================================================

    class InterferenceGraph {
    public:
        explicit InterferenceGraph(int num_nodes);

        /// Add an undirected interference edge.  Ignores self-loops.
        void AddEdge(int u, int v);

        /// Query whether an edge exists.
        bool HasEdge(int u, int v) const;

        /// Current degree of @p node.
        int Degree(int node) const;

        /// Neighbor set of @p node.
        const std::unordered_set<int>& Neighbors(int node) const;

        /// Record a MOVE pair (dst_id, src_id) for the Coalesce phase.
        void AddMove(int dst, int src);

        /// All recorded move pairs.
        const std::vector<std::pair<int, int>>& GetMoves() const {
            return moves_;
        }

        /// True if @p node appears in at least one move pair.
        bool IsMoveRelated(int node) const;

        int NumNodes() const {
            return num_nodes_;
        }

    private:
        int num_nodes_;
        std::vector<std::unordered_set<int>> adj_;
        std::vector<int> degree_;
        std::vector<std::pair<int, int>> moves_;
        std::unordered_set<int> move_related_;
    };

    // =====================================================================
    // Analysis result bundle
    // =====================================================================

    struct LivenessResult {
        RegIdMap reg_ids;
        std::vector<MipsBlock> blocks;
        InterferenceGraph ig;

        LivenessResult(RegIdMap ids, std::vector<MipsBlock> blks, InterferenceGraph graph) :
        reg_ids(std::move(ids)),
        blocks(std::move(blks)),
        ig(std::move(graph)) {}
    };

    /// Run the complete Build phase on the instruction buffer.
    /// Returns liveness information and the interference graph.
    LivenessResult BuildLiveness(const std::vector<MipsInst>& buffer);

} // namespace mips
