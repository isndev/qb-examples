/**
 * @file example9_trading_system.cpp
 * @brief High-performance trading system simulation using QB actors
 * 
 * This example demonstrates a professional-grade trading system with:
 * - Order Entry component for receiving client orders
 * - Matching Engine for executing trades
 * - Market Data for disseminating price information
 * 
 * The system runs across multiple cores for optimal performance.
 */
#include <qb/actor.h>
#include <qb/main.h>
#include <qb/io/async.h>
#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <unordered_map>
#include <deque>
#include <chrono>
#include <algorithm>
#include <random>
#include <iomanip>
#include <sstream>
#include <atomic>
#include <memory>
#include <mutex>

namespace {
    // Global settings
    const int NUM_CLIENTS = 10;
    const int NUM_SYMBOLS = 3;
    const int SIMULATION_DURATION_SECONDS = 10;
    const int ORDERS_PER_SECOND_PER_CLIENT = 5;
    
    // Performance tracking
    std::atomic<uint64_t> g_total_orders{0};
    std::atomic<uint64_t> g_total_trades{0};
    std::atomic<uint64_t> g_total_order_messages{0};
    std::atomic<uint64_t> g_total_market_data_messages{0};
    
    // System-wide timestamp for simulation time tracking
    std::atomic<uint64_t> g_current_timestamp{0};
    
    // Helper function to get current timestamp in microseconds
    uint64_t getCurrentTimestamp() {
        return static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::high_resolution_clock::now().time_since_epoch()
            ).count()
        );
    }
    
    // Generate a unique order ID
    std::string generateOrderId() {
        static std::atomic<uint64_t> next_id{1};
        std::stringstream ss;
        ss << "ORD-" << std::setw(10) << std::setfill('0') << next_id++;
        return ss.str();
    }
    
    // Available stock symbols
    const std::vector<std::string> SYMBOLS = {"AAPL", "MSFT", "GOOGL"};
    
    // Generate a random price around the base price
    double generatePrice(double base_price, double volatility = 0.02) {
        static std::random_device rd;
        static std::mt19937 gen(rd());
        std::normal_distribution<> d(0, volatility);
        
        // Apply random fluctuation to the base price
        double price = base_price * (1.0 + d(gen));
        // Round to 2 decimal places
        return std::round(price * 100) / 100;
    }
}

// ═════════════════════════════════════════════════════════════════
// DOMAIN MODELS
// ═════════════════════════════════════════════════════════════════

enum class Side {
    BUY,
    SELL
};

std::string sideToString(Side side) {
    return side == Side::BUY ? "BUY" : "SELL";
}

enum class OrderStatus {
    NEW,
    PARTIALLY_FILLED,
    FILLED,
    CANCELED,
    REJECTED
};

std::string statusToString(OrderStatus status) {
    switch (status) {
        case OrderStatus::NEW: return "NEW";
        case OrderStatus::PARTIALLY_FILLED: return "PARTIALLY_FILLED";
        case OrderStatus::FILLED: return "FILLED";
        case OrderStatus::CANCELED: return "CANCELED";
        case OrderStatus::REJECTED: return "REJECTED";
        default: return "UNKNOWN";
    }
}

/**
 * @brief Order model representing a client's trading instruction
 */
struct Order {
    std::string order_id;
    std::string client_id;
    std::string symbol;
    Side side;
    double price;
    int quantity;
    int filled_quantity = 0;
    OrderStatus status = OrderStatus::NEW;
    uint64_t timestamp;
    
    // Constructeur par défaut
    Order() : price(0.0), quantity(0), side(Side::BUY), timestamp(getCurrentTimestamp()), order_id(generateOrderId()) {}
    
    // Constructor for market orders
    Order(const std::string& client, const std::string& sym, Side s, int qty)
        : order_id(generateOrderId()), client_id(client), symbol(sym),
          side(s), quantity(qty), timestamp(getCurrentTimestamp()) {
        // Market orders have zero price (will match at best available)
        price = 0.0;
    }
    
    // Constructor for limit orders
    Order(const std::string& client, const std::string& sym, Side s, double p, int qty)
        : order_id(generateOrderId()), client_id(client), symbol(sym),
          side(s), price(p), quantity(qty), timestamp(getCurrentTimestamp()) {}
    
