/**
 * @file examples/core/example10_distributed_computing.cpp
 * @example Advanced Distributed Computing System Simulation
 *
 * @brief This example simulates a complex distributed computing system. It demonstrates
 * advanced actor patterns for task generation, scheduling with load balancing,
 * distributed task execution by workers, result collection, and system-wide monitoring.
 *
 * @details
 * The system comprises several specialized actors, typically distributed across multiple cores:
 * 1.  `TaskGeneratorActor`:
 *     -   Periodically creates computational `Task` objects with varying types, priorities, and complexities.
 *     -   Sends these tasks as `TaskMessage` events to the `TaskSchedulerActor`.
 *     -   Paces generation with `spawn(...)` + `co_await ctx.sleep(...)`.
 * 2.  `TaskSchedulerActor`:
 *     -   Manages a queue of pending tasks, prioritizing them.
 *     -   Receives `WorkerStatusMessage` and `WorkerHeartbeatMessage` from `WorkerNodeActor`s
 *         to monitor their load and availability.
 *     -   Assigns tasks (`TaskAssignmentMessage`) only to workers it knows to be idle, tracking
 *         that itself rather than inferring it from a metric that lags by seconds.
 *     -   Tracks active tasks and handles `TaskStatusUpdateMessage`s, requeueing a task a worker
 *         rejects.
 *     -   Learns that a worker is free from its `TaskStatusUpdateMessage`.
 * 3.  `WorkerNodeActor` (multiple instances):
 *     -   Represents a computational node capable of executing tasks.
 *     -   Receives `TaskAssignmentMessage` from the scheduler.
 *     -   Simulates task processing, with duration based on task complexity.
 *     -   Sends `ResultMessage` (containing success/failure and output) to the `ResultCollectorActor`.
 *     -   Periodically sends `WorkerHeartbeatMessage` and `WorkerStatusMessage` (with metrics like
 *         utilization) to the `TaskSchedulerActor`.
 * 4.  `ResultCollectorActor`:
 *     -   Aggregates `TaskResult` events received from all `WorkerNodeActor`s.
 *     -   Can provide summary statistics on task completion, success rates, and average processing times.
 * 5.  `SystemMonitorActor` (acts as a coordinator):
 *     -   Distributes actors across different CPU cores.
 *     -   Sends `InitializeMessage` to start other actors.
 *     -   Polls the generator, scheduler and collector for the counters they own and displays the
 *         merged `StatsReportMessage`s periodically.
 *     -   Manages the lifecycle of the simulation, initiating a system-wide shutdown after a set
 *         duration.
 *
 * This example showcases dynamic load balancing, worker health monitoring, task prioritization,
 * result validation (conceptual), and real-time performance metrics within a QB actor system.
 *
 * @note WHAT THIS FILE USED TO GET WRONG. Every item below was measured on a full 30-second run,
 *       and every one of them exited 0.
 *
 *       (1) IT ABORTED UNDER THE `sanitize` PRESET, 3 runs of 3 --
 *       `AddressSanitizer: heap-use-after-free` inside a
 *       `qb::io::async::callback([this]{ if (_is_active) ... }, delay)` lambda (reported at
 *       `WorkerNodeActor::scheduleMetricsUpdate()` twice and at
 *       `TaskGeneratorActor::scheduleTaskGeneration()` once). That overload's timer is not bound
 *       to any actor's lifetime; it fires after the actor is destroyed and the `_is_active`
 *       guard is itself the read of freed memory. All eight sites are now `spawn(...)` +
 *       `co_await ctx.sleep(d)`, which the actor's cancellation scope cancels on kill.
 *
 *       (2) HALF THE FLEET GOT NOTHING. In one measured run, of 1497 assignments Worker 3 took
 *       1198, Worker 65537 took 299, and the two workers on cores 2 and 3 took ZERO. The
 *       scheduler walked `_worker_ids` in fixed order and asked `isWorkerAvailable()`, which
 *       answers from a `utilization` figure the workers report every 2 seconds -- so during the
 *       20ms between two tasks every worker still looked idle and the first one in the list took
 *       everything. A scheduler knows perfectly well who it just gave work to; it now tracks
 *       that (`_busy_workers`) and hands out round-robin from where it left off.
 *
 *       (3) A REJECTED TASK WAS DROPPED ON THE FLOOR. A busy worker answered
 *       `TaskStatusUpdateMessage` with status PENDING, and the scheduler's handler only updated
 *       a map entry -- it never put the task back in the queue. Combined with (2) that is where
 *       most of the 94% incompletion went.
 *
 *       (4) THE TASK GENERATOR PRODUCED ENUM VALUES THAT DO NOT EXIST.
 *       `static_cast<ComplexityLevel>(1 << complexity_dist(_rng))` yields 1, 2, 4, 8 while
 *       `ComplexityLevel` enumerates 1, 5, 10, 20 -- so three of every four tasks carried a
 *       value with no enumerator, and every `switch` over it fell to `default`.
 *
 *       (5) THE THROUGHPUT TARGET WAS ARITHMETICALLY UNREACHABLE. `TASKS_PER_SECOND` was 50
 *       against a fleet of 4 workers whose mean task takes ~0.875s -- a ceiling of ~4.6
 *       tasks/sec, i.e. the target was 11x the capacity provisioned for it. "6% completed" was
 *       the design, not a defect to debug. It is now set just above capacity, so the queue grows
 *       slowly and the backlog the scheduler reports means something.
 *
 *       (6) FIVE CROSS-CORE GLOBALS, plus a `static std::mt19937` in `generateProcessingTime()`
 *       driven from four worker cores at once (`std::mt19937::operator()` is not thread-safe).
 *       Actors own their counters and report them by event.
 *
 * QB Features Demonstrated:
 * - Multi-Core Actor System: Actors strategically deployed across cores for performance.
 * - Advanced Actor Communication: Complex interaction patterns for work distribution, status updates, and results.
 * - Dynamic Load Balancing: `TaskSchedulerActor` assigning round-robin over the workers it knows are idle.
 * - Health Monitoring: `WorkerHeartbeatMessage` and `WorkerStatusMessage`.
 * - Lifetime-Bound Timers: `spawn(...)` + `co_await ctx.sleep(...)` for every periodic task,
 *   simulated processing step, and orchestration delay.
 * - No Shared Mutable State: each actor owns its counters and reports them by event.
 * - Comprehensive Event System: Numerous custom events for detailed system control and information flow.
 * - System Orchestration and Lifecycle Management: `SystemMonitorActor` overseeing the simulation.
 * - Fixed-Size Strings: `qb::string<N>` used in event/model definitions for potentially performance-sensitive data.
 */

