/**
 * @file example7_pub_sub.cpp
 * @brief Example of a publish/subscribe pattern implementation with qb-core
 * 
 * This example demonstrates how to implement a publish/subscribe (pub/sub) pattern
 * using qb-core actors. The pub/sub pattern allows for loose coupling between
 * message publishers and subscribers, enabling flexible broadcast communication.
 */

#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <memory>
#include <chrono>
#include <random>
#include <functional>
#include <thread>
#include <tuple>
#include <iomanip>
#include <qb/actor.h>
#include <qb/main.h>

using namespace qb;

// Message classes for the pub/sub system
class SubscribeMessage : public Event {
public:
    enum class Topic {
        WEATHER,
        NEWS,
        SPORTS,
        STOCK_PRICES,
        SYSTEM_STATUS
    };

    Topic topic;
    ActorId source_id;

    SubscribeMessage(Topic t, ActorId source = ActorId())
        : topic(t), source_id(source) {}
};

class UnsubscribeMessage : public Event {
public:
    SubscribeMessage::Topic topic;
    ActorId source_id;

    UnsubscribeMessage(SubscribeMessage::Topic t, ActorId source = ActorId())
        : topic(t), source_id(source) {}
};

class PublishMessage : public Event {
public:
    SubscribeMessage::Topic topic;
    std::string content;
    std::string publisher;
    uint64_t timestamp;

    PublishMessage(SubscribeMessage::Topic t, std::string content,
                  std::string publisher, uint64_t timestamp)
        : topic(t), content(std::move(content)), 
          publisher(std::move(publisher)), timestamp(timestamp) {}
};

class MessageReceivedMessage : public Event {
public:
    SubscribeMessage::Topic topic;
    std::string content;
    std::string publisher;
    uint64_t timestamp;

    MessageReceivedMessage(SubscribeMessage::Topic t, std::string content,
                          std::string publisher, uint64_t timestamp)
        : topic(t), content(std::move(content)), 
          publisher(std::move(publisher)), timestamp(timestamp) {}
};

class PrintHistoryMessage : public Event {
public:
    PrintHistoryMessage() = default;
};

class PrintStatisticsMessage : public Event {
public:
    PrintStatisticsMessage() = default;
};

class ListTopicsMessage : public Event {
    // Empty, just a request for topics
};

class TopicListMessage : public Event {
public:
    using Topic = SubscribeMessage::Topic;
    std::vector<Topic> available_topics;
    std::map<Topic, int> subscribers_count;
};

// Demo manager specific messages
class InitialSubscriptionsMessage : public Event {
public:
    ActorId subscriber_id;
    std::vector<SubscribeMessage::Topic> topics;
    
    InitialSubscriptionsMessage(ActorId id, const std::vector<SubscribeMessage::Topic>& t)
        : subscriber_id(id), topics(t) {}
};

// Utility function to get current timestamp in milliseconds
uint64_t getCurrentTimestamp() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count()
    );
}

// Step message for the demo sequence
class StepMessage : public Event {
public:
    int step;
    
    explicit StepMessage(int step_num) : step(step_num) {}
};

// Helper function to convert Topic enum to string
std::string topicToString(SubscribeMessage::Topic topic) {
    switch (topic) {
        case SubscribeMessage::Topic::WEATHER: return "WEATHER";
        case SubscribeMessage::Topic::STOCK_PRICES: return "STOCK_PRICES";
        case SubscribeMessage::Topic::NEWS: return "NEWS";
        case SubscribeMessage::Topic::SPORTS: return "SPORTS";
        case SubscribeMessage::Topic::SYSTEM_STATUS: return "SYSTEM_STATUS";
        default: return "UNKNOWN";
    }
}

// Helper function to pretty-print a set of topics
std::string formatTopicSet(const std::set<SubscribeMessage::Topic>& topics) {
    std::string result = "[";
    bool first = true;
    for (const auto& topic : topics) {
        if (!first) {
            result += ", ";
        }
        result += topicToString(topic);
        first = false;
    }
    result += "]";
    return result;
}

/**
 * @brief Broker Actor
 * 
 * Central hub that manages topic subscriptions and distributes messages to subscribers.
 */
