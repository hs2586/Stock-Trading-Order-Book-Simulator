#include <cassert>
#include "../src/orderbook.hpp"

void testExactFill() {
    OrderBook b;
    b.addLimitOrder(Side::Sell, 10100, 10);
    b.addLimitOrder(Side::Buy, 10100, 10);
    assert(b.trades().size() == 1);
    assert(b.trades()[0].qty == 10);
    assert(b.trades()[0].price == 10100);
    assert(b.bestAsk() == 0 && b.bestBid() == 0);   // book is empty
}

void testPartialFill() {
    OrderBook b;
    b.addLimitOrder(Side::Sell, 10100, 5);
    b.addLimitOrder(Side::Buy, 10100, 8);           // 5 trade, 3 rest as a bid
    assert(b.trades().size() == 1);
    assert(b.trades()[0].qty == 5);
    assert(b.bestAsk() == 0);
    assert(b.bestBid() == 10100);
}

void testBuySweepsTwoLevels() {
    OrderBook b;
    b.addLimitOrder(Side::Sell, 10100, 5);
    b.addLimitOrder(Side::Sell, 10200, 5);
    b.addLimitOrder(Side::Buy, 10200, 8);
    assert(b.trades().size() == 2);
    assert(b.trades()[0].qty == 5 && b.trades()[0].price == 10100);
    assert(b.trades()[1].qty == 3 && b.trades()[1].price == 10200);
    assert(b.bestAsk() == 10200);                   // 2 left at 102.00
}

void testNoMatch() {
    OrderBook b;
    b.addLimitOrder(Side::Sell, 10100, 5);
    b.addLimitOrder(Side::Buy, 10000, 5);
    assert(b.trades().empty());
    assert(b.bestBid() == 10000 && b.bestAsk() == 10100);
}

void testTimePriority() {
    OrderBook b;
    b.addLimitOrder(Side::Sell, 10100, 5);          // id 1, arrives first
    b.addLimitOrder(Side::Sell, 10100, 5);          // id 2
    b.addLimitOrder(Side::Buy, 10100, 5);
    assert(b.trades().size() == 1);
    assert(b.trades()[0].sellId == 1);              // oldest is filled first
}

void testTradesAtRestingPrice() {
    OrderBook b;
    b.addLimitOrder(Side::Sell, 10100, 5);
    b.addLimitOrder(Side::Buy, 10500, 5);           // willing to pay 105.00
    assert(b.trades()[0].price == 10100);           // but trades at 101.00
}

void testSellMatchesBid() {
    OrderBook b;
    b.addLimitOrder(Side::Buy, 10000, 10);          // id 1
    b.addLimitOrder(Side::Sell, 10000, 4);          // id 2
    assert(b.trades().size() == 1);
    assert(b.trades()[0].buyId == 1 && b.trades()[0].sellId == 2);
    assert(b.trades()[0].qty == 4 && b.trades()[0].price == 10000);
    assert(b.bestBid() == 10000);                   // 6 still resting
    assert(b.bestAsk() == 0);
}

void testSellSweepsTwoLevels() {
    OrderBook b;
    b.addLimitOrder(Side::Buy, 10000, 10);          // id 1
    b.addLimitOrder(Side::Buy, 9900, 5);            // id 2
    b.addLimitOrder(Side::Sell, 9900, 12);          // id 3
    assert(b.trades().size() == 2);
    assert(b.trades()[0].qty == 10 && b.trades()[0].price == 10000);
    assert(b.trades()[1].qty == 2 && b.trades()[1].price == 9900);
    assert(b.bestBid() == 9900);                    // 3 left at 99.00
    assert(b.bestAsk() == 0);
}

int main() {
    testExactFill();
    testPartialFill();
    testBuySweepsTwoLevels();
    testNoMatch();
    testTimePriority();
    testTradesAtRestingPrice();
    testSellMatchesBid();
    testSellSweepsTwoLevels();
    std::cout << "All tests passed\n";
    return 0;
}