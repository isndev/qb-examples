/**
 * @file examples/qbm/redis/example7_stream_processor.cpp
 * @example qbm-redis: Redis Streams with Consumer Groups and Actors
 *
 * @brief This advanced example demonstrates using Redis Streams for a scalable data
 * processing pipeline. It features multiple producer actors generating sensor data
 * and multiple consumer actors processing this data in parallel using consumer groups.
 *
 * @details
 * The system architecture includes:
 * 1.  `SensorProducerActor` (multiple instances, e.g., on Core 1):
 *     -   Simulates a sensor device that periodically generates readings (temperature, humidity, pressure).
 *     -   Connects to Redis using `qb::redis::tcp::client`.
 *     -   Adds new sensor readings to a common Redis Stream (e.g., "sensor-readings")
 *         using `_redis.xadd(stream_key, field_value_pairs)`.
 *     -   Uses `_redis.xtrim()` to cap the stream length.
 *     -   Uses `qb::ICallback` for periodic data generation.
 *     -   Notifies the `CoordinatorActor` about generated readings (for progress tracking)
 *         and when its target number of readings is sent (`ProcessingCompleteEvent`).
 * 2.  `StreamConsumerActor` (multiple instances, e.g., on Cores 2 & 3, in different consumer groups):
 *     -   Represents a worker that processes data from the Redis Stream.
 *     -   Connects to Redis using `qb::redis::tcp::client`.
 *     -   Creates or joins a specific consumer group for the "sensor-readings" stream
 *         using `_redis.xgroup_create(stream_key, group_name, start_id, true)`.
 *     -   Periodically polls for new messages assigned to its consumer identity within the group
 *         using `_redis.xreadgroup(stream_key, group_name, consumer_name, ">", count, block_ms)`.
 *     -   Parses the message fields (sensor_id, temperature, etc.) from the `xreadgroup` reply.
 *         (Note: The example shows manual iteration over the `qb::json` reply structure).
 *     -   Simulates data processing, including checking for alert conditions (e.g., high temperature)
 *         and sending `AlertEvent`s to the `CoordinatorActor`.
 *     -   Acknowledges successfully processed messages using `_redis.xack(stream_key, group_name, message_id)`.
 *     -   Uses `qb::ICallback` for its polling and processing loop.
 *     -   Notifies the `CoordinatorActor` upon completion (`ProcessingCompleteEvent`).
 * 3.  `CoordinatorActor` (e.g., on Core 0):
 *     -   Initializes the system, creating producer and consumer actors on various cores.
 *     -   Its `ActorId` is made globally available (`g_coordinator_id`) for other actors.
 *     -   Receives `SensorReadingEvent`s to track the total number of generated readings.
 *     -   Receives `AlertEvent`s from consumers.
 *     -   Receives `ProcessingCompleteEvent` from both producers and consumers.
 *     -   Orchestrates a graceful shutdown of the system by calling `qb::Main::stop()` once all
 *         producers have sent their data and all consumers have indicated completion or idleness.
 *
 * This example showcases a robust, distributed stream processing pattern.
 *
 * QB/QBM Redis Features Demonstrated:
 * - `qb::Actor`, `qb::Main`, `qb::Event`, `qb::ICallback`, `qb::BroadcastId`.
 * - `qb::redis::tcp::client` for stream operations.
 * - Redis Stream Commands:
 *   - `client.xadd(stream, fields_values_vector)`
 *   - `client.xtrim(stream, maxlen)`
 *   - `client.xlen(stream)`
 *   - `client.xgroup_create(stream, group, id, mkstream_flag)`
 *   - `client.xreadgroup(stream, group, consumer, id_or_gt, count, block_ms)` (returns `qb::json`)
 *   - `client.xack(stream, group, message_id, ...)`
 * - Multi-core actor deployment for parallel data production and consumption.
 * - `qb::string<N>` for event data.
 * - `qb::io::cout()`.
 * - `qb::Main::stop()`.
 */

#include <redis/redis.h>
#include <qb/actor.h>
#include <qb/main.h>
#include <qb/io/async.h>
#include <qb/string.h>
#include <qb/json.h>

// Redis Configuration
#define REDIS_URI "tcp://localhost:6379"
static constexpr char SENSOR_STREAM[] = "sensor-readings";
static constexpr int TARGET_READINGS = 1000000; // 500000 readings from each of 2 sensors

