/**
 * @file models/task.h
 * @brief Domain models: Task, TaskList, TaskEvent.
 *
 * - Task      – single row from PostgreSQL, serialises to/from JSON automatically.
 * - TaskList  – list response wrapper (includes cache metadata).
 * - TaskEvent – lightweight notification published to Redis → WebSocket clients.
 *
 * JSON round-trip is handled by NLOHMANN_DEFINE_TYPE_INTRUSIVE (zero-copy,
 * bidirectional).  The Task constructor from a pg::resultset::row maps columns
 * by position (see prepared statement `select_all_tasks` / `select_task_by_id`).
 */
#pragma once

#include <qb/json.h>
#include <qb/system/time.h>
#include <pgsql/pgsql.h>
#include <cstdint>
#include <string>
#include <vector>

namespace taskmanager {
namespace models {

// ─── Task ─────────────────────────────────────────────────────────────────────

/**
 * @brief Single task record.
 *
 * Column layout expected from the prepared statements:
 *   [0] id          INT4
 *   [1] title       TEXT
 *   [2] description TEXT (nullable)
 *   [3] status      TEXT  ("pending" | "in_progress" | "completed")
 *   [4] created_at  TIMESTAMP
 *   [5] updated_at  TIMESTAMP
 */
struct Task {
    int32_t     id{0};
    std::string title;
    std::string description;
    std::string status{"pending"};
    std::string created_at;
    std::string updated_at;

    Task() = default;

    /** Construct directly from a PostgreSQL result row. */
    explicit Task(const qb::pg::resultset::row &row) {
        id          = row[0].as<int32_t>();
        title       = row[1].as<std::string>();
        description = row[2].is_null() ? "" : row[2].as<std::string>();
        status      = row[3].as<std::string>();
        created_at  = row[4].as<std::string>();
        updated_at  = row[5].as<std::string>();
    }

    [[nodiscard]] bool is_valid() const noexcept { return id > 0; }

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(Task, id, title, description, status, created_at, updated_at)
};

// ─── TaskList ─────────────────────────────────────────────────────────────────

/**
 * @brief List response: array of tasks + total count + cache flag.
 *
 * Wire format:
 * @code{.json}
 * { "tasks": [...], "total": 5, "cached": false }
 * @endcode
 */
struct TaskList {
    std::vector<Task> tasks;
    std::size_t       total{0};
    bool              cached{false};

    TaskList() = default;

    explicit TaskList(const qb::pg::resultset &res, bool from_cache = false)
        : cached(from_cache) {
        tasks.reserve(res.size());
        for (const auto &row : res)
            tasks.emplace_back(row);
        total = tasks.size();
    }

    [[nodiscard]] qb::json to_json() const {
        qb::json j;
        j["total"]  = total;
        j["cached"] = cached;
        j["tasks"]  = qb::json::array();
        for (const auto &t : tasks)
            j["tasks"].push_back(t);
        return j;
    }
};

// ─── TaskEvent ────────────────────────────────────────────────────────────────

/**
 * @brief Notification broadcast over Redis Pub/Sub → WebSocket clients.
 *
 * Published on the `tasks:events` channel after every mutating operation.
 * Wire format:
 * @code{.json}
 * { "action": "created", "task_id": 42, "title": "My task", "timestamp": 1741000000 }
 * @endcode
 */
struct TaskEvent {
    std::string action;      ///< "created" | "updated" | "deleted"
    int32_t     task_id{0};
    std::string title;
    uint64_t    timestamp{0};

    TaskEvent() = default;

    TaskEvent(const std::string &act, int32_t tid, const std::string &ttl = "")
        : action(act)
        , task_id(tid)
        , title(ttl)
        , timestamp(static_cast<uint64_t>(qb::unix_nanos(qb::wall_now()))) {}

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(TaskEvent, action, task_id, title, timestamp)
};

} // namespace models
} // namespace taskmanager
