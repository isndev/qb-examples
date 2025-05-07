/**
 * @file example10_distributed_computing.cpp
 * @brief Advanced distributed computing system using QB actors for high-performance parallel processing
 * 
 * This example demonstrates a sophisticated distributed computing framework with:
 * - TaskGenerator for creating computational workloads
 * - TaskScheduler for distributing tasks with intelligent load balancing
 * - Multiple WorkerNodes for parallel processing across cores
 * - ResultCollector for aggregating and validating results
 * - SystemMonitor for tracking performance metrics and system health
 * 
 * The system demonstrates advanced concepts like:
 * - Dynamic load balancing
 * - Worker health monitoring
 * - Task prioritization
 * - Result validation
 * - Real-time performance metrics
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
#include <functional>
#include <cmath>

namespace {
    // Global settings
    constexpr int NUM_WORKERS = 4;
    constexpr int NUM_TASK_TYPES = 3;
    constexpr int SIMULATION_DURATION_SECONDS = 30;
    constexpr int TASKS_PER_SECOND = 50;
    
    // Task complexity levels (affects processing time)
    enum class ComplexityLevel {
        SIMPLE = 1,
        MEDIUM = 5,
        COMPLEX = 10,
        VERY_COMPLEX = 20
    };
    
    // Performance tracking
    std::atomic<uint64_t> g_total_tasks{0};
    std::atomic<uint64_t> g_completed_tasks{0};
    std::atomic<uint64_t> g_failed_tasks{0};
    std::atomic<uint64_t> g_total_task_messages{0};
    std::atomic<uint64_t> g_total_result_messages{0};
    
    // System-wide timestamp for simulation time tracking
    uint64_t getCurrentTimestamp() {
        return static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::high_resolution_clock::now().time_since_epoch()
            ).count()
        );
    }
    
    // Generate a unique task ID
    std::string generateTaskId() {
        static std::atomic<uint64_t> next_id{1};
        std::stringstream ss;
        ss << "TASK-" << std::setw(10) << std::setfill('0') << next_id++;
        return ss.str();
    }
    
    // Available task types
    const std::vector<qb::string<32>> TASK_TYPES = {
        "MATRIX_MULTIPLICATION",
        "PRIME_FACTORIZATION",
        "IMAGE_PROCESSING"
    };
    
    // Generate random processing time based on complexity
    double generateProcessingTime(ComplexityLevel level) {
        static std::random_device rd;
        static std::mt19937 gen(rd());
        
        double base_time = static_cast<int>(level) * 0.1; // base time in seconds
        std::uniform_real_distribution<> dist(base_time * 0.8, base_time * 1.2);
        
        return dist(gen);
    }
}

// ═════════════════════════════════════════════════════════════════
// DOMAIN MODELS
// ═════════════════════════════════════════════════════════════════

enum class TaskStatus {
    PENDING,
    ASSIGNED,
    IN_PROGRESS,
    COMPLETED,
    FAILED,
    CANCELED
};

std::string statusToString(TaskStatus status) {
    switch (status) {
        case TaskStatus::PENDING: return "PENDING";
        case TaskStatus::ASSIGNED: return "ASSIGNED";
        case TaskStatus::IN_PROGRESS: return "IN_PROGRESS";
        case TaskStatus::COMPLETED: return "COMPLETED";
        case TaskStatus::FAILED: return "FAILED";
        case TaskStatus::CANCELED: return "CANCELED";
        default: return "UNKNOWN";
    }
}

/**
 * @brief Task model representing a computational job
 */
struct Task {
    qb::string<64> task_id;
    qb::string<32> task_type;
    int priority;
    ComplexityLevel complexity;
    qb::string<256> data; // Input data for processing
    TaskStatus status;
    uint64_t creation_time;
    uint64_t start_time{0};
    uint64_t completion_time{0};
    
    Task() : 
        priority(0), 
        complexity(ComplexityLevel::SIMPLE), 
        status(TaskStatus::PENDING),
        creation_time(getCurrentTimestamp()) {
        task_id = generateTaskId().c_str();
    }
    
    Task(const char* type, int p, ComplexityLevel c, const char* input_data) :
        task_type(type),
        priority(p),
        complexity(c),
        data(input_data),
        status(TaskStatus::PENDING),
        creation_time(getCurrentTimestamp()) {
        task_id = generateTaskId().c_str();
    }
    
