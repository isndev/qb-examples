/**
 * @file examples/qbm/redis/example8_complex_actor_system.cpp
 * @example qbm-redis: Complex Actor System with Diverse Redis Usage
 *
 * @brief This advanced example demonstrates a multi-faceted actor system that leverages
 * various Redis data structures and patterns for different purposes, including
 * work queuing, caching, publish/subscribe notifications, and log aggregation.
 *
 * @details
 * The system is composed of several specialized actors interacting via QB events and Redis:
 * 1.  `WorkerActor` (multiple instances possible):
 *     -   Fetches jobs from a Redis List ("jobs:queue") using blocking pop (`_redis.brpop()`).
 *     -   Before processing, checks a Redis cache (string key like "cache:job:<job_id>") for existing results (`_redis.get()`).
 *     -   If not cached, simulates job processing and stores the result in a Redis Hash ("job:results")
 *         and its status in another Hash ("job:status").
 *     -   Caches the result using `_redis.setex()` with a TTL.
 *     -   Atomically increments its processing metrics in a Redis Hash ("worker:metrics") using a Lua script via `_redis.eval()`.
 *     -   Publishes a job completion message to a Redis Pub/Sub channel ("job:completed") using `_redis.publish()`.
 *     -   Logs its significant actions (init, job completion, shutdown) to a Redis Stream ("system:logs") using `_redis.xadd()`.
 * 2.  `CacheManagerActor`:
 *     -   Intended to manage cache entries (set with TTL, delete).
 *     -   Handles `CacheEvent`s for SET and DELETE operations.
 *     -   Publishes cache update/invalidation notifications to Pub/Sub channels.
 *     -   (Conceptual Pub/Sub subscription for invalidations shown, full handling not implemented in its `onCallback`).
 * 3.  `LogAggregatorActor`:
 *     -   Periodically reads new entries from the "system:logs" Redis Stream using `_redis.xread()`.
 *     -   Processes and displays these log entries.
 *     -   Can also receive `LogEvent`s from other actors to write directly to the stream.
 * 4.  `ClientActor` (multiple instances possible):
 *     -   Submits new jobs by `RPUSH`ing them to the "jobs:queue" Redis List.
 *     -   Tracks submitted jobs and can poll Redis Hashes ("job:status", "job:results") for completion.
 *     -   (Alternatively, could subscribe to "job:completed" Pub/Sub channel for notifications).
 * 5.  `CoordinatorActor`:
 *     -   Initializes and orchestrates the entire system: creates other actors.
 *     -   Its `ActorId` (`g_coordinator_id`) is globally accessible for other actors to send notifications.
 *     -   Manages client and worker registration (conceptually, by tracking their IDs).
 *     -   Can display overall system statistics by querying Redis (queue length, worker metrics, etc.).
 *     -   Handles graceful shutdown of the system.
 *
 * This example demonstrates how Redis can serve as a powerful backend for various aspects
 * of a distributed actor system, from message queuing and caching to logging and eventing.
 *
 * QB/QBM Redis Features Demonstrated:
 * - `qb::Actor`, `qb::Main`, `qb::Event`, `qb::ICallback`.
 * - `qb::redis::tcp::client` used extensively by multiple actors.
 * - Diverse Redis Commands:
 *   - Lists: `brpop()`, `rpush()`, `llen()`.
 *   - Hashes: `hset()`, `hget()`, `hgetall()`, `hincrby()` (via Lua).
 *   - Strings: `get()`, `setex()`, `del()`.
 *   - Pub/Sub: `publish()`. (Subscription handling in `CacheManagerActor` is conceptual).
 *   - Streams: `xadd()`, `xread()`, `xlen()`.
 *   - Scripting: `client.eval<ReturnType>(script, keys, args)`.
 *   - Server: `flushdb()`, `keys()`.
 * - Multi-core actor deployment.
 * - Complex inter-actor and actor-Redis communication patterns.
 * - `qb::string<N>`, `qb::json` (implicitly for stream results), `qb::io::cout()`.
 */

#include <redis/redis.h>
#include <qb/actor.h>
#include <qb/main.h>
#include <qb/io/async.h>
#include <qb/json.h>
#undef ERROR
#undef DELETE

// Redis Configuration
#define REDIS_URI "tcp://localhost:6379"

// =============== Event Definitions ===============

// Event for the coordinator to signal job creation
struct CreateJobEvent : public qb::Event {
    std::string job_id;
    std::string job_type;
    std::string job_data;

    CreateJobEvent(const std::string& id, const std::string& type, const std::string& data)
        : job_id(id), job_type(type), job_data(data) {}
};

// Event for client registration with coordinator
struct ClientRegistrationEvent : public qb::Event {
    std::string client_id;
    
    ClientRegistrationEvent(const std::string& id) : client_id(id) {}
};

// Event for worker registration with coordinator
struct WorkerRegistrationEvent : public qb::Event {
    std::string worker_id;
    
    WorkerRegistrationEvent(const std::string& id) : worker_id(id) {}
};

// Event for client shutdown notification
struct ClientShutdownEvent : public qb::Event {
    std::string client_id;
    
    ClientShutdownEvent(const std::string& id) : client_id(id) {}
};

// Event for cache operations
struct CacheEvent : public qb::Event {
    enum class Action { SET, GET, DELETE };
    
