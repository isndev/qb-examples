/**
 * @file examples/06-modules/pgsql/11-copy-and-cancel.cpp
 * @tier 06-modules
 * @teaches Stream COPY data through client callbacks without buffering a result set, observe the
 *          server's handshake parameters, and cancel a server-confirmed active query.
 * @demonstrates qb::pg::tcp::database, set_startup_option, parameter_status, server_version,
 *               scram_authenticated, copy_in, copy_out, cancel_async,
 *               qb::io::async::when_all, qb::io::async::init, qb::io::async::run_until,
 *               qb::io::async::coro_scheduler, qb::io::async::task<void>
 * @prerequisites 06-modules/pgsql/08-tls-and-limits, 06-modules/pgsql/10-streaming-results
 * @expect "[session] startup application_name reached the server; ParameterStatus reports its"
 * @expect "[auth] measured on this server: SCRAM-SHA-256 mutual authentication"
 * @expect "[copy-in] three source chunks became three rows through COPY FROM STDIN."
 * @expect "[copy-out] COPY TO STDOUT delivered exactly the three CSV rows to the sink."
 * @expect "[cancel] cancel_async() delivered an out-of-band CancelRequest while pg_sleep was"
 * @expect "[cancel] the query returned SQLSTATE 57014 and the original connection still answered"
 * @expect "=== copy and cancel complete: temporary table removed ==="
 *
 * COPY is not query(): COPY FROM STDIN asks the source for chunks until nullopt; COPY TO STDOUT
 * invokes the sink for each CopyData payload. The sink's string_view expires after its call, so
 * append it to owned storage. Neither operation materialises a result set. Here each source chunk
 * is one CSV row; the protocol also permits chunks split across row boundaries.
 *
 * A CancelRequest travels on a separate connection and uses the backend key of the connection
 * doing the work. A second session first confirms pg_sleep is active in pg_stat_activity, then
 * calls cancel_async() while the first coroutine awaits the query. This demonstrates delivery,
 * the server's cancellation verdict, and connection reuse; it does not measure loop latency.
 * cancel() is synchronous and can block its caller for up to two seconds: use it from another
 * thread, never on this core.
 * The server chooses the authentication method; scram_authenticated() is reported, not assumed.
 *
 * Build: cmake --build --preset release --target qb-example-modules-pgsql-copy-and-cancel
 * Run:   ./build/presets/release/examples/06-modules/pgsql/qb-example-modules-pgsql-copy-and-cancel
 *        (QB_EXAMPLE_PG_URI selects a PostgreSQL server; default test:test@localhost:5432[test])
 */

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <qb/io/async.h>
#include <qb/io/async/coroutine.h>
#include <qbm/pgsql/pgsql.h>
#include "example-database.h"

namespace {

constexpr const char *TABLE = "qb_example_copy_cancel";
constexpr const char *CSV   = "1,alpha\n2,beta\n3,gamma\n";

qb::io::async::task<std::string>
long_query(qb::pg::tcp::database &db) {
    auto reply = co_await db.query("SELECT pg_sleep(5)");
    co_return reply.ok() ? std::string{} : reply.error().code;
}

qb::io::async::task<bool>
cancel_when_active(qb::pg::tcp::database &observer, qb::pg::tcp::database &target, int pid) {
    const std::string probe = "SELECT count(*)::int FROM pg_stat_activity WHERE pid = " + std::to_string(pid)
                              + " AND query LIKE 'SELECT pg_sleep%' AND state = 'active'";
    // Bound the wait: a broken or denied probe cannot leave this example running forever.
    for (int attempt = 0; attempt < 250; ++attempt) {
        auto seen = co_await observer.query(probe);
        if (!seen.ok() || seen.result().size() != 1)
            co_return false;
        if (seen.result()[0][0].as<int>() > 0)
            co_return co_await target.cancel_async();
        auto pause = co_await observer.query("SELECT pg_sleep(0.02)");
        if (!pause.ok())
            co_return false;
    }
    co_return false;
}

} // namespace

