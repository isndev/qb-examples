/**
 * @file examples/core/example9_trading_system.cpp
 * @example Simulated Multi-Core Trading System
 *
 * @brief This example simulates a basic financial trading system, showcasing how
 * QB actors can be used to build complex, multi-component applications distributed
 * across multiple CPU cores. It includes order entry, a matching engine, and
 * market data dissemination.
 *
 * @details
 * The system is composed of several actor types:
 * 1.  `ClientActor` (multiple instances):
 *     -   Simulates trading clients by generating and sending `NewOrderMessage`s.
 *     -   Placed on cores 0, 2 and 3 -- never core 1, which belongs to the matching engine.
 *     -   Receives `ExecutionMessage`s for filled orders and `OrderStatusMessage`s.
 *     -   Paces order generation with `spawn(...)` + `co_await ctx.sleep(...)`.
 * 2.  `OrderEntryActor`:
 *     -   Acts as a gateway for client orders.
 *     -   Receives `NewOrderMessage`s, performs initial validation (not detailed), and forwards them
 *         to the `MatchingEngineActor`.
 *     -   Remembers which client owns which order, and routes `ExecutionMessage`s and
 *         `OrderStatusMessage`s coming back from the engine to that client.
 * 3.  `MatchingEngineActor`:
 *     -   The core of the trading system, placed on a dedicated core for low latency.
 *     -   Maintains `OrderBook`s for various financial symbols.
 *     -   Matches buy and sell orders based on price-time priority.
 *     -   Generates `Trade` objects upon successful matches, each carrying both sides' orders.
 *     -   Sends `TradeMessage`s (containing executed trade details) to the `MarketDataActor`.
 *     -   Sends `ExecutionMessage`s back through the `OrderEntryActor`.
 *     -   Publishes `MarketDataMessage` (top of book, last trade) to the `MarketDataActor`.
 * 4.  `MarketDataActor`:
 *     -   Receives `TradeMessage`s and `MarketDataMessage`s from the `MatchingEngineActor`.
 *     -   Disseminates `MarketDataMessage` to subscribed clients (client subscription to market data is conceptual here).
 * 5.  `SupervisorActor`:
 *     -   Initializes and orchestrates the entire system.
 *     -   Sends `InitializeMessage` to start other actors.
 *     -   Polls the gateway and the engine for their counters once a second and prints the merged
 *         `StatsReportMessage`s.
 *     -   Manages the simulation lifecycle, initiating a shutdown after a set duration by sending `qb::KillEvent` to actors.
 *
 * The example emphasizes actor communication patterns, state management within order books,
 * and multi-core deployment strategies for different components of a complex system.
 *
 * @note FOUR THINGS THIS FILE USED TO GET WRONG. They are all classes of mistake, not typos, and
 *       three of them were invisible in a release run that exited 0.
 *
 *       (1) A DANGLING TIMER, which AddressSanitizer reports on 3 runs out of 3.
 *       `ClientActor::scheduleNextOrder()` armed its next order with
 *       `qb::io::async::callback([this]{ if (!_is_active) return; ... }, delay)`. That overload
 *       is not bound to the actor's lifetime: at shutdown the client is destroyed while its
 *       timer is still pending, the timer fires anyway, and `if (!_is_active)` -- the guard
 *       written to make it safe -- IS the read of freed memory. `spawn(...)` +
 *       `co_await ctx.sleep(d)` is bound to the actor's cancellation scope: kill the actor and
 *       the coroutine unwinds instead of resuming.
 *
 *       (2) EVERY EXECUTION REPORT WENT NOWHERE. `MatchingEngineActor` had a
 *       `getSenderFromOrderId()` that returned a DEFAULT-CONSTRUCTED `qb::ActorId`, and
 *       `OrderEntryActor::on(ExecutionMessage&)` declared a local `qb::ActorId client_actor_id;`
 *       and pushed to it. A measured run produced 204 trades and ZERO
 *       "Client ... received execution" lines. The gateway now records the owner of each order
 *       and the engine replies through the gateway.
 *
 *       (3) THE WRONG OVERLOAD. `ExecutionMessage` had a second constructor taking
 *       `(std::string_view client_id, std::string_view tid, ...)` that fabricated an empty
 *       `Order`. `push<ExecutionMessage>(dest, trade.buy_order_id, trade.trade_id, ...)` bound
 *       to it silently -- passing an ORDER id where a CLIENT id was expected -- so even a
 *       correctly addressed report would have described an order that never existed. Trades now
 *       carry both sides' `std::shared_ptr<Order>` and that constructor is gone.
 *
 *       (4) TWO CROSS-CORE DATA RACES, plus four cross-core globals. ThreadSanitizer reported
 *       SIX `data race` warnings on the previous version of this file in a single 10-second run,
 *       of two kinds. The first: `generatePrice()` mutated a function-local
 *       `static std::mt19937` from ClientActors on three different cores, and
 *       `std::mt19937::operator()` is not thread-safe -- while the file already had the correct
 *       per-actor `_rng` and used BOTH IN THE SAME EXPRESSION. The second, and the more
 *       instructive one: every event carried a live `std::shared_ptr<Order>`, so the matching
 *       engine on core 1 wrote `filled_quantity` and `status` inside `matchBuyOrder` while a
 *       client on another core read `msg.order->status`. Boxing a payload makes the event
 *       RELOCATABLE; it does not make the pointee OWNED. See `snapshot()`. The four
 *       `std::atomic<uint64_t> g_*` counters were the same mistake made deliberately: an actor
 *       owns its state, and telemetry is an event like anything else.
 *
 * QB Features Demonstrated:
 * - Multi-Core Deployment: Assigning different actors (`MatchingEngineActor`, `ClientActor`s, etc.) to specific CPU cores via
 * `engine.addActor<T>(core_id, ...)`.
 * - Complex Actor Interactions: Multiple actors collaborating through message passing to achieve system goals.
 * - Custom Event Hierarchy: `OrderMessage` as a base for various order-related events.
 * - Lifetime-Bound Timers: `spawn(...)` + `co_await ctx.sleep(...)` for order generation and stats.
 * - No Shared Mutable State: each actor owns its counters and reports them by event, and an
 *   event carries a snapshot of what it describes rather than a handle to the sender's object.
 * - State Encapsulation: `MatchingEngineActor` managing `OrderBook` state internally.
 * - Application-Specific Logic: Implementation of order matching and market data generation.
 * - System Orchestration: `SupervisorActor` managing the lifecycle and monitoring of the system.
 * - Engine Management: `qb::Main`, `engine.start()`, `engine.join()`.
 */

