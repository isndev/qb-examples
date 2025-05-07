#include <iostream>
#include <vector>
#include <string>
#include <queue>
#include <mutex>
#include <random>
#include <chrono>

#include <qb/actor.h>
#include <qb/main.h>
#include <qb/event.h>
#include <qb/icallback.h>

// Thread-safe shared queue for actors
template <typename T>
class SharedQueue {
public:
    void push(const T& item) {
        std::lock_guard<std::mutex> lock(_mutex);
        _queue.push(item);
    }

    bool pop(T& item) {
        std::lock_guard<std::mutex> lock(_mutex);
        if (_queue.empty()) {
            return false;
        }
        item = _queue.front();
        _queue.pop();
        return true;
    }

    size_t size() const {
        std::lock_guard<std::mutex> lock(_mutex);
        return _queue.size();
    }

    bool empty() const {
        std::lock_guard<std::mutex> lock(_mutex);
        return _queue.empty();
    }

private:
    std::queue<T> _queue;
    mutable std::mutex _mutex;
};

// Message for scheduling delayed actions
struct DelayedActionMsg : public qb::Event {
    enum class Action {
        PRODUCE_ITEM,
        PROCESS_NEXT,
        CHECK_STATS
    };
    
    Action action;
    int delay_ms;
    
    DelayedActionMsg(Action action, int delay = 0)
        : action(action), delay_ms(delay) {}
};

// Message for processing a work item
struct WorkItemMsg : public qb::Event {
    int id;
    int complexity;  // Simulates the "complexity" of a work item
    
    WorkItemMsg(int i, int c)
        : id(i), complexity(c) {}
};

// Message to request statistics from a consumer
struct RequestStatsMsg : public qb::Event {
    qb::ActorId requester;
    
    explicit RequestStatsMsg(qb::ActorId req)
        : requester(req) {}
};

// Response message with consumer statistics
struct ReportStatsMsg : public qb::Event {
    int consumer_id;
    int items_processed;
    
    ReportStatsMsg(int id, int processed)
        : consumer_id(id), items_processed(processed) {}
};

// Producer that generates work items
class Producer : public qb::Actor {
public:
    Producer(std::shared_ptr<SharedQueue<WorkItemMsg>> queue)
        : _shared_queue(queue), _next_id(0) {
        registerEvent<DelayedActionMsg>(*this);
        registerEvent<qb::KillEvent>(*this);
    }

    bool onInit() override {
        qb::io::cout() << "Producer " << id() << ": Initialized\n";
        // Start generating work items
        push<DelayedActionMsg>(id(), DelayedActionMsg::Action::PRODUCE_ITEM);
        return true;
    }
    
    void on(DelayedActionMsg& msg) {
        if (msg.action == DelayedActionMsg::Action::PRODUCE_ITEM) {
            // Generate a random work item
            std::random_device rd;
            std::mt19937 gen(rd());
            std::uniform_int_distribution<> complexity_dist(1, 10);
            
            int complexity = complexity_dist(gen);
            auto work = WorkItemMsg(_next_id++, complexity);
            
            qb::io::cout() << "Producer: Generated work item " << work.id
                      << " with complexity " << work.complexity << std::endl;
            
            _shared_queue->push(work);
            
            // Schedule the generation of the next work item after a delay
            int next_delay = 500 + complexity * 100;
            push<DelayedActionMsg>(id(), DelayedActionMsg::Action::PRODUCE_ITEM, next_delay);
        }
    }
    
    void on(qb::KillEvent&) {
        qb::io::cout() << "Producer: Shutting down" << std::endl;
        kill();
    }

private:
    std::shared_ptr<SharedQueue<WorkItemMsg>> _shared_queue;
    int _next_id;
};

// Consumer that processes work items
class Consumer : public qb::Actor {
public:
    Consumer(std::shared_ptr<SharedQueue<WorkItemMsg>> queue, int id)
        : _shared_queue(queue), _consumer_id(id), _items_processed(0) {
        registerEvent<DelayedActionMsg>(*this);
        registerEvent<RequestStatsMsg>(*this);
        registerEvent<qb::KillEvent>(*this);
    }

    bool onInit() override {
        qb::io::cout() << "Consumer " << _consumer_id << " (" << id() << "): Initialized\n";
        // Start processing work items
        push<DelayedActionMsg>(id(), DelayedActionMsg::Action::PROCESS_NEXT);
        return true;
    }
    