    // Get expected processing time based on complexity
    double getExpectedProcessingTime() const {
        return static_cast<int>(complexity) * 0.1;
    }
    
    // Check if the task is high priority
    bool isHighPriority() const {
        return priority > 7;
    }
    
    std::string toString() const {
        std::stringstream ss;
        ss << task_id.c_str() << " | " << task_type.c_str() << " | Priority: " << priority
           << " | Complexity: " << static_cast<int>(complexity)
           << " | Status: " << statusToString(status);
        return ss.str();
    }
};

/**
 * @brief Result model representing the output of a computational task
 */
struct TaskResult {
    qb::string<64> task_id;
    bool success;
    qb::string<1024> result_data;
    uint64_t processing_time; // in microseconds
    
    TaskResult() : success(false), processing_time(0) {}
    
    TaskResult(const char* id, bool s, const char* result, uint64_t time) :
        task_id(id),
        success(s),
        result_data(result),
        processing_time(time) {}
    
    std::string toString() const {
        std::stringstream ss;
        ss << "Result for " << task_id.c_str() 
           << " | Success: " << (success ? "Yes" : "No")
           << " | Processing Time: " << std::fixed << std::setprecision(2) 
           << (processing_time / 1000000.0) << "s";
        return ss.str();
    }
};

/**
 * @brief Worker metrics for tracking performance
 */
struct WorkerMetrics {
    uint64_t total_tasks_processed{0};
    uint64_t total_processing_time{0}; // in microseconds
    uint64_t failed_tasks{0};
    uint64_t successful_tasks{0};
    double average_processing_time{0.0}; // in seconds
    double utilization{0.0}; // percentage of time spent processing
    uint64_t last_heartbeat{0};
    
    void updateAverages() {
        if (total_tasks_processed > 0) {
            average_processing_time = (total_processing_time / 1000000.0) / total_tasks_processed;
        }
    }
    
    std::string toString() const {
        std::stringstream ss;
        ss << "Tasks: " << total_tasks_processed 
           << " | Success Rate: " << std::fixed << std::setprecision(1)
           << (total_tasks_processed > 0 ? (successful_tasks * 100.0 / total_tasks_processed) : 0.0) << "%"
           << " | Avg Time: " << std::fixed << std::setprecision(3) << average_processing_time << "s"
           << " | Utilization: " << std::fixed << std::setprecision(1) << (utilization * 100.0) << "%";
        return ss.str();
    }
};

// ═════════════════════════════════════════════════════════════════
// EVENT MESSAGES
// ═════════════════════════════════════════════════════════════════

// Task-related messages
struct TaskMessage : public qb::Event {
    std::shared_ptr<Task> task;
    
    explicit TaskMessage(const std::shared_ptr<Task>& t) : task(t) {}
};

struct TaskAssignmentMessage : public TaskMessage {
    explicit TaskAssignmentMessage(const std::shared_ptr<Task>& t) : TaskMessage(t) {}
};

struct TaskCancellationMessage : public TaskMessage {
    explicit TaskCancellationMessage(const std::shared_ptr<Task>& t) : TaskMessage(t) {}
};

struct TaskStatusUpdateMessage : public TaskMessage {
    explicit TaskStatusUpdateMessage(const std::shared_ptr<Task>& t) : TaskMessage(t) {}
};

// Result-related messages
struct ResultMessage : public qb::Event {
    TaskResult result;
    
    explicit ResultMessage(const TaskResult& r) : result(r) {}
};

// Worker status messages
struct WorkerStatusMessage : public qb::Event {
    qb::ActorId worker_id;
    WorkerMetrics metrics;
    
    WorkerStatusMessage(qb::ActorId id, const WorkerMetrics& m) : worker_id(id), metrics(m) {}
};

struct WorkerHeartbeatMessage : public qb::Event {
    qb::ActorId worker_id;
    uint64_t timestamp;
    bool is_busy;
    
    WorkerHeartbeatMessage(qb::ActorId id, uint64_t time, bool busy) : 
        worker_id(id), timestamp(time), is_busy(busy) {}
};

// System messages
struct SystemStatsMessage : public qb::Event {
    uint64_t total_tasks;
    uint64_t completed_tasks;
    uint64_t failed_tasks;
    double elapsed_seconds;
    double tasks_per_second;
    