#include <array>
#include <deque>
#include <memory>
#include <string_view>
#include <qb/actor.h>
#include <qb/main.h>
#include <qb/io.h>
#include <qb/io/async.h>
#include <qb/string.h>
#include <chrono>

namespace {
// Global settings
const int NUM_CLIENTS                 = 10;
const int NUM_SYMBOLS                 = 3;
const int SIMULATION_DURATION_SECONDS = 10;

// Core 1 is dedicated to the matching engine, so clients are spread over the other three. The
// expression this replaced -- `(i % 3 == 1) ? 0 : (i % 3 + 1)` -- yielded {1, 0, 3}, putting a
// third of the clients on the very core its own comment said to keep clear.
constexpr std::array<int, 3> CLIENT_CORES{0, 2, 3};

// Helper function to get current timestamp in microseconds
uint64_t
getCurrentTimestamp() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now().time_since_epoch()).count());
}

// Generate a unique order ID.
//
// The atomic is legitimate here and is the only one left in the file: it is a pure counter with
// no reader, not shared application state. Everything an actor actually reasons about lives in
// the actor.
std::string
generateOrderId() {
    static std::atomic<uint64_t> next_id{1};
    std::stringstream            ss;
    ss << "ORD-" << std::setw(10) << std::setfill('0') << next_id++;
    return ss.str();
}

// Available stock symbols
const std::vector<std::string> SYMBOLS = {"AAPL", "MSFT", "GOOGL"};

// Generate a random price around the base price.
//
// `gen` is a PARAMETER, supplied by the calling actor. It used to be a function-local
// `static std::mt19937` mutated concurrently by clients on three cores -- a data race, and one
// that a release build will never report.
double
generatePrice(double base_price, std::mt19937 &gen, double volatility = 0.02) {
    std::normal_distribution<> d(0, volatility);

    // Apply random fluctuation to the base price
    double price = base_price * (1.0 + d(gen));
    // Round to 2 decimal places
    return std::round(price * 100) / 100;
}
} // namespace

// ═════════════════════════════════════════════════════════════════
// DOMAIN MODELS
// ═════════════════════════════════════════════════════════════════

enum class Side { BUY, SELL };

std::string
sideToString(Side side) {
    return side == Side::BUY ? "BUY" : "SELL";
}

enum class OrderStatus { NEW, PARTIALLY_FILLED, FILLED, CANCELED, REJECTED };

std::string
statusToString(OrderStatus status) {
    switch (status) {
        case OrderStatus::NEW:
            return "NEW";
        case OrderStatus::PARTIALLY_FILLED:
            return "PARTIALLY_FILLED";
        case OrderStatus::FILLED:
            return "FILLED";
        case OrderStatus::CANCELED:
            return "CANCELED";
        case OrderStatus::REJECTED:
            return "REJECTED";
        default:
            return "UNKNOWN";
    }
}

/**
 * @brief Order model representing a client's trading instruction
 */
struct Order {
    std::string order_id;
    std::string client_id;
    std::string symbol;
    Side        side;
    double      price;
    int         quantity;
    int         filled_quantity = 0;
    OrderStatus status          = OrderStatus::NEW;
    uint64_t    timestamp;

    // Constructeur par défaut
    Order()
        : order_id(generateOrderId())
        , side(Side::BUY)
        , price(0.0)
        , quantity(0)
        , timestamp(getCurrentTimestamp()) {}

    // Constructor for market orders
    Order(const std::string &client, const std::string &sym, Side s, int qty)
        : order_id(generateOrderId())
        , client_id(client)
        , symbol(sym)
        , side(s)
        , quantity(qty)
        , timestamp(getCurrentTimestamp()) {
        // Market orders have zero price (will match at best available)
        price = 0.0;
    }

    // Constructor for limit orders
    Order(const std::string &client, const std::string &sym, Side s, double p, int qty)
        : order_id(generateOrderId())
        , client_id(client)
        , symbol(sym)
        , side(s)
        , price(p)
        , quantity(qty)
        , timestamp(getCurrentTimestamp()) {}

    // Determine if the order is fully filled
    bool
    isFullyFilled() const {
        return filled_quantity >= quantity;
    }

    // Determine if the order is a market order
    bool
    isMarketOrder() const {
        return price == 0.0;
    }

    // Get the remaining unfilled quantity
    int
    getRemainingQuantity() const {
        return quantity - filled_quantity;
    }

    std::string
    toString() const {
        std::stringstream ss;
        ss << order_id << " | " << client_id << " | " << symbol << " | " << sideToString(side) << " | " << std::fixed << std::setprecision(2)
           << price << " | " << filled_quantity << "/" << quantity << " | " << statusToString(status);
        return ss.str();
    }
};

// Hand another actor a SNAPSHOT of an order, never the live object.
//
// THE `shared_ptr` PAYLOAD RULE, WHICH HAS TWO HALVES AND THIS FILE USED TO GET ONLY ONE.
// Boxing an unbounded payload behind a `std::shared_ptr` makes the EVENT relocatable: the engine
// memcpy-relocates events and never runs the source destructor, so the pointer moves and the
// characters stay put. That is the half everything here already did.
//
// The other half is OWNERSHIP. A `shared_ptr` that two actors on two cores both hold is shared
// mutable state, and the actor model's whole safety argument is that there is none. Measured with
// ThreadSanitizer on the previous version of this file: the matching engine on core 1 wrote
// `Order::filled_quantity` and `Order::status` inside `matchBuyOrder`/`matchSellOrder` while a
// ClientActor on another core read `msg.order->status` in `on(OrderStatusMessage&)` -- SIX
// `data race` reports in one 10-second run, none of them visible in release.
//
// So: relocatable is necessary and not sufficient. An event that leaves this actor carries a copy
// of what it describes, and the sender keeps the original. One `Order` copy per message is the
// price; a report is a value, not a handle.
std::shared_ptr<Order>
snapshot(const std::shared_ptr<Order> &order) {
    return std::make_shared<Order>(*order);
}

/**
 * @brief Trade model representing a matched pair of orders
 */
struct Trade {
    std::string trade_id;
    std::string buy_order_id;
    std::string sell_order_id;
    std::string symbol;
    double      price;
    int         quantity;
    uint64_t    timestamp;

    // Both sides of the match, so an execution report can name the ORDER it belongs to instead
    // of fabricating an empty one from an id. Held by `shared_ptr` because the book holds the
    // same objects: an execution and the book agree on status by construction.
    std::shared_ptr<Order> buy_order;
    std::shared_ptr<Order> sell_order;