class BrokerActor : public Actor {
private:
    using Topic = SubscribeMessage::Topic;
    
    // Mapping of topics to subscriber actors
    std::map<Topic, std::set<ActorId>> _topic_subscribers;
    
    // Statistics
    std::map<Topic, int> _messages_per_topic;
    std::map<ActorId, int> _messages_per_subscriber;
    
    // Convert topic enum to string for display
    std::string topicToString(Topic topic) const {
        switch (topic) {
            case Topic::WEATHER: return "WEATHER";
            case Topic::NEWS: return "NEWS";
            case Topic::SPORTS: return "SPORTS";
            case Topic::STOCK_PRICES: return "STOCK_PRICES";
            case Topic::SYSTEM_STATUS: return "SYSTEM_STATUS";
            default: return "UNKNOWN";
        }
    }

public:
    BrokerActor() {
        registerEvent<SubscribeMessage>(*this);
        registerEvent<UnsubscribeMessage>(*this);
        registerEvent<PublishMessage>(*this);
        registerEvent<PrintStatisticsMessage>(*this);
    }
    
    bool onInit() override {
        std::cout << "BrokerActor initialized with ID: " << id() << std::endl;
        return true;
    }
    
    void on(SubscribeMessage& msg) {
        // Add the subscriber to the topic
        _topic_subscribers[msg.topic].insert(msg.source_id);
        
        std::cout << "[Broker] New subscription: " 
                  << "Actor " << msg.source_id 
                  << " subscribed to topic " << topicToString(msg.topic) << std::endl;
    }
    
    void on(UnsubscribeMessage& msg) {
        // Remove the subscriber from the topic
        auto it = _topic_subscribers.find(msg.topic);
        if (it != _topic_subscribers.end()) {
            it->second.erase(msg.source_id);
            
            std::cout << "[Broker] Subscription removed: " 
                      << "Actor " << msg.source_id
                      << " unsubscribed from topic " << topicToString(msg.topic) << std::endl;
        }
    }
    
    void on(PublishMessage& msg) {
        // Increment message count for this topic
        _messages_per_topic[msg.topic]++;
        
        std::cout << "[Broker] Message published to topic " << topicToString(msg.topic) 
                  << " by " << msg.publisher 
                  << " with content: \"" << msg.content << "\"" << std::endl;
        
        // Forward the message to all subscribers of this topic
        auto it = _topic_subscribers.find(msg.topic);
        if (it != _topic_subscribers.end()) {
            std::cout << "[Broker] Forwarding message to " << it->second.size() << " subscribers" << std::endl;
            for (const auto& subscriber_id : it->second) {
                // Send the message to the subscriber
                push<MessageReceivedMessage>(
                    subscriber_id,
                    msg.topic,
                    msg.content,
                    msg.publisher,
                    msg.timestamp
                );
                
                // Update statistics
                _messages_per_subscriber[subscriber_id]++;
            }
        } else {
            std::cout << "[Broker] No subscribers for topic " << topicToString(msg.topic) << std::endl;
        }
    }
    
    void on(PrintStatisticsMessage& msg) {
        std::cout << "\n--- BROKER STATISTICS ---" << std::endl;
        
        // Print messages per topic
        std::cout << "Messages per topic:" << std::endl;
        for (const auto& [topic, count] : _messages_per_topic) {
            std::cout << "  " << topicToString(topic) << ": " << count << " messages" << std::endl;
        }
        
        // Print messages per subscriber
        std::cout << "Messages per subscriber:" << std::endl;
        for (const auto& [subscriber_id, count] : _messages_per_subscriber) {
            std::cout << "  Actor " << subscriber_id << ": " 
                      << count << " messages received" << std::endl;
        }
        
        // Print current subscription count
        std::cout << "Current subscribers per topic:" << std::endl;
        for (const auto& [topic, subscribers] : _topic_subscribers) {
            std::cout << "  " << topicToString(topic) << ": " 
                      << subscribers.size() << " subscribers" << std::endl;
        }
        
        std::cout << "------------------------" << std::endl;
    }
};

/**
 * @brief Publisher Actor
 * 
 * Publishes messages to specific topics on a schedule or when triggered.
 */
