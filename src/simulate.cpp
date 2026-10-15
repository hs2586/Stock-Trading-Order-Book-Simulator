// simulate.cpp - random market simulation with invariant checks and timing
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <random>
#include <vector>
#include "orderbook.hpp"

int main(int argc, char** argv) {
    int n = 100000;
    if (argc > 1) {
        n = std::atoi(argv[1]);
        if (n <= 0 || n > 10000000) {
            std::cerr << "n must be between 1 and 10,000,000\n";
            return 1;
        }
    }

    std::mt19937 rng(42);                                   // fixed seed = same run every time
    std::uniform_int_distribution<int> priceOffset(-50, 50);
    std::uniform_int_distribution<int> qtyDist(1, 100);
    std::uniform_int_distribution<int> action(0, 99);

    OrderBook book;
    std::vector<uint64_t> ids;                              // ids of limit orders we placed
    const int64_t mid = 10000;                              // 100.00
    long long limits = 0, markets = 0, cancels = 0, cancelsOk = 0;

    auto start = std::chrono::steady_clock::now();

    for (int i = 0; i < n; ++i) {
        int roll = action(rng);
        Side side = (rng() % 2 == 0) ? Side::Buy : Side::Sell;

        if (roll < 10 && !ids.empty()) {                    // 10%: cancel a random order
            size_t pick = rng() % ids.size();
            ++cancels;
            if (book.cancel(ids[pick])) ++cancelsOk;        // false if it was already filled
            ids[pick] = ids.back();
            ids.pop_back();
        } else if (roll < 30) {                             // 20%: market order
            book.addMarketOrder(side, qtyDist(rng));
            ++markets;
        } else {                                            // 70%: limit order near 100.00
            ids.push_back(book.addLimitOrder(side, mid + priceOffset(rng), qtyDist(rng)));
            ++limits;
        }

        // INVARIANT 1: the book must never be crossed after matching
        if (book.bestBid() != 0 && book.bestAsk() != 0 && book.bestBid() >= book.bestAsk()) {
            std::cerr << "INVARIANT BROKEN at order " << i << ": best bid "
                      << book.bestBid() << " >= best ask " << book.bestAsk() << "\n";
            return 1;
        }
    }

    auto end = std::chrono::steady_clock::now();
    double seconds = std::chrono::duration<double>(end - start).count();

    // INVARIANT 2: every trade has positive price and quantity
    long long volume = 0;
    for (const Trade& t : book.trades()) {
        if (t.price <= 0 || t.qty <= 0) {
            std::cerr << "INVARIANT BROKEN: bad trade\n";
            return 1;
        }
        volume += t.qty;
    }

    std::cout << "Orders processed : " << n << "\n"
              << "  limit          : " << limits << "\n"
              << "  market         : " << markets << "\n"
              << "  cancel tries   : " << cancels << " (" << cancelsOk << " succeeded)\n"
              << "Trades           : " << book.trades().size() << "\n"
              << "Traded volume    : " << volume << "\n"
              << "Time             : " << seconds << " s\n"
              << "Throughput       : " << static_cast<long long>(n / seconds) << " orders/s\n"
              << "All invariants held.\n";
    book.printBook();
    return 0;
}