// Global atomic counter for tracking processed messages across cores
std::atomic<int> GLOBAL_COUNTER{0};

// Forward declaration of actor IDs for cross-core communication
qb::ActorId g_coordinator_id;

// Sensor reading event for notifying coordinator only
struct SensorReadingEvent : qb::Event {
    qb::string<32> sensor_id;
    double temperature;
    double humidity;
    double pressure;
    
    SensorReadingEvent(const std::string& id, 
                     double temp, 
                     double hum, 
                     double press)
        : sensor_id(id), 
          temperature(temp),
          humidity(hum),
          pressure(press) {}
};

// Alert event for anomalous readings
struct AlertEvent : qb::Event {
    qb::string<32> sensor_id;
    qb::string<32> alert_type;
    qb::string<128> alert_message;
    
    AlertEvent(const std::string& id, 
              const std::string& type, 
              const std::string& message) 
        : sensor_id(id),
          alert_type(type),
          alert_message(message) {}
};

// Shutdown event
struct ShutdownEvent : qb::Event {
    explicit ShutdownEvent() {}
};

// Final stats event when processing is complete
struct ProcessingCompleteEvent : qb::Event {
    qb::string<32> entity_id;
    int processed_count;
    
    ProcessingCompleteEvent(const std::string& id, int count)
        : entity_id(id), processed_count(count) {}
};

// Sensor data generator actor
class SensorProducerActor : public qb::Actor, public qb::ICallback {
private:
    // Actor state
    qb::string<32> _sensor_id;
    int _readings_sent = 0;
    int _target_readings;
    // Random number generator for sensor data
    std::mt19937 _rng;
    std::uniform_real_distribution<> _dist{0.0, 1.0};
    
    // Redis client
    qb::redis::tcp::client _redis;
    bool _connected = false;
    
    // Generate random sensor readings
    double generate_random(double min, double max) {
        std::uniform_real_distribution<> dist(min, max);
        return dist(_rng);
    }

public:
    SensorProducerActor(std::string sensor_id, int target_readings = 20)
        : _sensor_id(sensor_id), 
          _target_readings(target_readings),
          _rng(std::random_device{}()),
          _redis(qb::io::uri(REDIS_URI))
    {
        // Redis client is initialized in the constructor using member initializer
    }
    
    ~SensorProducerActor() noexcept override = default;
    
    bool onInit() override {
        qb::io::cout() << "SensorProducer [" << _sensor_id << "] initialized on core " << getIndex() << std::endl;
        
        // Register for events
        registerEvent<ShutdownEvent>(*this);
        
        // Register for callbacks
        registerCallback(*this);
        
        try {
            // Connect to Redis - use the connect() method of the Redis client
            if (!_redis.connect()) {
                qb::io::cerr() << "SensorProducer [" << _sensor_id 
                     << "] failed to connect to Redis" << std::endl;
                return false;
            }
            
            _connected = true;
            qb::io::cout() << "SensorProducer [" << _sensor_id 
                 << "] connected to Redis" << std::endl;
                
            // Initialize the stream with a special entry to make sure it exists
            std::vector<std::pair<std::string, std::string>> init_entry = {
                {"sensor_id", _sensor_id.c_str()},
                {"type", "initialization"},
                {"timestamp", std::to_string(std::time(nullptr))}
            };
            
            // Using xadd without the third parameter (auto-generated ID)
            auto stream_id = _redis.xadd(SENSOR_STREAM, init_entry);
            
            // Trim the stream to keep it small (maximum 1000 entries)
            _redis.xtrim(SENSOR_STREAM, 1000);
            
            qb::io::cout() << "SensorProducer [" << _sensor_id << "] initialized stream with ID: " 
                 << stream_id.to_string() << std::endl;
            
            return true;
        }
        catch (const std::exception& e) {
            qb::io::cerr() << "SensorProducer Redis error: " << e.what() << std::endl;
            return false;
        }
    }
    
    // Callback to produce data periodically
    void onCallback() override {
        if (!_connected || !is_alive() || _readings_sent >= _target_readings) {
            if (_readings_sent >= _target_readings) {
                qb::io::cout() << "SensorProducer [" << _sensor_id << "] completed target readings" << std::endl;
                // Send completion notification and shutdown when done
                push<ProcessingCompleteEvent>(g_coordinator_id, _sensor_id.c_str(), _readings_sent);
                push<ShutdownEvent>(id());
                unregisterCallback(*this);
            }
            return;
        }
        
        produce_single_reading();
    }
    
