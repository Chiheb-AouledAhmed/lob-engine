# lob-engine — C++20 limit order book & matching engine

[![CI](https://github.com/Chiheb-AouledAhmed/lob-engine/actions/workflows/ci.yml/badge.svg)](https://github.com/Chiheb-AouledAhmed/lob-engine/actions/workflows/ci.yml)

A single-instrument **limit order book with price-time priority matching**, written in modern C++20.
It supports the order types used on electronic markets and is designed around the data-structure
choices that matter on a trading hot path.

## Features

- **Order types**: `Limit`, `Market`, `IOC` (immediate-or-cancel), `FOK` (fill-or-kill)
- **Price-time priority**: best price first, FIFO within a price level
- **Cancel** in O(1) once the order is located (intrusive doubly-linked list per level)
- **Modify** with exchange semantics: reducing quantity keeps time priority, a price change or
  quantity increase loses it (and may cross the spread)
- **Market data**: best bid/ask, spread, aggregated depth per level, volume at a price
- Trades are pushed to a user-supplied sink (callback), so the book has no I/O dependency

## Design

| Concern | Choice | Why |
|---|---|---|
| Prices | integer ticks (`int64_t`) | no floating-point rounding on the hot path |
| Price levels | `std::map` (bids descending, asks ascending) | best price is `begin()`; new level is O(log L) |
| Orders in a level | intrusive doubly-linked FIFO | O(1) append, O(1) removal from anywhere |
| Order lookup | `std::unordered_map<OrderId, Order*>` | O(1) average cancel / modify |
| Allocation | object pool (free list over `std::deque`) | no `new` per order, stable pointers |
| Concurrency | single-threaded per instrument | the standard pattern: one matching thread fed by a queue, no locks on the book |

## Build & test

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure   # 16 tests, incl. a randomized invariant test
./build/lob_bench 2000000                    # synthetic order flow benchmark
```

Sanitizer build (AddressSanitizer + UndefinedBehaviorSanitizer):

```bash
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug -DLOB_SANITIZE=ON && cmake --build build-asan -j
ctest --test-dir build-asan
```

## Benchmark

Synthetic flow: 60% passive limit orders around the mid, 25% cancels, 10% aggressive IOC,
5% market orders. Latency includes the trade callback.

Indicative run (GCC 13, `-O3`, single core of a cloud VM — numbers depend on the machine):

```
operations      : 2000000
trades          : 405167
throughput      : 2.51 M ops/s
latency p50/p99/p99.9 : 176 / 1313 / 4756 ns
```

## Testing strategy

- Unit tests for each rule: price priority, time priority, partial fills, market sweep, IOC, FOK
  all-or-nothing, cancel, priority-preserving vs priority-losing modifies, input validation.
- A **randomized property test** (20k operations) checks after every step that the book is
  never crossed, and at the end that quantity is conserved
  (`resting = added − 2 × traded − cancelled`).
- CI runs the suite in Release and under ASan/UBSan.

## Possible extensions

- Replace `std::map` by a flat array of levels indexed by tick for bounded price ranges
- Lock-free SPSC ring buffer between a gateway thread and the matching thread
- Multi-instrument engine with one book per symbol, and a FIX / binary order entry front end

## License

MIT