class MessagePublisher : public Actor {
private:
    using Topic = SubscribeMessage::Topic;
    std::string _name;
    ActorId _broker_id;
    
    struct TopicContent {
        std::vector<std::string> messages;
        size_t next_index = 0;
        
        std::string getNextMessage() {
            if (messages.empty()) return "No content available";
            std::string msg = messages[next_index];
            next_index = (next_index + 1) % messages.size();
            return msg;
        }
    };
    
    std::map<Topic, TopicContent> _topic_content;
    
    void setupDefaultContent() {
        // Weather updates
        _topic_content[Topic::WEATHER].messages = {
            "Sunny, 25°C",
            "Cloudy with chance of rain, 18°C",
            "Heavy thunderstorms expected, 22°C",
            "Clear skies, 20°C",
            "Heatwave warning, 38°C"
        };
        
        // News
        _topic_content[Topic::NEWS].messages = {
            "New breakthrough in quantum computing announced",
            "Global climate summit ends with new agreements",
            "Stock markets hit record high",
            "Space mission successfully lands on Mars",
            "Major sports team wins championship"
        };
        
        // Sports
        _topic_content[Topic::SPORTS].messages = {
            "Team A wins against Team B with score 3-1",
            "Marathon record broken by athlete from Kenya",
            "Tennis tournament final scheduled for Sunday",
            "Swimmer breaks Olympic record",
            "Basketball player signs record contract"
        };
        
        // Stock prices
        _topic_content[Topic::STOCK_PRICES].messages = {
            "AAPL: $150.25 (+1.2%)",
            "MSFT: $305.10 (-0.5%)",
            "GOOG: $2,750.15 (+2.1%)",
            "AMZN: $3,400.50 (-1.7%)",
            "TSLA: $800.75 (+5.3%)"
        };
        
        // System status
        _topic_content[Topic::SYSTEM_STATUS].messages = {
            "All systems operational",
            "Database maintenance in progress",
            "High CPU usage detected",
            "Network latency increased",
            "Memory usage at 85% capacity"
        };
    }
    
    // Publish a message to the specified topic
    void publishMessageToTopic(Topic topic) {
        if (_topic_content.find(topic) == _topic_content.end()) {
            return;
        }
        
        std::string content = _topic_content[topic].getNextMessage();
        uint64_t timestamp = getCurrentTimestamp();
        
        push<PublishMessage>(
            _broker_id,
            topic,
            content,
            _name,
            timestamp
        );
    }
    
public:
    MessagePublisher(ActorId broker_id, std::string name) 
        : _broker_id(broker_id), _name(std::move(name)) {
        setupDefaultContent();
        registerEvent<StepMessage>(*this);
    }
    
    bool onInit() override {
        std::cout << "MessagePublisher '" << _name << "' initialized with ID: " << id() << std::endl;
        return true;
    }
    