    // Determine if the order is fully filled
    bool isFullyFilled() const {
        return filled_quantity >= quantity;
    }
    
    // Determine if the order is a market order
    bool isMarketOrder() const {
        return price == 0.0;
    }
    
    // Get the remaining unfilled quantity
    int getRemainingQuantity() const {
        return quantity - filled_quantity;
    }
    
    std::string toString() const {
        std::stringstream ss;
        ss << order_id << " | " << client_id << " | " << symbol << " | " 
           << sideToString(side) << " | " << std::fixed << std::setprecision(2) << price 
           << " | " << filled_quantity << "/" << quantity 
           << " | " << statusToString(status);
        return ss.str();
    }
};

/**
 * @brief Trade model representing a matched pair of orders
 */
struct Trade {
    std::string trade_id;
    std::string buy_order_id;
    std::string sell_order_id;
    std::string symbol;
    double price;
    int quantity;
    uint64_t timestamp;
    
    Trade(const std::string& buy_id, const std::string& sell_id, 
          const std::string& sym, double p, int qty)
        : buy_order_id(buy_id), sell_order_id(sell_id), symbol(sym),
          price(p), quantity(qty), timestamp(getCurrentTimestamp()) {
        
        // Generate a unique trade ID
        static std::atomic<uint64_t> next_trade_id{1};
        std::stringstream ss;
        ss << "TRD-" << std::setw(10) << std::setfill('0') << next_trade_id++;
        trade_id = ss.str();
    }
    
    std::string toString() const {
        std::stringstream ss;
        ss << trade_id << " | " << symbol << " | " << std::fixed 
           << std::setprecision(2) << price << " | " << quantity;
        return ss.str();
    }
};

/**
 * @brief Price level in the order book
 */
struct PriceLevel {
    double price;
    std::deque<std::shared_ptr<Order>> orders;
    int total_quantity = 0;
    
    PriceLevel() : price(0.0) {}  // Constructeur par défaut
    explicit PriceLevel(double p) : price(p) {}
    