    // Generate and send a single data point
    void produce_single_reading() {
        if (!_connected || !is_alive() || _readings_sent >= _target_readings) {
            return;
        }
  
        try {
            // Generate random sensor data
            double temperature = generate_random(15.0, 40.0);  // Celsius
            double humidity = generate_random(30.0, 90.0);     // Percent
            double pressure = generate_random(980.0, 1030.0);  // hPa
            
            // Create sensor data
            std::vector<std::pair<std::string, std::string>> sensor_data = {
                {"sensor_id", _sensor_id.c_str()},
                {"temperature", std::to_string(temperature)},
                {"humidity", std::to_string(humidity)},
                {"pressure", std::to_string(pressure)},
                {"timestamp", std::to_string(std::time(nullptr))}
            };
            
            // Using xadd without the third parameter (auto-generated ID)
            auto id = _redis.xadd(SENSOR_STREAM, sensor_data);
            
            qb::io::cout() << "SensorProducer [" << _sensor_id 
                 << "] added reading " << (_readings_sent + 1) << "/" << _target_readings 
                 << " to stream with ID: " << id.to_string() << std::endl;
            
            _readings_sent++;
            
            // Notify coordinator about reading (for monitoring only)
            push<SensorReadingEvent>(g_coordinator_id, 
                                   _sensor_id.c_str(), 
                                   temperature, 
                                   humidity, 
                                   pressure);
        }
        catch (const std::exception& e) {
            qb::io::cerr() << "Error producing data: " << e.what() << std::endl;
        }
    }
    
    void on(const ShutdownEvent&) {
        qb::io::cout() << "SensorProducer [" << _sensor_id << "] shutting down after sending " 
             << _readings_sent << " readings" << std::endl;
        
        // Unregister callback to stop producing data
        unregisterCallback(*this);
        
        kill();
    }
};

// Coordinator actor to manage the example
class CoordinatorActor : public qb::Actor {
private:
    // Redis client
    qb::redis::tcp::client _redis;
    bool _connected = false;
    bool _shutdown_initiated = false;
    
    // Statistics
    int _total_readings = 0;
    int _target_readings = TARGET_READINGS;
    int _alert_count = 0;
    
    // Producer/consumer stats
    std::vector<std::pair<std::string, int>> _entity_stats;
    int _completed_entities = 0;
    int _expected_entities = 4;  // 2 producers + 2 consumers

public:
    CoordinatorActor() 
        : _redis(qb::io::uri(REDIS_URI))
    {
        // Redis client is initialized in the constructor using member initializer
    }
    
    ~CoordinatorActor() noexcept override = default;
    
    bool onInit() override {
        qb::io::cout() << "CoordinatorActor initialized on core " << getIndex() << std::endl;
        
        // Register for events
        registerEvent<SensorReadingEvent>(*this);
        registerEvent<AlertEvent>(*this);
        registerEvent<ProcessingCompleteEvent>(*this);
        registerEvent<ShutdownEvent>(*this);
        registerEvent<qb::KillEvent>(*this);
        
        try {
            // Connect to Redis
            if (!_redis.connect()) {
                qb::io::cerr() << "Coordinator failed to connect to Redis" << std::endl;
                return false;
            }
            
            _connected = true;
            qb::io::cout() << "Coordinator connected to Redis" << std::endl;
            
            // Clean up any existing stream data
            try {
                _redis.del(SENSOR_STREAM);
                qb::io::cout() << "Coordinator deleted existing stream" << std::endl;
            }
            catch (const std::exception&) {
                // Stream might not exist, which is fine
            }
            
            // Store our ID globally for other actors to use
            g_coordinator_id = id();
            qb::io::cout() << "Set global coordinator ID: " << g_coordinator_id << std::endl;
            
            return true;
        }
        catch (const std::exception& e) {
            qb::io::cerr() << "Coordinator initialization error: " << e.what() << std::endl;
            return false;
        }
    }
    
