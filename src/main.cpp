#include "orderbook.hpp"

int main() {
    OrderBook book;

    book.addLimitOrder(Side::Buy, 10000, 20);
    book.addLimitOrder(Side::Buy, 9900, 5);
    book.addLimitOrder(Side::Buy, 10000, 10);   // same price as the first buy

    book.addLimitOrder(Side::Sell, 10100, 10);
    book.addLimitOrder(Side::Sell, 10200, 15);
    book.addLimitOrder(Side::Sell, 10100, 5);   // same price as the first sell

    try {
        book.addLimitOrder(Side::Buy, 10000, 0);   // bad: quantity 0
    } catch (const std::exception& e) {
        std::cout << "Rejected: " << e.what() << "\n";
    }

    book.printBook();
    return 0;
}