// Throughput / latency benchmark on a synthetic order flow.
// Mix: 60% limit orders around the mid, 25% cancels, 10% aggressive IOC, 5% market orders.

#include "lob/order_book.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <random>
#include <vector>

using namespace lob;
using Clock = std::chrono::steady_clock;

int main(int argc, char** argv) {
    const std::size_t n_ops = argc > 1 ? std::stoull(argv[1]) : 2'000'000;

    std::uint64_t trades = 0;
    OrderBook book([&](const Trade&) { ++trades; });

    std::mt19937_64 rng(7);
    std::vector<OrderId> live;
    live.reserve(n_ops);
    std::vector<std::uint32_t> lat_ns;
    lat_ns.reserve(n_ops);

    OrderId next_id = 1;
    const auto start = Clock::now();
    for (std::size_t i = 0; i < n_ops; ++i) {
        const auto r = rng() % 100;
        const Side side = rng() % 2 ? Side::Buy : Side::Sell;
        const Price mid = 10'000;
        const auto t0 = Clock::now();
        if (r < 60 || live.empty()) {
            const Price offset = 1 + static_cast<Price>(rng() % 50);
            const Price px = side == Side::Buy ? mid - offset : mid + offset;
            const OrderId id = next_id++;
            if (book.add(id, side, OrderType::Limit, px, 1 + rng() % 100).status == AddStatus::Rested)
                live.push_back(id);
        } else if (r < 85) {
            const std::size_t k = rng() % live.size();
            book.cancel(live[k]);
            live[k] = live.back();
            live.pop_back();
        } else if (r < 95) {
            const Price px = side == Side::Buy ? mid + 5 : mid - 5;
            book.add(next_id++, side, OrderType::IOC, px, 1 + rng() % 200);
        } else {
            book.add(next_id++, side, OrderType::Market, 0, 1 + rng() % 200);
        }
        lat_ns.push_back(static_cast<std::uint32_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - t0).count()));
    }
    const double secs = std::chrono::duration<double>(Clock::now() - start).count();

    std::sort(lat_ns.begin(), lat_ns.end());
    auto pct = [&](double p) { return lat_ns[static_cast<std::size_t>(p * (lat_ns.size() - 1))]; };

    std::printf("operations      : %zu\n", n_ops);
    std::printf("trades          : %llu\n", static_cast<unsigned long long>(trades));
    std::printf("resting orders  : %zu\n", book.order_count());
    std::printf("throughput      : %.2f M ops/s\n", n_ops / secs / 1e6);
    std::printf("latency p50/p99/p99.9 : %u / %u / %u ns\n", pct(0.50), pct(0.99), pct(0.999));
    return 0;
}
