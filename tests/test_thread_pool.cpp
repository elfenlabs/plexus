#include "../src/thread_pool.h"
#include <atomic>
#include <chrono>
#include <future>
#include <gtest/gtest.h>
#include <thread>
#include <vector>

using namespace Plexus;

// ===========================================================================
// FixedFunction Tests
// ===========================================================================

TEST(FixedFunctionTest, DefaultConstruction) {
    FixedFunction<64> ff;
    EXPECT_FALSE(static_cast<bool>(ff));
}

TEST(FixedFunctionTest, LambdaConstruction) {
    int value = 0;
    FixedFunction<64> ff([&value]() { value = 42; });

    EXPECT_TRUE(static_cast<bool>(ff));
    ff();
    EXPECT_EQ(value, 42);
}

TEST(FixedFunctionTest, MoveConstruction) {
    int value = 0;
    FixedFunction<64> ff1([&value]() { value = 100; });

    FixedFunction<64> ff2(std::move(ff1));
    EXPECT_TRUE(static_cast<bool>(ff2));
    // ff1 should be empty after move
    EXPECT_FALSE(static_cast<bool>(ff1));

    ff2();
    EXPECT_EQ(value, 100);
}

TEST(FixedFunctionTest, MoveAssignment) {
    int value = 0;
    FixedFunction<64> ff1([&value]() { value = 200; });
    FixedFunction<64> ff2;

    ff2 = std::move(ff1);
    EXPECT_TRUE(static_cast<bool>(ff2));
    EXPECT_FALSE(static_cast<bool>(ff1));

    ff2();
    EXPECT_EQ(value, 200);
}

TEST(FixedFunctionTest, SelfMoveAssignment) {
    int value = 0;
    FixedFunction<64> ff([&value]() { value = 300; });

// Suppress self-move warning for this specific test
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wself-move"
    ff = std::move(ff);
#pragma GCC diagnostic pop

    // Self-move should leave ff in a valid state
    EXPECT_TRUE(static_cast<bool>(ff));
    ff();
    EXPECT_EQ(value, 300);
}

TEST(FixedFunctionTest, CaptureByValue) {
    int captured = 42;
    FixedFunction<64> ff([captured]() mutable {
        // Lambda captures by value
        captured += 1;
    });

    ff();
    EXPECT_EQ(captured, 42); // Original unchanged
}

// ===========================================================================
// ThreadPool Tests
// ===========================================================================

TEST(ThreadPoolTest, SingleTaskEnqueueAndWait) {
    ThreadPool pool;
    std::atomic<int> count{0};

    pool.enqueue([&count]() { count++; });
    pool.wait();

    EXPECT_EQ(count, 1);
}

TEST(ThreadPoolTest, BatchDispatch) {
    ThreadPool pool;
    std::atomic<int> count{0};
    constexpr int NUM_TASKS = 100;

    std::vector<ThreadPool::Task> tasks;
    for (int i = 0; i < NUM_TASKS; ++i) {
        tasks.emplace_back([&count]() { count++; });
    }

    pool.dispatch(std::move(tasks));
    pool.wait();

    EXPECT_EQ(count, NUM_TASKS);
}

TEST(ThreadPoolTest, EmptyDispatchNoOp) {
    ThreadPool pool;
    std::vector<ThreadPool::Task> tasks;
    pool.dispatch(std::move(tasks)); // Should be no-op
    pool.wait();                     // Should return immediately
}

TEST(ThreadPoolTest, ExceptionInTask) {
    ThreadPool pool;
    std::atomic<bool> second_ran{false};

    pool.enqueue([]() { throw std::runtime_error("test exception"); });
    pool.enqueue([&second_ran]() { second_ran = true; });
    pool.wait();

    // Exception in one task shouldn't prevent others
    EXPECT_TRUE(second_ran);
}

TEST(ThreadPoolTest, HighContention) {
    ThreadPool pool;
    std::atomic<int> count{0};
    constexpr int NUM_TASKS = 10000;

    for (int i = 0; i < NUM_TASKS; ++i) {
        pool.enqueue([&count]() { count++; });
    }
    pool.wait();

    EXPECT_EQ(count, NUM_TASKS);
}

