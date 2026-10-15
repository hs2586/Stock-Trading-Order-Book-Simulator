#pragma once
#include <cstdint>
#include <deque>
#include <functional>
#include <iomanip>
#include <iostream>
#include <map>
#include <stdexcept>

enum class Side { Buy, Sell };
enum class OrderType { Limit, Market };

struct Order {
    uint64_t  id;
    Side      side;
    OrderType type;
    int64_t   price;   // in cents: 10000 means 100.00
    int64_t   qty;
};

struct Trade {
    uint64_t buyId;
    uint64_t sellId;
    int64_t  price;
    int64_t  qty;
};