    Trade(const std::shared_ptr<Order> &buy, const std::shared_ptr<Order> &sell, const std::string &sym, double p, int qty)
        : buy_order_id(buy->order_id)
        , sell_order_id(sell->order_id)
        , symbol(sym)
        , price(p)
        , quantity(qty)
        , timestamp(getCurrentTimestamp())
        , buy_order(buy)
        , sell_order(sell) {
        // Generate a unique trade ID
        static std::atomic<uint64_t> next_trade_id{1};
        std::stringstream            ss;
        ss << "TRD-" << std::setw(10) << std::setfill('0') << next_trade_id++;
        trade_id = ss.str();
    }

    std::string
    toString() const {
        std::stringstream ss;
        ss << trade_id << " | " << symbol << " | " << std::fixed << std::setprecision(2) << price << " | " << quantity;
        return ss.str();
    }
};

/**
 * @brief Price level in the order book
 */
struct PriceLevel {
    double                             price;
    std::deque<std::shared_ptr<Order>> orders;
    int                                total_quantity = 0;

    PriceLevel()
        : price(0.0) {} // Constructeur par défaut
    explicit PriceLevel(double p)
        : price(p) {}

    int
    getTotalQuantity() const {
        int total = 0;
        for (const auto &order : orders) {
            total += order->getRemainingQuantity();
        }
        return total;
    }
};

/**
 * @brief Order book for a specific instrument
 */
class OrderBook {
private:
    std::string                                             _symbol;
    std::map<double, PriceLevel, std::greater<double>>      _bids; // Highest first
    std::map<double, PriceLevel>                            _asks; // Lowest first
    std::unordered_map<std::string, std::shared_ptr<Order>> _orders_by_id;

    // Last trade price and timestamp
    double   _last_price      = 0.0;
    uint64_t _last_trade_time = 0;

    // Order book statistics
    int _total_volume = 0;

    // Market price info
    double _open_price = 0.0;
    double _high_price = 0.0;
    double _low_price  = std::numeric_limits<double>::max();

public:
    OrderBook()
        : _symbol("") {} // Constructeur par défaut
    explicit OrderBook(const std::string &symbol)
        : _symbol(symbol) {}

    // Get basic book info
    std::string
    getSymbol() const {
        return _symbol;
    }
    double
    getLastPrice() const {
        return _last_price;
    }
    int
    getTotalVolume() const {
        return _total_volume;
    }

    // Get best bid and ask prices
    double
    getBestBidPrice() const {
        return _bids.empty() ? 0.0 : _bids.begin()->first;
    }

    double
    getBestAskPrice() const {
        return _asks.empty() ? 0.0 : _asks.begin()->first;
    }

    // Get total volume at best bid and ask
    int
    getBestBidVolume() const {
        return _bids.empty() ? 0 : _bids.begin()->second.getTotalQuantity();
    }

    int
    getBestAskVolume() const {
        return _asks.empty() ? 0 : _asks.begin()->second.getTotalQuantity();
    }

    // Add an order to the book
    void
    addOrder(const std::shared_ptr<Order> &order) {
        if (order->isMarketOrder()) {
            // Market orders are executed immediately so they don't go into the book
            return;
        }

        // Store order in the map
        _orders_by_id[order->order_id] = order;

        // Add to the appropriate side
        if (order->side == Side::BUY) {
            if (_bids.find(order->price) == _bids.end()) {
                _bids[order->price] = PriceLevel(order->price);
            }
            _bids[order->price].orders.push_back(order);
        } else {
            if (_asks.find(order->price) == _asks.end()) {
                _asks[order->price] = PriceLevel(order->price);
            }
            _asks[order->price].orders.push_back(order);
        }
    }

    // Remove an order from the book
    void
    removeOrder(const std::string &order_id) {
        auto order_it = _orders_by_id.find(order_id);
        if (order_it == _orders_by_id.end()) {
            return; // Order not found
        }

        auto order = order_it->second;

        // Remove from the price level
        if (order->side == Side::BUY) {
            auto price_it = _bids.find(order->price);
            if (price_it != _bids.end()) {
                auto &orders = price_it->second.orders;
                orders.erase(std::remove_if(orders.begin(), orders.end(),
                                            [&order_id](const std::shared_ptr<Order> &o) { return o->order_id == order_id; }),
                             orders.end());

                // Remove price level if empty
                if (orders.empty()) {
                    _bids.erase(price_it);
                }
            }
        } else {
            auto price_it = _asks.find(order->price);
            if (price_it != _asks.end()) {
                auto &orders = price_it->second.orders;
                orders.erase(std::remove_if(orders.begin(), orders.end(),
                                            [&order_id](const std::shared_ptr<Order> &o) { return o->order_id == order_id; }),
                             orders.end());

                // Remove price level if empty
                if (orders.empty()) {
                    _asks.erase(price_it);
                }
            }
        }

        // Remove from the map
        _orders_by_id.erase(order_id);
    }

    // Match orders and return resulting trades
    std::vector<Trade>
    matchOrders(const std::shared_ptr<Order> &incoming_order) {
        std::vector<Trade> trades;

        if (incoming_order->side == Side::BUY) {
            // Buy order - match with asks
            matchBuyOrder(incoming_order, trades);
        } else {
            // Sell order - match with bids
            matchSellOrder(incoming_order, trades);
        }

        // If there's any remaining quantity and it's not a market order, add to book
        if (incoming_order->getRemainingQuantity() > 0 && !incoming_order->isMarketOrder()) {
            addOrder(incoming_order);
        }

        return trades;
    }

private:
    // Match a buy order against the available asks
    void
    matchBuyOrder(const std::shared_ptr<Order> &buy_order, std::vector<Trade> &trades) {
        // For market orders, use the best available price
        double max_price = buy_order->isMarketOrder() ? std::numeric_limits<double>::max() : buy_order->price;

        // Continue matching as long as there are matching asks and the order has remaining quantity
        while (!_asks.empty() && buy_order->getRemainingQuantity() > 0) {
            // Get the best (lowest) ask price
            auto   ask_it    = _asks.begin();
            double ask_price = ask_it->first;

            // Check if we can match at this price
            if (ask_price > max_price) {
                break; // No matching ask prices
            }

            // Get the orders at this price level
            auto &ask_level  = ask_it->second;
            auto &ask_orders = ask_level.orders;

            // Match with orders at this price level
            while (!ask_orders.empty() && buy_order->getRemainingQuantity() > 0) {
                auto &sell_order = ask_orders.front();

                // Calculate the matched quantity
                int match_qty = std::min(buy_order->getRemainingQuantity(), sell_order->getRemainingQuantity());

                // Update order quantities
                buy_order->filled_quantity += match_qty;
                sell_order->filled_quantity += match_qty;

                // Create a trade
                trades.emplace_back(buy_order, sell_order, _symbol, ask_price, match_qty);

                // Update market stats
                _total_volume += match_qty;
                _last_price      = ask_price;
                _last_trade_time = getCurrentTimestamp();

                if (_high_price < ask_price)
                    _high_price = ask_price;
                if (_low_price > ask_price)
                    _low_price = ask_price;
                if (_open_price == 0)
                    _open_price = ask_price;

                // Update order status
                if (sell_order->isFullyFilled()) {
                    sell_order->status = OrderStatus::FILLED;
                    ask_orders.pop_front(); // Remove the filled order
                } else {
                    sell_order->status = OrderStatus::PARTIALLY_FILLED;
                    break; // The sell order still has quantity, so we're done with this buy order
                }
            }

            // If no more orders at this price level, remove it
            if (ask_orders.empty()) {
                _asks.erase(ask_it);
            }
        }

        // Update the buy order status
        if (buy_order->isFullyFilled()) {
            buy_order->status = OrderStatus::FILLED;
        } else if (buy_order->filled_quantity > 0) {
            buy_order->status = OrderStatus::PARTIALLY_FILLED;
        }
    }