    Action action;
    std::string key;
    std::string value;
    
    CacheEvent(Action act, std::string k, std::string v = "")
        : action(act), key(k), value(v) {}
};

// Event for log messages
struct LogEvent : public qb::Event {
    enum class Level { DEBUG, INFO, WARNING, ERROR };
    
    Level level;
    std::string component;
    std::string message;
    
    LogEvent(Level lvl, std::string comp, std::string msg)
        : level(lvl), component(comp), message(msg) {}
};

// Event to notify of job completion
struct JobCompletedEvent : public qb::Event {
    std::string job_id;
    bool success;
    std::string result;
    
    JobCompletedEvent(std::string id, bool s, std::string res)
        : job_id(id), success(s), result(res) {}
};

// Event to start worker system shutdown
struct ShutdownEvent : public qb::Event {};

// =============== Global Functions ===============

// Generate a random job ID
std::string generate_job_id() {
    static std::atomic<int> counter{0};
    return "job-" + std::to_string(std::time(nullptr)) + "-" + std::to_string(counter++);
}

// Convert LogEvent::Level to string
std::string level_to_string(LogEvent::Level level) {
    switch (level) {
        case LogEvent::Level::DEBUG: return "DEBUG";
        case LogEvent::Level::INFO: return "INFO";
        case LogEvent::Level::WARNING: return "WARNING";
        case LogEvent::Level::ERROR: return "ERROR";
        default: return "UNKNOWN";
    }
}

// Global IDs to track specific actors
qb::ActorId g_coordinator_id;

// =============== Actor Definitions ===============

// Worker actor that processes jobs from a Redis queue
class WorkerActor : public qb::Actor, public qb::ICallback {
private:
    // Redis client
    qb::redis::tcp::client _redis;
    bool _connected = false;
    qb::string<32> _worker_id;
    qb::string<32> _queue_key;
    int _max_jobs;
    int _jobs_processed = 0;
    qb::string<32> _coordinator_id;
    
    // Generate random processing time
    std::mt19937 _rng{std::random_device{}()};
    
    double _callback_period = 0.1; // Faster callbacks for more responsiveness
    
public:
    WorkerActor(std::string worker_id, std::string queue_key, int max_jobs = 1000)
        : _worker_id(worker_id), 
          _queue_key(queue_key), 
          _max_jobs(max_jobs),
          _redis(qb::io::uri(REDIS_URI))
    {
        // Redis client is initialized in the constructor using member initializer
    }
    
    ~WorkerActor() noexcept override = default;
    
    bool onInit() override {
        qb::io::cout() << "WorkerActor [" << _worker_id << "] initialized on core " << getIndex() << std::endl;
        
        // Register for callbacks and events
        registerCallback(*this);
        registerEvent<ShutdownEvent>(*this);
        
        // Store coordinator ID for reporting
        _coordinator_id = std::to_string(g_coordinator_id);
        
        // Connect to Redis
        try {
            if (!_redis.connect()) {
                qb::io::cerr() << "WorkerActor [" << _worker_id << "] failed to connect to Redis" << std::endl;
                return false;
            }
            _connected = true;
            qb::io::cout() << "WorkerActor [" << _worker_id << "] connected to Redis" << std::endl;
            
            // Register with coordinator
            push<WorkerRegistrationEvent>(g_coordinator_id, _worker_id.c_str());
            
            // Log initialization
            log_action("Worker initialized");
            
            return true;
        } catch (const std::exception& e) {
            qb::io::cerr() << "WorkerActor [" << _worker_id << "] Redis error: " << e.what() << std::endl;
            return false;
        }
    }
    
    // Log an action to Redis streams
    void log_action(const std::string& action) {
        try {
            // Create log entry
            std::vector<std::pair<std::string, std::string>> log_entry = {
                {"worker_id", _worker_id},
                {"action", action},
                {"timestamp", std::to_string(std::time(nullptr))}
            };
            
            // Add to log stream, using "*" for auto-generated ID
            _redis.xadd("system:logs", log_entry);
        } catch (const std::exception& e) {
            // Just print but don't fail on logging errors
            qb::io::cerr() << "Error logging to Redis: " << e.what() << std::endl;
        }
    }
    