    // Only monitoring data flow, not forwarding events
    void on(const SensorReadingEvent& event) {
        qb::io::cout() << "Coordinator received notification for reading from " << event.sensor_id 
             << ": temp=" << event.temperature 
             << ", humid=" << event.humidity 
             << ", press=" << event.pressure
             << " (Total: " << ++_total_readings << "/" << _target_readings << ")" << std::endl;
        
        // Check if we've reached our target
        if (_total_readings >= _target_readings && !_shutdown_initiated) {
            qb::io::cout() << "Received all expected readings, waiting for processing to complete..." << std::endl;
            
            // If consumers haven't reported completion yet, give them some time
            if (_completed_entities < _expected_entities) {
                qb::io::async::callback([this]() {
                    // Check again after a delay
                    if (_completed_entities < _expected_entities && !_shutdown_initiated) {
                        qb::io::cout() << "Notifying consumers to report completion..." << std::endl;
                        // If consumers haven't completed yet, send them a signal to complete
                        // Comme getActorsByCore n'existe pas, on utilise une autre approche pour notifier les consommateurs
                        for (int i = 2; i <= 3; i++) {
                            // Alternative: Utiliser un évènement de shutdown global
                            qb::io::cout() << "Broadcasting shutdown to core " << i << std::endl;
                            push<ShutdownEvent>(qb::BroadcastId(i)); // Broadcast à tous les acteurs sur le core i
                        }
                    }
                }, 2.0); // Wait 2 seconds before notifying consumers
            }
        }
    }
    
    void on(const AlertEvent& event) {
        qb::io::cout() << "Coordinator received alert: " << event.alert_type 
             << " for " << event.sensor_id << ": " 
             << event.alert_message << std::endl;
        
        _alert_count++;
    }
    
    void on(const ProcessingCompleteEvent& event) {
        qb::io::cout() << "Coordinator received completion from " << event.entity_id 
             << " with " << event.processed_count << " messages" << std::endl;
        
        // Add to entity stats
        _entity_stats.emplace_back(event.entity_id.c_str(), event.processed_count);
        _completed_entities++;
        
        qb::io::cout() << "Completed entities: " << _completed_entities << "/" << _expected_entities 
             << " (GLOBAL_COUNTER: " << GLOBAL_COUNTER << ")" << std::endl;
        
        // Check if all entities have completed and we have our target readings
        if (_completed_entities >= _expected_entities) {
            if (!_shutdown_initiated) {
                _shutdown_initiated = true;
                display_statistics();
                qb::io::async::callback([this]() {
                    qb::Main::stop();
                }, 1.0);
            }
        }
    }
    
    void display_statistics() {
        qb::io::cout() << "\n=== Final Statistics ===" << std::endl;
        qb::io::cout() << "Total readings received: " << _total_readings << std::endl;
        qb::io::cout() << "Total alerts detected: " << _alert_count << std::endl;
        qb::io::cout() << "Total messages processed (GLOBAL_COUNTER): " << GLOBAL_COUNTER << std::endl;
        
        qb::io::cout() << "\nEntity statistics:" << std::endl;
        for (const auto& [entity, count] : _entity_stats) {
            qb::io::cout() << "  " << entity << ": " << count << " messages" << std::endl;
        }
        
        qb::io::cout() << "\nSystem summary:" << std::endl;
        qb::io::cout() << "  Stream operations completed successfully" << std::endl;
        
        qb::io::cout() << "\nExample completed successfully!" << std::endl;
    }
    
    void on(const ShutdownEvent&) {
        if (!_shutdown_initiated) {
            _shutdown_initiated = true;
            qb::io::cout() << "Coordinator received shutdown request" << std::endl;
            display_statistics();
            qb::Main::stop();
        }
    }
    
    void on(const qb::KillEvent&) {
        qb::io::cout() << "Coordinator received kill event" << std::endl;
        kill();
    }
};

// Consumer group worker actor - processes stream data
class StreamConsumerActor : public qb::Actor, public qb::ICallback {
private:
    // Redis client
    qb::redis::tcp::client _redis;
    
    // Actor state
    bool _connected = false;
    bool _group_created = false;
    qb::string<32> _group_name;
    qb::string<32> _consumer_name;
    
    // Processing state
    int _processed_count = 0;
    bool _shutdown_ready = false;
    double _alert_threshold = 35.0;  // Temperature threshold for alerts
    int _empty_results_count = 0;    // Track consecutive empty results
    const int MAX_EMPTY_RESULTS = 5; // Threshold for completion check

public:
    StreamConsumerActor(std::string group_name, std::string consumer_name, 
                      double alert_threshold = 35.0) 
        : _group_name(group_name), 
          _consumer_name(consumer_name),
          _alert_threshold(alert_threshold),
          _redis(qb::io::uri(REDIS_URI))
    {
        // Redis client is initialized in the constructor using member initializer
    }
    
