#pragma once

#include <Interpreters/ActionsDAG.h>

namespace DB
{

/// This structure stores a node mapping from one DAG to another.
/// The rule is following:
/// * Input nodes are mapped by name.
/// * Function is mapped to function if all children are mapped and function names are same.
/// * Alias is mapped to it's children mapping.
/// * Monotonic function can be mapped to it's children mapping if direct mapping does not exist.
///   In this case, information about monotonicity is filled.
/// * Mapped node is nullptr if there is no mapping found.
///
/// Overall, directly mapped nodes represent equal calculations.
/// Notes:
/// * Mapped DAG can contain many nodes which represent the same calculation.
///   In this case mapping is ambiguous and only one node is mapped.
/// * Aliases for mapped DAG are not supported.
/// DAG for PK does not contain aliases and ambiguous nodes.
struct MatchedTrees
{
    struct Match;

    /// Monotonicity is calculated for monotonic functions chain.
    /// Chain is not strict if there is any non-strict monotonic function.
    struct Monotonicity
    {
        int direction = 1;
        bool strict = true;
        const Match * child_match = nullptr;
        const ActionsDAG::Node * child_node = nullptr;
    };

    struct Match
    {
        const ActionsDAG::Node * node = nullptr;
        std::optional<Monotonicity> monotonicity;
    };

    using Matches = std::unordered_map<const ActionsDAG::Node *, Match>;
};

MatchedTrees::Matches matchTrees(const ActionsDAG::NodeRawConstPtrs & inner_dag, const ActionsDAG & outer_dag, bool check_monotonicity = true);

/// Update SortDescription (inplace) by applying ActionsDAG.
///
/// Assuming that sorting properties are fulfilled for inputs, calculate sorting properties for the outputs.
/// If SortColumnDescription does not match any input, we keep it unchanged (the column was not affected by DAG).
///
/// Monotonic functions are supported.
/// There could be many possible results, e.g. for a DAG
/// (ts, value) -> (ts AS timestamp, -identity(ts), toStartOfHour(ts), -value)
/// with SortDescription (ts, value) possible results are
/// * (timestamp, -value DESC)           : the best one, cause no monotonic function is applied to 'ts'
/// * (-identity(ts) DESC, -value DESC)  : second, cause strict monotonic chain is applied to 'ts'
/// * (toStartOfHour(ts))                : third, cause non-strict monotonic function is applied to 'ts'
/// Only one "best" result is returned for now.
void applyActionsToSortDescription(
    SortDescription & description,
    const ActionsDAG & dag,
    /// Ignore this one output of DAG. Used for FilterStep where filter column is removed.
    const ActionsDAG::Node * output_to_skip = nullptr);

/// Traverses a set of root nodes (`nodes`) and attempts to replace them (and their subtrees)
/// with matched input nodes from the `matches` map, under the condition that:
/// - The match is non-monotonic (`!match.monotonicity`)
/// - The matched node is allowed (present in `allowed_inputs`)
///
/// This function performs a depth-first traversal to ensure all nodes and their dependencies
/// can be substituted by valid inputs. If any node ultimately resolves to an INPUT node that
/// has no valid match in `matches`, the function returns `std::nullopt` to indicate that
/// a consistent input substitution map cannot be constructed.
///
/// The primary use case is to construct the input substitution map required by `ActionsDAG::foldActionsByProjection`.
std::optional<std::unordered_map<const ActionsDAG::Node *, const ActionsDAG::Node *>> resolveMatchedInputs(
    const MatchedTrees::Matches & matches,
    const std::unordered_set<const ActionsDAG::Node *> & allowed_inputs,
    const ActionsDAG::NodeRawConstPtrs & nodes);
}