    SystemStatsMessage(uint64_t total, uint64_t completed, uint64_t failed, 
                      double seconds, double throughput) :
        total_tasks(total), completed_tasks(completed), failed_tasks(failed),
        elapsed_seconds(seconds), tasks_per_second(throughput) {}
};

struct InitializeMessage : public qb::Event {};
struct ShutdownMessage : public qb::Event {};

// Define UpdateWorkersMessage at the global level so both main and TaskSchedulerActor can use it
struct UpdateWorkersMessage : public qb::Event {
    std::vector<qb::ActorId> worker_ids;
    explicit UpdateWorkersMessage(const std::vector<qb::ActorId>& ids) : worker_ids(ids) {}
};

// ═════════════════════════════════════════════════════════════════
// SYSTEM ACTORS
// ═════════════════════════════════════════════════════════════════

/**
 * @brief TaskGenerator actor that creates computational tasks
 */
class TaskGeneratorActor : public qb::Actor {
private:
    qb::ActorId _scheduler_id;
    std::mt19937 _rng;
    bool _is_active{false};
    uint64_t _start_time;
    
public:
    explicit TaskGeneratorActor(qb::ActorId scheduler_id) : _scheduler_id(scheduler_id) {
        // Initialize random number generator
        std::random_device rd;
        _rng = std::mt19937(rd());
        
        // Register for message types
        registerEvent<InitializeMessage>(*this);
        registerEvent<ShutdownMessage>(*this);
    }
    
    bool onInit() override {
        qb::io::cout() << "TaskGeneratorActor initialized with ID: " << id() << std::endl;
        return true;
    }
    
    void on(InitializeMessage&) {
        _is_active = true;
        _start_time = getCurrentTimestamp();
        
        // Schedule periodic task generation
        scheduleTaskGeneration();
    }
    
    void on(ShutdownMessage&) {
        qb::io::cout() << "TaskGeneratorActor shutting down" << std::endl;
        _is_active = false;
        kill();
    }
    
private:
    void scheduleTaskGeneration() {
        if (!_is_active) return;
        
        // Calculate time for next batch of tasks
        double seconds_per_task = 1.0 / TASKS_PER_SECOND;
        
        // Schedule next task generation
        qb::io::async::callback([this]() {
            if (_is_active) {
                generateTask();
                scheduleTaskGeneration();
            }
        }, seconds_per_task);
    }
    
    void generateTask() {
        // Random task parameters
        std::uniform_int_distribution<> type_dist(0, NUM_TASK_TYPES - 1);
        std::uniform_int_distribution<> priority_dist(1, 10);
        std::uniform_int_distribution<> complexity_dist(0, 3);
        
        // Generate data size between 10 and 100
        std::uniform_int_distribution<> data_size_dist(10, 100);
        int data_size = data_size_dist(_rng);
        
        // Generate random data
        std::stringstream data_ss;
        std::uniform_int_distribution<> data_dist(0, 9);
        for (int i = 0; i < data_size; ++i) {
            data_ss << data_dist(_rng);
        }
        
        // Create the task
        ComplexityLevel complexity = static_cast<ComplexityLevel>(
            1 << complexity_dist(_rng)  // 1, 2, 4, 8
        );
        
        std::shared_ptr<Task> task = std::make_shared<Task>(
            TASK_TYPES[type_dist(_rng)].c_str(),
            priority_dist(_rng),
            complexity,
            data_ss.str().c_str()
        );
        
        // Send to scheduler
        push<TaskMessage>(_scheduler_id, task);
        
        // Update global statistics
        g_total_tasks++;
        g_total_task_messages++;
    }
};

/**
 * @brief TaskScheduler actor that distributes tasks to workers
 */
class TaskSchedulerActor : public qb::Actor {
private:
    std::vector<qb::ActorId> _worker_ids;
    std::map<qb::ActorId, WorkerMetrics> _worker_metrics;
    std::deque<std::shared_ptr<Task>> _task_queue;
    std::map<qb::string<64>, std::shared_ptr<Task>> _active_tasks;
    bool _is_active{false};
    