    void on(StepMessage& msg) {
        std::cout << "[MessagePublisher] Processing StepMessage " << msg.step << std::endl;
        switch (msg.step) {
            case 1: {
                // Publish a weather update
                std::string content = _topic_content[Topic::WEATHER].getNextMessage();
                uint64_t timestamp = getCurrentTimestamp();
                std::cout << "[MessagePublisher] Publishing weather update: " << content << std::endl;
                
                push<PublishMessage>(
                    _broker_id,
                    Topic::WEATHER,
                    content,
                    _name,
                    timestamp
                );
                break;
            }
            
            case 2: {
                // Publish news
                std::string content = _topic_content[Topic::NEWS].getNextMessage();
                uint64_t timestamp = getCurrentTimestamp();
                std::cout << "[MessagePublisher] Publishing news: " << content << std::endl;
                
                push<PublishMessage>(
                    _broker_id,
                    Topic::NEWS,
                    content,
                    _name,
                    timestamp
                );
                break;
            }
            
            case 3: {
                // Publish sports update
                std::string content = _topic_content[Topic::SPORTS].getNextMessage();
                uint64_t timestamp = getCurrentTimestamp();
                std::cout << "[MessagePublisher] Publishing sports update: " << content << std::endl;
                
                push<PublishMessage>(
                    _broker_id,
                    Topic::SPORTS,
                    content,
                    _name,
                    timestamp
                );
                break;
            }
            
            case 4: {
                // Publish stock prices
                std::string content = _topic_content[Topic::STOCK_PRICES].getNextMessage();
                uint64_t timestamp = getCurrentTimestamp();
                std::cout << "[MessagePublisher] Publishing stock prices: " << content << std::endl;
                
                push<PublishMessage>(
                    _broker_id,
                    Topic::STOCK_PRICES,
                    content,
                    _name,
                    timestamp
                );
                break;
            }
            
            case 5: {
                // Publish system status
                std::string content = _topic_content[Topic::SYSTEM_STATUS].getNextMessage();
                uint64_t timestamp = getCurrentTimestamp();
                std::cout << "[MessagePublisher] Publishing system status: " << content << std::endl;
                
                push<PublishMessage>(
                    _broker_id,
                    Topic::SYSTEM_STATUS,
                    content,
                    _name,
                    timestamp
                );
                break;
            }
            
            case 6: {
                // Publish multiple updates
                uint64_t timestamp = getCurrentTimestamp();
                
                std::string weather = _topic_content[Topic::WEATHER].getNextMessage();
                std::cout << "[MessagePublisher] Publishing weather update: " << weather << std::endl;
                push<PublishMessage>(_broker_id, Topic::WEATHER, weather, _name, timestamp);
                
                std::string news = _topic_content[Topic::NEWS].getNextMessage();
                std::cout << "[MessagePublisher] Publishing news: " << news << std::endl;
                push<PublishMessage>(_broker_id, Topic::NEWS, news, _name, timestamp + 1);
                
                std::string stocks = _topic_content[Topic::STOCK_PRICES].getNextMessage();
                std::cout << "[MessagePublisher] Publishing stock prices: " << stocks << std::endl;
                push<PublishMessage>(_broker_id, Topic::STOCK_PRICES, stocks, _name, timestamp + 2);
                break;
            }
            
            default:
                break;
        }
    }
};

/**
 * @brief Subscriber Actor
 * 
 * Receives messages from subscribed topics and maintains a history of received messages.
 */
class SubscriberActor : public Actor {
private:
    using Topic = SubscribeMessage::Topic;
    std::string _name;
    
    // Store received messages history
    struct MessageRecord {
        Topic topic;
        std::string content;
        std::string publisher;
        uint64_t published_timestamp;
        uint64_t received_timestamp;
        
        MessageRecord(Topic t, std::string c, std::string p, uint64_t pt)
            : topic(t), content(std::move(c)), publisher(std::move(p)),
              published_timestamp(pt), received_timestamp(getCurrentTimestamp()) {}
    };
    
    std::vector<MessageRecord> _message_history;
    
    // Statistics
    std::map<Topic, int> _messages_per_topic;
    
    // Convert topic enum to string for display
    std::string topicToString(Topic topic) const {
        switch (topic) {
            case Topic::WEATHER: return "WEATHER";
            case Topic::NEWS: return "NEWS";
            case Topic::SPORTS: return "SPORTS";
            case Topic::STOCK_PRICES: return "STOCK_PRICES";
            case Topic::SYSTEM_STATUS: return "SYSTEM_STATUS";
            default: return "UNKNOWN";
        }
    }

public:
    SubscriberActor(std::string name) : _name(std::move(name)) {
        registerEvent<MessageReceivedMessage>(*this);
        registerEvent<PrintHistoryMessage>(*this);
    }

    bool onInit() override {
        std::cout << "SubscriberActor '" << _name << "' initialized with ID: " << id() << std::endl;
        return true;
    }
    
    void on(MessageReceivedMessage& msg) {
        // Record the message
        _message_history.emplace_back(
            msg.topic, msg.content, msg.publisher, msg.timestamp
        );
        
        // Update statistics
        _messages_per_topic[msg.topic]++;
        
        // Display received message
        std::cout << "[" << _name << "] Received message from topic " 
                  << topicToString(msg.topic) << ": \"" << msg.content 
                  << "\" (from " << msg.publisher << ")" << std::endl;
    }
    