qb::io::async::task<void>
run_copy_and_cancel(bool &running, bool &ok) {
    struct StopOnExit {
        bool &flag;
        ~StopOnExit() {
            flag = false;
        }
    } stop{running};

    qb::pg::tcp::database db;
    db.set_startup_option("application_name", "qb-example-copy-cancel");
    if (!co_await db.connect(example_pg_connection_string())) {
        qb::io::cerr() << "PostgreSQL connection failed: " << db.error().what() << '\n';
        co_return;
    }

    auto       app         = co_await db.query("SELECT current_setting('application_name') AS name");
    const auto version     = db.parameter_status("server_version");
    const int  version_num = db.server_version();
    const bool session_ok  = app.ok() && app.result().size() == 1 && app.result()[0]["name"].as<std::string>() == "qb-example-copy-cancel"
                             && version.has_value() && !version->empty() && version_num > 0;
    qb::io::cout() << (session_ok ? "[session] startup application_name reached the server; ParameterStatus reports its\n"
                                    "          version and server_version() gives its numeric form.\n"
                                  : "[session] UNEXPECTED: startup option or server version missing\n");
    qb::io::cout() << "          (server_version='" << version.value_or("missing") << "', numeric=" << version_num << ")\n";
    qb::io::cout() << "[auth] measured on this server: SCRAM-SHA-256 mutual authentication "
                   << (db.scram_authenticated() ? "completed" : "was not selected") << ".\n";

    auto created = co_await db.execute(std::string("CREATE TEMP TABLE ") + TABLE + " (id int PRIMARY KEY, label text)");
    if (!created.ok()) {
        qb::io::cerr() << "CREATE TEMP TABLE failed: " << created.error().what() << '\n';
        co_return;
    }

    int        chunk   = 0;
    auto       loaded  = co_await db.copy_in(std::string("COPY ") + TABLE + " (id, label) FROM STDIN WITH (FORMAT csv)",
                                             [&chunk]() -> std::optional<std::string> {
                                          switch (chunk++) {
                                              case 0:
                                                  return "1,alpha\n";
                                              case 1:
                                                  return "2,beta\n";
                                              case 2:
                                                  return "3,gamma\n";
                                              default:
                                                  return std::nullopt;
                                          }
                                             });
    auto       counted = co_await db.query(std::string("SELECT count(*)::int AS n FROM ") + TABLE);
    const bool load_ok = loaded.ok() && chunk == 4 && counted.ok() && counted.result().size() == 1 && counted.result()[0]["n"].as<int>() == 3;
    qb::io::cout() << (load_ok ? "[copy-in] three source chunks became three rows through COPY FROM STDIN.\n"
                               : "[copy-in] UNEXPECTED: source chunks or row count mismatch\n");

    std::string csv;
    auto exported = co_await db.copy_out(std::string("COPY (SELECT id, label FROM ") + TABLE + " ORDER BY id) TO STDOUT WITH (FORMAT csv)",
                                         [&csv](std::string_view chunk_view) { csv.append(chunk_view); });
    const bool export_ok = exported.ok() && csv == CSV;
    qb::io::cout() << (export_ok ? "[copy-out] COPY TO STDOUT delivered exactly the three CSV rows to the sink.\n"
                                 : "[copy-out] UNEXPECTED: streamed CSV differs from loaded rows\n");

    qb::pg::tcp::database observer;
    if (!co_await observer.connect(example_pg_connection_string())) {
        qb::io::cerr() << "Observer connection failed: " << observer.error().what() << '\n';
        (void) co_await db.execute(std::string("DROP TABLE ") + TABLE);
        co_return;
    }
    const int         pid       = db.backend_pid();
    auto              both      = co_await qb::io::async::when_all(long_query(db), cancel_when_active(observer, db, pid));
    const std::string code      = std::get<0>(both);
    const bool        delivered = std::get<1>(both);
    const bool        canceled  = pid > 0 && delivered && code == "57014";
    auto              live      = co_await db.query("SELECT 1 AS one");
    const bool        survived  = live.ok() && live.result().size() == 1 && live.result()[0]["one"].as<int>() == 1;

    qb::io::cout() << (canceled ? "[cancel] cancel_async() delivered an out-of-band CancelRequest while pg_sleep was\n"
                                  "         active, as independently confirmed by the observer connection.\n"
                                : "[cancel] UNEXPECTED: active query was not canceled (SQLSTATE=" + code + ")\n");
    qb::io::cout() << (canceled && survived ? "[cancel] the query returned SQLSTATE 57014 and the original connection still answered\n"
                                              "         SELECT 1. cancel() blocks: call it from another thread, not this core.\n"
                                            : "[cancel] UNEXPECTED: connection did not survive cancellation\n");

    auto dropped = co_await db.execute(std::string("DROP TABLE ") + TABLE);
    ok           = session_ok && load_ok && export_ok && canceled && survived && dropped.ok();
    qb::io::cout() << (ok ? "=== copy and cancel complete: temporary table removed ===\n"
                          : "=== copy and cancel FAILED: check preceding verdicts ===\n");
}

int
main() {
    qb::io::async::init();
    bool running = true;
    bool ok      = false;
    qb::io::async::coro_scheduler().spawn(run_copy_and_cancel(running, ok));
    qb::io::async::run_until(running);
    return ok ? 0 : 1;
}
