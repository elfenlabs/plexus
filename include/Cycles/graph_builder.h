#pragma once
#include "Cycles/context.h"
#include "Cycles/node.h"
#include "Cycles/execution_graph.h"
#include <vector>

namespace Cycles {

class GraphBuilder {
public:
    explicit GraphBuilder(Context& ctx);

    // Phase 1: Registration
    void add_node(NodeConfig config);

    // Phase 2: Compilation
    // Throws std::runtime_error if cyclic dependency detected
    ExecutionGraph bake();

private:
    Context& m_ctx;
    std::vector<NodeConfig> m_nodes;
};

}
