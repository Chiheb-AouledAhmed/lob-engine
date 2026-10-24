#include "lob/order_book.hpp"

#include <gtest/gtest.h>

#include <random>
#include <vector>

using namespace lob;

namespace {

struct BookFixture : ::testing::Test {
    std::vector<Trade> trades;
    OrderBook book{[this](const Trade& t) { trades.push_back(t); }};
};

}  // namespace

TEST_F(BookFixture, EmptyBookHasNoPrices) {
    EXPECT_FALSE(book.best_bid());
    EXPECT_FALSE(book.best_ask());
    EXPECT_FALSE(book.spread());
    EXPECT_EQ(book.order_count(), 0u);
}

TEST_F(BookFixture, NonCrossingLimitOrdersRest) {
    EXPECT_EQ(book.add(1, Side::Buy, OrderType::Limit, 100, 10).status, AddStatus::Rested);
    EXPECT_EQ(book.add(2, Side::Sell, OrderType::Limit, 102, 5).status, AddStatus::Rested);
    EXPECT_EQ(*book.best_bid(), 100);
    EXPECT_EQ(*book.best_ask(), 102);
    EXPECT_EQ(*book.spread(), 2);
    EXPECT_TRUE(trades.empty());
}

TEST_F(BookFixture, CrossingOrderTradesAtRestingPrice) {
    book.add(1, Side::Sell, OrderType::Limit, 101, 10);
    const auto r = book.add(2, Side::Buy, OrderType::Limit, 105, 4);
    EXPECT_EQ(r.status, AddStatus::Filled);
    ASSERT_EQ(trades.size(), 1u);
    EXPECT_EQ(trades[0].price, 101);  // price improvement for the aggressor
    EXPECT_EQ(trades[0].quantity, 4u);
    EXPECT_EQ(book.volume_at(Side::Sell, 101), 6u);
}

TEST_F(BookFixture, TimePriorityWithinALevel) {
    book.add(1, Side::Sell, OrderType::Limit, 100, 5);
    book.add(2, Side::Sell, OrderType::Limit, 100, 5);
    book.add(3, Side::Buy, OrderType::Limit, 100, 7);
    ASSERT_EQ(trades.size(), 2u);
    EXPECT_EQ(trades[0].resting_id, 1u);
    EXPECT_EQ(trades[0].quantity, 5u);
    EXPECT_EQ(trades[1].resting_id, 2u);
    EXPECT_EQ(trades[1].quantity, 2u);
    EXPECT_FALSE(book.contains(1));
    EXPECT_TRUE(book.contains(2));
}

TEST_F(BookFixture, PricePriorityAcrossLevels) {
    book.add(1, Side::Sell, OrderType::Limit, 103, 5);
    book.add(2, Side::Sell, OrderType::Limit, 101, 5);
    book.add(3, Side::Sell, OrderType::Limit, 102, 5);
    book.add(4, Side::Buy, OrderType::Limit, 103, 12);
    ASSERT_EQ(trades.size(), 3u);
    EXPECT_EQ(trades[0].price, 101);
    EXPECT_EQ(trades[1].price, 102);
    EXPECT_EQ(trades[2].price, 103);
    EXPECT_EQ(trades[2].quantity, 2u);
}

TEST_F(BookFixture, PartialFillRestsRemainder) {
    book.add(1, Side::Sell, OrderType::Limit, 100, 3);
    const auto r = book.add(2, Side::Buy, OrderType::Limit, 100, 10);
    EXPECT_EQ(r.status, AddStatus::Rested);
    EXPECT_EQ(r.filled, 3u);
    EXPECT_EQ(r.remaining, 7u);
    EXPECT_EQ(*book.best_bid(), 100);
    EXPECT_EQ(book.volume_at(Side::Buy, 100), 7u);
    EXPECT_FALSE(book.best_ask());
}

TEST_F(BookFixture, MarketOrderSweepsAndDropsRemainder) {
    book.add(1, Side::Buy, OrderType::Limit, 99, 5);
    book.add(2, Side::Buy, OrderType::Limit, 98, 5);
    const auto r = book.add(3, Side::Sell, OrderType::Market, 0, 20);
    EXPECT_EQ(r.status, AddStatus::PartiallyFilled);
    EXPECT_EQ(r.filled, 10u);
    EXPECT_EQ(book.order_count(), 0u);
    EXPECT_FALSE(book.contains(3));
}

TEST_F(BookFixture, IocNeverRests) {
    book.add(1, Side::Sell, OrderType::Limit, 100, 5);
    const auto r = book.add(2, Side::Buy, OrderType::IOC, 99, 5);
    EXPECT_EQ(r.status, AddStatus::Cancelled);
    EXPECT_FALSE(book.contains(2));
    EXPECT_FALSE(book.best_bid());
}

TEST_F(BookFixture, FokIsAllOrNothing) {
    book.add(1, Side::Sell, OrderType::Limit, 100, 5);
    book.add(2, Side::Sell, OrderType::Limit, 101, 5);

    EXPECT_EQ(book.add(3, Side::Buy, OrderType::FOK, 101, 11).status, AddStatus::Cancelled);
    EXPECT_TRUE(trades.empty());
    EXPECT_EQ(book.order_count(), 2u);

    EXPECT_EQ(book.add(4, Side::Buy, OrderType::FOK, 101, 10).status, AddStatus::Filled);
    EXPECT_EQ(trades.size(), 2u);
    EXPECT_EQ(book.order_count(), 0u);
}

