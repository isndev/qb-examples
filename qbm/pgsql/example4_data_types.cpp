/**
 * @file examples/qbm/pgsql/example4_data_types.cpp
 * @example qbm-pgsql: Data Types
 *
 * @brief Demonstrates PostgreSQL data type mapping with the modern **coroutine** API:
 * creates a table with many column types, inserts a row with typed parameters, and
 * reads each column back using the appropriate C++ type.
 * The example is standalone qb-io: `init()` + `coro_scheduler().spawn()` + `run_until()`.
 *
 * Types covered: INT, SMALLINT, BIGINT, TEXT, VARCHAR, CHAR, BOOLEAN, NUMERIC,
 * REAL, DOUBLE PRECISION, DATE, TIME, TIMESTAMP, TIMESTAMPTZ, UUID, BYTEA,
 * JSON, JSONB, INT[], TEXT[], nullable TEXT.
 *
 * QB/QBM PostgreSQL features demonstrated:
 * - Standalone qb-io coroutine scaffolding: `init()` + `coro_scheduler().spawn()` + `run_until()`.
 * - `qb::pg::tcp::database` — the PostgreSQL client.
 * - `co_await db.prepare(name, sql, type_oid_sequence{...})` — all PG OIDs.
 * - `co_await db.execute(name, qb::pg::params{...})` — typed parameter packing.
 * - `row["col"].as<T>()` — typed field extraction.
 * - `qb::wall_time` / `qb::wall_now()` / `qb::format_utc()` / `qb::to_iso8601()`.
 * - `qb::uuid` / `qb::generate_random_uuid()`.
 * - `qb::json` — JSON / JSONB columns.
 */

#include <qbm/pgsql/pgsql.h>
#include <qb/io/async.h>
#include <qb/io/async/coroutine.h>
#include <qb/json.h>        // qb::json
#include <qb/system/time.h> // qb::wall_time, qb::wall_now, qb::format_utc, qb::to_iso8601
#include <qb/uuid.h>        // qb::uuid, qb::generate_random_uuid

#include <cctype>
#include <iomanip>
#include <limits>
#include <string>
#include <vector>

// IMPORTANT: Replace with your actual PostgreSQL connection string
const char *PG_CONNECTION_STRING = "tcp://test:test@localhost:5432[test]";

const char *DATA_TYPES_TABLE_SQL = "CREATE TABLE IF NOT EXISTS data_types_test ("
                                   "id SERIAL PRIMARY KEY, "
                                   "integer_col INT, "
                                   "smallint_col SMALLINT, "
                                   "bigint_col BIGINT, "
                                   "text_col TEXT, "
                                   "varchar_col VARCHAR(50), "
                                   "char_col CHAR(10), "
                                   "boolean_col BOOLEAN, "
                                   "numeric_col DECIMAL(12, 3), "
                                   "real_col REAL, "
                                   "double_col DOUBLE PRECISION, "
                                   "date_col DATE, "
                                   "time_col TIME WITHOUT TIME ZONE, "
                                   "timestamp_col TIMESTAMP WITHOUT TIME ZONE, "
                                   "timestamptz_col TIMESTAMP WITH TIME ZONE, "
                                   "uuid_col UUID, "
                                   "bytea_col BYTEA, "
                                   "json_col JSON, "
                                   "jsonb_col JSONB, "
                                   "integer_array_col INT[], "
                                   "text_array_col TEXT[], "
                                   "nullable_text_col TEXT"
                                   ");";

const char *PREPARE_INSERT_DATA_TYPES       = "insert_data_types_stmt_v4";
const char *PREPARE_SELECT_DATA_TYPES_BY_ID = "select_data_types_stmt_v4";

// ─── Schema setup ─────────────────────────────────────────────────────────────

