#pragma once
#include <algorithm>
#include <cstdint>
#include <deque>
#include <functional>
#include <iomanip>
#include <iostream>
#include <map>
#include <stdexcept>
#include <vector>

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
    std::vector<Trade> trades_;

    static void printLevel(int64_t price, const std::deque<Order>& orders) {
        int64_t total = 0;
        for (const Order& o : orders) {
            total += o.qty;
        }
        std::cout << std::setw(10) << std::fixed << std::setprecision(2)
                  << price / 100.0 << std::setw(8) << total << "\n";
    }

    void matchBuy(Order& o) {
        while (o.qty > 0 && !asks_.empty()) {
            auto levelIt = asks_.begin();            // lowest ask price
            int64_t askPrice = levelIt->first;
            if (askPrice > o.price) break;           // too expensive, stop

            std::deque<Order>& queue = levelIt->second;
            while (o.qty > 0 && !queue.empty()) {
                Order& resting = queue.front();      // oldest order at this price
                int64_t fill = std::min(o.qty, resting.qty);

                trades_.push_back({o.id, resting.id, askPrice, fill});

                o.qty -= fill;
                resting.qty -= fill;
                if (resting.qty == 0) {
                    queue.pop_front();               // fully filled, remove it
                }
            }
            if (queue.empty()) {
                asks_.erase(levelIt);                // remove the empty price level
            }
        }
    }

    void matchSell(Order& o) {
        while (o.qty > 0 && !bids_.empty()) {
            auto levelIt = bids_.begin();            // best (highest) bid
            int64_t bidPrice = levelIt->first;
            if (bidPrice < o.price) break;           // bid too low, stop

            std::deque<Order>& queue = levelIt->second;
            while (o.qty > 0 && !queue.empty()) {
                Order& resting = queue.front();
                int64_t fill = std::min(o.qty, resting.qty);

                trades_.push_back({resting.id, o.id, bidPrice, fill});

                o.qty -= fill;
                resting.qty -= fill;
                if (resting.qty == 0) {
                    queue.pop_front();
                }
            }
            if (queue.empty()) {
                bids_.erase(levelIt);
            }
        }
    }

public:
    uint64_t addLimitOrder(Side side, int64_t price, int64_t qty) {
        if (price <= 0) throw std::invalid_argument("price must be positive");
        if (qty <= 0)   throw std::invalid_argument("quantity must be positive");

        Order o{nextId_, side, OrderType::Limit, price, qty};
        nextId_++;

        if (side == Side::Buy) {
            matchBuy(o);
        } else {
            matchSell(o);
        }

        if (o.qty > 0) {                              // leftover rests in the book
            if (side == Side::Buy) {
                bids_[price].push_back(o);
            } else {
                asks_[price].push_back(o);
            }
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

    const std::vector<Trade>& trades() const { return trades_; }

    void printTrades() const {
        std::cout << "\nTrades (" << trades_.size() << "):\n";
        for (const Trade& t : trades_) {
            std::cout << "  buy#" << t.buyId << " sell#" << t.sellId << "  "
                      << t.qty << " @ " << std::fixed << std::setprecision(2)
                      << t.price / 100.0 << "\n";
        }
    }
};