    // Match a sell order against the available bids
    void
    matchSellOrder(const std::shared_ptr<Order> &sell_order, std::vector<Trade> &trades) {
        // For market orders, use any bid price
        double min_price = sell_order->isMarketOrder() ? 0.0 : sell_order->price;

        // Continue matching as long as there are matching bids and the order has remaining quantity
        while (!_bids.empty() && sell_order->getRemainingQuantity() > 0) {
            // Get the best (highest) bid price
            auto   bid_it    = _bids.begin();
            double bid_price = bid_it->first;

            // Check if we can match at this price
            if (bid_price < min_price) {
                break; // No matching bid prices
            }

            // Get the orders at this price level
            auto &bid_level  = bid_it->second;
            auto &bid_orders = bid_level.orders;

            // Match with orders at this price level
            while (!bid_orders.empty() && sell_order->getRemainingQuantity() > 0) {
                auto &buy_order = bid_orders.front();

                // Calculate the matched quantity
                int match_qty = std::min(sell_order->getRemainingQuantity(), buy_order->getRemainingQuantity());

                // Update order quantities
                sell_order->filled_quantity += match_qty;
                buy_order->filled_quantity += match_qty;

                // Create a trade
                trades.emplace_back(buy_order, sell_order, _symbol, bid_price, match_qty);

                // Update market stats
                _total_volume += match_qty;
                _last_price      = bid_price;
                _last_trade_time = getCurrentTimestamp();

                if (_high_price < bid_price)
                    _high_price = bid_price;
                if (_low_price > bid_price)
                    _low_price = bid_price;
                if (_open_price == 0)
                    _open_price = bid_price;

                // Update order status
                if (buy_order->isFullyFilled()) {
                    buy_order->status = OrderStatus::FILLED;
                    bid_orders.pop_front(); // Remove the filled order
                } else {
                    buy_order->status = OrderStatus::PARTIALLY_FILLED;
                    break; // The buy order still has quantity, so we're done with this sell order
                }
            }

            // If no more orders at this price level, remove it
            if (bid_orders.empty()) {
                _bids.erase(bid_it);
            }
        }

        // Update the sell order status
        if (sell_order->isFullyFilled()) {
            sell_order->status = OrderStatus::FILLED;
        } else if (sell_order->filled_quantity > 0) {
            sell_order->status = OrderStatus::PARTIALLY_FILLED;
        }
    }
};

// ═════════════════════════════════════════════════════════════════
// EVENT MESSAGES
// ═════════════════════════════════════════════════════════════════

// Base message for all order-related events
struct OrderMessage : public qb::Event {
    std::shared_ptr<Order> order;

    explicit OrderMessage(const std::shared_ptr<Order> &o)
        : order(o) {}
};

// New order submission
struct NewOrderMessage : public OrderMessage {
    explicit NewOrderMessage(const std::shared_ptr<Order> &o)
        : OrderMessage(o) {}
};

// Order execution notification.
//
// NOTE ON EVENT PAYLOADS, which applies to every event in this file: the engine relocates an
// event with `memcpy` and never runs the source destructor, so a payload member may hold no
// pointer into itself. On libstdc++ a SHORT std::string holds exactly that -- `_M_p` addresses
// its own inline buffer -- so after the relocation it still points at the old storage. libc++
// recomputes the pointer from `this`, which is why the defect is invisible on macOS and corrupts
// on Linux. This system runs its actors on four cores, so its events really are relocated.
// The two sanctioned shapes are `qb::string<N>` for a bounded payload and a `std::shared_ptr`
// for an unbounded one; both appear below.
struct ExecutionMessage : public qb::Event {
    std::shared_ptr<Order> order;
    qb::string<32>         trade_id;
    double                 execution_price;
    int                    execution_quantity;

    ExecutionMessage(const std::shared_ptr<Order> &o, std::string_view tid, double price, int quantity)
        : order(o)
        , trade_id(tid)
        , execution_price(price)
        , execution_quantity(quantity) {}

    // There used to be a second constructor here taking `(std::string_view client_id,
    // std::string_view tid, double, int)` that manufactured an empty `Order`. It existed to make
    // a call site compile, and the call site passed an ORDER id where it expected a CLIENT id.
    // An overload that silently accepts the wrong thing is worse than a compile error; the
    // engine now has the real `std::shared_ptr<Order>` and passes it.
};

// Order cancellation request
struct CancelOrderMessage : public OrderMessage {
    explicit CancelOrderMessage(const std::shared_ptr<Order> &o)
        : OrderMessage(o) {}
};

// Order status update
struct OrderStatusMessage : public OrderMessage {
    explicit OrderStatusMessage(const std::shared_ptr<Order> &o)
        : OrderMessage(o) {}
};

// Market data update with new prices. Pushed from the matching engine on core 1 to the market
// data actor on core 0, so `symbol` is relocated twice on every update -- see the note on
// ExecutionMessage above for why it cannot be a std::string.
struct MarketDataMessage : public qb::Event {
    qb::string<16> symbol;
    double         bid_price;
    int            bid_size;
    double         ask_price;
    int            ask_size;
    double         last_price;
    int            last_size;

