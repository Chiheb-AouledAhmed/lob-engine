#pragma once

#include <cstdint>
#include <string_view>

namespace lob {

using OrderId  = std::uint64_t;
using Price    = std::int64_t;   // price in ticks (integer, no floating point on the hot path)
using Quantity = std::uint64_t;
using Seq      = std::uint64_t;  // monotonically increasing sequence number (time priority)

enum class Side : std::uint8_t { Buy, Sell };

enum class OrderType : std::uint8_t {
    Limit,      // rests on the book if not fully filled
    Market,     // never rests; unfilled remainder is dropped
    IOC,        // immediate-or-cancel limit order
    FOK         // fill-or-kill limit order
};

constexpr Side opposite(Side s) noexcept { return s == Side::Buy ? Side::Sell : Side::Buy; }

constexpr std::string_view to_string(Side s) noexcept { return s == Side::Buy ? "BUY" : "SELL"; }

struct Trade {
    OrderId  aggressor_id;
    OrderId  resting_id;
    Side     aggressor_side;
    Price    price;
    Quantity quantity;
    Seq      seq;
};

}  // namespace lob