    // Load balancing parameters
    uint64_t _last_load_assessment{0};
    const uint64_t LOAD_ASSESSMENT_INTERVAL = 1000000; // 1 second
    
public:
    // Constructor now takes empty vector to be filled later
    TaskSchedulerActor() {
        // Register for message types
        registerEvent<TaskMessage>(*this);
        registerEvent<TaskStatusUpdateMessage>(*this);
        registerEvent<WorkerStatusMessage>(*this);
        registerEvent<WorkerHeartbeatMessage>(*this);
        registerEvent<ResultMessage>(*this);
        registerEvent<InitializeMessage>(*this);
        registerEvent<ShutdownMessage>(*this);
        registerEvent<UpdateWorkersMessage>(*this);
    }
    
    // Method to update worker IDs after construction
    void updateWorkers(const std::vector<qb::ActorId>& worker_ids) {
        _worker_ids = worker_ids;
    }
    
    bool onInit() override {
        qb::io::cout() << "TaskSchedulerActor initialized with ID: " << id() << std::endl;
        return true;
    }
    
    void on(InitializeMessage&) {
        _is_active = true;
        _last_load_assessment = getCurrentTimestamp();
        
        // Schedule load balancing assessment
        scheduleLoadAssessment();
    }
    
    void on(UpdateWorkersMessage& msg) {
        qb::io::cout() << "TaskScheduler received " << msg.worker_ids.size() << " worker IDs" << std::endl;
        _worker_ids = msg.worker_ids;
        // Try to schedule tasks now that we have workers
        scheduleTasks();
    }
    
    void on(TaskMessage& msg) {
        g_total_task_messages++;
        
        // Add task to queue
        _task_queue.push_back(msg.task);
        
        // Attempt to schedule tasks immediately
        scheduleTasks();
    }
    
    void on(TaskStatusUpdateMessage& msg) {
        auto task = msg.task;
        qb::string<64> task_id = task->task_id;
        
        // Update active task status
        if (_active_tasks.find(task_id) != _active_tasks.end()) {
            _active_tasks[task_id] = task;
        }
    }
    
    void on(WorkerStatusMessage& msg) {
        // Update worker metrics
        _worker_metrics[msg.worker_id] = msg.metrics;
    }
    
    void on(WorkerHeartbeatMessage& msg) {
        if (_worker_metrics.find(msg.worker_id) != _worker_metrics.end()) {
            _worker_metrics[msg.worker_id].last_heartbeat = msg.timestamp;
        }
    }
    
    void on(ResultMessage& msg) {
        g_total_result_messages++;
        
        // Remove from active tasks
        qb::string<64> task_id = msg.result.task_id;
        if (_active_tasks.find(task_id) != _active_tasks.end()) {
            _active_tasks.erase(task_id);
        }
        
        // Schedule more tasks since a worker is now free
        scheduleTasks();
    }
    
    void on(ShutdownMessage&) {
        qb::io::cout() << "TaskSchedulerActor shutting down" << std::endl;
        _is_active = false;
        
        // Cancel all pending tasks
        for (auto& task : _task_queue) {
            task->status = TaskStatus::CANCELED;
        }
        
        // Clear queues
        _task_queue.clear();
        _active_tasks.clear();
        
        kill();
    }
    
private:
    void scheduleTasks() {
        if (!_is_active || _task_queue.empty()) return;
        
        // Process high priority tasks first
        std::stable_sort(_task_queue.begin(), _task_queue.end(), 
            [](const std::shared_ptr<Task>& a, const std::shared_ptr<Task>& b) {
                return a->priority > b->priority;
            }
        );
        
        // Find available workers
        for (const auto& worker_id : _worker_ids) {
            if (_task_queue.empty()) break;
            
            auto& metrics = _worker_metrics[worker_id];
            bool worker_is_available = isWorkerAvailable(worker_id);
            
            if (worker_is_available) {
                // Get next task
                auto task = _task_queue.front();
                _task_queue.pop_front();
                
                // Assign to worker
                task->status = TaskStatus::ASSIGNED;
                _active_tasks[task->task_id] = task;
                
                // Send assignment
                push<TaskAssignmentMessage>(worker_id, task);
                
                qb::io::cout() << "Assigned " << task->toString() << " to Worker " << worker_id << std::endl;
            }
        }
    }
    
