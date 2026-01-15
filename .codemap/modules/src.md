# Module: src

**Purpose**: The `src` module implements a task execution engine with support for asynchronous execution, work-stealing thread pools, and graph-based task scheduling.

**Location**: `/home/yonder/projects/plexus/src`

**Consumes**: 
- Context and configuration data for building execution graphs
- Task definitions and dependencies from graph builders

**Produces**: 
- Executable task graphs with support for parallel or sequential execution
- Async execution handles for managing task completion and exceptions

## Dependencies

**Depends on**:
- `thread_pool.h` – Provides thread pool infrastructure and work-stealing queues for task scheduling
- `task_node_pool.h` – Manages allocation and reuse of task nodes for efficient memory usage
- `work_stealing_queue.h` – Implements thread-safe queues used in the thread pool for task distribution
- `execution_graph.h` – Defines the structure of execution graphs
- `context.h` – Provides resource registration and context management

**Depended by**:
- `plexus` – Core library that uses this module to execute tasks
- `graph_builder` – Builds execution graphs that are consumed by this module
- `runtime` – Provides runtime information like worker index and count

## Key Components

- `Executor` – Main class for running execution graphs asynchronously or synchronously
- `ThreadPool` – Manages worker threads and distributes tasks using work-stealing queues
- `TaskNodePool` – Efficiently allocates and reuses task nodes
- `WorkStealingQueue` – Thread-safe queue used for distributing tasks among workers
- `GraphBuilder` – Constructs execution graphs from node configurations

## Public Interface

- `Executor::run(const ExecutionGraph &, ExecutionMode)` – Synchronously executes a graph
- `Executor::run_async(const ExecutionGraph &, ExecutionMode)` – Asynchronously executes a graph, returns an `AsyncHandle`
- `AsyncHandle::wait()` – Blocks until async task completes
- `AsyncHandle::is_done()` – Checks if async task has completed
- `ThreadPool::dispatch(std::vector<Task> &&)` – Dispatches tasks to worker threads
- `GraphBuilder::bake()` – Finalizes and returns an `ExecutionGraph`

## Common Usage Patterns

### Basic Usage
```cpp
Executor executor(thread_pool);
auto handle = executor.run_async(graph, ExecutionMode::Parallel);
handle.wait(); // Wait for completion
```

### Error Handling
```cpp
try {
    auto handle = executor.run_async(graph, ExecutionMode::Parallel);
    handle.wait();
    auto exceptions = handle.get_exceptions();
    if (!exceptions.empty()) {
        // Handle exceptions
    }
} catch (const std::exception& e) {
    // Handle exceptions from execution
}
```

## Critical Patterns

```cpp
// AsyncHandle must be kept alive until wait() is called
auto handle = executor.run_async(graph, ExecutionMode::Parallel);
handle.wait(); // Must not move handle after this point
```

```cpp
// TaskNodePool ensures no memory leaks in task allocation
TaskNode* node = pool.alloc();
// ... use node ...
pool.free(node);
```

```cpp
// WorkStealingQueue must be used with proper synchronization
queue.push(task);
TaskNode* task = queue.pop(); // Thread-safe pop
```

## Invariants & Design Notes

- All task nodes are allocated and freed through `TaskNodePool` to avoid memory fragmentation
- `AsyncHandle` ensures that tasks are waited on properly and exceptions are collected
- `ThreadPool` uses a work-stealing algorithm to balance load across threads
- Execution graphs are built using `GraphBuilder` and then executed via `Executor`
- The `Context` provides a way to register and retrieve named resources used in tasks

## File List

- `executor.cpp` – Implements the `Executor` class for running execution graphs
- `task_node_pool.h` – Provides memory pool for `TaskNode` objects
- `work_stealing_queue.h` – Implements thread-safe work-stealing queue for task distribution
- `runtime.cpp` – Provides runtime information like worker index and count
- `graph_builder.cpp` – Builds execution graphs from node configurations
- `thread_pool.h` – Implements thread pool with work-stealing queues
- `execution_graph.cpp` – Implements dumping of execution graphs for debugging
- `context.cpp` – Manages context and resource registration
---
*Generated: 2026-01-15T10:39:02 | Source hash: f3ca6b0 | llmap v0.1.0*

---

## Related Modules

*No direct module dependencies detected.*
