#pragma once
#include <chrono>
#include <cstdint>

// Prices are stored as integer "ticks" (1 tick = 0.01) to avoid floating-point
// comparison bugs. Example: 101.25 is stored as 10125.
using OrderId  = std::uint64_t;
using Price    = std::int64_t;
using Quantity = std::uint32_t;
using Clock    = std::chrono::steady_clock;

enum class Side      { Buy, Sell };
enum class OrderType { Limit, Market };

struct Order {
    OrderId           id;
    Side              side;
    OrderType         type;
    Price             price;     // ignored for market orders
    Quantity          quantity;  // remaining quantity
    Clock::time_point timestamp;
};

// Produced by the matching engine (Day 2).
struct Trade {
    OrderId           buy_order_id;
    OrderId           sell_order_id;
    Price             price;
    Quantity          quantity;
    Clock::time_point timestamp;
};