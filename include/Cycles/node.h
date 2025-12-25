#pragma once
#include <string>
#include <functional>
#include <vector>
#include <cstdint>
#include "Cycles/context.h"

namespace Cycles {
    enum class Access { READ, WRITE };

    struct Dependency {
        ResourceID id;
        Access access;
    };

    struct NodeConfig {
        std::string debug_name;
        std::function<void()> work_function;
        std::vector<Dependency> dependencies;
        // Priority for deterministic ordering of independent nodes if needed
        int priority = 0; 
    };
}