    void on(DelayedActionMsg& msg) {
        if (msg.action == DelayedActionMsg::Action::PROCESS_NEXT) {
            WorkItemMsg work(0, 0);
            if (_shared_queue->pop(work)) {
                // Process the work item
                qb::io::cout() << "Consumer " << _consumer_id << ": Processing work item " << work.id
                          << " with complexity " << work.complexity << std::endl;
                
                _items_processed++;
                
                // Simulate processing time based on complexity
                int process_time = work.complexity * 200;
                push<DelayedActionMsg>(id(), DelayedActionMsg::Action::PROCESS_NEXT, process_time);
            } else {
                // Queue is empty, check again later
                push<DelayedActionMsg>(id(), DelayedActionMsg::Action::PROCESS_NEXT, 100);
            }
        }
    }
    
    void on(RequestStatsMsg& msg) {
        // Send current statistics to the requester
        push<ReportStatsMsg>(msg.requester, _consumer_id, _items_processed);
    }
    
    void on(qb::KillEvent&) {
        qb::io::cout() << "Consumer " << _consumer_id << ": Shutting down after processing "
                  << _items_processed << " items" << std::endl;
        kill();
    }

private:
    std::shared_ptr<SharedQueue<WorkItemMsg>> _shared_queue;
    int _consumer_id;
    int _items_processed;
};

// Supervisor that monitors the queue and consumers
class Supervisor : public qb::Actor {
public:
    Supervisor(std::shared_ptr<SharedQueue<WorkItemMsg>> queue,
              std::vector<qb::ActorId> consumers)
        : _shared_queue(queue), _consumers(consumers), 
          _is_shutting_down(false), _running_time(0),
          _pending_responses(0), _total_processed(0) {
        registerEvent<DelayedActionMsg>(*this);
        registerEvent<ReportStatsMsg>(*this);
        registerEvent<qb::KillEvent>(*this);
    }

    bool onInit() override {
        qb::io::cout() << "Supervisor " << id() << ": Initialized\n";
        // Schedule periodic queue monitoring
        push<DelayedActionMsg>(id(), DelayedActionMsg::Action::CHECK_STATS, 1000);
        return true;
    }
    
    void on(DelayedActionMsg& msg) {
        if (msg.action == DelayedActionMsg::Action::CHECK_STATS) {
            // Reset counters for a new statistics collection
            _pending_responses = _consumers.size();
            _total_processed = 0;
            
            // Request statistics from all consumers
            for (const auto& consumer_id : _consumers) {
                push<RequestStatsMsg>(consumer_id, id());
            }
            
            // After receiving all responses, processing will continue in on(ReportStatsMsg)
        }
    }
    
    void on(ReportStatsMsg& msg) {
        // Accumulate statistics
        _total_processed += msg.items_processed;
        _pending_responses--;
        
        // If this is the last response, display statistics and decide what to do next
        if (_pending_responses == 0) {
            qb::io::cout() << "Supervisor: Queue size = " << _shared_queue->size()
                      << ", Total processed = " << _total_processed << std::endl;
            
            // After 15 seconds, stop the application
            if (_running_time >= 15000 && !_is_shutting_down) {
                qb::io::cout() << "Supervisor: Shutting down all actors..." << std::endl;
                _is_shutting_down = true;
                
                // Send KillEvent to all actors (including itself)
                broadcast<qb::KillEvent>();
            } else {
                _running_time += 1000;
                push<DelayedActionMsg>(id(), DelayedActionMsg::Action::CHECK_STATS, 1000);
            }
        }
    }
    
    void on(qb::KillEvent&) {
        qb::io::cout() << "Supervisor: Shutting down" << std::endl;
        kill();
    }

private:
    std::shared_ptr<SharedQueue<WorkItemMsg>> _shared_queue;
    std::vector<qb::ActorId> _consumers;
    bool _is_shutting_down;
    int _running_time;
    int _pending_responses;
    int _total_processed;
};

int main() {
    // Create the main engine
    qb::Main engine;
    
    // Create a shared queue
    auto shared_queue = std::make_shared<SharedQueue<WorkItemMsg>>();
    
    // Create the producer
    auto producer_id = engine.addActor<Producer>(0, shared_queue);
    
    // Create multiple consumers
    std::vector<qb::ActorId> consumer_ids;
    for (int i = 0; i < 3; ++i) {
        consumer_ids.push_back(engine.addActor<Consumer>(0, shared_queue, i));
    }
    
    // Create the supervisor
    engine.addActor<Supervisor>(0, shared_queue, consumer_ids);
    
    qb::io::cout() << "Main: Starting QB engine\n";
    engine.start();
    
    qb::io::cout() << "Main: Waiting for actors to complete\n";
    engine.join();
    
    qb::io::cout() << "Example completed." << std::endl;
    return 0;
} 