    // Constructeur par défaut
    MarketDataMessage()
        : bid_price(0.0)
        , bid_size(0)
        , ask_price(0.0)
        , ask_size(0)
        , last_price(0.0)
        , last_size(0) {}

    MarketDataMessage(std::string_view sym, double bp, int bs, double ap, int as, double lp, int ls)
        : symbol(sym)
        , bid_price(bp)
        , bid_size(bs)
        , ask_price(ap)
        , ask_size(as)
        , last_price(lp)
        , last_size(ls) {}
};

// Trade notification message.
//
// The trade is held behind a `std::shared_ptr` -- exactly as OrderMessage holds its Order above --
// and NOT by value. A by-value `Trade` would splice its four std::string members straight into the
// event, and this message really does cross a core boundary (matching engine on core 1 -> market
// data on core 0); the handler then calls `toString()` on strings whose characters were left
// behind in the sender's pipe. Boxing it is the sanctioned shape for an unbounded payload: the
// pointer is relocated, the characters never move.
struct TradeMessage : public qb::Event {
    std::shared_ptr<Trade> trade;

    explicit TradeMessage(const Trade &t)
        : trade(std::make_shared<Trade>(t)) {
        // A `Trade` also holds both sides' `Order`s, which the matching engine keeps mutating on
        // its own core. Copying the Trade copies those two pointers, not the orders -- so
        // snapshot them as well, and this message owns everything it points at.
        trade->buy_order  = snapshot(t.buy_order);
        trade->sell_order = snapshot(t.sell_order);
    }
};

// Telemetry, done the actor way: the supervisor ASKS, each component ANSWERS with the counters
// it owns, and nothing is shared. This replaces four `std::atomic<uint64_t>` globals written by
// actors on four cores.
struct StatsRequestMessage : public qb::Event {};

struct StatsReportMessage : public qb::Event {
    uint64_t total_orders{0};         // orders accepted by the gateway
    uint64_t order_messages{0};       // order-related messages the gateway handled
    uint64_t total_trades{0};         // trades produced by the matching engine
    uint64_t market_data_messages{0}; // market-data updates the engine published

    StatsReportMessage(uint64_t orders, uint64_t order_msgs, uint64_t trades, uint64_t md_msgs)
        : total_orders(orders)
        , order_messages(order_msgs)
        , total_trades(trades)
        , market_data_messages(md_msgs) {}
};

// Initialization message
struct InitializeMessage : public qb::Event {
    // Add initialization parameters if needed
};

// Self-addressed wake-ups produced by each actor's own coroutine timers.
struct OrderTickEvent : public qb::Event {};
struct StatsTickEvent : public qb::Event {};
struct ShutdownTickEvent : public qb::Event {};

// ═════════════════════════════════════════════════════════════════
// TRADING SYSTEM ACTORS
// ═════════════════════════════════════════════════════════════════

/**
 * @brief Client actor that generates orders
 */
class ClientActor : public qb::Actor {
private:
    std::string  _client_id;
    qb::ActorId  _order_entry_id;
    std::string  _preferred_symbol;
    double       _base_price;
    std::mt19937 _rng;
    bool         _is_active = false;

public:
    ClientActor(const std::string &client_id, qb::ActorId order_entry_id, const std::string &symbol, double base_price)
        : _client_id(client_id)
        , _order_entry_id(order_entry_id)
        , _preferred_symbol(symbol)
        , _base_price(base_price) {
        // Initialize random number generator
        std::random_device rd;
        _rng = std::mt19937(rd());

        // Register for messages
        registerEvent<ExecutionMessage>(*this);
        registerEvent<OrderStatusMessage>(*this);
        registerEvent<InitializeMessage>(*this);
        registerEvent<OrderTickEvent>(*this);
        registerEvent<qb::KillEvent>(*this);
    }

    qb::io::async::task<bool>
    onInit() override {
        qb::io::cout() << "ClientActor " << _client_id << " initialized with ID: " << id() << std::endl;
        co_return true;
    }

    void
    on(InitializeMessage &) {
        _is_active = true;
        scheduleNextOrder();
    }

    void
    on(OrderTickEvent const &) {
        if (!_is_active)
            return;

        generateRandomOrder();
        scheduleNextOrder();
    }

    void
    on(ExecutionMessage &msg) {
        // Handle execution report
        qb::io::cout() << "Client " << _client_id << " received execution: " << msg.trade_id << " for " << msg.execution_quantity << " at $"
                       << msg.execution_price << std::endl;
    }

    void
    on(OrderStatusMessage &msg) {
        // Handle order status update
        qb::io::cout() << "Client " << _client_id << " order status: " << msg.order->order_id << " is now " << statusToString(msg.order->status)
                       << std::endl;
    }

    void
    on(qb::KillEvent &) {
        _is_active = false;
        kill();
    }

private:
    void
    scheduleNextOrder() {
        if (!_is_active)
            return;

        // Generate a random order at random intervals
        std::uniform_real_distribution<> delay_dist(0.1, 0.5); // 100ms to 500ms delay
        const auto                       delay = std::chrono::duration_cast<qb::duration>(std::chrono::duration<double>(delay_dist(_rng)));

        // Schedule the next order. `spawn` is bound to this actor's cancellation scope, so when
        // the supervisor kills this client the pending sleep is cancelled and the coroutine
        // unwinds -- no lambda survives to look at `_is_active` on a destroyed actor. The guard
        // in `on(OrderTickEvent const&)` above only covers the ordinary "tick already queued
        // when we went inactive" case; it is not what keeps this safe.
        spawn([delay](qb::ScopedCoroContext ctx) -> qb::io::async::task<void> {
            co_await ctx.sleep(delay);
            ctx.template push<OrderTickEvent>();
        });
    }

    void
    generateRandomOrder() {
        // Random order parameters
        std::uniform_int_distribution<>  side_dist(0, 1);
        std::uniform_int_distribution<>  qty_dist(1, 100);
        std::uniform_real_distribution<> price_dist(0.95, 1.05);
        std::uniform_int_distribution<>  symbol_idx_dist(0, NUM_SYMBOLS - 1);

        // Decide the symbol (with preference for the assigned one)
        std::string symbol = _preferred_symbol;
        if (symbol_idx_dist(_rng) == 0) { // 1/3 chance to trade a different symbol
            symbol = SYMBOLS[symbol_idx_dist(_rng)];
        }

        // Decide side, quantity and price. Both random draws come from THIS actor's `_rng`; the
        // previous version mixed a per-actor generator and a shared static one in this single
        // expression.
        Side   side     = side_dist(_rng) ? Side::BUY : Side::SELL;
        int    quantity = qty_dist(_rng);
        double price    = generatePrice(_base_price, _rng, 0.02) * price_dist(_rng);
        price           = std::round(price * 100) / 100; // Round to 2 decimal places

        // Create the order
        auto order = std::make_shared<Order>(_client_id, symbol, side, price, quantity);

        // Send to order entry
        push<NewOrderMessage>(_order_entry_id, order);
    }
};