    void on(PrintHistoryMessage& msg) {
        std::cout << "\n=== " << _name << "'s Message History ===\n";
        
        if (_message_history.empty()) {
            std::cout << "No messages received." << std::endl;
            return;
        }
        
        // Print statistics
        std::cout << "Total messages received: " << _message_history.size() << std::endl;
        std::cout << "Messages by topic:" << std::endl;
        for (const auto& [topic, count] : _messages_per_topic) {
            std::cout << "  " << topicToString(topic) << ": " << count << " messages" << std::endl;
        }
        
        // Print last 5 messages (or fewer if history is shorter)
        std::cout << "\nMost recent messages:" << std::endl;
        int count = 0;
        int start_idx = static_cast<int>(_message_history.size()) - 1;
        int end_idx = std::max(0, start_idx - 4);
        
        for (int i = start_idx; i >= end_idx; i--) {
            const auto& record = _message_history[i];
            uint64_t latency = record.received_timestamp - record.published_timestamp;
            
            std::cout << count + 1 << ". [" << topicToString(record.topic) << "] " 
                      << record.content << " (from " << record.publisher 
                      << ", latency: " << latency << "ms)" << std::endl;
            count++;
        }
        
        std::cout << "=================================\n" << std::endl;
    }
};

// Demo sequence control message
class DelayedActionMessage : public Event {
public:
    enum class Action {
        SETUP_SUBSCRIPTIONS,
        PUBLISH_MESSAGES,
        PRINT_HISTORY,
        PRINT_BROKER_STATS,
        UNSUBSCRIBE,
        FINISH_DEMO
    };
    
    Action action;
    int step;
    
    DelayedActionMessage(Action a, int s = 0)
        : action(a), step(s) {}
};

/**
 * @brief Demo Controller
 * 
 * Controls the demo sequence and manages actor creation.
 */
class DemoController : public Actor, public ICallback {
private:
    using Topic = SubscribeMessage::Topic;
    ActorId _broker_id;
    ActorId _publisher_id;
    std::vector<ActorId> _subscriber_ids;
    int _current_step = 0;
    
    void printSeparator(const std::string& title) {
        std::cout << "\n\n==================================\n";
        std::cout << "  " << title;
        std::cout << "\n==================================\n" << std::endl;
    }
    
public:
    DemoController(ActorId broker_id, ActorId publisher_id, 
                  const std::vector<ActorId>& subscriber_ids)
        : _broker_id(broker_id),
          _publisher_id(publisher_id),
          _subscriber_ids(subscriber_ids) {
        // Register event handlers
        registerEvent<DelayedActionMessage>(*this);
    }

    bool onInit() override {
        std::cout << "DemoController initialized with ID: " << id() << std::endl;
        // Register callback to start demo after initialization
        registerCallback(*this);
        return true;
    }
    
    void onCallback() override {
        runDemo();
    }
    
    void runDemo() {
        printSeparator("PUB/SUB SYSTEM DEMO");
        std::cout << "Starting demo sequence..." << std::endl;
        
        // Start the demo sequence with a delayed message to self
        push<DelayedActionMessage>(id(), DelayedActionMessage::Action::SETUP_SUBSCRIPTIONS);
    }
    
    void on(DelayedActionMessage& msg) {
        switch (msg.action) {
            case DelayedActionMessage::Action::SETUP_SUBSCRIPTIONS:
                setupSubscriptions(msg.step);
                break;
                
            case DelayedActionMessage::Action::PUBLISH_MESSAGES:
                publishMessages(msg.step);
                break;
                
            case DelayedActionMessage::Action::PRINT_HISTORY:
                printMessageHistory(msg.step);
                break;
                
            case DelayedActionMessage::Action::PRINT_BROKER_STATS:
                printBrokerStatistics();
                break;
                
            case DelayedActionMessage::Action::UNSUBSCRIBE:
                handleUnsubscribe(msg.step);
                break;
                
            case DelayedActionMessage::Action::FINISH_DEMO:
                finishDemo();
                break;
        }
    }
    