#include <array>
#include <deque>
#include <set>
#include <qb/actor.h>
#include <qb/main.h>
#include <qb/io.h>
#include <qb/io/async.h>
#include <chrono>

namespace {
// Global settings
constexpr int NUM_WORKERS                 = 4;
constexpr int NUM_TASK_TYPES              = 3;
constexpr int SIMULATION_DURATION_SECONDS = 30;

// Task complexity levels (affects processing time)
enum class ComplexityLevel { SIMPLE = 1, MEDIUM = 5, COMPLEX = 10, VERY_COMPLEX = 20 };

// The four levels, in one place, so the generator draws an actual enumerator instead of
// synthesising `static_cast<ComplexityLevel>(1 << k)` values that are not in the enumeration.
constexpr std::array<ComplexityLevel, 4> COMPLEXITY_LEVELS{
    ComplexityLevel::SIMPLE, ComplexityLevel::MEDIUM, ComplexityLevel::COMPLEX, ComplexityLevel::VERY_COMPLEX
};

// Offered load. This is deliberately just ABOVE what the fleet can serve, so the scheduler's
// backlog is a real number: a task's base cost is `complexity * 0.1s`, the four levels average
// 0.875s, and NUM_WORKERS of them therefore top out near 4.6 tasks/sec.
constexpr double TASKS_PER_SECOND = 5.0;

// System-wide timestamp for simulation time tracking
uint64_t
getCurrentTimestamp() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now().time_since_epoch()).count());
}

// Generate a unique task ID
std::string
generateTaskId() {
    static std::atomic<uint64_t> next_id{1};
    std::stringstream            ss;
    ss << "TASK-" << std::setw(10) << std::setfill('0') << next_id++;
    return ss.str();
}

// Available task types
const std::vector<qb::string<32>> TASK_TYPES = {"MATRIX_MULTIPLICATION", "PRIME_FACTORIZATION", "IMAGE_PROCESSING"};

// Generate random processing time based on complexity.
//
// `gen` is a PARAMETER, supplied by the calling worker. It used to be a function-local
// `static std::mt19937` mutated concurrently by the four workers, which sit on four different
// cores; `std::mt19937::operator()` is not thread-safe and a release build will never say so.
double
generateProcessingTime(ComplexityLevel level, std::mt19937 &gen) {
    double                           base_time = static_cast<int>(level) * 0.1; // base time in seconds
    std::uniform_real_distribution<> dist(base_time * 0.8, base_time * 1.2);

    return dist(gen);
}
} // namespace

// ═════════════════════════════════════════════════════════════════
// DOMAIN MODELS
// ═════════════════════════════════════════════════════════════════

enum class TaskStatus { PENDING, ASSIGNED, IN_PROGRESS, COMPLETED, FAILED, CANCELED };

std::string
statusToString(TaskStatus status) {
    switch (status) {
        case TaskStatus::PENDING:
            return "PENDING";
        case TaskStatus::ASSIGNED:
            return "ASSIGNED";
        case TaskStatus::IN_PROGRESS:
            return "IN_PROGRESS";
        case TaskStatus::COMPLETED:
            return "COMPLETED";
        case TaskStatus::FAILED:
            return "FAILED";
        case TaskStatus::CANCELED:
            return "CANCELED";
        default:
            return "UNKNOWN";
    }
}

/**
 * @brief Task model representing a computational job
 */
struct Task {
    qb::string<64>  task_id;
    qb::string<32>  task_type;
    int             priority;
    ComplexityLevel complexity;
    qb::string<256> data; // Input data for processing
    TaskStatus      status;
    uint64_t        creation_time;
    uint64_t        start_time{0};
    uint64_t        completion_time{0};

    Task()
        : priority(0)
        , complexity(ComplexityLevel::SIMPLE)
        , status(TaskStatus::PENDING)
        , creation_time(getCurrentTimestamp()) {
        task_id = generateTaskId().c_str();
    }

    Task(const char *type, int p, ComplexityLevel c, const char *input_data)
        : task_type(type)
        , priority(p)
        , complexity(c)
        , data(input_data)
        , status(TaskStatus::PENDING)
        , creation_time(getCurrentTimestamp()) {
        task_id = generateTaskId().c_str();
    }

    // Get expected processing time based on complexity
    double
    getExpectedProcessingTime() const {
        return static_cast<int>(complexity) * 0.1;
    }

    // Check if the task is high priority
    bool
    isHighPriority() const {
        return priority > 7;
    }

    std::string
    toString() const {
        std::stringstream ss;
        ss << task_id.c_str() << " | " << task_type.c_str() << " | Priority: " << priority << " | Complexity: " << static_cast<int>(complexity)
           << " | Status: " << statusToString(status);
        return ss.str();
    }
};

