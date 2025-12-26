# Cycles

Cycles is a high-performance, multithreaded task scheduling framework for C++20 based on Directed Acyclic Graphs (DAG). It orchestrates units of work (Nodes) based on data access rules (Dependencies) to dynamically execute tasks as soon as their prerequisites are met.

## Features

- **Dynamic Task Graph**: Nodes execute immediately upon dependency resolution, maximizing parallelism.
- **Strict Phase Separation**: Heavy validation and optimization during "Baking", allocation-free execution during "Runtime".
- **Data-Driven Dependencies**: Automatic topology inference based on Read/Write access to Resources.
- **Fail-Fast Safety**: Immediate termination on cyclic dependencies or invalid access patterns.
- **Modern C++**: Built with C++20.

## Building

Cycles uses CMake. To build the library and tests:

```bash
mkdir build
cd build
cmake ..
make
ctest
```

## Basic Usage

```cpp
#include "Cycles/context.h"
#include "Cycles/graph_builder.h"
#include "Cycles/executor.h"
#include <iostream>

void example() {
    Cycles::Context ctx;
    auto buffer_id = ctx.register_resource("BufferA");

    Cycles::GraphBuilder builder(ctx);

    // Node A: Writes to BufferA
    builder.add_node({
        "Writer",
        []() { std::cout << "Writing...\n"; },
        {{buffer_id, Cycles::Access::WRITE}}
    });

    // Node B: Reads from BufferA
    builder.add_node({
        "Reader",
        []() { std::cout << "Reading...\n"; },
        {{buffer_id, Cycles::Access::READ}}
    });

    // Bake into an execution graph
    auto graph = builder.bake();

    // Execute
    Cycles::ThreadPool pool;
    Cycles::Executor executor(pool);
    executor.run(graph);
}
```

## Advanced Usage

### Dependency Resolution (WAW, WAR)
Cycles automatically handles complex dependency chains.

- **Read-After-Write (RAW)**: A Reader will always run after a Writer of the same resource has finished.
- **Write-After-Write (WAW)**: If multiple nodes write to the same resource, the order is determined by registration order (or priority). The second writer will run after the first.
- **Write-After-Read (WAR)**: A Writer will run after all current Readers of a resource have finished.

### Profiling
You can hook into the `Executor` to measure performance.

```cpp
Cycles::Executor executor(pool);
executor.set_profiler_callback([](const char* name, double duration_ms) {
    std::cout << "[Profile] " << name << " took " << duration_ms << "ms\n"; 
});
executor.run(graph);
```

## Documentation
To generate full API documentation:
```bash
cd build
make docs
```
Open `docs/doxygen/html/index.html` in your browser.