/**
 * @brief Order Entry actor that validates and routes orders
 */
class OrderEntryActor : public qb::Actor {
private:
    qb::ActorId                                             _matching_engine_id;
    std::unordered_map<std::string, std::shared_ptr<Order>> _active_orders;
    // Who sent each order. This is the piece that was missing: without it the gateway had no
    // address to route an execution report to, and pushed to a default-constructed `ActorId`.
    std::unordered_map<std::string, qb::ActorId> _order_owner;

    uint64_t _orders_accepted{0};
    uint64_t _order_messages{0};

public:
    explicit OrderEntryActor(qb::ActorId matching_engine_id)
        : _matching_engine_id(matching_engine_id) {
        // Register for messages
        registerEvent<NewOrderMessage>(*this);
        registerEvent<CancelOrderMessage>(*this);
        registerEvent<ExecutionMessage>(*this);
        registerEvent<OrderStatusMessage>(*this);
        registerEvent<StatsRequestMessage>(*this);
        registerEvent<qb::KillEvent>(*this);
    }

    qb::io::async::task<bool>
    onInit() override {
        qb::io::cout() << "OrderEntryActor initialized with ID: " << id() << std::endl;
        co_return true;
    }

    void
    on(NewOrderMessage &msg) {
        _order_messages++;

        auto order = msg.order;

        // Validate the order
        if (order->quantity <= 0) {
            order->status = OrderStatus::REJECTED;
            push<OrderStatusMessage>(msg.getSource(), snapshot(order));
            return;
        }

        // Track the order and remember who owns it. `_active_orders` gets its OWN copy: the
        // original is about to become the matching engine's exclusive property.
        _active_orders[order->order_id] = snapshot(order);
        _order_owner[order->order_id]   = msg.getSource();
        _orders_accepted++;

        // Send acknowledgment to client -- again a copy, so the client can read it on its own
        // core while the engine works on the original.
        push<OrderStatusMessage>(msg.getSource(), snapshot(order));

        // Forward to matching engine, which is now the only actor holding this object.
        push<NewOrderMessage>(_matching_engine_id, order);
    }

    void
    on(CancelOrderMessage &msg) {
        _order_messages++;

        auto order_id = msg.order->order_id;

        // Check if the order exists and is active
        if (_active_orders.find(order_id) != _active_orders.end()) {
            // Forward to matching engine
            push<CancelOrderMessage>(_matching_engine_id, snapshot(msg.order));
        } else {
            // Order not found or already completed
            auto order    = msg.order;
            order->status = OrderStatus::REJECTED;
            push<OrderStatusMessage>(msg.getSource(), snapshot(order));
        }
    }

    void
    on(ExecutionMessage &msg) {
        _order_messages++;

        // Update order status
        auto order_id = msg.order->order_id;
        if (_active_orders.find(order_id) != _active_orders.end()) {
            // Order is active - update it
            _active_orders[order_id] = msg.order;

            // If order is filled or canceled, remove from active orders
            if (msg.order->status == OrderStatus::FILLED || msg.order->status == OrderStatus::CANCELED) {
                _active_orders.erase(order_id);
            }
        }

        // Forward execution to the client that placed the order -- its own copy, since the line
        // above may have parked `msg.order` in this actor's map.
        forwardToOwner<ExecutionMessage>(order_id, snapshot(msg.order), msg.trade_id, msg.execution_price, msg.execution_quantity);
    }

    // Status updates that come BACK from the matching engine (cancellations) are routed the same
    // way. The gateway's own acknowledgements above go straight to `msg.getSource()`.
    void
    on(OrderStatusMessage &msg) {
        if (msg.getSource() != _matching_engine_id)
            return;

        _order_messages++;
        forwardToOwner<OrderStatusMessage>(msg.order->order_id, snapshot(msg.order));
    }

    void
    on(StatsRequestMessage &msg) {
        push<StatsReportMessage>(msg.getSource(), _orders_accepted, _order_messages, 0, 0);
    }

    void
    on(qb::KillEvent const &) {
        kill();
    }

private:
    template <typename _Event, typename... _Args>
    void
    forwardToOwner(const std::string &order_id, _Args &&...args) const {
        auto owner = _order_owner.find(order_id);
        if (owner == _order_owner.end())
            return; // unknown order: nothing to answer

        push<_Event>(owner->second, std::forward<_Args>(args)...);
    }
};

/**
 * @brief Matching Engine actor that matches orders and produces trades
 */
class MatchingEngineActor : public qb::Actor {
private:
    std::unordered_map<std::string, OrderBook> _order_books;
    qb::ActorId                                _market_data_id;
    // The gateway every order arrives through, learned from the first order rather than wired in
    // by `main()`. Execution reports travel back the way orders came.
    qb::ActorId _order_entry_id{};

    uint64_t _trades{0};
    uint64_t _market_data_messages{0};

public:
    explicit MatchingEngineActor(qb::ActorId market_data_id)
        : _market_data_id(market_data_id) {
        // Register for messages
        registerEvent<NewOrderMessage>(*this);
        registerEvent<CancelOrderMessage>(*this);
        registerEvent<InitializeMessage>(*this);
        registerEvent<StatsRequestMessage>(*this);
        registerEvent<qb::KillEvent>(*this);
    }

    qb::io::async::task<bool>
    onInit() override {
        qb::io::cout() << "MatchingEngineActor initialized with ID: " << id() << std::endl;
        co_return true;
    }

    void
    on(InitializeMessage &) {
        // Initialize order books for all symbols
        for (const auto &symbol : SYMBOLS) {
            _order_books.emplace(symbol, OrderBook(symbol));

            // Set initial market data
            double base_price = (symbol == "AAPL") ? 175.0 : (symbol == "MSFT") ? 320.0 : 130.0;

            publishMarketData(symbol, base_price, 0, base_price, 0, base_price, 0);
        }
    }

