// dash_cli.cpp - batch interface used by the Streamlit dashboard.
// Reads commands from stdin (one per line) and prints ONE JSON object at the end.
//
//   B <price> <qty>      limit buy        (price in dollars, e.g. 100.50)
//   S <price> <qty>      limit sell
//   MB <qty>             market buy
//   MS <qty>             market sell
//   C <id>               cancel an order
//   SIM <orders> <seed> <mid_price> <spread_ticks> <market_pct> <cancel_pct>
//                        random market simulation
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <random>
#include <sstream>
#include <string>
#include <vector>
#include "orderbook.hpp"

static std::string esc(const std::string& s) {
    std::string out;
    for (unsigned char c : s) {
        if (c == '"') out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else if (c < 0x20) out += ' ';
        else out += static_cast<char>(c);
    }
    return out;
}

struct LogEntry {
    std::string cmd;
    std::string msg;
};

static void printLevels(const std::vector<std::pair<int64_t, int64_t>>& levels) {
    std::cout << "[";
    for (size_t i = 0; i < levels.size(); ++i) {
        if (i) std::cout << ",";
        std::cout << "[" << levels[i].first << "," << levels[i].second << "]";
    }
    std::cout << "]";
}

int main() {
    OrderBook book;
    std::vector<LogEntry> log;
    long long ordersAccepted = 0;
    bool invariantsOk = true;
    std::string invariantMsg;
    double simSeconds = 0;
    long long simOrders = 0;

    std::string line;
    while (std::getline(std::cin, line)) {
        std::istringstream in(line);
        std::string cmd;
        if (!(in >> cmd)) continue;
        try {
            if (cmd == "B" || cmd == "S") {
                double price;
                long long qty;
                if (!(in >> price >> qty)) { log.push_back({line, "error: usage " + cmd + " <price> <qty>"}); continue; }
                const size_t before = book.trades().size();
                const uint64_t id = book.addLimitOrder(cmd == "B" ? Side::Buy : Side::Sell,
                                                       std::llround(price * 100), qty);
                ++ordersAccepted;
                log.push_back({line, "order #" + std::to_string(id) + " accepted, " +
                                         std::to_string(book.trades().size() - before) + " trade(s)" +
                                         (book.hasOrder(id) ? ", resting in book" : ", fully filled")});
            } else if (cmd == "MB" || cmd == "MS") {
                long long qty;
                if (!(in >> qty)) { log.push_back({line, "error: usage " + cmd + " <qty>"}); continue; }
                const size_t before = book.trades().size();
                const uint64_t id = book.addMarketOrder(cmd == "MB" ? Side::Buy : Side::Sell, qty);
                ++ordersAccepted;
                log.push_back({line, "market order #" + std::to_string(id) + " done, " +
                                         std::to_string(book.trades().size() - before) + " trade(s)"});
            } else if (cmd == "C") {
                unsigned long long id;
                if (!(in >> id)) { log.push_back({line, "error: usage C <id>"}); continue; }
                log.push_back({line, book.cancel(id) ? "cancelled order #" + std::to_string(id)
                                                     : "no open order with id " + std::to_string(id)});
            } else if (cmd == "SIM") {
                long long n;
                unsigned seed;
                double mid;
                int spread, marketPct, cancelPct;
                if (!(in >> n >> seed >> mid >> spread >> marketPct >> cancelPct)) {
                    log.push_back({line, "error: usage SIM <orders> <seed> <mid> <spread> <market%> <cancel%>"});
                    continue;
                }
                const int64_t midTicks = std::llround(mid * 100);
                if (n < 1 || n > 2000000 || midTicks <= spread || spread < 1 || spread > 10000 ||
                    marketPct < 0 || cancelPct < 0 || marketPct + cancelPct > 100) {
                    log.push_back({line, "error: simulation parameters out of range"});
                    continue;
                }

                std::mt19937 rng(seed);
                std::uniform_int_distribution<int> offset(-spread, spread), qtyDist(1, 100), pct(0, 99);
                std::vector<uint64_t> ids;
                long long limits = 0, markets = 0, cancels = 0, cancelsOk = 0;
                const auto start = std::chrono::steady_clock::now();

                for (long long i = 0; i < n; ++i) {
                    const int roll = pct(rng);
                    const Side side = (rng() % 2 == 0) ? Side::Buy : Side::Sell;
                    // prices are placed around the last traded price (or the start price)
                    int64_t ref = book.trades().empty() ? midTicks : book.trades().back().price;
                    ref = std::max<int64_t>(ref, spread + 1);

                    if (roll < cancelPct && !ids.empty()) {
                        const size_t pick = rng() % ids.size();
                        ++cancels;
                        if (book.cancel(ids[pick])) ++cancelsOk;
                        ids[pick] = ids.back();
                        ids.pop_back();
                    } else if (roll < cancelPct + marketPct) {
                        book.addMarketOrder(side, qtyDist(rng));
                        ++markets;
                    } else {
                        ids.push_back(book.addLimitOrder(side, ref + offset(rng), qtyDist(rng)));
                        ++limits;
                    }

                    // invariant: the book must never be crossed after matching
                    if (book.bestBid() != 0 && book.bestAsk() != 0 && book.bestBid() >= book.bestAsk()) {
                        invariantsOk = false;
                        invariantMsg = "crossed book at simulated order " + std::to_string(i);
                        break;
                    }
                }
                simSeconds += std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
                simOrders += n;
                ordersAccepted += limits + markets;
                log.push_back({line, "simulated " + std::to_string(n) + " orders: " + std::to_string(limits) +
                                         " limit, " + std::to_string(markets) + " market, " +
                                         std::to_string(cancels) + " cancel tries (" + std::to_string(cancelsOk) +
                                         " succeeded)"});
            } else {
                log.push_back({line, "error: unknown command"});
            }
        } catch (const std::exception& e) {
            log.push_back({line, std::string("error: ") + e.what()});
        }
    }

    // ---------------- one JSON object ----------------
    const std::vector<Trade>& trades = book.trades();
    long long volume = 0;
    for (const Trade& t : trades) volume += t.qty;

    std::cout << "{\"log\":[";
    for (size_t i = 0; i < log.size(); ++i) {
        if (i) std::cout << ",";
        std::cout << "{\"cmd\":\"" << esc(log[i].cmd) << "\",\"msg\":\"" << esc(log[i].msg) << "\"}";
    }
    std::cout << "],\"book\":{\"bids\":";
    printLevels(book.bidLevels(15));
    std::cout << ",\"asks\":";
    printLevels(book.askLevels(15));
    std::cout << "},\"trades\":[";
    const size_t first = trades.size() > 200 ? trades.size() - 200 : 0;   // last 200 trades
    for (size_t i = first; i < trades.size(); ++i) {
        if (i > first) std::cout << ",";
        std::cout << "{\"buy\":" << trades[i].buyId << ",\"sell\":" << trades[i].sellId
                  << ",\"price\":" << trades[i].price << ",\"qty\":" << trades[i].qty << "}";
    }
    std::cout << "],\"price_series\":[";
    const size_t step = std::max<size_t>(1, trades.size() / 2000);          // at most ~2000 points
    bool firstPoint = true;
    for (size_t i = 0; i < trades.size(); i += step) {
        if (!firstPoint) std::cout << ",";
        std::cout << trades[i].price;
        firstPoint = false;
    }
    std::cout << "],\"stats\":{\"orders\":" << ordersAccepted << ",\"trades\":" << trades.size()
              << ",\"volume\":" << volume << ",\"best_bid\":" << book.bestBid()
              << ",\"best_ask\":" << book.bestAsk() << ",\"sim_orders\":" << simOrders
              << ",\"sim_seconds\":" << simSeconds
              << ",\"orders_per_sec\":" << (simSeconds > 0 ? static_cast<long long>(simOrders / simSeconds) : 0)
              << ",\"invariants_ok\":" << (invariantsOk ? "true" : "false")
              << ",\"invariant_message\":\"" << esc(invariantMsg) << "\"}}\n";
    return 0;
}