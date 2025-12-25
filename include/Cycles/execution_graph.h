#pragma once
#include <vector>
#include <functional>

namespace Cycles {
    struct ExecutionGraph {
        struct Wave {
            std::vector<std::function<void()>> tasks;
        };
        std::vector<Wave> waves;
    };
}
