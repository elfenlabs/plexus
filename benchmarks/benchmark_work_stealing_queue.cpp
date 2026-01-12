#include "work_stealing_queue.h"
#include <benchmark/benchmark.h>
#include <vector>

namespace {

struct BenchNode {
    int value;
};

} // namespace

static void BM_WSQ_PushPop(benchmark::State &state) {
    const int batch = static_cast<int>(state.range(0));
    std::vector<BenchNode> nodes(static_cast<size_t>(batch));
    Plexus::WorkStealingQueue<BenchNode> queue(static_cast<size_t>(batch * 2));

    for (auto _ : state) {
        for (int i = 0; i < batch; ++i) {
            queue.push(&nodes[static_cast<size_t>(i)]);
        }
        for (int i = 0; i < batch; ++i) {
            benchmark::DoNotOptimize(queue.pop());
        }
    }
    state.SetItemsProcessed(state.iterations() * batch * 2);
}
BENCHMARK(BM_WSQ_PushPop)->Range(1024, 65536);

static void BM_WSQ_StealEmpty(benchmark::State &state) {
    Plexus::WorkStealingQueue<BenchNode> queue(1024);

    for (auto _ : state) {
        benchmark::DoNotOptimize(queue.steal());
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_WSQ_StealEmpty);

static void BM_WSQ_StealHit(benchmark::State &state) {
    const int batch = static_cast<int>(state.range(0));
    std::vector<BenchNode> nodes(static_cast<size_t>(batch));
    Plexus::WorkStealingQueue<BenchNode> queue(static_cast<size_t>(batch * 2));

    for (auto _ : state) {
        state.PauseTiming();
        for (int i = 0; i < batch; ++i) {
            queue.push(&nodes[static_cast<size_t>(i)]);
        }
        state.ResumeTiming();

        for (int i = 0; i < batch; ++i) {
            benchmark::DoNotOptimize(queue.steal());
        }
    }
    state.SetItemsProcessed(state.iterations() * batch);
}
BENCHMARK(BM_WSQ_StealHit)->Range(1024, 65536);