    bool isWorkerAvailable(qb::ActorId worker_id) {
        if (_worker_metrics.find(worker_id) == _worker_metrics.end()) {
            // No metrics yet, assume available
            return true;
        }
        
        // Check if worker is responding
        const auto& metrics = _worker_metrics[worker_id];
        uint64_t now = getCurrentTimestamp();
        const uint64_t HEARTBEAT_TIMEOUT = 5000000; // 5 seconds
        
        if (now - metrics.last_heartbeat > HEARTBEAT_TIMEOUT) {
            qb::io::cout() << "Warning: Worker " << worker_id << " seems unresponsive!" << std::endl;
            return false;
        }
        
        // Check if worker is not overloaded
        if (metrics.utilization > 0.8) {
            // Worker is busy
            return false;
        }
        
        return true;
    }
    
    void scheduleLoadAssessment() {
        if (!_is_active) return;
        
        // Schedule periodic load assessment
        qb::io::async::callback([this]() {
            if (!_is_active) return;
            
            assessLoadBalance();
            scheduleLoadAssessment();
        }, 1.0); // Check every 1 second
    }
    
    void assessLoadBalance() {
        if (_worker_metrics.empty()) return;
        
        _last_load_assessment = getCurrentTimestamp();
        
        // Calculate average worker utilization
        double total_utilization = 0.0;
        for (const auto& pair : _worker_metrics) {
            total_utilization += pair.second.utilization;
        }
        double avg_utilization = total_utilization / _worker_metrics.size();
        
        // Log load balancing info
        qb::io::cout() << "Load balancing assessment - Avg utilization: "
                 << std::fixed << std::setprecision(1) << (avg_utilization * 100) 
                 << "%, Queued tasks: " << _task_queue.size() 
                 << ", Active tasks: " << _active_tasks.size() << std::endl;
        
        // If queue is backing up, we could potentially add more workers here
    }
};

/**
 * @brief WorkerNode actor that processes computational tasks
 */
class WorkerNodeActor : public qb::Actor {
private:
    qb::ActorId _scheduler_id;
    qb::ActorId _collector_id;
    std::shared_ptr<Task> _current_task;
    WorkerMetrics _metrics;
    bool _is_busy{false};
    bool _is_active{false};
    uint64_t _simulation_start_time;
    uint64_t _busy_start_time{0};
    
public:
    WorkerNodeActor(qb::ActorId scheduler_id, qb::ActorId collector_id) 
        : _scheduler_id(scheduler_id), _collector_id(collector_id) {
        
        // Register for message types
        registerEvent<TaskAssignmentMessage>(*this);
        registerEvent<TaskCancellationMessage>(*this);
        registerEvent<InitializeMessage>(*this);
        registerEvent<ShutdownMessage>(*this);
    }
    
    bool onInit() override {
        qb::io::cout() << "WorkerNodeActor initialized with ID: " << id() << std::endl;
        return true;
    }
    
    void on(InitializeMessage&) {
        _is_active = true;
        _simulation_start_time = getCurrentTimestamp();
        _metrics.last_heartbeat = _simulation_start_time;
        
        // Start sending heartbeats
        scheduleHeartbeat();
        
        // Start sending metrics
        scheduleMetricsUpdate();
    }
    
    void on(TaskAssignmentMessage& msg) {
        if (_is_busy) {
            // Worker is already busy, reject task
            auto task = msg.task;
            task->status = TaskStatus::PENDING;
            push<TaskStatusUpdateMessage>(_scheduler_id, task);
            return;
        }
        
        // Start processing the task
        _current_task = msg.task;
        _current_task->status = TaskStatus::IN_PROGRESS;
        _current_task->start_time = getCurrentTimestamp();
        
        _is_busy = true;
        _busy_start_time = getCurrentTimestamp();
        
        // Update scheduler about task status
        push<TaskStatusUpdateMessage>(_scheduler_id, _current_task);
        
        // Schedule task completion based on complexity
        double processing_time = generateProcessingTime(_current_task->complexity);
        
        qb::io::async::callback([this]() {
            if (!_is_active) return;
            
            completeCurrentTask();
        }, processing_time);
    }
    
    void on(TaskCancellationMessage& msg) {
        if (!_is_busy || !_current_task || _current_task->task_id != msg.task->task_id) {
            return;
        }
        
        // Cancel current task
        _current_task->status = TaskStatus::CANCELED;
        _is_busy = false;
        
        // Send update to scheduler
        push<TaskStatusUpdateMessage>(_scheduler_id, _current_task);
        
        _current_task = nullptr;
    }
    
