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
class OrderBook {
private:
    // key = price, value = queue of orders at that price (oldest first)
    std::map<int64_t, std::deque<Order>, std::greater<int64_t>> bids_;  // highest price first
    std::map<int64_t, std::deque<Order>> asks_;                         // lowest price first
    uint64_t nextId_ = 1;

    static void printLevel(int64_t price, const std::deque<Order>& orders) {
        int64_t total = 0;
        for (const Order& o : orders) {
            total += o.qty;
        }
        std::cout << std::setw(10) << std::fixed << std::setprecision(2)
                  << price / 100.0 << std::setw(8) << total << "\n";
    }

public:
    uint64_t addLimitOrder(Side side, int64_t price, int64_t qty) {
        if (price <= 0) throw std::invalid_argument("price must be positive");
        if (qty <= 0)   throw std::invalid_argument("quantity must be positive");

        Order o{nextId_, side, OrderType::Limit, price, qty};
        nextId_++;

        if (side == Side::Buy) {
            bids_[price].push_back(o);
        } else {
            asks_[price].push_back(o);
        }
        return o.id;
    }

    void printBook() const {
        std::cout << "\n=== ASKS (sellers) ===\n";
        for (auto it = asks_.rbegin(); it != asks_.rend(); ++it) {
            printLevel(it->first, it->second);
        }
        std::cout << "--------------------------\n";
        std::cout << "=== BIDS (buyers) ===\n";
        for (auto it = bids_.begin(); it != bids_.end(); ++it) {
            printLevel(it->first, it->second);
        }
    }
};