    // Poll for jobs in the queue
    void onCallback() override {
        if (!_connected || _jobs_processed >= _max_jobs) {
            if (_jobs_processed >= _max_jobs) {
                qb::io::cout() << "WorkerActor [" << _worker_id << "] reached max jobs, shutting down" << std::endl;
                push<ShutdownEvent>(id());
            }
            return;
        }
        
        try {
            // Try to get one job with a 1-second timeout (moderate timeout)
            qb::io::cout() << "WorkerActor [" << _worker_id << "] polling for jobs..." << std::endl;
            auto result = _redis.brpop({_queue_key}, 1);
            
            // Check if we got a job
            if (result.has_value()) {
                qb::io::cout() << "WorkerActor [" << _worker_id << "] DEQUEUED a job" << std::endl;
                const auto& key_val_pair = *result;
                const auto& job_data = key_val_pair.second;
                if (!job_data.empty()) {
                    process_job(job_data);
                } else {
                    qb::io::cout() << "WorkerActor [" << _worker_id << "] received EMPTY job data" << std::endl;
                }
            } else {
                // No jobs available in the timeout period
                qb::io::cout() << "WorkerActor [" << _worker_id << "] no jobs available" << std::endl;
            }
        } catch (const std::exception& e) {
            qb::io::cerr() << "WorkerActor [" << _worker_id << "] error polling queue: " << e.what() << std::endl;
            
            // Short sleep to avoid tight looping on errors
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
    
    // Process a job from the queue
    void process_job(const std::string& job_data) {
        // Improved logging to debug parsing issues
        qb::io::cout() << "WorkerActor [" << _worker_id << "] processing job: " << job_data << std::endl;
        
        // Parse job data (format: job_id|job_type|data) with more robust parsing
        std::string job_id, job_type, data;
        
        try {
            size_t pos1 = job_data.find('|');
            if (pos1 != std::string::npos) {
                job_id = job_data.substr(0, pos1);
                
                size_t pos2 = job_data.find('|', pos1 + 1);
                if (pos2 != std::string::npos) {
                    job_type = job_data.substr(pos1 + 1, pos2 - pos1 - 1);
                    data = job_data.substr(pos2 + 1);
                } else {
                    // Malformed: No second separator
                    qb::io::cerr() << "WorkerActor [" << _worker_id << "] missing second separator in job data" << std::endl;
                    return;
                }
            } else {
                // Malformed: No separator
                qb::io::cerr() << "WorkerActor [" << _worker_id << "] missing separator in job data" << std::endl;
                return;
            }
            
            if (job_id.empty() || job_type.empty()) {
                qb::io::cerr() << "WorkerActor [" << _worker_id << "] received invalid job data: empty job_id or job_type" << std::endl;
                return;
            }
        } catch (const std::exception& e) {
            qb::io::cerr() << "WorkerActor [" << _worker_id << "] error parsing job data: " << e.what() << std::endl;
            return;
        }
        
        // Log detailed info for debugging
        qb::io::cout() << "WorkerActor [" << _worker_id << "] parsed job: id=" << job_id 
             << ", type=" << job_type << ", data=" << data << std::endl;
        
        // First check if the result is in cache
        try {
            auto cached_result = _redis.get("cache:job:" + job_id);
            if (cached_result.has_value()) {
                qb::io::cout() << "WorkerActor [" << _worker_id << "] using cached result for " << job_id << std::endl;
                
                // Store result in Redis hash
                _redis.hset("job:results", job_id, *cached_result);
                
                // Update job status
                _redis.hset("job:status", job_id, "completed");
                
                // Increment metric counter
                try {
                    std::string script = "local counter = redis.call('HINCRBY', 'worker:metrics', ARGV[1], 1); return counter";
                    auto counter_result = _redis.eval<long long>(script, {}, {_worker_id.c_str()});
                } catch (const std::exception& e) {
                    qb::io::cerr() << "Error incrementing metrics: " << e.what() << std::endl;
                }
                
                // Log the job completion (from cache)
                log_action("Completed job " + job_id + " (cached)");
                
                // Send completion event to coordinator
                push<JobCompletedEvent>(g_coordinator_id, job_id, true, *cached_result);
                
                _jobs_processed++;
                return;
            }
        } catch (const std::exception& e) {
            // Ignore cache errors and proceed with normal processing
            qb::io::cerr() << "Cache check error: " << e.what() << std::endl;
        }
        
        // Simulate processing time based on job type - MINIMIZED TIME FOR EXAMPLE
        std::uniform_real_distribution<> dist(0.001, 0.01); // 1-10ms for faster processing
        double processing_time = dist(_rng);
        std::this_thread::sleep_for(std::chrono::duration<double>(processing_time));
        
        // Generate result based on job type
        bool success = true;
        std::string result;
        
        if (job_type == "compute") {
            // Simulate computation
            try {
                int value = std::stoi(data);
                result = std::to_string(value * value);
            } catch (...) {
                success = false;
                result = "ERROR: Invalid input for computation";
            }
        } else if (job_type == "encode") {
            // Simple encoding
            result = "";
            for (char c : data) {
                result += std::to_string(static_cast<int>(c)) + ",";
            }
        } else {
            // Default processing
            result = "Processed: " + data;
        }
        
        try {
            // Store result in Redis hash
            _redis.hset("job:results", job_id, result);
        
            // Update job status
            _redis.hset("job:status", job_id, success ? "completed" : "failed");
            
            // Cache the result for future use
            _redis.setex("cache:job:" + job_id, 60, result); // Cache for 60 seconds
        
            // Increment metric counter with Lua script
            try {
                std::string script = "local counter = redis.call('HINCRBY', 'worker:metrics', ARGV[1], 1); return counter";
                auto counter_result = _redis.eval<long long>(script, {}, {_worker_id.c_str()});
            } catch (const std::exception& e) {
                qb::io::cerr() << "Error incrementing metrics: " << e.what() << std::endl;
            }
            
            // Publish result notification
            _redis.publish("job:completed", job_id + "|" + (success ? "success" : "failed") + "|" + result);
            
            // Log the job completion
            log_action("Completed job " + job_id);
            
            // Send completion event to coordinator
            push<JobCompletedEvent>(g_coordinator_id, job_id, success, result);
            
            _jobs_processed++;
            
            qb::io::cout() << "WorkerActor [" << _worker_id << "] completed job " << job_id 
                  << " (total: " << _jobs_processed << "/" << _max_jobs << ")" << std::endl;
        } catch (const std::exception& e) {
            qb::io::cerr() << "WorkerActor [" << _worker_id << "] error processing job result: " << e.what() << std::endl;
        }
    }
    
    void on(const ShutdownEvent&) {
        qb::io::cout() << "WorkerActor [" << _worker_id << "] shutting down after processing " 
             << _jobs_processed << " jobs" << std::endl;
        
        // Unregister callback
        unregisterCallback(*this);
        
        // Log shutdown
        log_action("Worker shutting down");
        
        kill();
    }
};

// Cache manager actor for distributed caching
class CacheManagerActor : public qb::Actor, public qb::ICallback {
private:
    qb::redis::tcp::client _redis;
    bool _connected = false;
    
    // Default TTL for cache entries in seconds
    int _ttl_seconds = 300;
    
    double _callback_period = 1.0;
    
public:
    CacheManagerActor(int ttl_seconds = 300) 
        : _ttl_seconds(ttl_seconds),
          _redis(qb::io::uri(REDIS_URI))
    {
        // Redis client is initialized in the constructor using member initializer
    }
    
    ~CacheManagerActor() noexcept override = default;
    
    bool onInit() override {
        qb::io::cout() << "CacheManagerActor initialized on core " << getIndex() << std::endl;
        
        // Register for events
        registerEvent<CacheEvent>(*this);
        registerEvent<ShutdownEvent>(*this);
        
        // Register for callbacks
        registerCallback(*this);
        
        // Connect to Redis
        try {
            if (!_redis.connect()) {
                qb::io::cerr() << "CacheManagerActor failed to connect to Redis" << std::endl;
                return false;
            }
            _connected = true;
            qb::io::cout() << "CacheManagerActor connected to Redis" << std::endl;
            
            // Subscribe to cache invalidation notifications - simplified approach
            _redis.publish("cache:invalidations", "subscribed");
            qb::io::cout() << "CacheManager initialized subscription to cache:invalidations channel" << std::endl;
            
            return true;
        } catch (const std::exception& e) {
            qb::io::cerr() << "CacheManagerActor Redis error: " << e.what() << std::endl;
            return false;
        }
    }
    
    void onCallback() override {
        // This callback would handle redis subscription messages
        // For simplicity, we don't implement full subscription handling here
    }
    
    void on(const CacheEvent& event) {
        if (!_connected) return;
        
        try {
            std::string full_key = "cache:" + event.key;
            
            switch (event.action) {
                case CacheEvent::Action::SET: {
                    // Set cache with expiration
                    _redis.setex(full_key, _ttl_seconds, event.value);
                
                    // Publish notification about cache update
                    _redis.publish("cache:updates", full_key);
                    break;
                }
                case CacheEvent::Action::DELETE: {
                    // Delete the key
                    _redis.del(full_key);
                
                    // Publish notification about cache invalidation
                    _redis.publish("cache:invalidations", full_key);
                    break;
                }
                case CacheEvent::Action::GET:
                    // This would be handled directly by the requesting actor
                    break;
            }
        } catch (const std::exception& e) {
            qb::io::cerr() << "CacheManagerActor error: " << e.what() << std::endl;
        }
    }
    
    void on(const ShutdownEvent&) {
        qb::io::cout() << "CacheManagerActor shutting down" << std::endl;
        
        try {
            // Unsubscribe is unnecessary with our current implementation
            // since we're not using a proper subscription client
        } catch (const std::exception& e) {
            qb::io::cerr() << "Error during shutdown: " << e.what() << std::endl;
        }
        
        // Unregister callback
        unregisterCallback(*this);
        
        kill();
    }
};

// Log aggregator actor for centralized logging
class LogAggregatorActor : public qb::Actor, public qb::ICallback {
private:
    // Redis client
    qb::redis::tcp::client _redis;
    bool _connected = false;
    std::string _log_stream_key = "system:logs";
    std::string _last_id = "0";  // Start from the beginning
    
public:
    LogAggregatorActor() 
        : _redis(qb::io::uri(REDIS_URI))
    {
        // Redis client is initialized in the constructor using member initializer
    }
    
    ~LogAggregatorActor() noexcept override = default;
    
    bool onInit() override {
        qb::io::cout() << "LogAggregatorActor initialized on core " << getIndex() << std::endl;
        
        // Register for events and callbacks
        registerEvent<LogEvent>(*this);
        registerEvent<ShutdownEvent>(*this);
        registerCallback(*this);
        
        // Connect to Redis
        try {
            // Connect to Redis
            if (!_redis.connect()) {
                qb::io::cerr() << "LogAggregatorActor failed to connect to Redis" << std::endl;
                return false;
            }
            
            _connected = true;
            qb::io::cout() << "LogAggregatorActor connected to Redis" << std::endl;
            
            // Initialize the log stream with a special entry
            std::vector<std::pair<std::string, std::string>> init_entry = {
                {"source", "system"},
                {"level", "INFO"},
                {"message", "Log system initialized"},
                {"timestamp", std::to_string(std::time(nullptr))}
            };
            
            // Add initialization entry to the stream - exactly like example7
            auto stream_id = _redis.xadd(_log_stream_key, init_entry);
            
            qb::io::cout() << "LogAggregatorActor initialized log stream with ID: " 
                 << stream_id.to_string() << std::endl;
            
            return true;
        } catch (const std::exception& e) {
            qb::io::cerr() << "LogAggregatorActor initialization error: " << e.what() << std::endl;
            return false;
        }
    }
    
    // Poll for new log entries and process them
    void onCallback() override {
        if (!_connected) return;
        
        try {
            // Read from the log stream exactly like example7
            auto results = _redis.xread({_log_stream_key}, {_last_id}, 10);
            
            // If results are empty, nothing to do
            if (results.empty() || results.is_null()) {
                return;
            }
            
            // Debug output
            // qb::io::cout() << "COMPLETE JSON DUMP: " << results.dump(2) << std::endl;
            // qb::io::cout() << "Stream results type: " << results.type_name() << std::endl;
            
            // Process the results exactly like example7 StreamConsumerActor does
            if (results.is_array() && !results.empty()) {
                // Process each stream in the response
                for (const auto& stream_obj : results) {
                    // Iterate through stream keys and entries
                    for (auto it = stream_obj.begin(); it != stream_obj.end(); ++it) {
                        // Get stream name
                        std::string stream_name = it.key();
                        
                        // Skip if not our stream
                        if (stream_name != _log_stream_key) continue;
                        
                        // Get messages array
                        const auto& messages = it.value();
                        
                        // Process each message
                        for (const auto& msg_entry : messages) {
                            // Each msg_entry is like {"id":{fields}}
                            for (auto msg_it = msg_entry.begin(); msg_it != msg_entry.end(); ++msg_it) {
                                // Get message ID
                                std::string message_id = msg_it.key();
                                
                                // Update last seen ID
                                _last_id = message_id;
                                
                                // Get fields
                                const auto& fields = msg_it.value();
                                
                                // Skip if not an object
                                if (!fields.is_object()) continue;
                                
                                // Extract fields
                                std::string component = "unknown";
                                std::string level = "INFO";
                                std::string message = "";
                                
                                if (fields.contains("component")) {
                                    std::string comp = fields["component"].dump();
                                    // Remove quotes if present
                                    if (comp.front() == '"' && comp.back() == '"') {
                                        comp = comp.substr(1, comp.size() - 2);
                                    }
                                    component = comp;
                                } else if (fields.contains("worker_id")) {
                                    std::string worker = fields["worker_id"].dump();
                                    if (worker.front() == '"' && worker.back() == '"') {
                                        worker = worker.substr(1, worker.size() - 2);
                                    }
                                    component = "worker:" + worker;
                                } else if (fields.contains("source")) {
                                    std::string src = fields["source"].dump();
                                    if (src.front() == '"' && src.back() == '"') {
                                        src = src.substr(1, src.size() - 2);
                                    }
                                    component = src;
                                }
                                
                                if (fields.contains("level")) {
                                    std::string lvl = fields["level"].dump();
                                    if (lvl.front() == '"' && lvl.back() == '"') {
                                        lvl = lvl.substr(1, lvl.size() - 2);
                                    }
                                    level = lvl;
                                }
                                
                                if (fields.contains("message")) {
                                    std::string msg = fields["message"].dump();
                                    if (msg.front() == '"' && msg.back() == '"') {
                                        msg = msg.substr(1, msg.size() - 2);
                                    }
                                    message = msg;
                                } else if (fields.contains("action")) {
                                    std::string act = fields["action"].dump();
                                    if (act.front() == '"' && act.back() == '"') {
                                        act = act.substr(1, act.size() - 2);
                                    }
                                    message = act;
                                }
                                
                                // Display log message
                                qb::io::cout() << "[LOG] [" << level << "] [" << component << "] " 
                                     << message << " (ID: " << message_id << ")" << std::endl;
                            }
                        }
                    }
                }
            }
        } catch (const std::exception& e) {
            qb::io::cerr() << "LogAggregatorActor error reading stream: " << e.what() << std::endl;
        }
    }
    
    void on(const LogEvent& event) {
        if (!_connected) return;
        
        try {
            // Create log entry fields exactly like in example7
            std::vector<std::pair<std::string, std::string>> log_entry = {
                {"component", event.component},
                {"level", level_to_string(event.level)},
                {"message", event.message},
                {"timestamp", std::to_string(std::time(nullptr))}
            };
            
            // Add to log stream - pattern from example7
            auto id = _redis.xadd(_log_stream_key, log_entry);
            
            qb::io::cout() << "LogAggregatorActor: Added log to stream with ID: " 
                 << id.to_string() << std::endl;
        } catch (const std::exception& e) {
            qb::io::cerr() << "LogAggregatorActor error adding to stream: " << e.what() << std::endl;
        }
    }
    
    void on(const ShutdownEvent&) {
        qb::io::cout() << "LogAggregatorActor shutting down" << std::endl;
        
        try {
            // Add a final log entry
            std::vector<std::pair<std::string, std::string>> log_entry = {
                {"source", "system"},
                {"level", "INFO"},
                {"message", "Log system shutting down"},
                {"timestamp", std::to_string(std::time(nullptr))}
            };
            
            _redis.xadd(_log_stream_key, log_entry);
        } catch (...) {
            // Ignore errors during shutdown
        }
        
        // Unregister callback
        unregisterCallback(*this);
        
        // Clean up
        kill();
    }
};

// Client actor that creates jobs and tracks results
class ClientActor : public qb::Actor, public qb::ICallback {
private:
    qb::redis::tcp::client _redis{qb::io::uri(REDIS_URI)};
    bool _connected = false;
    qb::string<32> _client_id;
    int _total_jobs;
    int _jobs_submitted = 0;
    int _jobs_completed = 0;
    std::set<std::string> _pending_jobs;
    
    // Job types with their relative frequencies
    std::vector<std::pair<std::string, int>> _job_types = {
        {"compute", 60},
        {"encode", 30},
        {"process", 10}
    };
    
    // Random generators
    std::mt19937 _rng{std::random_device{}()};
    std::uniform_int_distribution<> _job_type_dist{1, 100};
    std::uniform_int_distribution<> _data_value_dist{1, 1000};
    
    double _callback_period = 0.05; // Faster job submission
    bool _shutting_down = false;
    
public:
    ClientActor(std::string client_id, int total_jobs = 100)
        : _client_id(client_id), _total_jobs(total_jobs)
    {
        // Redis client is initialized in member initializer list
    }
    
    ~ClientActor() noexcept override = default;
    
    bool onInit() override {
        qb::io::cout() << "ClientActor [" << _client_id << "] initialized on core " << getIndex() << std::endl;
        
        // Register for callbacks and events
        registerCallback(*this);
        registerEvent<JobCompletedEvent>(*this);
        registerEvent<ShutdownEvent>(*this);
        
        // Connect to Redis
        try {
            if (!_redis.connect()) {
                qb::io::cerr() << "ClientActor [" << _client_id << "] failed to connect to Redis" << std::endl;
                return false;
            }
            _connected = true;
            qb::io::cout() << "ClientActor [" << _client_id << "] connected to Redis" << std::endl;
            
            // Register with coordinator
            push<ClientRegistrationEvent>(g_coordinator_id, _client_id.c_str());
            
            return true;
        } catch (const std::exception& e) {
            qb::io::cerr() << "ClientActor [" << _client_id << "] Redis error: " << e.what() << std::endl;
            return false;
        }
    }
    
    void onCallback() override {
        if (_shutting_down) return;
        
        // Submit jobs if we haven't reached the total
        if (_connected && _jobs_submitted < _total_jobs) {
            submit_job();
        }
        
        // Check for completed jobs
        check_completed_jobs();
        
        // If all jobs are done, we can shut down
        if (_jobs_submitted >= _total_jobs && _jobs_completed >= _total_jobs) {
            qb::io::cout() << "ClientActor [" << _client_id << "] completed all jobs, shutting down" << std::endl;
            shutdown();
        }
    }
    
    // Submit a new job to the queue
    void submit_job() {
        // Select a job type based on frequency
        std::string job_type = select_job_type();
        
        // Generate some random data
        std::string data = generate_job_data(job_type);
        
        // Generate a unique job ID
        std::string job_id = generate_job_id();
        
        qb::io::cout() << "ClientActor [" << _client_id << "] submitting job " 
             << job_id << " of type " << job_type << std::endl;
        
        try {
            // Create the full job data string
            std::string full_job_data = job_id + "|" + job_type + "|" + data;
            
            // Use RPUSH to add to the end of the queue for FIFO order with BRPOP
            qb::io::cout() << "ClientActor [" << _client_id << "] ENQUEUEING job: " << job_id << std::endl;
            _redis.rpush("jobs:queue", full_job_data);
            
            // Record job status
            _redis.hset("job:status", job_id, "pending");
            
            // Track this job
            _pending_jobs.insert(job_id);
            _jobs_submitted++;
            
            // Notify coordinator
            push<CreateJobEvent>(g_coordinator_id, job_id, job_type, data);
            
            // Very small delay between job submissions
            std::uniform_real_distribution<> dist(0.001, 0.01);
            std::this_thread::sleep_for(std::chrono::duration<double>(dist(_rng)));
        } catch (const std::exception& e) {
            qb::io::cerr() << "ClientActor [" << _client_id << "] error submitting job: " << e.what() << std::endl;
        }
    }
    
    // Check for jobs that have been completed
    void check_completed_jobs() {
        if (_pending_jobs.empty()) return;
        
        try {
            // Create a copy to avoid iterator invalidation
            auto pending_copy = _pending_jobs;
            
            for (const auto& job_id : pending_copy) {
                // Check job status
                auto status = _redis.hget("job:status", job_id);
                
                if (status.has_value()) {
                    std::string job_status = *status;
                    
                    if (job_status == "completed" || job_status == "failed") {
                        // Job is done, get the result
                        auto result = _redis.hget("job:results", job_id);
                        
                        if (result.has_value()) {
                            qb::io::cout() << "ClientActor [" << _client_id << "] found completed job " 
                                 << job_id << ": " << *result << std::endl;
                            
                            _pending_jobs.erase(job_id);
                            _jobs_completed++;
                        }
                    }
                }
            }
        } catch (const std::exception& e) {
            qb::io::cerr() << "ClientActor [" << _client_id << "] error checking completed jobs: " << e.what() << std::endl;
        }
    }
    
    // Handle job completion events (alternative to polling)
    void on(const JobCompletedEvent& event) {
        // Remove from pending jobs if it's one of ours
        if (_pending_jobs.find(event.job_id) != _pending_jobs.end()) {
            qb::io::cout() << "ClientActor [" << _client_id << "] received completion for job " 
                 << event.job_id << " with result: " << event.result << std::endl;
            
            _pending_jobs.erase(event.job_id);
            _jobs_completed++;
        }
    }
    
    void on(const ShutdownEvent&) {
        shutdown();
    }
    
    void shutdown() {
        if (_shutting_down) return;
        
        _shutting_down = true;
        qb::io::cout() << "ClientActor [" << _client_id << "] shutting down after submitting " 
             << _jobs_submitted << " jobs and completing " << _jobs_completed << std::endl;
        
        // Notify coordinator about our shutdown
        push<ClientShutdownEvent>(g_coordinator_id, _client_id.c_str());
        
        // Unregister callback
        unregisterCallback(*this);
        
        kill();
    }
    
private:
    // Select job type based on weighted distribution
    std::string select_job_type() {
        int value = _job_type_dist(_rng);
        int cumulative = 0;
        
        for (const auto& job_type : _job_types) {
            cumulative += job_type.second;
            if (value <= cumulative) {
                return job_type.first;
            }
        }
        
        return _job_types[0].first; // Default fallback
    }
    
    // Generate sample job data
    std::string generate_job_data(const std::string& job_type) {
        if (job_type == "compute") {
            return std::to_string(_data_value_dist(_rng));
        } else if (job_type == "encode") {
            return "sample-text-" + std::to_string(_data_value_dist(_rng));
        } else {
            return "generic-data-" + std::to_string(_data_value_dist(_rng));
        }
    }
};

// Coordinator actor to manage the example
class CoordinatorActor : public qb::Actor, public qb::ICallback {
private:
    qb::redis::tcp::client _redis{qb::io::uri(REDIS_URI)};
    bool _connected = false;
    
    // Track active clients and workers
    std::unordered_set<std::string> _active_clients;
    std::unordered_set<std::string> _active_workers;
    
    bool _shutdown_initiated = false;
    int _completed_clients = 0;
    int _expected_clients;
    int _no_activity_count = 0;

    // For metrics display
    unsigned int _display_counter = 0;
    
    // Log aggregator ID for sending logs
    qb::ActorId _log_aggregator_id;

public:
    CoordinatorActor(int num_clients = 2) : _expected_clients(num_clients) {}
    
    ~CoordinatorActor() noexcept override = default;
    
    bool onInit() override {
        qb::io::cout() << "CoordinatorActor initialized on core " << getIndex() << std::endl;
        
        // Register for events
        registerEvent<CreateJobEvent>(*this);
        registerEvent<JobCompletedEvent>(*this);
        registerEvent<ShutdownEvent>(*this);
        registerEvent<qb::KillEvent>(*this);
        registerEvent<ClientRegistrationEvent>(*this);
        registerEvent<ClientShutdownEvent>(*this);
        registerEvent<WorkerRegistrationEvent>(*this);
        
        // Register for callbacks
        registerCallback(*this);
        
        // Store global ID for other actors to access
        g_coordinator_id = id();
        
        // Connect to Redis
        try {
            if (!_redis.connect()) {
                qb::io::cerr() << "Coordinator failed to connect to Redis" << std::endl;
                return false;
            }
            _connected = true;
            qb::io::cout() << "Coordinator connected to Redis" << std::endl;
            
            // Clear previous data
            _redis.flushdb();
            qb::io::cout() << "Cleared Redis database for a clean start" << std::endl;
            
            return true;
        } catch (const std::exception& e) {
            qb::io::cerr() << "Coordinator Redis error: " << e.what() << std::endl;
            return false;
        }
    }
    
    // Handle client registration
    void on(const ClientRegistrationEvent& event) {
        _active_clients.insert(event.client_id);
        qb::io::cout() << "Coordinator: Client " << event.client_id << " registered" << std::endl;
    }
    
    // Handle client shutdown
    void on(const ClientShutdownEvent& event) {
        if (_active_clients.find(event.client_id) != _active_clients.end()) {
            _active_clients.erase(event.client_id);
            qb::io::cout() << "Coordinator: Client " << event.client_id << " shutdown" << std::endl;
        }
    }
    
    // Handle worker registration
    void on(const WorkerRegistrationEvent& event) {
        _active_workers.insert(event.worker_id);
        qb::io::cout() << "Coordinator: Worker " << event.worker_id << " registered" << std::endl;
    }
    
    void onCallback() override {
        if (!_connected) return;
        
        // Limit metrics display frequency
        _display_counter++;
        if (_display_counter % 10 == 0) { // Only display every 10th callback
            // Display system statistics periodically
            // display_statistics();
            
            // Check if system should automatically shut down
            check_auto_shutdown();
        }
        
        // Check if all clients are done, and if so, initiate shutdown
        if (!_shutdown_initiated && _completed_clients >= _expected_clients) {
            initiate_shutdown();
        }
    }
    
    // Check if we should automatically shut down the system
    void check_auto_shutdown() {
        try {
            // Get queue length
            auto queue_len = _redis.llen("jobs:queue");
            
            // Check if system should shut down (no clients and no jobs)
            if (!_shutdown_initiated && _active_clients.empty() && queue_len == 0) {
                // Count consecutive times with no activity
                _no_activity_count++;
                
                // After a few consecutive checks with no activity, shut down
                if (_no_activity_count >= 3) {
                    qb::io::cout() << "All clients finished, no jobs in queue, shutting down system" << std::endl;
                    
                    // Initiate shutdown
                    initiate_shutdown();
                }
            } else {
                // Reset the counter if there's activity
                if (!_active_clients.empty() || queue_len > 0) {
                    _no_activity_count = 0;
                }
            }
        } catch (const std::exception& e) {
            qb::io::cerr() << "Error in auto-shutdown check: " << e.what() << std::endl;
        }
    }
    
    void on(const CreateJobEvent& event) {
        if (!_connected) return;
        
        qb::io::cout() << "Coordinator received job creation notification of type: " 
             << event.job_type << std::endl;
        
        // Log the job creation if log aggregator is set
        if (_log_aggregator_id != qb::ActorId()) {
            push<LogEvent>(_log_aggregator_id, LogEvent::Level::INFO, "coordinator", 
                      "New job created of type " + event.job_type);
        }
    }
    
    void on(const JobCompletedEvent& event) {
        if (!_connected) return;
        
        qb::io::cout() << "Coordinator received job completion notification for " 
             << event.job_id << " (success: " << (event.success ? "true" : "false") << ")" << std::endl;
        
        // Log the job completion if log aggregator is set
        if (_log_aggregator_id != qb::ActorId()) {
            push<LogEvent>(_log_aggregator_id, LogEvent::Level::INFO, "coordinator", 
                      "Job " + event.job_id + " completed with status: " + 
                      (event.success ? "success" : "failure"));
        }
    }
    
    void on(const ShutdownEvent&) {
        if (_shutdown_initiated) return;
        
        _completed_clients++;
        qb::io::cout() << "Coordinator received shutdown notification from a client ("
             << _completed_clients << "/" << _expected_clients << " completed)" << std::endl;
        
        if (_completed_clients >= _expected_clients) {
            initiate_shutdown();
        }
    }
    
    void on(const qb::KillEvent&) {
        qb::io::cout() << "Coordinator received kill event" << std::endl;
        
        // Force immediate shutdown
        broadcast<ShutdownEvent>();
    }
    
    void initiate_shutdown() {
        if (_shutdown_initiated) return;
        
        _shutdown_initiated = true;
        qb::io::cout() << "Coordinator initiating system shutdown" << std::endl;
        
        // Log the shutdown
        if (_log_aggregator_id != qb::ActorId()) {
            push<LogEvent>(_log_aggregator_id, LogEvent::Level::INFO, "coordinator", "System shutdown initiated");
        }
        
        // Wait a moment to let final logs be processed
        std::this_thread::sleep_for(std::chrono::seconds(1));
        
        // Send shutdown event to all known workers
        for (auto& worker_id : _active_workers) {
            // Log which workers are being shut down
            qb::io::cout() << "Sending shutdown event to worker " << worker_id << std::endl;
        }
        
        // Broadcast the shutdown event to all actors
        broadcast<ShutdownEvent>();
                
        // And finally ourselves
        std::this_thread::sleep_for(std::chrono::seconds(1));
        kill();
        
        qb::io::cout() << "Shutdown events sent to all actors" << std::endl;
    }
    
    void display_statistics() {
        try {
            // Get queue length
            auto queue_len = _redis.llen("jobs:queue");
            
            // Get worker metrics
            auto worker_metrics = _redis.hgetall("worker:metrics");
            qb::io::cout() << "Worker metrics:" << std::endl;
            
            for (const auto& [worker, count] : worker_metrics) {
                qb::io::cout() << "  " << worker << ": " << count << " jobs" << std::endl;
            }
            
            // Get stream info
            auto stream_len = _redis.xlen("system:logs");
            qb::io::cout() << "Logs stream length: " << stream_len << " entries" << std::endl;
                
            // Get cache keys
            auto cache_keys = _redis.keys("cache:*");
            qb::io::cout() << "Cache entries: " << cache_keys.size() << std::endl;
            
            // Display queue length
            qb::io::cout() << "Jobs queue length: " << queue_len << std::endl;
            
        } catch (const std::exception& e) {
            qb::io::cerr() << "Error displaying statistics: " << e.what() << std::endl;
        }
    }

    // Set the log aggregator ID for sending logs
    void setLogAggregatorId(qb::ActorId id) {
        _log_aggregator_id = id;
    }
};

// Main function to set up the example
int main(int, char**) {
    // Initialize QB async I/O
    qb::io::async::init();

    qb::io::cout() << "Starting Complex Redis Actor System Example" << std::endl;
    
    // Create the engine
    qb::Main engine;
    
    // Create actors on specific cores
    // Core 0 - coordinator and system services
    engine.addActor<CoordinatorActor>(0);
    engine.addActor<CacheManagerActor>(0);
    engine.addActor<LogAggregatorActor>(0);
    
    // Core 1 - worker actors
    engine.addActor<WorkerActor>(1, "worker1", "jobs:queue", 500);
    engine.addActor<WorkerActor>(1, "worker2", "jobs:queue", 500);
    
    // Core 2 - client actors
    engine.addActor<ClientActor>(2, "client1", 50);
    engine.addActor<ClientActor>(2, "client2", 50);
    
    // Run the example - the actors will find each other via g_coordinator_id
    engine.start();
    engine.join();
    
    qb::io::cout() << "Complex Redis Actor System Example completed" << std::endl;
    
    return 0;
} 