# Module: include

**Purpose**: This module provides the core abstractions and utilities for managing resources, nodes, execution graphs, and context in a computational graph framework.

**Location**: `/home/yonder/projects/plexus/include`

## Dependencies

**Depends on**:
- `context.h` - Provides the context management for the system.
- `node.h` - Defines the node structure and its configurations.
- `execution_graph.h` - Manages the execution graph and nodes within it.
- `resource.h` - Handles resource access (read/write) mechanisms.

**Depended by**:
- `graph_builder.h` - Uses various components from this module to build graphs.
- `executor.h` - Utilizes resources, context, and nodes for executing tasks in parallel.

## Key Components

- **Resource**: Manages read and write access to data resources.
- **Node**: Represents a computational node with dependencies and configurations.
- **ExecutionGraph**: Manages the structure of the execution graph.
- **Context**: Provides the runtime environment and state management.
- **GraphBuilder**: Constructs execution graphs by adding nodes.

## Public Interface

- `ReadAccess<T>` - Returns a read access object for a resource.
- `WriteAccess<T>` - Returns a write access object for a resource.
- `NodeGroupConfig` - Configures groups of nodes.
- `NodeConfig` - Configures individual nodes.
- `GraphBuilder::add_node()` - Adds a node to the graph.
- `Executor` - Manages the execution of tasks in parallel.

## Invariants & Design Notes

- The resource system ensures that read and write operations are properly managed to avoid data races.
- Nodes must be configured with appropriate dependencies before being added to the graph.
- The context provides a consistent runtime environment for all nodes within the graph.
- The graph builder ensures that the structure of the execution graph is valid and can be executed.

## File List

- `runtime.h` – Defines basic system utilities.
- `resource.h` – Manages resource access mechanisms.
- `execution_graph.h` – Manages the structure of the execution graph.
- `node.h` – Defines node structures and configurations.
- `context.h` – Provides context management for the system.
- `graph_builder.h` – Constructs execution graphs by adding nodes.
- `executor.h` – Manages task execution in parallel.
- `function_traits.h` – Provides utility functions to infer function traits.