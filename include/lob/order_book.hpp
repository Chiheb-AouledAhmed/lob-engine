#pragma once

#include "lob/types.hpp"

#include <cstddef>
#include <deque>
#include <functional>
#include <map>
#include <optional>
#include <unordered_map>
#include <vector>

namespace lob {

enum class AddStatus : std::uint8_t {
    Rested,          // (partially) filled, remainder rests on the book
    Filled,          // fully filled
    PartiallyFilled, // IOC / Market: partially filled, remainder dropped
    Cancelled,       // IOC / Market with no liquidity, or FOK that could not be fully filled
    Rejected         // invalid input (duplicate id, zero quantity, bad price)
};

struct AddResult {
    AddStatus status;
    Quantity  filled;
    Quantity  remaining;
};

struct LevelView {
    Price       price;
    Quantity    quantity;
    std::size_t order_count;
};

/// Single-instrument limit order book with price-time priority matching.
///
/// Design notes
///  * Price levels live in two ordered maps (bids descending, asks ascending), so the best
///    price is always `begin()` - O(1) access, O(log L) insertion of a new level.
///  * Orders at a level form an intrusive doubly-linked FIFO, so cancel is O(1) once the order
///    is found through the id index (hash map).
///  * Orders are allocated from a pool (free list over a std::deque) to avoid a heap allocation
///    per order on the hot path; std::deque keeps pointers stable while it grows.
///  * Not thread-safe by design: a matching engine is typically single-threaded per instrument,
///    fed by a queue (see README).
class OrderBook {
public:
    using TradeSink = std::function<void(const Trade&)>;

    explicit OrderBook(TradeSink sink = {});

    OrderBook(const OrderBook&)            = delete;
    OrderBook& operator=(const OrderBook&) = delete;

    AddResult add(OrderId id, Side side, OrderType type, Price price, Quantity qty);

    /// Removes a resting order. Returns false if the id is unknown.
    bool cancel(OrderId id);

    /// Changes quantity and/or price of a resting order.
    ///  * Same price and lower quantity: keeps time priority (standard exchange behaviour).
    ///  * Price change or quantity increase: loses priority (cancel + re-insert), and may match.
    std::optional<AddResult> modify(OrderId id, Price new_price, Quantity new_qty);

    [[nodiscard]] std::optional<Price> best_bid() const;
    [[nodiscard]] std::optional<Price> best_ask() const;
    [[nodiscard]] std::optional<Price> spread() const;
    [[nodiscard]] std::vector<LevelView> depth(Side side, std::size_t levels) const;
    [[nodiscard]] std::size_t order_count() const noexcept { return index_.size(); }
    [[nodiscard]] bool contains(OrderId id) const { return index_.contains(id); }
    [[nodiscard]] Quantity volume_at(Side side, Price price) const;

private:
    struct Level;

    struct Order {
        OrderId  id{};
        Side     side{};
        Price    price{};
        Quantity remaining{};
        Seq      seq{};
        Order*   prev{nullptr};
        Order*   next{nullptr};
        Level*   level{nullptr};
    };

    struct Level {
        Price       price{};
        Quantity    total{};
        std::size_t count{};
        Order*      head{nullptr};
        Order*      tail{nullptr};
    };

    using BidLevels = std::map<Price, Level, std::greater<>>;
    using AskLevels = std::map<Price, Level, std::less<>>;

    template <class Levels>
    Quantity match(Levels& opposite, OrderId aggressor, Side side, Price limit, Quantity qty, bool is_market);

    template <class Levels>
    Quantity available(const Levels& opposite, Price limit, Quantity wanted, bool is_market) const;

    void rest(OrderId id, Side side, Price price, Quantity qty);
    void unlink(Order* o);
    void erase_level_if_empty(Level* level, Side side);

    Order* allocate();
    void   release(Order* o);

    BidLevels bids_;
    AskLevels asks_;
    std::unordered_map<OrderId, Order*> index_;
    std::vector<Order*> free_list_;
    std::deque<Order> pool_;
    TradeSink sink_;
    Seq seq_{0};
};

}  // namespace lob