TEST(ThreadPoolTest, WorkerThreadIdentity) {
    ThreadPool pool(2);
    std::atomic<int> worker_count{0};
    std::atomic<int> non_worker_count{0};

    // Enqueue from main thread (non-worker)
    for (int i = 0; i < 10; ++i) {
        pool.enqueue([&worker_count, &non_worker_count]() {
            if (t_worker_index != SIZE_MAX) {
                worker_count++;
            } else {
                non_worker_count++;
            }
        });
    }
    pool.wait();

    // All tasks should have run on worker threads
    EXPECT_EQ(worker_count, 10);
    EXPECT_EQ(non_worker_count, 0);
}

TEST(ThreadPoolTest, WaitMultipleTimes) {
    ThreadPool pool;
    std::atomic<int> count{0};

    pool.enqueue([&count]() { count++; });
    pool.wait();
    EXPECT_EQ(count, 1);

    pool.enqueue([&count]() { count++; });
    pool.wait();
    EXPECT_EQ(count, 2);
}

TEST(ThreadPoolTest, ZeroInitialThreadsFallback) {
    // When hardware_concurrency is 0, fall back to 2
    // We can't easily test this, but we can test with explicit count
    ThreadPool pool(1);
    std::atomic<int> count{0};

    pool.enqueue([&count]() { count++; });
    pool.wait();

    EXPECT_EQ(count, 1);
}

TEST(ThreadPoolTest, TasksFromWorkerThread) {
    ThreadPool pool;
    std::atomic<int> count{0};

    // Task that enqueues more tasks from within a worker
    pool.enqueue([&pool, &count]() {
        count++;
        pool.enqueue([&count]() { count++; });
        pool.enqueue([&count]() { count++; });
    });
    pool.wait();

    EXPECT_EQ(count, 3);
}

TEST(ThreadPoolTest, ConcurrentExternalEnqueueStress) {
    ThreadPool pool;

    constexpr int producers = 6;
    constexpr int tasks_per_producer = 8000;
    constexpr int total_tasks = producers * tasks_per_producer;

    std::atomic<int> remaining{total_tasks};
    std::atomic<int> executed{0};
    std::promise<void> done;
    auto done_future = done.get_future();

    std::vector<std::thread> threads;
    threads.reserve(producers);
    for (int t = 0; t < producers; ++t) {
        threads.emplace_back([&]() {
            for (int i = 0; i < tasks_per_producer; ++i) {
                pool.enqueue([&]() {
                    executed.fetch_add(1, std::memory_order_relaxed);
                    if (remaining.fetch_sub(1, std::memory_order_acq_rel) == 1) {
                        done.set_value();
                    }
                });
            }
        });
    }

    for (auto &t : threads) {
        t.join();
    }

    if (done_future.wait_for(std::chrono::seconds(10)) != std::future_status::ready) {
        pool.shutdown(ShutdownMode::Cancel);
        FAIL() << "Timed out waiting for all tasks (possible missed wakeup or lost task)";
    }

    EXPECT_EQ(executed.load(std::memory_order_relaxed), total_tasks);
    EXPECT_EQ(remaining.load(std::memory_order_relaxed), 0);
}

TEST(ThreadPoolTest, ConcurrentDispatchStress) {
    ThreadPool pool;

    constexpr int producers = 4;
    constexpr int batches_per_producer = 250;
    constexpr int batch_size = 32;
    constexpr int total_tasks = producers * batches_per_producer * batch_size;

    std::atomic<int> remaining{total_tasks};
    std::atomic<int> executed{0};
    std::promise<void> done;
    auto done_future = done.get_future();

    std::vector<std::thread> threads;
    threads.reserve(producers);
    for (int t = 0; t < producers; ++t) {
        threads.emplace_back([&]() {
            for (int b = 0; b < batches_per_producer; ++b) {
                std::vector<ThreadPool::Task> tasks;
                tasks.reserve(batch_size);
                for (int i = 0; i < batch_size; ++i) {
                    tasks.emplace_back([&]() {
                        executed.fetch_add(1, std::memory_order_relaxed);
                        if (remaining.fetch_sub(1, std::memory_order_acq_rel) == 1) {
                            done.set_value();
                        }
                    });
                }
                pool.dispatch(std::move(tasks));
            }
        });
    }

    for (auto &t : threads) {
        t.join();
    }

    if (done_future.wait_for(std::chrono::seconds(10)) != std::future_status::ready) {
        pool.shutdown(ShutdownMode::Cancel);
        FAIL() << "Timed out waiting for all dispatched tasks (possible missed wakeup or lost task)";
    }

    EXPECT_EQ(executed.load(std::memory_order_relaxed), total_tasks);
    EXPECT_EQ(remaining.load(std::memory_order_relaxed), 0);
}
