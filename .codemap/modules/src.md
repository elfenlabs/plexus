# Module: src

**Purpose**: This module provides the core execution and management functionalities for running tasks in parallel or sequentially, including task scheduling, resource management, and graph construction.

**Location**: `/home/yonder/projects/plexus/src`

## Dependencies

**Depends on**:
- `thread_pool.h` – Manages worker threads and task queues.
- `work_stealing_queue.h` – Implements work-stealing queues for efficient task distribution among workers.
- `task_node_pool.h` – Manages a pool of tasks to be executed.

**Depended by**:
- Unknown

## Key Components

- **Executor**: Manages the execution of graphs in various modes (parallel, sequential).
  - Functions: `run`, `run_async`, `run_parallel`, `run_sequential`, `run_task`
- **TaskNodePool**: Manages a pool of task nodes.
  - Classes/Structs: `FixedFunction`, `TaskNodePool`, `TaskNode`
- **WorkStealingQueue**: Implements work-stealing queues for efficient load balancing among threads.
  - Classes/Structs: `WorkStealingQueue`

## Public Interface

- `Executor::run(const ExecutionGraph &graph, ExecutionMode mode)` – Runs the given graph in the specified execution mode (parallel or sequential).
- `Executor::run_async(const ExecutionGraph &graph, ExecutionMode mode)` – Asynchronously runs the given graph.
- `Executor::run_parallel(const ExecutionGraph &graph, std::shared_ptr<AsyncHandle::State> async_state)` – Runs the graph in parallel with state management.
- `Executor::run_sequential(const ExecutionGraph &graph, std::shared_ptr<AsyncHandle::State> async_state)` – Runs the graph sequentially with state management.

## Invariants & Design Notes

- The `Executor` ensures that tasks are executed according to the specified mode (parallel or sequential).
- The `TaskNodePool` maintains a pool of task nodes, which can be reused for efficient memory management.
- The `WorkStealingQueue` ensures load balancing by stealing tasks from other queues when necessary.

## File List

- `executor.cpp` – Implements core execution functionalities and manages the execution graph.
- `task_node_pool.h` – Manages a pool of task nodes to optimize resource usage.
- `work_stealing_queue.h` – Implements work-stealing queues for efficient task distribution among workers.
- `runtime.cpp` – Provides runtime utilities, such as determining current worker index and worker count.
- `graph_builder.cpp` – Constructs execution graphs from configuration data.
- `thread_pool.h` – Manages a pool of threads and their associated tasks.
- `execution_graph.cpp` – Implements the core functionalities for managing execution graphs.
- `context.cpp` – Manages context resources, such as registering and retrieving resource names.