    int getTotalQuantity() const {
        int total = 0;
        for (const auto& order : orders) {
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
    std::string _symbol;
    std::map<double, PriceLevel, std::greater<double>> _bids; // Highest first
    std::map<double, PriceLevel> _asks; // Lowest first
    std::unordered_map<std::string, std::shared_ptr<Order>> _orders_by_id;
    
    // Last trade price and timestamp
    double _last_price = 0.0;
    uint64_t _last_trade_time = 0;
    
    // Order book statistics
    int _total_volume = 0;
    
    // Market price info
    double _open_price = 0.0;
    double _high_price = 0.0;
    double _low_price = std::numeric_limits<double>::max();
    
public:
    OrderBook() : _symbol("") {}  // Constructeur par défaut
    explicit OrderBook(const std::string& symbol) : _symbol(symbol) {}
    
    // Get basic book info
    std::string getSymbol() const { return _symbol; }
    double getLastPrice() const { return _last_price; }
    int getTotalVolume() const { return _total_volume; }
    
    // Get best bid and ask prices
    double getBestBidPrice() const {
        return _bids.empty() ? 0.0 : _bids.begin()->first;
    }
    
    double getBestAskPrice() const {
        return _asks.empty() ? 0.0 : _asks.begin()->first;
    }
    
    // Get total volume at best bid and ask
    int getBestBidVolume() const {
        return _bids.empty() ? 0 : _bids.begin()->second.getTotalQuantity();
    }
    
    int getBestAskVolume() const {
        return _asks.empty() ? 0 : _asks.begin()->second.getTotalQuantity();
    }
    
    // Add an order to the book
    void addOrder(const std::shared_ptr<Order>& order) {
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
    void removeOrder(const std::string& order_id) {
        auto order_it = _orders_by_id.find(order_id);
        if (order_it == _orders_by_id.end()) {
            return; // Order not found
        }
        
        auto order = order_it->second;
        
        // Remove from the price level
        if (order->side == Side::BUY) {
            auto price_it = _bids.find(order->price);
            if (price_it != _bids.end()) {
                auto& orders = price_it->second.orders;
                orders.erase(std::remove_if(orders.begin(), orders.end(),
                    [&order_id](const std::shared_ptr<Order>& o) {
                        return o->order_id == order_id;
                    }), orders.end());
                
                // Remove price level if empty
                if (orders.empty()) {
                    _bids.erase(price_it);
                }
            }
        } else {
            auto price_it = _asks.find(order->price);
            if (price_it != _asks.end()) {
                auto& orders = price_it->second.orders;
                orders.erase(std::remove_if(orders.begin(), orders.end(),
                    [&order_id](const std::shared_ptr<Order>& o) {
                        return o->order_id == order_id;
                    }), orders.end());
                
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
    std::vector<Trade> matchOrders(const std::shared_ptr<Order>& incoming_order) {
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
    void matchBuyOrder(const std::shared_ptr<Order>& buy_order, std::vector<Trade>& trades) {
        // For market orders, use the best available price
        double max_price = buy_order->isMarketOrder() ? 
            std::numeric_limits<double>::max() : buy_order->price;
        
        // Continue matching as long as there are matching asks and the order has remaining quantity
        while (!_asks.empty() && buy_order->getRemainingQuantity() > 0) {
            // Get the best (lowest) ask price
            auto ask_it = _asks.begin();
            double ask_price = ask_it->first;
            
            // Check if we can match at this price
            if (ask_price > max_price) {
                break; // No matching ask prices
            }
            
            // Get the orders at this price level
            auto& ask_level = ask_it->second;
            auto& ask_orders = ask_level.orders;
            
            // Match with orders at this price level
            while (!ask_orders.empty() && buy_order->getRemainingQuantity() > 0) {
                auto& sell_order = ask_orders.front();
                
                // Calculate the matched quantity
                int match_qty = std::min(buy_order->getRemainingQuantity(), 
                                        sell_order->getRemainingQuantity());
                
                // Update order quantities
                buy_order->filled_quantity += match_qty;
                sell_order->filled_quantity += match_qty;
                
                // Create a trade
                trades.emplace_back(buy_order->order_id, sell_order->order_id, 
                                   _symbol, ask_price, match_qty);
                
                // Update market stats
                _total_volume += match_qty;
                _last_price = ask_price;
                _last_trade_time = getCurrentTimestamp();
                
                if (_high_price < ask_price) _high_price = ask_price;
                if (_low_price > ask_price) _low_price = ask_price;
                if (_open_price == 0) _open_price = ask_price;
                
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
    void matchSellOrder(const std::shared_ptr<Order>& sell_order, std::vector<Trade>& trades) {
        // For market orders, use any bid price
        double min_price = sell_order->isMarketOrder() ? 0.0 : sell_order->price;
        
        // Continue matching as long as there are matching bids and the order has remaining quantity
        while (!_bids.empty() && sell_order->getRemainingQuantity() > 0) {
            // Get the best (highest) bid price
            auto bid_it = _bids.begin();
            double bid_price = bid_it->first;
            
            // Check if we can match at this price
            if (bid_price < min_price) {
                break; // No matching bid prices
            }
            
            // Get the orders at this price level
            auto& bid_level = bid_it->second;
            auto& bid_orders = bid_level.orders;
            
            // Match with orders at this price level
            while (!bid_orders.empty() && sell_order->getRemainingQuantity() > 0) {
                auto& buy_order = bid_orders.front();
                
                // Calculate the matched quantity
                int match_qty = std::min(sell_order->getRemainingQuantity(), 
                                        buy_order->getRemainingQuantity());
                
                // Update order quantities
                sell_order->filled_quantity += match_qty;
                buy_order->filled_quantity += match_qty;
                
                // Create a trade
                trades.emplace_back(buy_order->order_id, sell_order->order_id, 
                                   _symbol, bid_price, match_qty);
                
                // Update market stats
                _total_volume += match_qty;
                _last_price = bid_price;
                _last_trade_time = getCurrentTimestamp();
                
                if (_high_price < bid_price) _high_price = bid_price;
                if (_low_price > bid_price) _low_price = bid_price;
                if (_open_price == 0) _open_price = bid_price;
                
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
    
    explicit OrderMessage(const std::shared_ptr<Order>& o) : order(o) {}
};

// New order submission
struct NewOrderMessage : public OrderMessage {
    explicit NewOrderMessage(const std::shared_ptr<Order>& o) : OrderMessage(o) {}
};

// Order execution notification
struct ExecutionMessage : public qb::Event {
    std::shared_ptr<Order> order;
    std::string trade_id;
    double execution_price;
    int execution_quantity;

    ExecutionMessage(const std::shared_ptr<Order>& o, const std::string& tid, 
                    double price, int quantity)
        : order(o), trade_id(tid), execution_price(price), execution_quantity(quantity) {}
    
    // Nouveau constructeur pour accepter des chaînes
    ExecutionMessage(const std::string& client_id, const std::string& tid, 
                    double price, int quantity)
        : trade_id(tid), execution_price(price), execution_quantity(quantity) {
        order = std::make_shared<Order>();
        order->client_id = client_id;
    }
};

// Order cancellation request
struct CancelOrderMessage : public OrderMessage {
    explicit CancelOrderMessage(const std::shared_ptr<Order>& o) : OrderMessage(o) {}
};

// Order status update
struct OrderStatusMessage : public OrderMessage {
    explicit OrderStatusMessage(const std::shared_ptr<Order>& o) : OrderMessage(o) {}
};

// Market data update with new prices
struct MarketDataMessage : public qb::Event {
    std::string symbol;
    double bid_price;
    int bid_size;
    double ask_price;
    int ask_size;
    double last_price;
    int last_size;

    // Constructeur par défaut
    MarketDataMessage() : bid_price(0.0), bid_size(0), ask_price(0.0), ask_size(0), last_price(0.0), last_size(0) {}
    
    MarketDataMessage(const std::string& sym, double bp, int bs, double ap, int as,
                     double lp, int ls)
        : symbol(sym), bid_price(bp), bid_size(bs), ask_price(ap), ask_size(as),
          last_price(lp), last_size(ls) {}
};

// Trade notification message
struct TradeMessage : public qb::Event {
    Trade trade;
    
    explicit TradeMessage(const Trade& t) : trade(t) {}
};

// Performance statistics message
struct StatisticsMessage : public qb::Event {
    uint64_t total_orders;
    uint64_t total_trades;
    uint64_t order_messages;
    uint64_t market_data_messages;
    double elapsed_seconds;
    
    StatisticsMessage(uint64_t orders, uint64_t trades, uint64_t order_msgs, 
                     uint64_t md_msgs, double seconds)
        : total_orders(orders), total_trades(trades), order_messages(order_msgs),
          market_data_messages(md_msgs), elapsed_seconds(seconds) {}
};

// Initialization message
struct InitializeMessage : public qb::Event {
    // Add initialization parameters if needed
};

// ═════════════════════════════════════════════════════════════════
// TRADING SYSTEM ACTORS
// ═════════════════════════════════════════════════════════════════

/**
 * @brief Client actor that generates orders
 */
class ClientActor : public qb::Actor {
private:
    std::string _client_id;
    qb::ActorId _order_entry_id;
    std::string _preferred_symbol;
    double _base_price;
    std::mt19937 _rng;
    bool _is_active = false;
    
public:
    ClientActor(const std::string& client_id, qb::ActorId order_entry_id, 
               const std::string& symbol, double base_price) 
        : _client_id(client_id), _order_entry_id(order_entry_id),
          _preferred_symbol(symbol), _base_price(base_price) {
        
        // Initialize random number generator
        std::random_device rd;
        _rng = std::mt19937(rd());
        
        // Register for messages
        registerEvent<ExecutionMessage>(*this);
        registerEvent<OrderStatusMessage>(*this);
        registerEvent<InitializeMessage>(*this);
        registerEvent<qb::KillEvent>(*this);
    }
    
    bool onInit() override {
        std::cout << "ClientActor " << _client_id << " initialized with ID: " << id() << std::endl;
        return true;
    }
    
    void on(InitializeMessage&) {
        _is_active = true;
        scheduleNextOrder();
    }
    
    void on(ExecutionMessage& msg) {
        // Handle execution report
        std::cout << "Client " << _client_id << " received execution: " 
                << msg.trade_id << " for " << msg.execution_quantity 
                << " at $" << msg.execution_price << std::endl;
    }
    
    void on(OrderStatusMessage& msg) {
        // Handle order status update
        std::cout << "Client " << _client_id << " order status: " 
                << msg.order->order_id << " is now " 
                << statusToString(msg.order->status) << std::endl;
    }
    
    void on(qb::KillEvent&) {
        _is_active = false;
        kill();
    }
    
private:
    void scheduleNextOrder() {
        if (!_is_active) return;
        
        // Generate a random order at random intervals
        std::uniform_real_distribution<> delay_dist(0.1, 0.5);  // 100ms to 500ms delay
        
        // Schedule the next order
        qb::io::async::callback([this]() {
            if (!_is_active) return;
            generateRandomOrder();
            scheduleNextOrder();  // Schedule the next order
        }, delay_dist(_rng));
    }
    
    void generateRandomOrder() {
        // Random order parameters
        std::uniform_int_distribution<> side_dist(0, 1);
        std::uniform_int_distribution<> qty_dist(1, 100);
        std::uniform_real_distribution<> price_dist(0.95, 1.05);
        std::uniform_int_distribution<> symbol_idx_dist(0, NUM_SYMBOLS - 1);
        
        // Decide the symbol (with preference for the assigned one)
        std::string symbol = _preferred_symbol;
        if (symbol_idx_dist(_rng) == 0) {  // 1/3 chance to trade a different symbol
            symbol = SYMBOLS[symbol_idx_dist(_rng)];
        }
        
        // Decide side, quantity and price
        Side side = side_dist(_rng) ? Side::BUY : Side::SELL;
        int quantity = qty_dist(_rng);
        double price = generatePrice(_base_price, 0.02) * price_dist(_rng);
        price = std::round(price * 100) / 100;  // Round to 2 decimal places
        
        // Create the order
        auto order = std::make_shared<Order>(_client_id, symbol, side, price, quantity);
        
        // Send to order entry
        push<NewOrderMessage>(_order_entry_id, order);
        
        // Update statistics
        g_total_orders++;
    }
};

/**
 * @brief Order Entry actor that validates and routes orders
 */
class OrderEntryActor : public qb::Actor {
private:
    qb::ActorId _matching_engine_id;
    std::unordered_map<std::string, std::shared_ptr<Order>> _active_orders;
    
public:
    explicit OrderEntryActor(qb::ActorId matching_engine_id) 
        : _matching_engine_id(matching_engine_id) {
        
        // Register for messages
        registerEvent<NewOrderMessage>(*this);
        registerEvent<CancelOrderMessage>(*this);
        registerEvent<ExecutionMessage>(*this);
        registerEvent<qb::KillEvent>(*this);
    }
    
    bool onInit() override {
        std::cout << "OrderEntryActor initialized with ID: " << id() << std::endl;
        return true;
    }
    
    void on(NewOrderMessage& msg) {
        g_total_order_messages++;
        
        auto order = msg.order;
        
        // Validate the order
        if (order->quantity <= 0) {
            order->status = OrderStatus::REJECTED;
            push<OrderStatusMessage>(msg.getSource(), order);
            return;
        }
        
        // Track the order
        _active_orders[order->order_id] = order;
        
        // Send acknowledgment to client
        push<OrderStatusMessage>(msg.getSource(), order);
        
        // Forward to matching engine
        push<NewOrderMessage>(_matching_engine_id, order);
    }
    
    void on(CancelOrderMessage& msg) {
        g_total_order_messages++;
        
        auto order_id = msg.order->order_id;
        
        // Check if the order exists and is active
        if (_active_orders.find(order_id) != _active_orders.end()) {
            // Forward to matching engine
            push<CancelOrderMessage>(_matching_engine_id, msg.order);
        } else {
            // Order not found or already completed
            auto order = msg.order;
            order->status = OrderStatus::REJECTED;
            push<OrderStatusMessage>(msg.getSource(), order);
        }
    }
    
    void on(ExecutionMessage& msg) {
        // Update order status
        auto order_id = msg.order->order_id;
        if (_active_orders.find(order_id) != _active_orders.end()) {
            // Order is active - update it
            _active_orders[order_id] = msg.order;
            
            // If order is filled or canceled, remove from active orders
            if (msg.order->status == OrderStatus::FILLED || 
                msg.order->status == OrderStatus::CANCELED) {
                _active_orders.erase(order_id);
            }
        }
        
        // Forward execution to client
        qb::ActorId client_actor_id;
        push<ExecutionMessage>(client_actor_id, msg.order, msg.trade_id, 
                              msg.execution_price, msg.execution_quantity);
    }
    
    void on(qb::KillEvent&) {
        kill();
    }
};

/**
 * @brief Matching Engine actor that matches orders and produces trades
 */
class MatchingEngineActor : public qb::Actor {
private:
    std::unordered_map<std::string, OrderBook> _order_books;
    qb::ActorId _market_data_id;
    
public:
    explicit MatchingEngineActor(qb::ActorId market_data_id) 
        : _market_data_id(market_data_id) {
        
        // Register for messages
        registerEvent<NewOrderMessage>(*this);
        registerEvent<CancelOrderMessage>(*this);
        registerEvent<InitializeMessage>(*this);
        registerEvent<qb::KillEvent>(*this);
    }
    
    bool onInit() override {
        std::cout << "MatchingEngineActor initialized with ID: " << id() << std::endl;
        return true;
    }
    
    void on(InitializeMessage&) {
        // Initialize order books for all symbols
        for (const auto& symbol : SYMBOLS) {
            _order_books.emplace(symbol, OrderBook(symbol));
            
            // Set initial market data
            double base_price = (symbol == "AAPL") ? 175.0 : 
                               (symbol == "MSFT") ? 320.0 : 130.0;
            
            publishMarketData(symbol, base_price, 0, base_price, 0, base_price, 0);
        }
    }
    
    void on(NewOrderMessage& msg) {
        auto order = msg.order;
        
        // Check if we have an order book for this symbol
        if (_order_books.find(order->symbol) == _order_books.end()) {
            _order_books.emplace(order->symbol, OrderBook(order->symbol));
        }
        
        // Get the order book
        auto& order_book = _order_books[order->symbol];
        
        // Try to match the order
        auto trades = order_book.matchOrders(order);
        
        // Process resulting trades
        for (const auto& trade : trades) {
            // Increment trade counter
            g_total_trades++;
            
            // Notify clients of execution
            executeTrade(trade);
            
            // Send trade to market data
            push<TradeMessage>(_market_data_id, trade);
        }
        
        // Update market data
        publishMarketDataForSymbol(order->symbol);
    }
    
    void on(CancelOrderMessage& msg) {
        auto order = msg.order;
        
        // Check if we have an order book for this symbol
        if (_order_books.find(order->symbol) == _order_books.end()) {
            return;  // Symbol not found
        }
        
        // Get the order book
        auto& order_book = _order_books[order->symbol];
        
        // Remove order from the book
        order_book.removeOrder(order->order_id);
        
        // Update the order status
        order->status = OrderStatus::CANCELED;
        
        // Notify client
        qb::ActorId client_actor_id;
        push<OrderStatusMessage>(client_actor_id, order);
        
        // Update market data
        publishMarketDataForSymbol(order->symbol);
    }
    
    void on(qb::KillEvent&) {
        kill();
    }
    
private:
    // Process a trade
    void executeTrade(const Trade& trade) {
        // For the buy side
        auto& buy_book = _order_books[trade.symbol];
        
        // For the sell side
        auto& sell_book = _order_books[trade.symbol];
        
        // Create and send execution notifications
        auto buyer = getSenderFromOrderId(trade.buy_order_id);
        if (buyer) {
            push<ExecutionMessage>(buyer, trade.buy_order_id, trade.trade_id, 
                                  trade.price, trade.quantity);
        }
        
        auto seller = getSenderFromOrderId(trade.sell_order_id);
        if (seller) {
            push<ExecutionMessage>(seller, trade.sell_order_id, trade.trade_id, 
                                  trade.price, trade.quantity);
        }
    }
    
    // Publish market data for a specific symbol
    void publishMarketDataForSymbol(const std::string& symbol) {
        // Check if we have an order book for this symbol
        if (_order_books.find(symbol) == _order_books.end()) {
            return;  // Symbol not found
        }
        
        // Get the order book
        const auto& order_book = _order_books[symbol];
        
        // Get market data
        double bid_price = order_book.getBestBidPrice();
        int bid_size = order_book.getBestBidVolume();
        double ask_price = order_book.getBestAskPrice();
        int ask_size = order_book.getBestAskVolume();
        double last_price = order_book.getLastPrice();
        
        // Publish market data
        publishMarketData(symbol, bid_price, bid_size, ask_price, ask_size, last_price, 0);
    }
    
    // Helper to publish market data
    void publishMarketData(const std::string& symbol, double bid_price, int bid_size,
                          double ask_price, int ask_size, double last_price, int last_size) {
        g_total_market_data_messages++;
        
        push<MarketDataMessage>(
            _market_data_id,
            symbol, bid_price, bid_size, ask_price, ask_size, last_price, last_size
        );
    }
    
    // Get sender ID from order ID (placeholder implementation)
    qb::ActorId getSenderFromOrderId(const std::string& order_id) {
        // In a real system, we would maintain a mapping of order IDs to sender IDs
        // For simplicity, we'll just return an empty ActorId
        return qb::ActorId();
    }
};

/**
 * @brief Market Data actor that disseminates price information
 */
class MarketDataActor : public qb::Actor {
private:
    std::map<std::string, MarketDataMessage> _latest_market_data;
    std::vector<qb::ActorId> _subscribers;
    
public:
    MarketDataActor() {
        // Register for messages
        registerEvent<MarketDataMessage>(*this);
        registerEvent<TradeMessage>(*this);
        registerEvent<qb::KillEvent>(*this);
    }
    
    bool onInit() override {
        std::cout << "MarketDataActor initialized with ID: " << id() << std::endl;
        return true;
    }
    
    void on(MarketDataMessage& msg) {
        // Store the latest market data
        _latest_market_data[msg.symbol] = msg;
        
        // Log the market data
        std::cout << "Market Data: " << msg.symbol 
                 << " Bid: " << std::fixed << std::setprecision(2) << msg.bid_price 
                 << " x " << msg.bid_size
                 << " Ask: " << msg.ask_price 
                 << " x " << msg.ask_size
                 << " Last: " << msg.last_price << std::endl;
        
        // Broadcast to subscribers
        for (const auto& subscriber_id : _subscribers) {
            push<MarketDataMessage>(subscriber_id, msg.symbol, msg.bid_price, msg.bid_size,
                                   msg.ask_price, msg.ask_size, msg.last_price, msg.last_size);
        }
    }
    
    void on(TradeMessage& msg) {
        // Log the trade
        std::cout << "Trade: " << msg.trade.toString() << std::endl;
    }
    
    void on(qb::KillEvent&) {
        kill();
    }
    
    // Add a subscriber
    void addSubscriber(qb::ActorId subscriber_id) {
        _subscribers.push_back(subscriber_id);
    }
};

/**
 * @brief Supervisor actor that manages the trading system
 */
class SupervisorActor : public qb::Actor {
private:
    qb::ActorId _matching_engine_id;
    qb::ActorId _order_entry_id;
    qb::ActorId _market_data_id;
    std::vector<qb::ActorId> _client_ids;
    
    uint64_t _start_time;
    bool _is_active = false;
    
public:
    SupervisorActor(qb::ActorId matching_engine, qb::ActorId order_entry, 
                   qb::ActorId market_data, const std::vector<qb::ActorId>& clients)
        : _matching_engine_id(matching_engine), _order_entry_id(order_entry),
          _market_data_id(market_data), _client_ids(clients) {
        
        // Register for messages
        registerEvent<StatisticsMessage>(*this);
        registerEvent<qb::KillEvent>(*this);
        registerEvent<InitializeMessage>(*this);
    }
    
    bool onInit() override {
        std::cout << "SupervisorActor initialized with ID: " << id() << std::endl;

        push<InitializeMessage>(id());
        return true;
    }
    
    void on(InitializeMessage&) {
        _is_active = true;
        _start_time = getCurrentTimestamp();
        
        std::cout << "Trading system starting..." << std::endl;
        
        // Initialize the matching engine
        push<InitializeMessage>(_matching_engine_id);
        
        // Initialize all clients
        for (const auto& client_id : _client_ids) {
            push<InitializeMessage>(client_id);
        }
        
        // Schedule performance report
        schedulePerformanceReport();
        
        // Schedule system shutdown
        qb::io::async::callback([this]() {
            if (_is_active) {
                shutdownSystem();
            }
        }, SIMULATION_DURATION_SECONDS);
    }
    
    void on(StatisticsMessage& msg) {
        // Log the statistics
        std::cout << "\n======= TRADING SYSTEM STATISTICS =======" << std::endl;
        std::cout << "Total Orders: " << msg.total_orders << std::endl;
        std::cout << "Total Trades: " << msg.total_trades << std::endl;
        std::cout << "Order Messages: " << msg.order_messages << std::endl;
        std::cout << "Market Data Messages: " << msg.market_data_messages << std::endl;
        std::cout << "Elapsed Time: " << std::fixed << std::setprecision(2) 
                 << msg.elapsed_seconds << " seconds" << std::endl;
        
        // Calculate performance metrics
        double orders_per_sec = msg.total_orders / msg.elapsed_seconds;
        double trades_per_sec = msg.total_trades / msg.elapsed_seconds;
        double messages_per_sec = (msg.order_messages + msg.market_data_messages) / msg.elapsed_seconds;
        
        std::cout << "Performance: " << std::fixed << std::setprecision(2) 
                 << orders_per_sec << " orders/sec, " 
                 << trades_per_sec << " trades/sec, " 
                 << messages_per_sec << " messages/sec" << std::endl;
        std::cout << "==========================================" << std::endl;
    }
    
    void on(qb::KillEvent&) {
        _is_active = false;
        kill();
    }
    
private:
    void schedulePerformanceReport() {
        if (!_is_active) return;
        
        // Schedule periodic performance reports
        qb::io::async::callback([this]() {
            if (!_is_active) return;
            
            // Calculate elapsed time
            uint64_t current_time = getCurrentTimestamp();
            double elapsed_seconds = (current_time - _start_time) / 1000000.0;
            
            // Send statistics message to self
            push<StatisticsMessage>(
                id(),
                g_total_orders.load(),
                g_total_trades.load(),
                g_total_order_messages.load(),
                g_total_market_data_messages.load(),
                elapsed_seconds
            );
            
            // Schedule next report
            schedulePerformanceReport();
        }, 1.0); // Report every 1 second
    }
    
    void shutdownSystem() {
        std::cout << "\nTrading system shutting down..." << std::endl;
        
        // Calculate final statistics
        uint64_t current_time = getCurrentTimestamp();
        double elapsed_seconds = (current_time - _start_time) / 1000000.0;
        
        push<StatisticsMessage>(
            id(),
            g_total_orders.load(),
            g_total_trades.load(),
            g_total_order_messages.load(),
            g_total_market_data_messages.load(),
            elapsed_seconds
        );
        
        // Send kill events to all actors
        for (const auto& client_id : _client_ids) {
            push<qb::KillEvent>(client_id);
        }
        
        push<qb::KillEvent>(_market_data_id);
        push<qb::KillEvent>(_order_entry_id);
        push<qb::KillEvent>(_matching_engine_id);
        
        // Finally, kill self after a short delay
        qb::io::async::callback([this]() {
            broadcast<qb::KillEvent>();
        }, 0.5);
    }
};

/**
 * Main function to set up and run the trading system
 */
int main() {
    std::cout << "Initializing multi-core trading system..." << std::endl;
    
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
        int core_id = (i % 3 == 1) ? 0 : (i % 3 + 1);
        
        // Each client focuses on a specific symbol
        std::string symbol = SYMBOLS[i % NUM_SYMBOLS];
        
        // Set base price for the symbol
        double base_price = (symbol == "AAPL") ? 175.0 : 
                           (symbol == "MSFT") ? 320.0 : 130.0;
        
        // Create client with unique ID
        std::string client_id = "Client-" + std::to_string(i + 1);
        auto actor_id = engine.addActor<ClientActor>(
            core_id, client_id, order_entry_id, symbol, base_price
        );
        
        client_ids.push_back(actor_id);
    }
    
    // Create supervisor actor (core 0)
    auto supervisor_id = engine.addActor<SupervisorActor>(
        0, matching_engine_id, order_entry_id, market_data_id, client_ids
    );
    
    // Start the system
    engine.start();
    
    // Wait for the system to complete
    engine.join();
    
    std::cout << "Trading system simulation completed" << std::endl;
    return 0;
} 