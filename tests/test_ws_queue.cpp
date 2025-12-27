#include "../src/work_stealing_queue.h"
#include <atomic>
#include <cassert>
#include <iostream>
#include <thread>
#include <vector>

using namespace Plexus;

void test_basic() {
    WorkStealingQueue<int> q(16);
    assert(q.push(1));
    assert(q.push(2));
    assert(q.size() == 2);

    auto v = q.pop();
    assert(v.has_value());
    assert(*v == 2); // LIFO

    v = q.pop();
    assert(v.has_value());
    assert(*v == 1);

    assert(q.empty());
}

void test_steal() {
    WorkStealingQueue<int> q(16);
    q.push(10);
    q.push(20);

    auto v = q.steal(); // FIFO
    assert(v.has_value());
    assert(*v == 10);

    auto v2 = q.pop(); // LIFO
    assert(v2.has_value());
    assert(*v2 == 20);

    assert(q.empty());
}

void test_contention() {
    WorkStealingQueue<int> q(1024);
    std::atomic<bool> done{false};
    std::atomic<int> stolen_count{0};

    // Thief thread
    std::thread thief([&]() {
        while (!done) {
            if (q.steal()) {
                stolen_count++;
            }
        }
        // Drain rest
        while (q.steal()) {
            stolen_count++;
        }
    });

    int pushed_count = 100000;
    int local_popped = 0;

    for (int i = 0; i < pushed_count; ++i) {
        while (!q.push(i)) {
            // Full, pop some
            if (q.pop())
                local_popped++;
        }

        // Randomly pop locally too
        if (i % 3 == 0) {
            if (q.pop())
                local_popped++;
        }
    }

    done = true;
    thief.join();

    // Drain local
    while (q.pop())
        local_popped++;

    std::cout << "Pushed: " << pushed_count << "\n";
    std::cout << "Local Popped: " << local_popped << "\n";
    std::cout << "Stolen: " << stolen_count << "\n";
    std::cout << "Total: " << (local_popped + stolen_count) << "\n";

    assert(local_popped + stolen_count == pushed_count);
}

int main() {
    test_basic();
    test_steal();
    test_contention();
    std::cout << "ALL TESTS PASSED\n";
    return 0;
}