    void setupSubscriptions(int step) {
        if (step == 0) {
            printSeparator("STEP 1: SETTING UP SUBSCRIPTIONS");
        }
        
        switch (step) {
            case 0:
                // Weather subscriber subscribes to weather updates
                std::cout << "Subscribing WeatherWatcher to WEATHER topic..." << std::endl;
                push<SubscribeMessage>(_broker_id, Topic::WEATHER, _subscriber_ids[1]);
                push<DelayedActionMessage>(id(), DelayedActionMessage::Action::SETUP_SUBSCRIPTIONS, 1);
                break;
                
            case 1:
                // News subscriber subscribes to news
                std::cout << "Subscribing NewsReader to NEWS topic..." << std::endl;
                push<SubscribeMessage>(_broker_id, Topic::NEWS, _subscriber_ids[0]);
                push<DelayedActionMessage>(id(), DelayedActionMessage::Action::SETUP_SUBSCRIPTIONS, 2);
                break;
                
            case 2:
                // News subscriber subscribes to stock prices
                std::cout << "Subscribing NewsReader to STOCK_PRICES topic..." << std::endl;
                push<SubscribeMessage>(_broker_id, Topic::STOCK_PRICES, _subscriber_ids[0]);
                push<DelayedActionMessage>(id(), DelayedActionMessage::Action::SETUP_SUBSCRIPTIONS, 3);
                break;
                
            case 3:
                // Stock tracker subscribes to stock prices
                std::cout << "Subscribing StockTracker to STOCK_PRICES topic..." << std::endl;
                push<SubscribeMessage>(_broker_id, Topic::STOCK_PRICES, _subscriber_ids[2]);
                push<DelayedActionMessage>(id(), DelayedActionMessage::Action::SETUP_SUBSCRIPTIONS, 4);
                break;
                
            case 4:
                // Stock tracker subscribes to system status
                std::cout << "Subscribing StockTracker to SYSTEM_STATUS topic..." << std::endl;
                push<SubscribeMessage>(_broker_id, Topic::SYSTEM_STATUS, _subscriber_ids[2]);
                push<DelayedActionMessage>(id(), DelayedActionMessage::Action::PUBLISH_MESSAGES, 0);
                break;
        }
    }
    
    void publishMessages(int step) {
        if (step == 0) {
            printSeparator("STEP 2: PUBLISHING MESSAGES");
            std::cout << "\n-- Publishing direct messages --" << std::endl;
        }
        
        switch (step) {
            case 0:
                // Publish a weather update
                push<PublishMessage>(
                    _broker_id,
                    Topic::WEATHER,
                    "Direct weather update: Sunny, 25°C",
                    "DemoController",
                    getCurrentTimestamp()
                );
                push<DelayedActionMessage>(id(), DelayedActionMessage::Action::PUBLISH_MESSAGES, 1);
                break;
                
            case 1:
                // Publish news
                push<PublishMessage>(
                    _broker_id,
                    Topic::NEWS,
                    "Direct news: Breaking news about QB actors",
                    "DemoController",
                    getCurrentTimestamp()
                );
                push<DelayedActionMessage>(id(), DelayedActionMessage::Action::PUBLISH_MESSAGES, 2);
                break;
                
            case 2:
                // Publish stock prices
                push<PublishMessage>(
                    _broker_id,
                    Topic::STOCK_PRICES,
                    "Direct stock prices: QB stock up by 10%",
                    "DemoController",
                    getCurrentTimestamp()
                );
                
                // Wait for messages to be processed before proceeding
                std::cout << "\nWaiting for messages to be processed..." << std::endl;
                push<DelayedActionMessage>(id(), DelayedActionMessage::Action::PRINT_HISTORY, 0);
                break;
        }
    }
    
    void printMessageHistory(int step) {
        if (step == 0) {
            printSeparator("STEP 3: MESSAGE HISTORY");
        }
        
        if (step < _subscriber_ids.size()) {
            push<PrintHistoryMessage>(_subscriber_ids[step]);
            push<DelayedActionMessage>(id(), DelayedActionMessage::Action::PRINT_HISTORY, step + 1);
        } else {
            // All subscriber histories printed, move to broker statistics
            push<DelayedActionMessage>(id(), DelayedActionMessage::Action::PRINT_BROKER_STATS);
        }
    }
    