qb::io::async::task<bool>
setup_schema(qb::pg::tcp::database &db) {
    qb::io::cout() << "Initializing data_types_test table and preparing statements..." << std::endl;

    {
        auto r = co_await db.execute(DATA_TYPES_TABLE_SQL);
        if (!r.ok()) {
            qb::io::cerr() << "Failed to create data_types_test table: " << r.error().what() << std::endl;
            co_return false;
        }
    }

    {
        auto r = co_await db.prepare(PREPARE_INSERT_DATA_TYPES,
                                     "INSERT INTO data_types_test ("
                                     "integer_col, smallint_col, bigint_col, text_col, varchar_col, char_col, "
                                     "boolean_col, numeric_col, real_col, double_col, date_col, time_col, "
                                     "timestamp_col, timestamptz_col, uuid_col, bytea_col, json_col, jsonb_col, "
                                     "integer_array_col, text_array_col, nullable_text_col"
                                     ") VALUES ("
                                     "$1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11, $12, $13, $14, $15, $16, $17, $18, $19, $20, $21"
                                     ") RETURNING id;",
                                     qb::pg::type_oid_sequence{qb::pg::oid::int4,       qb::pg::oid::int2,        qb::pg::oid::int8,
                                                               qb::pg::oid::text,       qb::pg::oid::varchar,     qb::pg::oid::bpchar,
                                                               qb::pg::oid::boolean,    qb::pg::oid::float8,      qb::pg::oid::float4,
                                                               qb::pg::oid::float8,     qb::pg::oid::date,        qb::pg::oid::time,
                                                               qb::pg::oid::timestamp,  qb::pg::oid::timestamptz, qb::pg::oid::uuid,
                                                               qb::pg::oid::bytea,      qb::pg::oid::json,        qb::pg::oid::jsonb,
                                                               qb::pg::oid::int4_array, qb::pg::oid::text_array,  qb::pg::oid::text});
        if (!r.ok()) {
            qb::io::cerr() << "Failed to prepare insert statement: " << r.error().what() << std::endl;
            co_return false;
        }
    }

    {
        // Every column is read back natively with its binary decoder:
        //   - numeric_col      -> qb::pg::detail::numeric (binary NUMERIC decoder)
        //   - date_col/time_col -> qb::date / qb::time_of_day (civil types)
        //   - integer_array_col -> std::vector<int>, text_array_col -> std::vector<std::string>
        // No more ::text workarounds; see display_row().
        auto r = co_await db.prepare(PREPARE_SELECT_DATA_TYPES_BY_ID,
                                     "SELECT id, integer_col, smallint_col, bigint_col, text_col, "
                                     "varchar_col, char_col, boolean_col, numeric_col, "
                                     "real_col, double_col, date_col, "
                                     "time_col, timestamp_col, "
                                     "timestamptz_col, uuid_col, bytea_col, json_col, jsonb_col, "
                                     "integer_array_col, "
                                     "text_array_col, nullable_text_col "
                                     "FROM data_types_test WHERE id = $1;",
                                     qb::pg::type_oid_sequence{qb::pg::oid::int4});
        if (!r.ok()) {
            qb::io::cerr() << "Failed to prepare select statement: " << r.error().what() << std::endl;
            co_return false;
        }
    }

    qb::io::cout() << "Data types schema and statements initialized." << std::endl;
    co_return true;
}

// ─── Display a retrieved row ───────────────────────────────────────────────────