TEST_F(BookFixture, CancelRemovesOrderAndEmptyLevel) {
    book.add(1, Side::Buy, OrderType::Limit, 100, 5);
    book.add(2, Side::Buy, OrderType::Limit, 99, 5);
    EXPECT_TRUE(book.cancel(1));
    EXPECT_FALSE(book.cancel(1));
    EXPECT_EQ(*book.best_bid(), 99);
    EXPECT_EQ(book.depth(Side::Buy, 10).size(), 1u);
}

TEST_F(BookFixture, ModifyDownKeepsPriority) {
    book.add(1, Side::Sell, OrderType::Limit, 100, 10);
    book.add(2, Side::Sell, OrderType::Limit, 100, 10);
    book.modify(1, 100, 4);
    book.add(3, Side::Buy, OrderType::Limit, 100, 4);
    ASSERT_EQ(trades.size(), 1u);
    EXPECT_EQ(trades[0].resting_id, 1u);
}

TEST_F(BookFixture, ModifyUpLosesPriority) {
    book.add(1, Side::Sell, OrderType::Limit, 100, 10);
    book.add(2, Side::Sell, OrderType::Limit, 100, 10);
    book.modify(1, 100, 15);
    book.add(3, Side::Buy, OrderType::Limit, 100, 4);
    ASSERT_EQ(trades.size(), 1u);
    EXPECT_EQ(trades[0].resting_id, 2u);
}

TEST_F(BookFixture, ModifyPriceCanCross) {
    book.add(1, Side::Sell, OrderType::Limit, 105, 10);
    book.add(2, Side::Buy, OrderType::Limit, 100, 10);
    const auto r = book.modify(2, 105, 10);
    ASSERT_TRUE(r);
    EXPECT_EQ(r->status, AddStatus::Filled);
    EXPECT_EQ(book.order_count(), 0u);
}

TEST_F(BookFixture, RejectsInvalidOrders) {
    EXPECT_EQ(book.add(1, Side::Buy, OrderType::Limit, 100, 0).status, AddStatus::Rejected);
    EXPECT_EQ(book.add(1, Side::Buy, OrderType::Limit, -1, 5).status, AddStatus::Rejected);
    book.add(1, Side::Buy, OrderType::Limit, 100, 5);
    EXPECT_EQ(book.add(1, Side::Buy, OrderType::Limit, 100, 5).status, AddStatus::Rejected);
}

TEST_F(BookFixture, DepthAggregatesLevels) {
    book.add(1, Side::Buy, OrderType::Limit, 100, 5);
    book.add(2, Side::Buy, OrderType::Limit, 100, 3);
    book.add(3, Side::Buy, OrderType::Limit, 99, 7);
    const auto d = book.depth(Side::Buy, 5);
    ASSERT_EQ(d.size(), 2u);
    EXPECT_EQ(d[0].price, 100);
    EXPECT_EQ(d[0].quantity, 8u);
    EXPECT_EQ(d[0].order_count, 2u);
    EXPECT_EQ(d[1].price, 99);
}

// Property test: after any random sequence of operations, the book is never crossed and the
// total quantity resting equals the quantity added minus traded minus cancelled.
TEST(OrderBookProperty, RandomFlowKeepsInvariants) {
    std::mt19937_64 rng(42);
    Quantity traded = 0;
    OrderBook book([&](const Trade& t) { traded += t.quantity; });

    Quantity added_resting = 0;  // quantity that entered the book as limit orders
    Quantity cancelled = 0;
    std::vector<OrderId> live;
    std::unordered_map<OrderId, Quantity> qty_of;

    for (OrderId id = 1; id <= 20'000; ++id) {
        const int op = static_cast<int>(rng() % 10);
        if (op < 7 || live.empty()) {
            const Side side = rng() % 2 ? Side::Buy : Side::Sell;
            const Price price = 950 + static_cast<Price>(rng() % 100);
            const Quantity qty = 1 + rng() % 50;
            const auto r = book.add(id, side, OrderType::Limit, price, qty);
            added_resting += qty;
            if (r.status == AddStatus::Rested) live.push_back(id);
        } else {
            const std::size_t k = rng() % live.size();
            const OrderId victim = live[k];
            live[k] = live.back();
            live.pop_back();
            if (book.contains(victim)) {
                Quantity before = 0;
                for (auto s : {Side::Buy, Side::Sell})
                    for (const auto& lvl : book.depth(s, 1000)) before += lvl.quantity;
                book.cancel(victim);
                Quantity after = 0;
                for (auto s : {Side::Buy, Side::Sell})
                    for (const auto& lvl : book.depth(s, 1000)) after += lvl.quantity;
                cancelled += before - after;
            }
        }
        if (book.best_bid() && book.best_ask()) {
            ASSERT_LT(*book.best_bid(), *book.best_ask()) << "book crossed at op " << id;
        }
    }

    Quantity resting = 0;
    for (auto s : {Side::Buy, Side::Sell})
        for (const auto& lvl : book.depth(s, 1000)) resting += lvl.quantity;
    // Each trade consumes quantity from both the aggressor and the resting order.
    EXPECT_EQ(resting, added_resting - 2 * traded - cancelled);
}
