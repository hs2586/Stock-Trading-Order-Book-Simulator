#pragma once
#include <algorithm>
#include <cstdint>
#include <deque>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <stdexcept>
#include <unordered_map>
#include <utility>
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
    // remembers where each resting order lives: id -> (side, price)
    std::unordered_map<uint64_t, std::pair<Side, int64_t>> index_;
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
                    index_.erase(resting.id);        // no longer in the book
                    queue.pop_front();
                }
            }
            if (queue.empty()) {
                asks_.erase(levelIt);
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
                    index_.erase(resting.id);
                    queue.pop_front();
                }
            }
            if (queue.empty()) {
                bids_.erase(levelIt);
            }
        }
    }

    // Works for both bids_ and asks_ (that is why it is a template)
    template <typename Book>
    static void removeFromLevel(Book& book, int64_t price, uint64_t id) {
        auto levelIt = book.find(price);
        if (levelIt == book.end()) return;
        std::deque<Order>& queue = levelIt->second;
        for (auto it = queue.begin(); it != queue.end(); ++it) {
            if (it->id == id) {
                queue.erase(it);
                break;
            }
        }
        if (queue.empty()) {
            book.erase(levelIt);
        }
    }

    // Shared by limit and market orders
    uint64_t submit(Side side, OrderType type, int64_t price, int64_t qty) {
        if (qty <= 0) throw std::invalid_argument("quantity must be positive");
        if (type == OrderType::Limit && price <= 0)
            throw std::invalid_argument("price must be positive");

        Order o{nextId_, side, type, price, qty};
        nextId_++;

        if (side == Side::Buy) {
            matchBuy(o);
        } else {
            matchSell(o);
        }

        // Only limit orders may rest in the book. A market order's leftover is dropped.
        if (o.qty > 0 && type == OrderType::Limit) {
            if (side == Side::Buy) {
                bids_[price].push_back(o);
            } else {
                asks_[price].push_back(o);
            }
            index_[o.id] = {side, price};
        }
        return o.id;
    }

public:
    uint64_t addLimitOrder(Side side, int64_t price, int64_t qty) {
        return submit(side, OrderType::Limit, price, qty);
    }

    // A market order accepts any price: a buy uses the highest possible price,
    // a sell uses 0, so the matching loops never stop because of price.
    uint64_t addMarketOrder(Side side, int64_t qty) {
        int64_t anyPrice = (side == Side::Buy) ? std::numeric_limits<int64_t>::max() : 0;
        return submit(side, OrderType::Market, anyPrice, qty);
    }

    // Returns true if the order was found and removed
    bool cancel(uint64_t id) {
        auto found = index_.find(id);
        if (found == index_.end()) return false;
        Side side = found->second.first;
        int64_t price = found->second.second;
        if (side == Side::Buy) {
            removeFromLevel(bids_, price, id);
        } else {
            removeFromLevel(asks_, price, id);
        }
        index_.erase(found);
        return true;
    }

    bool hasOrder(uint64_t id) const { return index_.count(id) != 0; }

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
    int64_t bestBid() const { return bids_.empty() ? 0 : bids_.begin()->first; }
    int64_t bestAsk() const { return asks_.empty() ? 0 : asks_.begin()->first; }

    void printTrades() const {
        std::cout << "\nTrades (" << trades_.size() << "):\n";
        for (const Trade& t : trades_) {
            std::cout << "  buy#" << t.buyId << " sell#" << t.sellId << "  "
                      << t.qty << " @ " << std::fixed << std::setprecision(2)
                      << t.price / 100.0 << "\n";
        }
    }
};