    ~StreamConsumerActor() noexcept override = default;
    
    bool onInit() override {
        qb::io::cout() << "StreamConsumer [" << _consumer_name << "] in group [" 
             << _group_name << "] initialized on core " << getIndex() << std::endl;
        
        // Register for events from coordinator
        registerEvent<ShutdownEvent>(*this);
        
        // Register for callback
        registerCallback(*this);
        
        try {
            // Connect to Redis
            if (!_redis.connect()) {
                qb::io::cerr() << "StreamConsumer [" << _consumer_name << "] failed to connect to Redis" << std::endl;
                return false;
            }
            
            _connected = true;
            qb::io::cout() << "StreamConsumer [" << _consumer_name << "] connected to Redis" << std::endl;
            
            // Create the consumer group
            if (create_consumer_group()) {
                qb::io::cout() << "StreamConsumer [" << _consumer_name << "] created/joined consumer group" << std::endl;
            } else {
                qb::io::cerr() << "StreamConsumer [" << _consumer_name << "] failed to create consumer group" << std::endl;
                return false;
            }
            
            return true;
        }
        catch (const std::exception& e) {
            qb::io::cerr() << "StreamConsumer initialization error: " << e.what() << std::endl;
            return false;
        }
    }
    
    // Required onCallback implementation for ICallback
    void onCallback() override {
        if (!_connected || !is_alive() || _shutdown_ready) {
            return;
        }
        
        // Read and process messages from the stream
        poll_stream_for_messages();
        
        // Log status
        if (_processed_count > 0 && _processed_count % 10 == 0) {
            qb::io::cout() << "StreamConsumer [" << _consumer_name 
                 << "] active, processed " << _processed_count 
                 << " messages (GLOBAL: " << GLOBAL_COUNTER << ")" << std::endl;
        }
    }
    
