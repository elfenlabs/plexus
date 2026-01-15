# Module: include

**Purpose**: Provides core abstractions for building and executing dataflow graphs with resource management, node execution, and concurrency control.

**Location**: `/home/yonder/projects/plexus/include`

**Consumes**: 
- Context and resource definitions
- Node configurations and dependencies
- Function traits for type deduction

**Produces**: 
- Execution graphs
- Node execution handles
- Resource access wrappers

## Dependencies

**Depends on**:
- `plexus/context.h` – For managing execution context and node state
- `plexus/node.h` – For defining node structures and dependencies
- `plexus/resource.h` – For resource access and lifecycle management
- `plexus/execution_graph.h` – For graph structure and node relationships
- `plexus/function_traits.h` – For type deduction in node creation

**Depended by**:
- `plexus/executor` – Uses `ExecutionGraph` and `GraphBuilder` to run tasks
- `plexus/graph_builder` – Builds execution graphs from node definitions
- Unknown – No other modules explicitly depend on this module

## Key Components

- `GraphBuilder` – Constructs execution graphs by adding nodes with dependencies
- `Resource` – Encapsulates shared data with read/write access control
- `Context` – Manages global execution state and node lifecycle
- `Executor` – Runs graphs asynchronously using a thread pool
- `ReadAccess` / `WriteAccess` – RAII wrappers for safe resource access

## Public Interface

- `GraphBuilder::add_node(...)` – Adds a node with specified work and resource accesses
- `Resource<T>::get()` / `get_mut()` – Accesses resource data safely
- `Read(const Resource<T>&)` / `Write(Resource<T>&)` – Wraps access for dependency tracking
- `Executor::execute()` – Runs an execution graph asynchronously
- `Context::get_node()` – Retrieves node configuration by ID

## Common Usage Patterns

### Basic Usage
```cpp
Context ctx;
GraphBuilder builder(ctx);

auto node_id = builder.add_node("compute", []() { /* work */ },
                                Write(res));
```

### Async Execution
```cpp
Executor exec;
auto handle = exec.execute(graph);
handle.wait(); // Wait for completion
```

## Critical Patterns

```cpp
// Resources must be accessed via Read/Write wrappers
auto access = Read(resource);
auto data = access.get(); // Safe read access
```

```cpp
// Node functions must be callable with correct signature
auto node_id = builder.add_node("func", [](int x) { return x * 2; },
                                 Read(res));
```

```cpp
// GraphBuilder tracks dependencies automatically
auto node_id = builder.add_node("process", work, Read(res1), Write(res2));
```

## Invariants & Design Notes

- All resource access must go through `Read`/`Write` wrappers to track dependencies
- Nodes are added in topological order to ensure valid execution graph
- `Context` owns all nodes and resources; no external ownership is allowed
- `Executor` uses a thread pool for asynchronous execution with futures
- `GraphBuilder` deduces node types using `function_traits` for compile-time safety

## File List

- `runtime.h` – Defines runtime-related types and utilities
- `resource.h` – Declares `Resource`, `ReadAccess`, `WriteAccess` types
- `execution_graph.h` – Defines `ExecutionGraph` and `Node` structures
- `node.h` – Declares `Dependency`, `NodeConfig`, and related types
- `context.h` – Defines `Context` for managing execution state
- `graph_builder.h` – Implements `GraphBuilder` for constructing execution graphs
- `executor.h` – Implements `Executor` for running graphs asynchronously
- `function_traits.h` – Provides compile-time introspection of function signatures
---
*Generated: 2026-01-15T10:39:08 | Source hash: 8e07e99 | llmap v0.1.0*

---

## Related Modules

*No direct module dependencies detected.*