    void printBrokerStatistics() {
        printSeparator("STEP 4: BROKER STATISTICS");
        push<PrintStatisticsMessage>(_broker_id);
        push<DelayedActionMessage>(id(), DelayedActionMessage::Action::UNSUBSCRIBE, 0);
    }
    
    void handleUnsubscribe(int step) {
        if (step == 0) {
            printSeparator("STEP 5: UNSUBSCRIBING");
            
            // Unsubscribe weather watcher from weather updates
            std::cout << "Unsubscribing WeatherWatcher from WEATHER topic..." << std::endl;
            push<UnsubscribeMessage>(_broker_id, Topic::WEATHER, _subscriber_ids[1]);
            push<DelayedActionMessage>(id(), DelayedActionMessage::Action::UNSUBSCRIBE, 1);
        } else if (step == 1) {
            // Publish one more weather update (that won't be received)
            std::cout << "\nPublishing one more weather update (should not be received by WeatherWatcher)..." << std::endl;
            push<PublishMessage>(
                _broker_id,
                Topic::WEATHER,
                "Direct weather update: Storm coming",
                "DemoController",
                getCurrentTimestamp()
            );
            push<DelayedActionMessage>(id(), DelayedActionMessage::Action::UNSUBSCRIBE, 2);
        } else if (step == 2) {
            // Show final statistics
            printSeparator("FINAL STATISTICS");
            push<PrintStatisticsMessage>(_broker_id);
            push<DelayedActionMessage>(id(), DelayedActionMessage::Action::UNSUBSCRIBE, 3);
        } else if (step == 3) {
            // Print final history for the weather watcher to show no new message was received
            std::cout << "\nChecking WeatherWatcher's final message history:" << std::endl;
            push<PrintHistoryMessage>(_subscriber_ids[1]);
            push<DelayedActionMessage>(id(), DelayedActionMessage::Action::FINISH_DEMO);
        }
    }
    
    void finishDemo() {
        std::cout << "\nDemo completed!" << std::endl;
        
        // Schedule shutdown of all actors
        std::cout << "Shutting down all actors..." << std::endl;
        
        // First terminate subscribers
        for (const auto& sub_id : _subscriber_ids) {
            push<KillEvent>(sub_id);
        }
        
        // Then terminate publisher
        push<KillEvent>(_publisher_id);
        
        // Then terminate broker
        push<KillEvent>(_broker_id);
        
        // Finally terminate self
        kill();
    }
};

/**
 * Pub/Sub Example
 * 
 * This example demonstrates a publish-subscribe system using the Actor model.
 * It includes:
 * - A central broker that manages topic subscriptions
 * - Multiple subscriber actors that receive messages on topics they subscribe to
 * - A publisher actor that sends messages to the broker
 * - A demo controller that orchestrates the example
 */
int main() {
    std::cout << "Starting Pub/Sub System Example..." << std::endl;
    
    // Create the main engine
    qb::Main engine;
    
    // Step 1: Create the broker actor
    auto broker_id = engine.addActor<BrokerActor>(0);
    
    // Step 2: Create subscriber actors
    auto news_reader_id = engine.addActor<SubscriberActor>(0, std::string("NewsReader"));
    auto weather_watcher_id = engine.addActor<SubscriberActor>(0, std::string("WeatherWatcher"));
    auto stock_tracker_id = engine.addActor<SubscriberActor>(0, std::string("StockTracker"));
    
    std::vector<ActorId> subscriber_ids = {
        news_reader_id, weather_watcher_id, stock_tracker_id
    };
    
    // Step 3: Create the publisher actor
    auto publisher_id = engine.addActor<MessagePublisher>(0, broker_id, std::string("WeatherStation"));
    
    // Step 4: Create the demo controller with all the actor IDs
    engine.addActor<DemoController>(0, broker_id, publisher_id, subscriber_ids);
    
    std::cout << "Starting QB engine\n";
    engine.start();
    
    // The demo controller will run the demo automatically via onCallback
    
    // Join all actors - wait for completion
    engine.join();
    std::cout << "Engine shutdown, exiting\n";
    
    return 0;
} 