    void on(ShutdownMessage&) {
        qb::io::cout() << "WorkerNodeActor " << id() << " shutting down" << std::endl;
        _is_active = false;
        
        // Cancel current task if any
        if (_is_busy && _current_task) {
            _current_task->status = TaskStatus::CANCELED;
            push<TaskStatusUpdateMessage>(_scheduler_id, _current_task);
        }
        
        kill();
    }
    
private:
    void completeCurrentTask() {
        if (!_is_busy || !_current_task) return;
        
        uint64_t completion_time = getCurrentTimestamp();
        uint64_t processing_time = completion_time - _current_task->start_time;
        
        // Randomly succeed or fail (95% success rate)
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_real_distribution<> dist(0, 1);
        bool success = dist(gen) < 0.95;
        
        // Update task status
        _current_task->status = success ? TaskStatus::COMPLETED : TaskStatus::FAILED;
        _current_task->completion_time = completion_time;
        
        // Update metrics
        _metrics.total_tasks_processed++;
        _metrics.total_processing_time += processing_time;
        if (success) {
            _metrics.successful_tasks++;
        } else {
            _metrics.failed_tasks++;
        }
        _metrics.updateAverages();
        
        // Generate result data
        std::stringstream result_ss;
        if (success) {
            // Simulated result data
            result_ss << "Processed " << _current_task->task_type.c_str() 
                     << " task with input size " << _current_task->data.size()
                     << " in " << (processing_time / 1000.0) << "ms";
        } else {
            result_ss << "Failed to process task: Error code " << (std::rand() % 100);
        }
        
        // Create result
        TaskResult result(
            _current_task->task_id.c_str(),
            success,
            result_ss.str().c_str(),
            processing_time
        );
        
        // Send to result collector
        push<ResultMessage>(_collector_id, result);
        
        // Send status update to scheduler
        push<TaskStatusUpdateMessage>(_scheduler_id, _current_task);
        
        qb::io::cout() << "Worker " << id() << " completed task " << _current_task->task_id.c_str()
                 << " with status: " << statusToString(_current_task->status) << std::endl;
        
        // Update global counters
        if (success) {
            g_completed_tasks++;
        } else {
            g_failed_tasks++;
        }
        
        // Reset worker state
        _current_task = nullptr;
        _is_busy = false;
    }
    
    void scheduleHeartbeat() {
        if (!_is_active) return;
        
        // Send heartbeat to scheduler
        push<WorkerHeartbeatMessage>(_scheduler_id, id(), getCurrentTimestamp(), _is_busy);
        
        // Schedule next heartbeat
        qb::io::async::callback([this]() {
            if (_is_active) {
                scheduleHeartbeat();
            }
        }, 1.0); // Heartbeat every 1 second
    }
    
    void scheduleMetricsUpdate() {
        if (!_is_active) return;
        
        // Update utilization
        uint64_t now = getCurrentTimestamp();
        uint64_t total_time = now - _simulation_start_time;
        uint64_t busy_time = _metrics.total_processing_time;
        
        if (_is_busy) {
            busy_time += (now - _busy_start_time);
        }
        
        _metrics.utilization = total_time > 0 ? static_cast<double>(busy_time) / total_time : 0.0;
        _metrics.last_heartbeat = now;
        
        // Send metrics to scheduler
        push<WorkerStatusMessage>(_scheduler_id, id(), _metrics);
        
        // Schedule next update
        qb::io::async::callback([this]() {
            if (_is_active) {
                scheduleMetricsUpdate();
            }
        }, 2.0); // Update metrics every 2 seconds
    }
};

/**
 * @brief ResultCollector actor that aggregates and validates results
 */
class ResultCollectorActor : public qb::Actor {
private:
    std::map<qb::string<64>, TaskResult> _results;
    bool _is_active{false};
    
public:
    ResultCollectorActor() {
        // Register for message types
        registerEvent<ResultMessage>(*this);
        registerEvent<InitializeMessage>(*this);
        registerEvent<ShutdownMessage>(*this);
    }
    
    bool onInit() override {
        qb::io::cout() << "ResultCollectorActor initialized with ID: " << id() << std::endl;
        return true;
    }
    
    void on(InitializeMessage&) {
        _is_active = true;
    }
    
