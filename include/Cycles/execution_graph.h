#pragma once
#include <functional>
#include <vector>

namespace Cycles {
/**
 * @brief Roughly a baked schedule of tasks.
 *
 * An ExecutionGraph organizes tasks into "Waves". All tasks in a single wave
 * are guaranteed to be independent of each other (conforming to dependencies)
 * and can safely run in parallel.
 */
struct ExecutionGraph {
  /**
   * @brief A group of parallelizable tasks.
   */
  struct Wave {
    std::vector<std::function<void()>> tasks; ///< The list of work functions.
  };
  std::vector<Wave> waves; ///< The ordered sequence of waves to execute.
};
} // namespace Cycles