    void
    on(NewOrderMessage &msg) {
        _order_entry_id = msg.getSource();

        auto order = msg.order;

        // Check if we have an order book for this symbol
        if (_order_books.find(order->symbol) == _order_books.end()) {
            _order_books.emplace(order->symbol, OrderBook(order->symbol));
        }

        // Get the order book
        auto &order_book = _order_books[order->symbol];

        // Try to match the order
        auto trades = order_book.matchOrders(order);

        // Process resulting trades
        for (const auto &trade : trades) {
            // Increment trade counter
            _trades++;

            // Notify clients of execution
            executeTrade(trade);

            // Send trade to market data
            push<TradeMessage>(_market_data_id, trade);
        }

        // Update market data
        publishMarketDataForSymbol(order->symbol);
    }

    void
    on(StatsRequestMessage &msg) {
        push<StatsReportMessage>(msg.getSource(), 0, 0, _trades, _market_data_messages);
    }

    void
    on(CancelOrderMessage &msg) {
        auto order = msg.order;

        // Check if we have an order book for this symbol
        if (_order_books.find(order->symbol) == _order_books.end()) {
            return; // Symbol not found
        }

        // Get the order book
        auto &order_book = _order_books[order->symbol];

        // Remove order from the book
        order_book.removeOrder(order->order_id);

        // Update the order status
        order->status = OrderStatus::CANCELED;

        // Notify the client, through the gateway that knows who they are.
        if (_order_entry_id.is_valid())
            push<OrderStatusMessage>(_order_entry_id, snapshot(order));

        // Update market data
        publishMarketDataForSymbol(order->symbol);
    }

    void
    on(qb::KillEvent const &) {
        kill();
    }

private:
    // Process a trade
    void
    executeTrade(const Trade &trade) {
        // The engine does not know which client placed which order, and should not: routing is
        // the gateway's job. It sends both reports there, carrying the real `Order` objects, and
        // the gateway looks the owners up.
        //
        // What this replaced was a `getSenderFromOrderId()` whose body was
        // `return qb::ActorId();` under a comment saying "For simplicity, we'll just return an
        // empty ActorId" -- guarded by `if (buyer)`, so both pushes were silently skipped and no
        // execution report was ever delivered.
        if (!_order_entry_id.is_valid())
            return;

        push<ExecutionMessage>(_order_entry_id, snapshot(trade.buy_order), trade.trade_id, trade.price, trade.quantity);
        push<ExecutionMessage>(_order_entry_id, snapshot(trade.sell_order), trade.trade_id, trade.price, trade.quantity);
    }

    // Publish market data for a specific symbol
    void
    publishMarketDataForSymbol(const std::string &symbol) {
        // Check if we have an order book for this symbol
        if (_order_books.find(symbol) == _order_books.end()) {
            return; // Symbol not found
        }

        // Get the order book
        const auto &order_book = _order_books[symbol];

        // Get market data
        double bid_price  = order_book.getBestBidPrice();
        int    bid_size   = order_book.getBestBidVolume();
        double ask_price  = order_book.getBestAskPrice();
        int    ask_size   = order_book.getBestAskVolume();
        double last_price = order_book.getLastPrice();

        // Publish market data
        publishMarketData(symbol, bid_price, bid_size, ask_price, ask_size, last_price, 0);
    }

    // Helper to publish market data
    void
    publishMarketData(const std::string &symbol, double bid_price, int bid_size, double ask_price, int ask_size, double last_price,
                      int last_size) {
        _market_data_messages++;

        push<MarketDataMessage>(_market_data_id, symbol, bid_price, bid_size, ask_price, ask_size, last_price, last_size);
    }
};

/**
 * @brief Market Data actor that disseminates price information
 */
class MarketDataActor : public qb::Actor {
private:
    std::map<std::string, MarketDataMessage> _latest_market_data;
    std::vector<qb::ActorId>                 _subscribers;

public:
    MarketDataActor() {
        // Register for messages
        registerEvent<MarketDataMessage>(*this);
        registerEvent<TradeMessage>(*this);
        registerEvent<qb::KillEvent>(*this);
    }

    qb::io::async::task<bool>
    onInit() override {
        qb::io::cout() << "MarketDataActor initialized with ID: " << id() << std::endl;
        co_return true;
    }

    void
    on(MarketDataMessage &msg) {
        // Store the latest market data. `symbol` is a fixed-size qb::string, so it is converted
        // once here to the std::string this actor's own (never relocated) map is keyed by.
        _latest_market_data[msg.symbol.c_str()] = msg;

        // Log the market data
        qb::io::cout() << "Market Data: " << msg.symbol << " Bid: " << std::fixed << std::setprecision(2) << msg.bid_price << " x "
                       << msg.bid_size << " Ask: " << msg.ask_price << " x " << msg.ask_size << " Last: " << msg.last_price << std::endl;

        // Broadcast to subscribers
        for (const auto &subscriber_id : _subscribers) {
            push<MarketDataMessage>(subscriber_id, msg.symbol, msg.bid_price, msg.bid_size, msg.ask_price, msg.ask_size, msg.last_price,
                                    msg.last_size);
        }
    }

    void
    on(TradeMessage &msg) {
        // Log the trade
        qb::io::cout() << "Trade: " << msg.trade->toString() << std::endl;
    }

    void
    on(qb::KillEvent &) {
        kill();
    }

    // Add a subscriber
    void
    addSubscriber(qb::ActorId subscriber_id) {
        _subscribers.push_back(subscriber_id);
    }
};

/**
 * @brief Supervisor actor that manages the trading system
 */
class SupervisorActor : public qb::Actor {
private:
    qb::ActorId              _matching_engine_id;
    qb::ActorId              _order_entry_id;
    qb::ActorId              _market_data_id;
    std::vector<qb::ActorId> _client_ids;

    uint64_t _start_time    = 0;
    bool     _is_active     = false;
    bool     _shutting_down = false;

    // Merged snapshot, rebuilt on every polling round.
    int      _pending_reports      = 0;
    uint64_t _total_orders         = 0;
    uint64_t _order_messages       = 0;
    uint64_t _total_trades         = 0;
    uint64_t _market_data_messages = 0;

public:
    SupervisorActor(qb::ActorId matching_engine, qb::ActorId order_entry, qb::ActorId market_data, const std::vector<qb::ActorId> &clients)
        : _matching_engine_id(matching_engine)
        , _order_entry_id(order_entry)
        , _market_data_id(market_data)
        , _client_ids(clients) {
        // Register for messages
        registerEvent<StatsReportMessage>(*this);
        registerEvent<StatsTickEvent>(*this);
        registerEvent<ShutdownTickEvent>(*this);
        registerEvent<qb::KillEvent>(*this);
        registerEvent<InitializeMessage>(*this);
    }

