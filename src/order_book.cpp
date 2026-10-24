#include "lob/order_book.hpp"

#include <algorithm>
#include <type_traits>

namespace lob {

OrderBook::OrderBook(TradeSink sink) : sink_(std::move(sink)) {}

// ---------------------------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------------------------

AddResult OrderBook::add(OrderId id, Side side, OrderType type, Price price, Quantity qty) {
    const bool is_market = type == OrderType::Market;
    if (qty == 0 || index_.contains(id) || (!is_market && price <= 0)) {
        return {AddStatus::Rejected, 0, qty};
    }

    // Fill-or-kill: check available liquidity first, without touching the book.
    if (type == OrderType::FOK) {
        const Quantity avail = side == Side::Buy ? available(asks_, price, qty, false)
                                                 : available(bids_, price, qty, false);
        if (avail < qty) return {AddStatus::Cancelled, 0, qty};
    }

    const Quantity filled = side == Side::Buy ? match(asks_, id, side, price, qty, is_market)
                                              : match(bids_, id, side, price, qty, is_market);
    const Quantity remaining = qty - filled;

    if (remaining == 0) return {AddStatus::Filled, filled, 0};

    if (type == OrderType::Limit) {
        rest(id, side, price, remaining);
        return {AddStatus::Rested, filled, remaining};
    }

    // Market / IOC: drop the remainder.
    return {filled > 0 ? AddStatus::PartiallyFilled : AddStatus::Cancelled, filled, remaining};
}

bool OrderBook::cancel(OrderId id) {
    const auto it = index_.find(id);
    if (it == index_.end()) return false;
    Order* o = it->second;
    index_.erase(it);
    unlink(o);
    release(o);
    return true;
}

std::optional<AddResult> OrderBook::modify(OrderId id, Price new_price, Quantity new_qty) {
    const auto it = index_.find(id);
    if (it == index_.end()) return std::nullopt;
    Order* o = it->second;

    if (new_qty == 0) {
        cancel(id);
        return AddResult{AddStatus::Cancelled, 0, 0};
    }

    // Quantity reduction at the same price keeps time priority.
    if (new_price == o->price && new_qty <= o->remaining) {
        const Quantity delta = o->remaining - new_qty;
        o->remaining = new_qty;
        o->level->total -= delta;
        return AddResult{AddStatus::Rested, 0, new_qty};
    }

    // Otherwise: lose priority -> cancel and re-submit as a new limit order (may cross).
    const Side side = o->side;
    cancel(id);
    return add(id, side, OrderType::Limit, new_price, new_qty);
}

std::optional<Price> OrderBook::best_bid() const {
    if (bids_.empty()) return std::nullopt;
    return bids_.begin()->first;
}

std::optional<Price> OrderBook::best_ask() const {
    if (asks_.empty()) return std::nullopt;
    return asks_.begin()->first;
}

std::optional<Price> OrderBook::spread() const {
    const auto b = best_bid();
    const auto a = best_ask();
    if (!b || !a) return std::nullopt;
    return *a - *b;
}

std::vector<LevelView> OrderBook::depth(Side side, std::size_t levels) const {
    std::vector<LevelView> out;
    out.reserve(levels);
    auto collect = [&](const auto& book) {
        for (const auto& [price, level] : book) {
            if (out.size() == levels) break;
            out.push_back({price, level.total, level.count});
        }
    };
    if (side == Side::Buy) collect(bids_); else collect(asks_);
    return out;
}

Quantity OrderBook::volume_at(Side side, Price price) const {
    if (side == Side::Buy) {
        const auto it = bids_.find(price);
        return it == bids_.end() ? 0 : it->second.total;
    }
    const auto it = asks_.find(price);
    return it == asks_.end() ? 0 : it->second.total;
}

// ---------------------------------------------------------------------------------------------
// Matching
// ---------------------------------------------------------------------------------------------

template <class Levels>
Quantity OrderBook::match(Levels& opposite, OrderId aggressor, Side side, Price limit, Quantity qty,
                          bool is_market) {
    Quantity filled = 0;
    auto crosses = [&](Price level_price) {
        if (is_market) return true;
        return side == Side::Buy ? level_price <= limit : level_price >= limit;
    };

    while (filled < qty && !opposite.empty()) {
        auto level_it = opposite.begin();
        Level& level = level_it->second;
        if (!crosses(level.price)) break;

        // Walk the FIFO queue at this price level (time priority).
        while (filled < qty && level.head != nullptr) {
            Order* resting = level.head;
            const Quantity traded = std::min(qty - filled, resting->remaining);

            resting->remaining -= traded;
            level.total -= traded;
            filled += traded;

            if (sink_) sink_(Trade{aggressor, resting->id, side, level.price, traded, ++seq_});

            if (resting->remaining == 0) {
                index_.erase(resting->id);
                // Unlink head without erasing the level while we iterate on it.
                level.head = resting->next;
                if (level.head) level.head->prev = nullptr; else level.tail = nullptr;
                --level.count;
                release(resting);
            }
        }
        if (level.head == nullptr) opposite.erase(level_it);
    }
    return filled;
}

template <class Levels>
Quantity OrderBook::available(const Levels& opposite, Price limit, Quantity wanted, bool is_market) const {
    Quantity total = 0;
    for (const auto& [price, level] : opposite) {
        const bool crosses = is_market || (std::is_same_v<Levels, AskLevels> ? price <= limit : price >= limit);
        if (!crosses) break;
        total += level.total;
        if (total >= wanted) break;
    }
    return total;
}

// ---------------------------------------------------------------------------------------------
// Book maintenance
// ---------------------------------------------------------------------------------------------

void OrderBook::rest(OrderId id, Side side, Price price, Quantity qty) {
    Level* level = nullptr;
    if (side == Side::Buy) {
        auto [it, _] = bids_.try_emplace(price);
        level = &it->second;
    } else {
        auto [it, _] = asks_.try_emplace(price);
        level = &it->second;
    }
    level->price = price;

    Order* o = allocate();
    *o = Order{id, side, price, qty, ++seq_, level->tail, nullptr, level};
    if (level->tail) level->tail->next = o; else level->head = o;
    level->tail = o;
    level->total += qty;
    ++level->count;
    index_.emplace(id, o);
}

void OrderBook::unlink(Order* o) {
    Level* level = o->level;
    if (o->prev) o->prev->next = o->next; else level->head = o->next;
    if (o->next) o->next->prev = o->prev; else level->tail = o->prev;
    level->total -= o->remaining;
    --level->count;
    erase_level_if_empty(level, o->side);
}

void OrderBook::erase_level_if_empty(Level* level, Side side) {
    if (level->head != nullptr) return;
    if (side == Side::Buy) bids_.erase(level->price); else asks_.erase(level->price);
}

OrderBook::Order* OrderBook::allocate() {
    if (!free_list_.empty()) {
        Order* o = free_list_.back();
        free_list_.pop_back();
        return o;
    }
    return &pool_.emplace_back();
}

void OrderBook::release(Order* o) { free_list_.push_back(o); }

}  // namespace lob