void
display_row(const qb::pg::resultset &rs, int id) {
    if (rs.empty()) {
        qb::io::cerr() << "Data types row with ID " << id << " not found after insert." << std::endl;
        return;
    }
    qb::io::cout() << "\n--- Retrieved Data for ID: " << id << " ---" << std::endl;
    const auto &row = rs[0];

    qb::io::cout() << "ID: " << row["id"].as<int>() << std::endl;
    qb::io::cout() << "Integer: " << row["integer_col"].as<int>() << std::endl;
    qb::io::cout() << "SmallInt: " << row["smallint_col"].as<short>() << std::endl;
    qb::io::cout() << "BigInt: " << row["bigint_col"].as<std::int64_t>() << std::endl;
    qb::io::cout() << "Text: " << row["text_col"].as<std::string>() << std::endl;
    qb::io::cout() << "Varchar: " << row["varchar_col"].as<std::string>() << std::endl;
    qb::io::cout() << "Char: [" << row["char_col"].as<std::string>() << "]" << std::endl;
    qb::io::cout() << "Boolean: " << (row["boolean_col"].as<bool>() ? "true" : "false") << std::endl;
    qb::io::cout() << "Numeric: " << row["numeric_col"].as<qb::pg::detail::numeric>().str() << std::endl;
    qb::io::cout() << "Real: " << std::fixed << std::setprecision(5) << row["real_col"].as<float>() << std::endl;
    qb::io::cout() << "Double: " << std::fixed << std::setprecision(10) << row["double_col"].as<double>() << std::endl;

    qb::io::cout() << "Date: " << row["date_col"].as<qb::date>().to_string() << std::endl;
    qb::io::cout() << "Time: " << row["time_col"].as<qb::time_of_day>().to_string() << std::endl;
    qb::io::cout() << "Timestamp: " << qb::to_iso8601(row["timestamp_col"].as<qb::wall_time>()) << std::endl;
    qb::io::cout() << "TimestampTZ: " << qb::to_iso8601(row["timestamptz_col"].as<qb::wall_time>()) << " (UTC)" << std::endl;

    qb::io::cout() << "UUID: " << uuids::to_string(row["uuid_col"].as<qb::uuid>()) << std::endl;

    auto bytea_data = row["bytea_col"].as<std::vector<char>>();
    qb::io::cout() << "ByteA: ";
    for (char c : bytea_data) {
        if (std::isprint(static_cast<unsigned char>(c)))
            qb::io::cout() << c;
        else
            qb::io::cout() << ".";
    }
    qb::io::cout() << " (Size: " << bytea_data.size() << ")" << std::endl;

    qb::io::cout() << "JSON: " << row["json_col"].as<qb::json>().dump(2) << std::endl;
    qb::io::cout() << "JSONB: " << row["jsonb_col"].as<qb::jsonb>().dump(2) << std::endl;

    // Arrays are decoded natively into std::vector<T> by the binary array decoder.
    auto print_array = [](const std::string &label, auto const &vec) {
        qb::io::cout() << label;
        for (size_t i = 0; i < vec.size(); ++i)
            qb::io::cout() << (i ? ", " : "") << vec[i];
        qb::io::cout() << std::endl;
    };
    print_array("Integer Array: ", row["integer_array_col"].as<std::vector<int>>());
    print_array("Text Array: ", row["text_array_col"].as<std::vector<std::string>>());

    auto nullable_val = row["nullable_text_col"].as<std::optional<std::string>>();
    qb::io::cout() << "Nullable Text: " << (nullable_val ? *nullable_val : "NULL") << std::endl;
}

// ─── Main coroutine ───────────────────────────────────────────────────────────