    qb::io::async::task<bool>
    onInit() override {
        qb::io::cout() << "SupervisorActor initialized with ID: " << id() << std::endl;

        push<InitializeMessage>(id());
        co_return true;
    }

    void
    on(InitializeMessage &) {
        _is_active  = true;
        _start_time = getCurrentTimestamp();

        qb::io::cout() << "Trading system starting..." << std::endl;

        // Initialize the matching engine
        push<InitializeMessage>(_matching_engine_id);

        // Initialize all clients
        for (const auto &client_id : _client_ids) {
            push<InitializeMessage>(client_id);
        }

        // Schedule performance report
        schedulePerformanceReport();

        // Schedule system shutdown
        spawn([](qb::ScopedCoroContext ctx) -> qb::io::async::task<void> {
            co_await ctx.sleep(std::chrono::seconds(SIMULATION_DURATION_SECONDS));
            ctx.template push<ShutdownTickEvent>();
        });
    }

    void
    on(StatsTickEvent const &) {
        requestStats();

        if (_is_active)
            schedulePerformanceReport();
    }

    void
    on(ShutdownTickEvent const &) {
        if (!_is_active)
            return;

        qb::io::cout() << "\nTrading system shutting down..." << std::endl;

        _is_active     = false;
        _shutting_down = true;

        // One last polling round, so the final figures are the components' own and not a
        // snapshot the supervisor guessed at.
        requestStats();
    }

    void
    on(StatsReportMessage &msg) {
        // Each component reports only the counters it owns; the zeros are the other one's.
        _total_orders += msg.total_orders;
        _order_messages += msg.order_messages;
        _total_trades += msg.total_trades;
        _market_data_messages += msg.market_data_messages;

        if (--_pending_reports > 0)
            return;

        printStatistics();

        if (_shutting_down)
            stopEverything();
    }

    void
    on(qb::KillEvent const &) {
        _is_active = false;
        kill();
    }

private:
    void
    requestStats() {
        _pending_reports      = 2;
        _total_orders         = 0;
        _order_messages       = 0;
        _total_trades         = 0;
        _market_data_messages = 0;

        push<StatsRequestMessage>(_order_entry_id);
        push<StatsRequestMessage>(_matching_engine_id);
    }

    void
    printStatistics() const {
        const double elapsed_seconds = (getCurrentTimestamp() - _start_time) / 1000000.0;
        if (elapsed_seconds <= 0.0)
            return;

        qb::io::cout() << "\n======= TRADING SYSTEM STATISTICS =======" << std::endl;
        qb::io::cout() << "Total Orders: " << _total_orders << std::endl;
        qb::io::cout() << "Total Trades: " << _total_trades << std::endl;
        qb::io::cout() << "Order Messages: " << _order_messages << std::endl;
        qb::io::cout() << "Market Data Messages: " << _market_data_messages << std::endl;
        qb::io::cout() << "Elapsed Time: " << std::fixed << std::setprecision(2) << elapsed_seconds << " seconds" << std::endl;

        // Calculate performance metrics
        double orders_per_sec   = _total_orders / elapsed_seconds;
        double trades_per_sec   = _total_trades / elapsed_seconds;
        double messages_per_sec = (_order_messages + _market_data_messages) / elapsed_seconds;

        qb::io::cout() << "Performance: " << std::fixed << std::setprecision(2) << orders_per_sec << " orders/sec, " << trades_per_sec
                       << " trades/sec, " << messages_per_sec << " messages/sec" << std::endl;
        qb::io::cout() << "==========================================" << std::endl;
    }

    void
    schedulePerformanceReport() const {
        // Report every 1 second. Bound to this actor: the last one is cancelled at shutdown
        // instead of firing into a destroyed supervisor.
        spawn([](qb::ScopedCoroContext ctx) -> qb::io::async::task<void> {
            co_await ctx.sleep(std::chrono::seconds(1));
            ctx.template push<StatsTickEvent>();
        });
    }

    void
    stopEverything() const {
        // Send kill events to all actors
        for (const auto &client_id : _client_ids) {
            push<qb::KillEvent>(client_id);
        }

        push<qb::KillEvent>(_market_data_id);
        push<qb::KillEvent>(_order_entry_id);
        push<qb::KillEvent>(_matching_engine_id);

        // Then stop everything that is left, this actor included. `ctx.broadcast` is the
        // coroutine-side equivalent of `Actor::broadcast`; the delay lets the events already in
        // flight drain first.
        spawn([](qb::ScopedCoroContext ctx) -> qb::io::async::task<void> {
            co_await ctx.sleep(std::chrono::milliseconds(500));
            ctx.template broadcast<qb::KillEvent>();
        });
    }
};

/**
 * Main function to set up and run the trading system
 */
int
main() {
    qb::io::cout() << "Initializing multi-core trading system..." << std::endl;

    // Create the main engine with multiple cores
    qb::Main engine;

    // Create market data actor (core 0)
    auto market_data_id = engine.addActor<MarketDataActor>(0);

    // Create matching engine actor (core 1 - dedicated for low latency)
    auto matching_engine_id = engine.addActor<MatchingEngineActor>(1, market_data_id);

    // Create order entry actor (core 2)
    auto order_entry_id = engine.addActor<OrderEntryActor>(2, matching_engine_id);

    // Create client actors (distribute across cores)
    std::vector<qb::ActorId> client_ids;
    for (int i = 0; i < NUM_CLIENTS; ++i) {
        // Distribute clients across cores (except core 1 which is dedicated to matching engine)
        int core_id = CLIENT_CORES[i % CLIENT_CORES.size()];

        // Each client focuses on a specific symbol
        std::string symbol = SYMBOLS[i % NUM_SYMBOLS];

        // Set base price for the symbol
        double base_price = (symbol == "AAPL") ? 175.0 : (symbol == "MSFT") ? 320.0 : 130.0;

        // Create client with unique ID
        std::string client_id = "Client-" + std::to_string(i + 1);
        auto        actor_id  = engine.addActor<ClientActor>(core_id, client_id, order_entry_id, symbol, base_price);

        client_ids.push_back(actor_id);
    }

    // Create supervisor actor (core 0)
    engine.addActor<SupervisorActor>(0, matching_engine_id, order_entry_id, market_data_id, client_ids);

    // Start the system
    engine.start();

    // Wait for the system to complete
    engine.join();

    qb::io::cout() << "Trading system simulation completed" << std::endl;
    return 0;
}