    void on(ResultMessage& msg) {
        // Store the result
        _results[msg.result.task_id] = msg.result;
        
        // Log result details
        qb::io::cout() << "Collected: " << msg.result.toString() << std::endl;
    }
    
    void on(ShutdownMessage&) {
        qb::io::cout() << "ResultCollectorActor shutting down" << std::endl;
        
        // Print summary statistics
        qb::io::cout() << "\n===== RESULT SUMMARY =====" << std::endl;
        qb::io::cout() << "Total results collected: " << _results.size() << std::endl;
        
        // Count successful and failed results
        size_t successful = 0;
        uint64_t total_time = 0;
        
        for (const auto& pair : _results) {
            const auto& result = pair.second;
            if (result.success) {
                successful++;
                total_time += result.processing_time;
            }
        }
        
        double success_rate = _results.empty() ? 0.0 : (successful * 100.0 / _results.size());
        double avg_time = successful > 0 ? (total_time / 1000000.0 / successful) : 0.0;
        
        qb::io::cout() << "Successful results: " << successful << " ("
                 << std::fixed << std::setprecision(1) << success_rate << "%)" << std::endl;
        qb::io::cout() << "Average processing time: "
                 << std::fixed << std::setprecision(3) << avg_time << " seconds" << std::endl;
        qb::io::cout() << "===========================" << std::endl;
        
        _is_active = false;
        kill();
    }
};

/**
 * @brief SystemMonitor actor that tracks overall system performance
 */
class SystemMonitorActor : public qb::Actor {
private:
    qb::ActorId _task_generator_id;
    qb::ActorId _scheduler_id;
    qb::ActorId _collector_id;
    std::vector<qb::ActorId> _worker_ids;
    
    uint64_t _start_time;
    bool _is_active{false};
    
public:
    SystemMonitorActor(qb::ActorId generator, qb::ActorId scheduler, 
                      qb::ActorId collector, const std::vector<qb::ActorId>& workers)
        : _task_generator_id(generator), _scheduler_id(scheduler),
          _collector_id(collector), _worker_ids(workers) {
        
        // Register for message types
        registerEvent<SystemStatsMessage>(*this);
        registerEvent<InitializeMessage>(*this);
        registerEvent<ShutdownMessage>(*this);
    }
    
    bool onInit() override {
        qb::io::cout() << "SystemMonitorActor initialized with ID: " << id() << std::endl;
        
        // Start the system
        push<InitializeMessage>(id());
        
        return true;
    }
    
    void on(InitializeMessage&) {
        _is_active = true;
        _start_time = getCurrentTimestamp();
        
        qb::io::cout() << "\n===== DISTRIBUTED COMPUTING SYSTEM STARTING =====" << std::endl;
        qb::io::cout() << "Workers: " << _worker_ids.size() << std::endl;
        qb::io::cout() << "Task Types: " << NUM_TASK_TYPES << std::endl;
        qb::io::cout() << "Target Throughput: " << TASKS_PER_SECOND << " tasks/sec" << std::endl;
        qb::io::cout() << "Simulation Duration: " << SIMULATION_DURATION_SECONDS << " seconds" << std::endl;
        qb::io::cout() << "=================================================" << std::endl;
        
        // Initialize components
        push<InitializeMessage>(_task_generator_id);
        push<InitializeMessage>(_scheduler_id);
        push<InitializeMessage>(_collector_id);
        
        for (const auto& worker_id : _worker_ids) {
            push<InitializeMessage>(worker_id);
        }
        
        // Update the scheduler with worker IDs after initialization
        push<UpdateWorkersMessage>(_scheduler_id, _worker_ids);
        
        // Schedule performance reports
        schedulePerformanceReport();
        
        // Schedule system shutdown
        qb::io::async::callback([this]() {
            if (_is_active) {
                shutdownSystem();
            }
        }, SIMULATION_DURATION_SECONDS);
    }
    