// All PostgreSQL work happens inside a coroutine; `running` is flipped to false on ANY exit path
// (scope guard) so the run_until() loop in main() stops once the coroutine is done.
qb::io::async::task<void>
run_data_types_demo(bool &running) {
    struct StopOnExit {
        bool &r;
        ~StopOnExit() {
            r = false;
        }
    } stop{running};

    qb::pg::tcp::database db;

    if (!co_await db.connect(PG_CONNECTION_STRING)) {
        qb::io::cerr() << "Failed to connect to PostgreSQL: " << db.error().what() << std::endl;
        qb::io::cerr() << "SQLSTATE: " << db.error().code << std::endl;
        co_return;
    }
    qb::io::cout() << "Successfully connected to PostgreSQL." << std::endl;

    if (!co_await setup_schema(db))
        co_return;

    qb::io::cout() << "\n--- Inserting and Selecting Various Data Types ---" << std::endl;

    // Sample data
    int          p_integer_col  = 12345;
    short        p_smallint_col = 123;
    std::int64_t p_bigint_col   = 1234567890123LL;
    std::string  p_text_col     = "This is a text field.";
    std::string  p_varchar_col  = "Varchar up to 50";
    std::string  p_char_col     = "char(10)  ";
    bool         p_boolean_col  = true;
    // NUMERIC: the client sends parameters in BINARY format, and this module does
    // not implement PostgreSQL's binary NUMERIC digit-array encoding. We therefore
    // bind the value as a double (float8 binary) and let the NUMERIC column store it
    // (DECIMAL accepts the float8 assignment cast). The retrieved value is exact to
    // the column's scale (3 decimals).
    double        p_numeric_col = 12345.678;
    float         p_real_col    = 123.45f;
    double        p_double_col  = 9876.54321;
    qb::wall_time now_ts        = qb::wall_now();
    // DATE / TIME are civil types (no instant), so they map to the qb core civil
    // vocabulary — qb::date and qb::time_of_day — not to qb::wall_time (an instant,
    // used below for timestamp/timestamptz).
    qb::date          p_date_col        = qb::date::from_wall_time(now_ts);
    qb::time_of_day   p_time_col        = qb::time_of_day::from_hms(14, 30, 45, 123456);
    qb::wall_time     p_timestamp_col   = now_ts;
    qb::wall_time     p_timestamptz_col = now_ts;
    qb::uuid          p_uuid_col        = qb::generate_random_uuid();
    std::vector<char> p_bytea_col       = {'b', 'y', 't', 'e', '\0', 'a', 'r', 'r', 'a', 'y'};
    qb::json          p_json_col        = {{"key1", "value1"}, {"key2", 100}};
    // JSONB must be bound as qb::jsonb: its converter prepends PostgreSQL's JSONB
    // version byte (1). Binding a qb::json here would send raw JSON text, which the
    // server would misread as "jsonb version number 123" ('{').
    qb::jsonb                     p_jsonb_col         = {{"item", "jsonb_item"}, {"active", true}, {"nested", {1, 2, 3}}};
    std::vector<int>              p_integer_array_col = {10, 20, 30, 40};
    std::vector<std::string_view> p_text_array_col    = {"apple", "banana", "cherry"};
    std::optional<std::string>    p_nullable_text_col = "This is not null.";

    // INSERT.
    auto ins = co_await db.execute(
        PREPARE_INSERT_DATA_TYPES,
        qb::pg::params{p_integer_col, p_smallint_col, p_bigint_col, p_text_col,  p_varchar_col,       p_char_col,       p_boolean_col,
                       p_numeric_col, p_real_col,     p_double_col, p_date_col,  p_time_col,          p_timestamp_col,  p_timestamptz_col,
                       p_uuid_col,    p_bytea_col,    p_json_col,   p_jsonb_col, p_integer_array_col, p_text_array_col, p_nullable_text_col});
    if (!ins.ok()) {
        qb::io::cerr() << "Failed to insert data types row: " << ins.error().what() << std::endl;
        if (!ins.error().detail.empty())
            qb::io::cerr() << "Detail: " << ins.error().detail << std::endl;
        co_return;
    }
    const auto &ins_rs = ins.result();
    if (ins_rs.empty() || ins_rs[0].size() == 0) {
        qb::io::cerr() << "Insert data types row did not return an ID." << std::endl;
        co_return;
    }
    int new_id = ins_rs[0][0].as<int>();
    qb::io::cout() << "Successfully inserted data types row with ID: " << new_id << std::endl;

    // SELECT back.
    auto sel = co_await db.execute(PREPARE_SELECT_DATA_TYPES_BY_ID, qb::pg::params{new_id});
    if (!sel.ok()) {
        qb::io::cerr() << "Error selecting data types row ID " << new_id << ": " << sel.error().what() << std::endl;
        qb::io::cerr() << "SQLSTATE: " << sel.error().code << std::endl;
    } else {
        display_row(sel.result(), new_id);
    }

    // Cleanup.
    qb::io::cout() << "Cleaning up data_types_test table..." << std::endl;
    {
        [[maybe_unused]] auto r = co_await db.execute("DROP TABLE IF EXISTS data_types_test;");
        if (r.ok())
            qb::io::cout() << "data_types_test table dropped." << std::endl;
        else
            qb::io::cerr() << "Failed to drop data_types_test table: " << r.error().what() << std::endl;
    }

    qb::io::cout() << "Data types example completed." << std::endl;
    co_return;
}

int
main() {
    if (std::string(PG_CONNECTION_STRING) == "tcp://user:password@host:port[dbname]") {
        qb::io::cerr() << "WARNING: Using default PG_CONNECTION_STRING. Please update it." << std::endl;
    }

    // Initialize the async system (required for standalone qb-io apps).
    qb::io::async::init();

    // Spawn the coroutine and drive the event loop until it completes.
    bool running = true;
    auto task    = run_data_types_demo(running);
    qb::io::async::coro_scheduler().spawn(std::move(task));
    qb::io::async::run_until(running);

    qb::io::cout() << "Application finished." << std::endl;
    return 0;
}