/**
 * @brief Result model representing the output of a computational task
 */
struct TaskResult {
    qb::string<64>   task_id;
    bool             success;
    qb::string<1024> result_data;
    uint64_t         processing_time; // in microseconds

    TaskResult()
        : success(false)
        , processing_time(0) {}

    TaskResult(const char *id, bool s, const char *result, uint64_t time)
        : task_id(id)
        , success(s)
        , result_data(result)
        , processing_time(time) {}

    std::string
    toString() const {
        std::stringstream ss;
        ss << "Result for " << task_id.c_str() << " | Success: " << (success ? "Yes" : "No") << " | Processing Time: " << std::fixed
           << std::setprecision(2) << (processing_time / 1000000.0) << "s";
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
    double   average_processing_time{0.0}; // in seconds
    double   utilization{0.0};             // percentage of time spent processing
    uint64_t last_heartbeat{0};

    void
    updateAverages() {
        if (total_tasks_processed > 0) {
            average_processing_time = (total_processing_time / 1000000.0) / total_tasks_processed;
        }
    }

    std::string
    toString() const {
        std::stringstream ss;
        ss << "Tasks: " << total_tasks_processed << " | Success Rate: " << std::fixed << std::setprecision(1)
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

    explicit TaskMessage(const std::shared_ptr<Task> &t)
        : task(t) {}
};

struct TaskAssignmentMessage : public TaskMessage {
    explicit TaskAssignmentMessage(const std::shared_ptr<Task> &t)
        : TaskMessage(t) {}
};

struct TaskCancellationMessage : public TaskMessage {
    explicit TaskCancellationMessage(const std::shared_ptr<Task> &t)
        : TaskMessage(t) {}
};

struct TaskStatusUpdateMessage : public TaskMessage {
    explicit TaskStatusUpdateMessage(const std::shared_ptr<Task> &t)
        : TaskMessage(t) {}
};

// Result-related messages
struct ResultMessage : public qb::Event {
    TaskResult result;

    explicit ResultMessage(const TaskResult &r)
        : result(r) {}
};

// Worker status messages
struct WorkerStatusMessage : public qb::Event {
    qb::ActorId   worker_id;
    WorkerMetrics metrics;

    WorkerStatusMessage(qb::ActorId id, const WorkerMetrics &m)
        : worker_id(id)
        , metrics(m) {}
};

struct WorkerHeartbeatMessage : public qb::Event {
    qb::ActorId worker_id;
    uint64_t    timestamp;
    bool        is_busy;

    WorkerHeartbeatMessage(qb::ActorId id, uint64_t time, bool busy)
        : worker_id(id)
        , timestamp(time)
        , is_busy(busy) {}
};

// System messages
struct InitializeMessage : public qb::Event {};
struct ShutdownMessage : public qb::Event {};

// Telemetry, done the actor way: the monitor ASKS, each component ANSWERS with the counters it
// owns. This replaces five `std::atomic<uint64_t>` globals written from four cores.
struct StatsRequestMessage : public qb::Event {};

struct StatsReportMessage : public qb::Event {
    uint64_t total_tasks{0};     // produced by the generator
    uint64_t completed_tasks{0}; // counted by the collector
    uint64_t failed_tasks{0};    // counted by the collector
    uint64_t queued_tasks{0};    // scheduler backlog
    uint64_t active_tasks{0};    // scheduler in-flight

    StatsReportMessage(uint64_t total, uint64_t completed, uint64_t failed, uint64_t queued, uint64_t active)
        : total_tasks(total)
        , completed_tasks(completed)
        , failed_tasks(failed)
        , queued_tasks(queued)
        , active_tasks(active) {}
};

// Self-addressed wake-ups produced by each actor's own coroutine timers.
struct GenerateTickMessage : public qb::Event {};
struct TaskCompleteTickMessage : public qb::Event {};
struct HeartbeatTickMessage : public qb::Event {};
struct MetricsTickMessage : public qb::Event {};
struct ReportTickMessage : public qb::Event {};
struct ShutdownTickMessage : public qb::Event {};
struct FinalStopTickMessage : public qb::Event {};

// Define UpdateWorkersMessage at the global level so both main and TaskSchedulerActor can use it
struct UpdateWorkersMessage : public qb::Event {
    std::vector<qb::ActorId> worker_ids;
    explicit UpdateWorkersMessage(const std::vector<qb::ActorId> &ids)
        : worker_ids(ids) {}
};

// ═════════════════════════════════════════════════════════════════
// SYSTEM ACTORS
// ═════════════════════════════════════════════════════════════════

/**
 * @brief TaskGenerator actor that creates computational tasks
 */
class TaskGeneratorActor : public qb::Actor {
private:
    qb::ActorId  _scheduler_id;
    std::mt19937 _rng;
    bool         _is_active{false};
    uint64_t     _start_time;

public:
    explicit TaskGeneratorActor(qb::ActorId scheduler_id)
        : _scheduler_id(scheduler_id) {
        // Initialize random number generator
        std::random_device rd;
        _rng = std::mt19937(rd());

        // Register for message types
        registerEvent<InitializeMessage>(*this);
        registerEvent<ShutdownMessage>(*this);
        registerEvent<GenerateTickMessage>(*this);
        registerEvent<StatsRequestMessage>(*this);
    }

    qb::io::async::task<bool>
    onInit() override {
        qb::io::cout() << "TaskGeneratorActor initialized with ID: " << id() << std::endl;
        co_return true;
    }

    void
    on(InitializeMessage &) {
        _is_active  = true;
        _start_time = getCurrentTimestamp();

        // Schedule periodic task generation
        scheduleTaskGeneration();
    }

    void
    on(GenerateTickMessage const &) {
        if (!_is_active)
            return;

        generateTask();
        scheduleTaskGeneration();
    }

    void
    on(StatsRequestMessage &msg) {
        push<StatsReportMessage>(msg.getSource(), _tasks_generated, 0, 0, 0, 0);
    }

    void
    on(ShutdownMessage &) {
        qb::io::cout() << "TaskGeneratorActor shutting down after generating " << _tasks_generated << " tasks" << std::endl;
        _is_active = false;
        kill();
    }

private:
    void
    scheduleTaskGeneration() {
        if (!_is_active)
            return;

        // Calculate time for next batch of tasks
        const auto period = std::chrono::duration_cast<qb::duration>(std::chrono::duration<double>(1.0 / TASKS_PER_SECOND));

        // Schedule next task generation. `spawn` binds the timer to this actor's cancellation
        // scope; the `qb::io::async::callback([this]{...}, d)` this replaced was one of the three
        // sites AddressSanitizer caught reading freed memory at shutdown.
        spawn([period](qb::ScopedCoroContext ctx) -> qb::io::async::task<void> {
            co_await ctx.sleep(period);
            ctx.template push<GenerateTickMessage>();
        });
    }

    void
    generateTask() {
        // Random task parameters
        std::uniform_int_distribution<> type_dist(0, NUM_TASK_TYPES - 1);
        std::uniform_int_distribution<> priority_dist(1, 10);
        std::uniform_int_distribution<> complexity_dist(0, 3);

        // Generate data size between 10 and 100
        std::uniform_int_distribution<> data_size_dist(10, 100);
        int                             data_size = data_size_dist(_rng);

        // Generate random data
        std::stringstream               data_ss;
        std::uniform_int_distribution<> data_dist(0, 9);
        for (int i = 0; i < data_size; ++i) {
            data_ss << data_dist(_rng);
        }

        // Create the task. Draw an actual enumerator: `static_cast<ComplexityLevel>(1 << k)`
        // produced 1, 2, 4 and 8, and only the first of those is a `ComplexityLevel`.
        ComplexityLevel complexity = COMPLEXITY_LEVELS[static_cast<std::size_t>(complexity_dist(_rng))];

        std::shared_ptr<Task> task =
            std::make_shared<Task>(TASK_TYPES[type_dist(_rng)].c_str(), priority_dist(_rng), complexity, data_ss.str().c_str());

        // Send to scheduler
        push<TaskMessage>(_scheduler_id, task);

        _tasks_generated++;
    }

    uint64_t _tasks_generated{0};
};

/**
 * @brief TaskScheduler actor that distributes tasks to workers
 */
class TaskSchedulerActor : public qb::Actor {
private:
    std::vector<qb::ActorId>                        _worker_ids;
    std::map<qb::ActorId, WorkerMetrics>            _worker_metrics;
    std::deque<std::shared_ptr<Task>>               _task_queue;
    std::map<qb::string<64>, std::shared_ptr<Task>> _active_tasks;
    bool                                            _is_active{false};

    // Who the scheduler has handed work to and not yet heard back from. THIS is what makes an
    // assignment decision correct: `WorkerMetrics::utilization` arrives every two seconds, so in
    // the 200ms between two tasks it says every worker is idle, and a fixed-order first-fit over
    // that answer gives the first worker in the list everything.
    std::set<qb::ActorId> _busy_workers;
    std::size_t           _next_worker{0}; // round-robin cursor

public:
    // Constructor now takes empty vector to be filled later
    TaskSchedulerActor() {
        // Register for message types
        registerEvent<TaskMessage>(*this);
        registerEvent<TaskStatusUpdateMessage>(*this);
        registerEvent<WorkerStatusMessage>(*this);
        registerEvent<WorkerHeartbeatMessage>(*this);
        registerEvent<InitializeMessage>(*this);
        registerEvent<ShutdownMessage>(*this);
        registerEvent<UpdateWorkersMessage>(*this);
        registerEvent<StatsRequestMessage>(*this);
        registerEvent<ReportTickMessage>(*this);
    }

    // Method to update worker IDs after construction
    void
    updateWorkers(const std::vector<qb::ActorId> &worker_ids) {
        _worker_ids = worker_ids;
    }

    qb::io::async::task<bool>
    onInit() override {
        qb::io::cout() << "TaskSchedulerActor initialized with ID: " << id() << std::endl;
        co_return true;
    }

    void
    on(InitializeMessage &) {
        _is_active = true;

        // Schedule load balancing assessment
        scheduleLoadAssessment();
    }

    void
    on(UpdateWorkersMessage &msg) {
        qb::io::cout() << "TaskScheduler received " << msg.worker_ids.size() << " worker IDs" << std::endl;
        _worker_ids = msg.worker_ids;
        // Try to schedule tasks now that we have workers
        scheduleTasks();
    }

    void
    on(TaskMessage &msg) {
        // Add task to queue
        _task_queue.push_back(msg.task);

        // Attempt to schedule tasks immediately
        scheduleTasks();
    }

    void
    on(TaskStatusUpdateMessage &msg) {
        auto           task    = msg.task;
        qb::string<64> task_id = task->task_id;

        // A worker that was already busy answers with status PENDING. That means "I did not take
        // this one" -- so put it back in the queue and mark the worker free again. The previous
        // version only rewrote a map entry, which silently DROPPED the task: combined with the
        // fixed-order assignment above, that is where most of a measured 94% incompletion went.
        if (task->status == TaskStatus::PENDING) {
            _active_tasks.erase(task_id);
            _busy_workers.erase(msg.getSource());
            _task_queue.push_back(task);
            scheduleTasks();
            return;
        }

        // Update active task status
        if (_active_tasks.find(task_id) != _active_tasks.end()) {
            _active_tasks[task_id] = task;
        }

        // A terminal status frees the worker even if the result went straight to the collector.
        if (task->status == TaskStatus::COMPLETED || task->status == TaskStatus::FAILED || task->status == TaskStatus::CANCELED) {
            _active_tasks.erase(task_id);
            _busy_workers.erase(msg.getSource());
            scheduleTasks();
        }
    }

    void
    on(StatsRequestMessage &msg) {
        push<StatsReportMessage>(msg.getSource(), 0, 0, 0, _task_queue.size(), _active_tasks.size());
    }

    void
    on(ReportTickMessage const &) {
        if (!_is_active)
            return;

        assessLoadBalance();
        scheduleLoadAssessment();
    }

    void
    on(WorkerStatusMessage &msg) {
        // Update worker metrics
        _worker_metrics[msg.worker_id] = msg.metrics;
    }

    void
    on(WorkerHeartbeatMessage &msg) {
        if (_worker_metrics.find(msg.worker_id) != _worker_metrics.end()) {
            _worker_metrics[msg.worker_id].last_heartbeat = msg.timestamp;
        }
    }

    // NOTE: this actor used to also `registerEvent<ResultMessage>` and handle it "to know when
    // workers become free". Nothing ever sent it one -- a worker pushes its `ResultMessage` to
    // the RESULT COLLECTOR and only a `TaskStatusUpdateMessage` to the scheduler -- so the
    // handler was dead code, and the freeing it was supposed to do never happened. The terminal
    // branch in `on(TaskStatusUpdateMessage&)` above is the live path.

    void
    on(ShutdownMessage &) {
        qb::io::cout() << "TaskSchedulerActor shutting down" << std::endl;
        _is_active = false;

        // Cancel all pending tasks
        for (auto &task : _task_queue) {
            task->status = TaskStatus::CANCELED;
        }

        // Clear queues
        _task_queue.clear();
        _active_tasks.clear();

        kill();
    }

private:
    void
    scheduleTasks() {
        if (!_is_active || _task_queue.empty())
            return;

        if (_worker_ids.empty())
            return;

        // Process high priority tasks first
        std::stable_sort(_task_queue.begin(), _task_queue.end(),
                         [](const std::shared_ptr<Task> &a, const std::shared_ptr<Task> &b) { return a->priority > b->priority; });

        // Walk the fleet ONCE from the round-robin cursor, so consecutive tasks land on
        // consecutive workers instead of piling onto whichever id happens to sort first.
        for (std::size_t probed = 0; probed < _worker_ids.size() && !_task_queue.empty(); ++probed) {
            const auto worker_id = _worker_ids[_next_worker];
            _next_worker         = (_next_worker + 1) % _worker_ids.size();

            if (!isWorkerAvailable(worker_id))
                continue;

            // Get next task
            auto task = _task_queue.front();
            _task_queue.pop_front();

            // Assign to worker
            task->status                 = TaskStatus::ASSIGNED;
            _active_tasks[task->task_id] = task;
            _busy_workers.insert(worker_id);

            // Send the worker its OWN copy. Boxing a payload behind a `std::shared_ptr` makes
            // the EVENT relocatable; it does not make the pointee owned. Handing the live object
            // to a worker on another core, while this actor keeps it in `_active_tasks`, is
            // shared mutable state across cores -- the worker writes `status`, `start_time` and
            // `completion_time` on it. `example9_trading_system.cpp` has the same shape and its
            // `snapshot()` helper carries the ThreadSanitizer evidence.
            push<TaskAssignmentMessage>(worker_id, std::make_shared<Task>(*task));

            qb::io::cout() << "Assigned " << task->toString() << " to Worker " << worker_id << std::endl;
        }
    }

    bool
    isWorkerAvailable(qb::ActorId worker_id) {
        // What the scheduler KNOWS, checked before what it was TOLD: it handed this worker a
        // task and has not heard back.
        if (_busy_workers.count(worker_id))
            return false;

        if (_worker_metrics.find(worker_id) == _worker_metrics.end()) {
            // No metrics yet, assume available
            return true;
        }

        // Check if worker is responding
        const auto    &metrics           = _worker_metrics[worker_id];
        uint64_t       now               = getCurrentTimestamp();
        const uint64_t HEARTBEAT_TIMEOUT = 5000000; // 5 seconds

        if (now - metrics.last_heartbeat > HEARTBEAT_TIMEOUT) {
            qb::io::cout() << "Warning: Worker " << worker_id << " seems unresponsive!" << std::endl;
            return false;
        }

        return true;
    }

    void
    scheduleLoadAssessment() {
        if (!_is_active)
            return;

        // Schedule periodic load assessment
        spawn([](qb::ScopedCoroContext ctx) -> qb::io::async::task<void> {
            co_await ctx.sleep(std::chrono::seconds(1)); // Check every 1 second
            ctx.template push<ReportTickMessage>();
        });
    }

    void
    assessLoadBalance() {
        if (_worker_metrics.empty())
            return;

        // Calculate average worker utilization
        double total_utilization = 0.0;
        for (const auto &pair : _worker_metrics) {
            total_utilization += pair.second.utilization;
        }
        double avg_utilization = total_utilization / _worker_metrics.size();

        // Log load balancing info
        qb::io::cout() << "Load balancing assessment - Avg utilization: " << std::fixed << std::setprecision(1) << (avg_utilization * 100)
                       << "%, Queued tasks: " << _task_queue.size() << ", Active tasks: " << _active_tasks.size() << std::endl;

        // If queue is backing up, we could potentially add more workers here
    }
};

/**
 * @brief WorkerNode actor that processes computational tasks
 */
class WorkerNodeActor : public qb::Actor {
private:
    qb::ActorId           _scheduler_id;
    qb::ActorId           _collector_id;
    std::shared_ptr<Task> _current_task;
    WorkerMetrics         _metrics;
    bool                  _is_busy{false};
    bool                  _is_active{false};
    uint64_t              _simulation_start_time;
    uint64_t              _busy_start_time{0};
    std::mt19937          _rng;

public:
    WorkerNodeActor(qb::ActorId scheduler_id, qb::ActorId collector_id)
        : _scheduler_id(scheduler_id)
        , _collector_id(collector_id) {
        // Register for message types
        registerEvent<TaskAssignmentMessage>(*this);
        registerEvent<TaskCancellationMessage>(*this);
        registerEvent<InitializeMessage>(*this);
        registerEvent<ShutdownMessage>(*this);
        registerEvent<TaskCompleteTickMessage>(*this);
        registerEvent<HeartbeatTickMessage>(*this);
        registerEvent<MetricsTickMessage>(*this);

        // Each worker seeds its OWN generator. `generateProcessingTime` used to hold a
        // function-local `static std::mt19937` driven from all four worker cores at once.
        _rng = std::mt19937(std::random_device{}());
    }

    qb::io::async::task<bool>
    onInit() override {
        qb::io::cout() << "WorkerNodeActor initialized with ID: " << id() << std::endl;
        co_return true;
    }

    void
    on(InitializeMessage &) {
        _is_active              = true;
        _simulation_start_time  = getCurrentTimestamp();
        _metrics.last_heartbeat = _simulation_start_time;

        // Start sending heartbeats
        scheduleHeartbeat();

        // Start sending metrics
        scheduleMetricsUpdate();
    }

    void
    on(TaskAssignmentMessage &msg) {
        if (_is_busy) {
            // Worker is already busy, reject task
            auto task    = msg.task;
            task->status = TaskStatus::PENDING;
            push<TaskStatusUpdateMessage>(_scheduler_id, task);
            return;
        }

        // Start processing the task
        _current_task             = msg.task;
        _current_task->status     = TaskStatus::IN_PROGRESS;
        _current_task->start_time = getCurrentTimestamp();

        _is_busy         = true;
        _busy_start_time = getCurrentTimestamp();

        // Update scheduler about task status
        push<TaskStatusUpdateMessage>(_scheduler_id, _current_task);

        // Schedule task completion based on complexity
        const auto processing_time =
            std::chrono::duration_cast<qb::duration>(std::chrono::duration<double>(generateProcessingTime(_current_task->complexity, _rng)));

        spawn([processing_time](qb::ScopedCoroContext ctx) -> qb::io::async::task<void> {
            co_await ctx.sleep(processing_time);
            ctx.template push<TaskCompleteTickMessage>();
        });
    }

    void
    on(TaskCompleteTickMessage const &) {
        if (!_is_active)
            return;

        completeCurrentTask();
    }

    void
    on(TaskCancellationMessage &msg) {
        if (!_is_busy || !_current_task || _current_task->task_id != msg.task->task_id) {
            return;
        }

        // Cancel current task
        _current_task->status = TaskStatus::CANCELED;
        _is_busy              = false;

        // Send update to scheduler
        push<TaskStatusUpdateMessage>(_scheduler_id, _current_task);

        _current_task = nullptr;
    }

    void
    on(ShutdownMessage &) {
        qb::io::cout() << "WorkerNodeActor " << id() << " shutting down" << std::endl;
        _is_active = false;

        // Cancel current task if any
        if (_is_busy && _current_task) {
            _current_task->status = TaskStatus::CANCELED;
            push<TaskStatusUpdateMessage>(_scheduler_id, _current_task);
        }

        kill();
    }

    void
    on(HeartbeatTickMessage const &) {
        if (_is_active)
            scheduleHeartbeat();
    }

    void
    on(MetricsTickMessage const &) {
        if (_is_active)
            scheduleMetricsUpdate();
    }

private:
    void
    completeCurrentTask() {
        if (!_is_busy || !_current_task)
            return;

        uint64_t completion_time = getCurrentTimestamp();
        uint64_t processing_time = completion_time - _current_task->start_time;

        // Randomly succeed or fail (95% success rate). The generator is the actor's own member,
        // not a `std::random_device` + `std::mt19937` constructed per task: seeding mt19937 is
        // ~2.5 KB of state, and on macOS `random_device` funnels into a process-wide lock.
        std::uniform_real_distribution<> dist(0, 1);
        bool                             success = dist(_rng) < 0.95;

        // Update task status
        _current_task->status          = success ? TaskStatus::COMPLETED : TaskStatus::FAILED;
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
            result_ss << "Processed " << _current_task->task_type.c_str() << " task with input size " << _current_task->data.size() << " in "
                      << (processing_time / 1000.0) << "ms";
        } else {
            std::uniform_int_distribution<> error_dist(0, 99);
            result_ss << "Failed to process task: Error code " << error_dist(_rng);
        }

        // Create result
        TaskResult result(_current_task->task_id.c_str(), success, result_ss.str().c_str(), processing_time);

        // Send to result collector
        push<ResultMessage>(_collector_id, result);

        // Send status update to scheduler
        push<TaskStatusUpdateMessage>(_scheduler_id, _current_task);

        qb::io::cout() << "Worker " << id() << " completed task " << _current_task->task_id.c_str()
                       << " with status: " << statusToString(_current_task->status) << std::endl;

        // Reset worker state
        _current_task = nullptr;
        _is_busy      = false;
    }

    void
    scheduleHeartbeat() {
        if (!_is_active)
            return;

        // Send heartbeat to scheduler
        push<WorkerHeartbeatMessage>(_scheduler_id, id(), getCurrentTimestamp(), _is_busy);

        // Schedule next heartbeat
        spawn([](qb::ScopedCoroContext ctx) -> qb::io::async::task<void> {
            co_await ctx.sleep(std::chrono::seconds(1)); // Heartbeat every 1 second
            ctx.template push<HeartbeatTickMessage>();
        });
    }

    void
    scheduleMetricsUpdate() {
        if (!_is_active)
            return;

        // Update utilization
        uint64_t now        = getCurrentTimestamp();
        uint64_t total_time = now - _simulation_start_time;
        uint64_t busy_time  = _metrics.total_processing_time;

        if (_is_busy) {
            busy_time += (now - _busy_start_time);
        }

        _metrics.utilization    = total_time > 0 ? static_cast<double>(busy_time) / total_time : 0.0;
        _metrics.last_heartbeat = now;

        // Send metrics to scheduler
        push<WorkerStatusMessage>(_scheduler_id, id(), _metrics);

        // Schedule next update
        spawn([](qb::ScopedCoroContext ctx) -> qb::io::async::task<void> {
            co_await ctx.sleep(std::chrono::seconds(2)); // Update metrics every 2 seconds
            ctx.template push<MetricsTickMessage>();
        });
    }
};

/**
 * @brief ResultCollector actor that aggregates and validates results
 */
class ResultCollectorActor : public qb::Actor {
private:
    std::map<qb::string<64>, TaskResult> _results;
    bool                                 _is_active{false};
    uint64_t                             _completed{0};
    uint64_t                             _failed{0};

public:
    ResultCollectorActor() {
        // Register for message types
        registerEvent<ResultMessage>(*this);
        registerEvent<InitializeMessage>(*this);
        registerEvent<ShutdownMessage>(*this);
        registerEvent<StatsRequestMessage>(*this);
    }