    void on(SystemStatsMessage& msg) {
        // Log the statistics
        qb::io::cout() << "\n===== SYSTEM STATISTICS =====" << std::endl;
        qb::io::cout() << "Total Tasks: " << msg.total_tasks << std::endl;
        qb::io::cout() << "Completed Tasks: " << msg.completed_tasks
                 << " (" << std::fixed << std::setprecision(1) 
                 << (msg.total_tasks > 0 ? (msg.completed_tasks * 100.0 / msg.total_tasks) : 0.0) 
                 << "%)" << std::endl;
        qb::io::cout() << "Failed Tasks: " << msg.failed_tasks
                 << " (" << std::fixed << std::setprecision(1) 
                 << (msg.total_tasks > 0 ? (msg.failed_tasks * 100.0 / msg.total_tasks) : 0.0)
                 << "%)" << std::endl;
        qb::io::cout() << "Elapsed Time: " << std::fixed << std::setprecision(2)
                 << msg.elapsed_seconds << " seconds" << std::endl;
        qb::io::cout() << "Throughput: " << std::fixed << std::setprecision(2)
                 << msg.tasks_per_second << " tasks/sec" << std::endl;
        qb::io::cout() << "===========================" << std::endl;
    }
    
    void on(ShutdownMessage&) {
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
            
            // Calculate throughput
            double tasks_per_second = elapsed_seconds > 0 ? 
                (g_completed_tasks + g_failed_tasks) / elapsed_seconds : 0;
            
            // Send statistics message to self
            push<SystemStatsMessage>(
                id(),
                g_total_tasks.load(),
                g_completed_tasks.load(),
                g_failed_tasks.load(),
                elapsed_seconds,
                tasks_per_second
            );
            
            // Schedule next report
            schedulePerformanceReport();
        }, 2.0); // Report every 2 seconds
    }
    
    void shutdownSystem() {
        qb::io::cout() << "\nDistributed computing system shutting down..." << std::endl;
        
        // Calculate final statistics
        uint64_t current_time = getCurrentTimestamp();
        double elapsed_seconds = (current_time - _start_time) / 1000000.0;
        double tasks_per_second = elapsed_seconds > 0 ? 
            (g_completed_tasks + g_failed_tasks) / elapsed_seconds : 0;
        
        push<SystemStatsMessage>(
            id(),
            g_total_tasks.load(),
            g_completed_tasks.load(),
            g_failed_tasks.load(),
            elapsed_seconds,
            tasks_per_second
        );
        
        // Send shutdown message to all components
        push<ShutdownMessage>(_task_generator_id);
        push<ShutdownMessage>(_scheduler_id);
        
        for (const auto& worker_id : _worker_ids) {
            push<ShutdownMessage>(worker_id);
        }
        
        // Shutdown result collector last to get final statistics
        push<ShutdownMessage>(_collector_id);
        
        // Kill self after a short delay
        qb::io::async::callback([this]() {
            broadcast<ShutdownMessage>();
        }, 0.5);
    }
};

/**
 * Main function to set up and run the distributed computing system
 */
int main() {
    try {
        qb::io::cout() << "Initializing distributed computing system..." << std::endl;
        
        // Create the main engine with multiple cores
        qb::Main engine;
        
        // Step 1: Create ResultCollector (Core 0)
        auto collector_id = engine.addActor<ResultCollectorActor>(0);
        
        // Step 2: Create TaskScheduler (Core 0)
        std::vector<qb::ActorId> worker_ids; // Will be populated after creating workers
        auto scheduler_id = engine.addActor<TaskSchedulerActor>(0);
        
        // Step 3: Create WorkerNodes (distributed across cores)
        for (int i = 0; i < NUM_WORKERS; ++i) {
            // Distribute workers across cores (use modulo 4 for cores 0-3)
            int core_id = i % 4;
            auto worker_id = engine.addActor<WorkerNodeActor>(core_id, scheduler_id, collector_id);
            worker_ids.push_back(worker_id);
        }
        
        // We can't send a message directly to the scheduler before the system starts
        // Instead, we'll update the worker IDs in the SystemMonitor, which will
        // send the message to the scheduler after initialization
        
        // Step 4: Create TaskGenerator (Core 0)
        auto generator_id = engine.addActor<TaskGeneratorActor>(0, scheduler_id);
        
        // Step 5: Create SystemMonitor (Core 0)
        auto monitor_id = engine.addActor<SystemMonitorActor>(
            0, generator_id, scheduler_id, collector_id, worker_ids
        );
        
        // Start the system
        engine.start();
        
        // Wait for completion
        engine.join();
        
        qb::io::cout() << "Distributed computing simulation completed successfully" << std::endl;
        
    } catch (const std::exception& e) {
        qb::io::cerr() << "Exception: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
} 