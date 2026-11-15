#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <numeric>
#include <thread>
#include <vector>

#include "core/util/bounded_queue.hpp"

using mp::core::BoundedQueue;
using namespace std::chrono_literals;

TEST_CASE("BoundedQueue delivers everything exactly once to several consumers", "[queue]") {
    BoundedQueue<int> queue(8);
    constexpr int kProducers = 4;
    constexpr int kPerProducer = 5'000;
    std::atomic<long long> sum{0};
    std::atomic<int> count{0};

    std::vector<std::thread> consumers;
    for (int c = 0; c < 3; ++c) {
        consumers.emplace_back([&] {
            int v = 0;
            while (queue.pop(v)) {
                sum += v;
                ++count;
            }
        });
    }
    std::vector<std::thread> producers;
    for (int p = 0; p < kProducers; ++p) {
        producers.emplace_back([&] {
            for (int i = 1; i <= kPerProducer; ++i) {
                REQUIRE(queue.push(i));
            }
        });
    }
    for (auto& t : producers) {
        t.join();
    }
    queue.close();
    for (auto& t : consumers) {
        t.join();
    }
    CHECK(count == kProducers * kPerProducer);
    CHECK(sum == static_cast<long long>(kProducers) * kPerProducer * (kPerProducer + 1) / 2);
}

TEST_CASE("BoundedQueue: close lets consumers drain, then ends; pushes are refused", "[queue]") {
    BoundedQueue<int> queue(4);
    REQUIRE(queue.push(1));
    REQUIRE(queue.push(2));
    queue.close();
    CHECK_FALSE(queue.push(3));
    int v = 0;
    REQUIRE(queue.pop(v));
    CHECK(v == 1);
    REQUIRE(queue.pop(v));
    CHECK(v == 2);
    CHECK_FALSE(queue.pop(v));
}

TEST_CASE("BoundedQueue::popFor times out, then reports closed", "[queue]") {
    BoundedQueue<int> queue(2);
    int v = 0;
    CHECK(queue.popFor(v, 10ms) == BoundedQueue<int>::PopResult::Timeout);
    REQUIRE(queue.push(7));
    CHECK(queue.popFor(v, 10ms) == BoundedQueue<int>::PopResult::Item);
    CHECK(v == 7);
    queue.close();
    CHECK(queue.popFor(v, 10ms) == BoundedQueue<int>::PopResult::Closed);
}

TEST_CASE("BoundedQueue blocks a producer while full and wakes it when closed", "[queue]") {
    BoundedQueue<int> queue(1);
    REQUIRE(queue.push(1));
    std::atomic<bool> returned{false};
    bool accepted = true;
    std::thread producer([&] {
        accepted = queue.push(2);  // full: waits
        returned = true;
    });
    std::this_thread::sleep_for(50ms);
    CHECK_FALSE(returned);
    queue.close();
    producer.join();
    CHECK(returned);
    CHECK_FALSE(accepted);
}
