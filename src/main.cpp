#include "orderbook.hpp"

int main() {
    OrderBook book;

    book.addLimitOrder(Side::Buy, 10000, 10);   // order 1
    book.addLimitOrder(Side::Buy, 9900, 5);     // order 2
    book.addLimitOrder(Side::Sell, 9900, 12);   // order 3: should match both buys

    book.printTrades();
    book.printBook();
    return 0;
}