    // Poll for new messages from Redis Stream
    void poll_stream_for_messages() {
        try {
            // Using xreadgroup with the appropriate parameters
            auto results = _redis.xreadgroup(SENSOR_STREAM, _group_name.c_str(), 
                                         _consumer_name.c_str(), ">", 1000, 100);
            
            // If results are empty, increment the counter
            if (results.empty() || results.is_null()) {
                _empty_results_count++;
                
                // If we've seen several empty results in a row, we might be done
                if (_empty_results_count > MAX_EMPTY_RESULTS && GLOBAL_COUNTER >= TARGET_READINGS && !_shutdown_ready) {
                    qb::io::cout() << "StreamConsumer [" << _consumer_name 
                        << "] no more messages, shutting down" << std::endl;
                    push<ProcessingCompleteEvent>(g_coordinator_id, _consumer_name.c_str(), _processed_count);
                    push<ShutdownEvent>(id());
                }
                return;
            }
            
            // Reset empty counter since we got messages
            _empty_results_count = 0;
            
            // Correct processing of the Redis Stream JSON format:
            // [{"stream-key":[{"message-id":{"field1":"value1","field2":"value2"}}, ...]}]
            if (results.is_array() && !results.empty()) {
                // Loop through each element in the outer array
                for (const auto& stream_obj : results) {
                    // Loop through each key-value pair in the object (stream_name:messages)
                    for (auto it = stream_obj.begin(); it != stream_obj.end(); ++it) {
                        std::string stream_key = it.key();
                        
                        // Check if this is our stream
                        if (stream_key.find(SENSOR_STREAM) != std::string::npos) {
                            const auto& messages = it.value();
                            
                            // Loop through the messages
                            for (const auto& msg_entry : messages) {
                                // Each msg_entry is an object {"message-id":{...fields...}}
                                for (auto msg_it = msg_entry.begin(); msg_it != msg_entry.end(); ++msg_it) {
                                    // Extract the message ID
                                    std::string message_id = msg_it.key();
                                    
                                    // Access the message fields
                                    auto& fields = msg_it.value();
                                    
                                    // Skip entry if it's not an object (to handle irregular formats)
                                    if (!fields.is_object()) continue;

                                    // Extract data from fields (now organized as a normal JSON object)
                                    std::string sensor_id;
                                    double temperature = 0.0;
                                    double humidity = 0.0;
                                    double pressure = 0.0;
                                    
                                    // Check if this is an initialization message
                                    if (fields.contains("type") && fields["type"] == "initialization") {
                                        continue; // Skip initialization messages
                                    }
                                    
                                    // Extract fields directly from the JSON object
                                    if (fields.contains("sensor_id")) {
                                        sensor_id = fields["sensor_id"].dump();
                                        // Remove quotes if present
                                        if (sensor_id.front() == '"' && sensor_id.back() == '"') {
                                            sensor_id = sensor_id.substr(1, sensor_id.size() - 2);
                                        }
                                    }
                                    
                                    if (fields.contains("temperature")) {
                                        std::string temp_str = fields["temperature"].dump();
                                        // Clean string if necessary (remove quotes)
                                        if (temp_str.front() == '"' && temp_str.back() == '"') {
                                            temp_str = temp_str.substr(1, temp_str.size() - 2);
                                        }
                                        temperature = std::stod(temp_str);
                                    }
                                    
                                    if (fields.contains("humidity")) {
                                        std::string humid_str = fields["humidity"].dump();
                                        // Clean string if necessary (remove quotes)
                                        if (humid_str.front() == '"' && humid_str.back() == '"') {
                                            humid_str = humid_str.substr(1, humid_str.size() - 2);
                                        }
                                        humidity = std::stod(humid_str);
                                    }
                                    
                                    if (fields.contains("pressure")) {
                                        std::string press_str = fields["pressure"].dump();
                                        // Clean string if necessary (remove quotes)
                                        if (press_str.front() == '"' && press_str.back() == '"') {
                                            press_str = press_str.substr(1, press_str.size() - 2);
                                        }
                                        pressure = std::stod(press_str);
                                    }
                                    
                                    // If we have a sensor ID, process the data
                                    if (!sensor_id.empty()) {
                                        // Process the extracted data
                                        process_extracted_data(message_id, sensor_id, temperature, humidity, pressure);
                                        
                                        // Acknowledge the message using xack
                                        _redis.xack(SENSOR_STREAM, _group_name.c_str(), message_id);
                                    }
                                }
                            }
                        }
                    }
                }
            }
            
            // Check if we've reached the global target
            if (GLOBAL_COUNTER >= TARGET_READINGS && !_shutdown_ready) {
                qb::io::cout() << "StreamConsumer [" << _consumer_name 
                    << "] global target reached: " << GLOBAL_COUNTER << "/" << TARGET_READINGS << std::endl;
                push<ProcessingCompleteEvent>(g_coordinator_id, _consumer_name.c_str(), _processed_count);
            }
        }
        catch (const std::exception& e) {
            qb::io::cerr() << "StreamConsumer [" << _consumer_name << "] error polling stream: " 
                << e.what() << std::endl;
        }
    }
    
    // Process extracted data from a stream message
    void process_extracted_data(const std::string& message_id, const std::string& sensor_id, 
                               double temperature, double humidity, double pressure) {
        // Skip if it's not a sensor reading (e.g., initialization entry)
        if (sensor_id.empty() || temperature == 0.0) {
            return;
        }
        
        qb::io::cout() << "StreamConsumer [" << _consumer_name 
             << "] processing message " << message_id 
             << " from sensor " << sensor_id
             << " (Temp: " << temperature
             << ", Humid: " << humidity
             << ", Press: " << pressure << ")" << std::endl;
        
        // Check for thresholds and send alerts
        if (temperature > _alert_threshold) {
            qb::io::cout() << "StreamConsumer [" << _consumer_name 
                 << "] ALERT: High temperature " << temperature 
                 << " from sensor " << sensor_id << std::endl;
            
            // Send alert to coordinator
            if (_group_name == "alerts-group") {
                std::string alert_message = "High temperature: " + 
                                          std::to_string(temperature) + "°C";
                push<AlertEvent>(g_coordinator_id, sensor_id, "HIGH_TEMP", alert_message);
            }
        }
        
        // Increment counters
        _processed_count++;
        GLOBAL_COUNTER++;
        
        qb::io::cout() << "StreamConsumer [" << _consumer_name 
            << "] counter incremented to " << _processed_count 
            << " (GLOBAL_COUNTER: " << GLOBAL_COUNTER << ")" << std::endl;
    }
    
