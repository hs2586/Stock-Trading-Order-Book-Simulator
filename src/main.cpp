#include <cmath>
#include <iostream>
#include <sstream>
#include <string>
#include "orderbook.hpp"

void printHelp() {
    std::cout << "Commands:\n"
              << "  B <price> <qty>   limit buy      (example: B 100.50 10)\n"
              << "  S <price> <qty>   limit sell     (example: S 101.00 5)\n"
              << "  MB <qty>          market buy\n"
              << "  MS <qty>          market sell\n"
              << "  C <id>            cancel an order\n"
              << "  P                 print the book\n"
              << "  T                 print all trades\n"
              << "  H                 help\n"
              << "  Q                 quit\n";
}

int main() {
    OrderBook book;
    printHelp();

    std::string line;
    while (true) {
        std::cout << "> ";
        if (!std::getline(std::cin, line)) break;        // end of input

        std::istringstream in(line);
        std::string cmd;
        if (!(in >> cmd)) continue;                      // empty line

        try {
            if (cmd == "B" || cmd == "S") {
                double price;
                long long qty;
                if (!(in >> price >> qty)) {
                    std::cout << "usage: " << cmd << " <price> <qty>\n";
                    continue;
                }
                Side side = (cmd == "B") ? Side::Buy : Side::Sell;
                size_t before = book.trades().size();
                uint64_t id = book.addLimitOrder(side, std::llround(price * 100), qty);
                std::cout << "order #" << id << " accepted, "
                          << (book.trades().size() - before) << " trade(s)"
                          << (book.hasOrder(id) ? ", resting in book\n" : "\n");
            } else if (cmd == "MB" || cmd == "MS") {
                long long qty;
                if (!(in >> qty)) {
                    std::cout << "usage: " << cmd << " <qty>\n";
                    continue;
                }
                Side side = (cmd == "MB") ? Side::Buy : Side::Sell;
                size_t before = book.trades().size();
                uint64_t id = book.addMarketOrder(side, qty);
                std::cout << "market order #" << id << " done, "
                          << (book.trades().size() - before) << " trade(s)\n";
            } else if (cmd == "C") {
                unsigned long long id;
                if (!(in >> id)) {
                    std::cout << "usage: C <id>\n";
                    continue;
                }
                std::cout << (book.cancel(id) ? "cancelled\n" : "no open order with that id\n");
            } else if (cmd == "P") {
                book.printBook();
            } else if (cmd == "T") {
                book.printTrades();
            } else if (cmd == "H") {
                printHelp();
            } else if (cmd == "Q") {
                break;
            } else {
                std::cout << "unknown command, type H for help\n";
            }
        } catch (const std::exception& e) {
            std::cout << "error: " << e.what() << "\n";
        }
    }
    return 0;
}