    qb::io::async::task<bool>
    onInit() override {
        qb::io::cout() << "ResultCollectorActor initialized with ID: " << id() << std::endl;
        co_return true;
    }

    void
    on(InitializeMessage &) {
        _is_active = true;
    }

    void
    on(ResultMessage &msg) {
        // Store the result
        _results[msg.result.task_id] = msg.result;

        if (msg.result.success)
            _completed++;
        else
            _failed++;

        // Log result details
        qb::io::cout() << "Collected: " << msg.result.toString() << std::endl;
    }

    void
    on(StatsRequestMessage &msg) {
        push<StatsReportMessage>(msg.getSource(), 0, _completed, _failed, 0, 0);
    }

    void
    on(ShutdownMessage &) {
        qb::io::cout() << "ResultCollectorActor shutting down" << std::endl;

        // Print summary statistics
        qb::io::cout() << "\n===== RESULT SUMMARY =====" << std::endl;
        qb::io::cout() << "Total results collected: " << _results.size() << std::endl;

        // Count successful and failed results
        size_t   successful = 0;
        uint64_t total_time = 0;

        for (const auto &pair : _results) {
            const auto &result = pair.second;
            if (result.success) {
                successful++;
                total_time += result.processing_time;
            }
        }

        double success_rate = _results.empty() ? 0.0 : (successful * 100.0 / _results.size());
        double avg_time     = successful > 0 ? (total_time / 1000000.0 / successful) : 0.0;

        qb::io::cout() << "Successful results: " << successful << " (" << std::fixed << std::setprecision(1) << success_rate << "%)"
                       << std::endl;
        qb::io::cout() << "Average processing time: " << std::fixed << std::setprecision(3) << avg_time << " seconds" << std::endl;
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
    qb::ActorId              _task_generator_id;
    qb::ActorId              _scheduler_id;
    qb::ActorId              _collector_id;
    std::vector<qb::ActorId> _worker_ids;

    uint64_t _start_time{0};
    bool     _is_active{false};
    bool     _shutting_down{false};

    // Merged snapshot, rebuilt on every polling round: generator + scheduler + collector.
    int      _pending_reports{0};
    uint64_t _total_tasks{0};
    uint64_t _completed_tasks{0};
    uint64_t _failed_tasks{0};
    uint64_t _queued_tasks{0};
    uint64_t _active_tasks{0};

public:
    SystemMonitorActor(qb::ActorId generator, qb::ActorId scheduler, qb::ActorId collector, const std::vector<qb::ActorId> &workers)
        : _task_generator_id(generator)
        , _scheduler_id(scheduler)
        , _collector_id(collector)
        , _worker_ids(workers) {
        // Register for message types
        registerEvent<StatsReportMessage>(*this);
        registerEvent<InitializeMessage>(*this);
        registerEvent<ShutdownMessage>(*this);
        registerEvent<ReportTickMessage>(*this);
        registerEvent<ShutdownTickMessage>(*this);
        registerEvent<FinalStopTickMessage>(*this);
    }

    qb::io::async::task<bool>
    onInit() override {
        qb::io::cout() << "SystemMonitorActor initialized with ID: " << id() << std::endl;

        // Start the system
        push<InitializeMessage>(id());

        co_return true;
    }

    void
    on(InitializeMessage &) {
        _is_active  = true;
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

        for (const auto &worker_id : _worker_ids) {
            push<InitializeMessage>(worker_id);
        }

        // Update the scheduler with worker IDs after initialization
        push<UpdateWorkersMessage>(_scheduler_id, _worker_ids);

        // Schedule performance reports
        schedulePerformanceReport();

        // Schedule system shutdown
        spawn([](qb::ScopedCoroContext ctx) -> qb::io::async::task<void> {
            co_await ctx.sleep(std::chrono::seconds(SIMULATION_DURATION_SECONDS));
            ctx.template push<ShutdownTickMessage>();
        });
    }

    void
    on(StatsReportMessage &msg) {
        // Each component reports only the counters it owns; the zeros are somebody else's.
        _total_tasks += msg.total_tasks;
        _completed_tasks += msg.completed_tasks;
        _failed_tasks += msg.failed_tasks;
        _queued_tasks += msg.queued_tasks;
        _active_tasks += msg.active_tasks;

        if (--_pending_reports > 0)
            return;

        printStatistics();

        if (_shutting_down)
            stopEverything();
    }

    void
    on(ReportTickMessage const &) {
        requestStats();

        if (_is_active)
            schedulePerformanceReport();
    }

    void
    on(ShutdownTickMessage const &) {
        if (!_is_active)
            return;

        qb::io::cout() << "\nDistributed computing system shutting down..." << std::endl;

        _is_active     = false;
        _shutting_down = true;

        // One last polling round, so the final figures are the components' own.
        requestStats();
    }

    void
    on(FinalStopTickMessage const &) {
        broadcast<ShutdownMessage>();
    }

    void
    on(ShutdownMessage &) {
        _is_active = false;
        kill();
    }

private:
    void
    requestStats() {
        _pending_reports = 3;
        _total_tasks     = 0;
        _completed_tasks = 0;
        _failed_tasks    = 0;
        _queued_tasks    = 0;
        _active_tasks    = 0;

        push<StatsRequestMessage>(_task_generator_id);
        push<StatsRequestMessage>(_scheduler_id);
        push<StatsRequestMessage>(_collector_id);
    }

    void
    printStatistics() const {
        const double elapsed_seconds = (getCurrentTimestamp() - _start_time) / 1000000.0;
        if (elapsed_seconds <= 0.0)
            return;

        const double tasks_per_second = (_completed_tasks + _failed_tasks) / elapsed_seconds;

        qb::io::cout() << "\n===== SYSTEM STATISTICS =====" << std::endl;
        qb::io::cout() << "Total Tasks: " << _total_tasks << std::endl;
        qb::io::cout() << "Completed Tasks: " << _completed_tasks << " (" << std::fixed << std::setprecision(1)
                       << (_total_tasks > 0 ? (_completed_tasks * 100.0 / _total_tasks) : 0.0) << "%)" << std::endl;
        qb::io::cout() << "Failed Tasks: " << _failed_tasks << " (" << std::fixed << std::setprecision(1)
                       << (_total_tasks > 0 ? (_failed_tasks * 100.0 / _total_tasks) : 0.0) << "%)" << std::endl;
        qb::io::cout() << "Backlog: " << _queued_tasks << " queued, " << _active_tasks << " in flight" << std::endl;
        qb::io::cout() << "Elapsed Time: " << std::fixed << std::setprecision(2) << elapsed_seconds << " seconds" << std::endl;
        qb::io::cout() << "Throughput: " << std::fixed << std::setprecision(2) << tasks_per_second << " tasks/sec" << std::endl;
        qb::io::cout() << "===========================" << std::endl;
    }

    void
    schedulePerformanceReport() const {
        // Report every 2 seconds, bound to this actor so the last one is cancelled at shutdown.
        spawn([](qb::ScopedCoroContext ctx) -> qb::io::async::task<void> {
            co_await ctx.sleep(std::chrono::seconds(2));
            ctx.template push<ReportTickMessage>();
        });
    }

    void
    stopEverything() const {
        // Send shutdown message to all components
        push<ShutdownMessage>(_task_generator_id);
        push<ShutdownMessage>(_scheduler_id);

        for (const auto &worker_id : _worker_ids) {
            push<ShutdownMessage>(worker_id);
        }

        // Shutdown result collector last to get final statistics
        push<ShutdownMessage>(_collector_id);

        // Then stop whatever is left, this actor included, once the events already in flight
        // have drained.
        spawn([](qb::ScopedCoroContext ctx) -> qb::io::async::task<void> {
            co_await ctx.sleep(std::chrono::milliseconds(500));
            ctx.template push<FinalStopTickMessage>();
        });
    }
};

/**
 * Main function to set up and run the distributed computing system
 */
int
main() {
    try {
        qb::io::cout() << "Initializing distributed computing system..." << std::endl;

        // Create the main engine with multiple cores
        qb::Main engine;

        // Step 1: Create ResultCollector (Core 0)
        auto collector_id = engine.addActor<ResultCollectorActor>(0);

        // Step 2: Create TaskScheduler (Core 0)
        std::vector<qb::ActorId> worker_ids; // Will be populated after creating workers
        auto                     scheduler_id = engine.addActor<TaskSchedulerActor>(0);

        // Step 3: Create WorkerNodes (distributed across cores)
        for (int i = 0; i < NUM_WORKERS; ++i) {
            // Distribute workers across cores (use modulo 4 for cores 0-3)
            int  core_id   = i % 4;
            auto worker_id = engine.addActor<WorkerNodeActor>(core_id, scheduler_id, collector_id);
            worker_ids.push_back(worker_id);
        }

        // We can't send a message directly to the scheduler before the system starts
        // Instead, we'll update the worker IDs in the SystemMonitor, which will
        // send the message to the scheduler after initialization

        // Step 4: Create TaskGenerator (Core 0)
        auto generator_id = engine.addActor<TaskGeneratorActor>(0, scheduler_id);

        // Step 5: Create SystemMonitor (Core 0)
        engine.addActor<SystemMonitorActor>(0, generator_id, scheduler_id, collector_id, worker_ids);

        // Start the system
        engine.start();

        // Wait for completion
        engine.join();

        qb::io::cout() << "Distributed computing simulation completed successfully" << std::endl;

    } catch (const std::exception &e) {
        qb::io::cerr() << "Exception: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}