    bool create_consumer_group() {
        if (_group_created) {
            return true;
        }
        
        try {
            // Check if stream exists with xlen
            auto stream_length = _redis.xlen(SENSOR_STREAM);
            bool stream_exists = (stream_length > 0);
            
            if (!stream_exists) {
                // Prepare initialization entry for the stream if it doesn't exist
                std::vector<std::pair<std::string, std::string>> init_fields = {
                    {"init", "true"},
                    {"timestamp", std::to_string(std::time(nullptr))},
                    {"source", "consumer"},
                    {"consumer_name", _consumer_name.c_str()}
                };
                
                // Add initialization entry to the stream using xadd
                _redis.xadd(SENSOR_STREAM, init_fields);
                qb::io::cout() << "StreamConsumer [" << _consumer_name 
                    << "] initialized stream" << std::endl;
            }
            
            // Create the consumer group starting from the beginning of the stream
            // (with mkstream=true to create the stream if it doesn't exist)
            _redis.xgroup_create(SENSOR_STREAM, _group_name.c_str(), "0", true);
            qb::io::cout() << "StreamConsumer [" << _consumer_name 
                << "] created group " << _group_name << std::endl;
            
            _group_created = true;
            return true;
        }
        catch (const std::exception& e) {
            qb::io::cerr() << "StreamConsumer [" << _consumer_name << "] error in create_consumer_group: " 
                << e.what() << std::endl;
            
            // If group already exists, this is fine
            if (std::string(e.what()).find("BUSYGROUP") != std::string::npos) {
                qb::io::cout() << "StreamConsumer [" << _consumer_name 
                    << "] group already exists, considering it as created" << std::endl;
                _group_created = true;
                return true;
            }
            return false;
        }
    }
    
    void on(const ShutdownEvent&) {
        _shutdown_ready = true;
        qb::io::cout() << "StreamConsumer [" << _consumer_name << "] shutting down. "
             << "Processed " << _processed_count << " messages" << std::endl;
        
        // Send notification about processed messages if not already sent
        push<ProcessingCompleteEvent>(g_coordinator_id, _consumer_name.c_str(), _processed_count);
        
        // Stop the callback
        unregisterCallback(*this);
        
        // Delay to ensure last messages are processed
        qb::io::async::callback([this]() {
            qb::io::cout() << "StreamConsumer [" << _consumer_name << "] final count: " 
                 << _processed_count << " messages" << std::endl;
            kill();
        }, 0.5);
    }
};

int main() {
    // Initialize the async system
    qb::io::async::init();
    qb::io::cout() << "Starting Redis Stream Processor Example" << std::endl;
    
    // Create the engine
    qb::Main engine;
    
    // Add coordinator actor to core 0
    auto coordinator_id = engine.addActor<CoordinatorActor>(0);
    qb::io::cout() << "Created coordinator actor on core 0: " << coordinator_id << std::endl;
    // Make coordinator ID globally accessible
    g_coordinator_id = coordinator_id;
    
    // Create actor builders for the producer actors on core 1
    auto core_1 = engine.core(1).builder();
    // Add producers actors to core 1 using the builders
    auto producer_ids = core_1
                            .addActor<SensorProducerActor>("sensor001", 500000)
                            .addActor<SensorProducerActor>("sensor002", 500000)
                            .idList();
    qb::io::cout() << "Created producer actors on core 1: " << std::endl
         << "  Producer 1: " << producer_ids[0] << std::endl
         << "  Producer 2: " << producer_ids[1] << std::endl;
    
    // Create actors for the consumer actors on cores 2 and 3
    auto consumer_ids = qb::actor_id_list{
        engine.addActor<StreamConsumerActor>(2, "alerts-group", "consumer1", 35.0),
        engine.addActor<StreamConsumerActor>(3, "analytics-group", "consumer2", 35.0)
    };
    
    qb::io::cout() << "Created consumer actors: " << std::endl
         << "  Consumer 1 (alerts-group) on core 2: " << consumer_ids[0] << std::endl
         << "  Consumer 2 (analytics-group) on core 3: " << consumer_ids[1] << std::endl;
    
    // Start the engine
    engine.start(true);
    qb::io::cout() << "Engine started, actors running on multiple cores..." << std::endl;
    
    // Wait for engine to stop (when coordinator signals completion)
    engine.join();
    
    qb::io::cout() << "Engine stopped, all actors terminated" << std::endl;
    qb::io::cout() << "Redis Stream Processor Example completed" << std::endl;
    
    return 0;
} 