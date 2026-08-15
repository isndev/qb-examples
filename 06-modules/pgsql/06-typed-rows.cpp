/**
 * @file examples/06-modules/pgsql/06-typed-rows.cpp
 * @tier 06-modules
 * @teaches Reading a result set without writing a loop: a row as a std::tuple, `one<>()` for the
 *          single-row query, `all<>()` versus the lazy `rows<>()` view, `field::text()` for a
 *          read with no copy, and `resultset::json()` instead of a hand-written to_json().
 * @demonstrates qb::pg::tcp::database, qb::pg::results, execute, query,
 *               one<int, std::string>, all<int, std::string>,
 *               rows<int, std::string, std::optional<std::string>, double>,
 *               as<std::tuple<int, std::string, std::optional<std::string>, double>>,
 *               text, view, is_null, json, size,
 *               qb::io::async::init, qb::io::async::run_until, qb::io::async::coro_scheduler,
 *               qb::io::async::task<void>
 * @prerequisites 06-modules/pgsql/02-parameters
 * @expect "Connected — building a small table to read four different ways."
 * @expect "[loop] the by-name loop is fine, and it is also where two runtime errors live: a"
 * @expect "[tuple] row::as<std::tuple<...>> decodes the whole row in one call and binds it to"
 * @expect "[one] one<>() is the single-row query without the ceremony: no empty() check, no [0],"
 * @expect "[all] all<>() materialises a vector<tuple> you own; rows<>() is the same sequence as a"
 * @expect "[view] field::text() is a string_view INTO the result set — no allocation, and no"
 * @expect "[json] resultset::json() renders the whole set as a JSON array in one call, so a"
 * @expect "=== typed rows complete: tuple, one, all, rows, text and json — and the table is gone"
 *
 * THE PROBLEM
 * -----------
 * Every pgsql example before this one reads results the same way: a `for` over the rows, then
 * `row["name"].as<std::string>()` per column. That is correct and it is the right thing for one
 * column. It is also where two runtime failures live, and both of them look like a typo:
 *
 *   * the column NAME is a string, so `row["nmae"]` compiles and throws at run time;
 *   * `as<T>()` on a NULL field throws `value_is_null` unless `T` is a `std::optional`.
 *
 * The typed accessors move both of those to ONE place — the tuple you declare — where they are
 * stated once for the whole query instead of once per column per loop.
 *
 * FOUR ACCESSORS, AND THE ONE THING THAT DECIDES BETWEEN THEM
 * -----------------------------------------------------------
 *   row.as<std::tuple<Ts...>>()   one row, decoded whole. Structured bindings follow.
 *   res.one<Ts...>()              std::optional<std::tuple<Ts...>> — the FIRST row or nullopt.
 *                                 This is the shape of every "SELECT ... WHERE id = $1".
 *   res.all<Ts...>()              std::vector<std::tuple<Ts...>>, eager. You own it; it outlives
 *                                 the result set.
 *   res.rows<Ts...>()             the same sequence as a LAZY VIEW. Nothing is copied and nothing
 *                                 is allocated — and it BORROWS the result set, so it must not
 *                                 outlive it. That is the whole difference.
 *
 * ...and `field::text()` is the same trade one level down: a `std::string_view` into the result
 * set's own row storage rather than the `std::string` copy `as<std::string>()` makes. Free to
 * take, illegal to keep.
 *
 * WHAT THE ARCHITECTURE DOCUMENT CALLED THESE
 * -------------------------------------------
 * It named `first_as` and `views<Ts...>`. Neither exists in this tree; the shipped names are
 * `one<Ts...>` and `rows<Ts...>` (`qbm/pgsql/src/qbm/pgsql/resultset.h:214/230/248`), and this
 * file is written against the header rather than against the plan.
 *
 * The table is created and DROPPED by this program, so running it twice is the same as running it
 * once.
 *
 * Build:
 *   cmake --preset release
 *   cmake --build --preset release --target qb-example-modules-pgsql-typed-rows
 * Run (needs a PostgreSQL on 127.0.0.1:5432 with test:test@[test]):
 *   ./build/presets/release/examples/06-modules/pgsql/qb-example-modules-pgsql-typed-rows
 */

#include <optional>
#include <string>
#include <tuple>
#include <vector>
#include <qb/io/async.h>
#include <qb/io/async/coroutine.h>
#include <qbm/pgsql/pgsql.h>

namespace {

const char           *PG_CONNECTION_STRING = "tcp://test:test@localhost:5432[test]";
constexpr const char *TABLE                = "qb_example_typed_rows";

} // namespace

qb::io::async::task<void>
run_typed_rows(bool &running, bool &ok) {
    struct StopOnExit {
        bool &r;
        ~StopOnExit() {
            r = false;
        }
    } stop{running};

    qb::pg::tcp::database db;
    if (!co_await db.connect(PG_CONNECTION_STRING)) {
        qb::io::cerr() << "Failed to connect to PostgreSQL: " << db.error().what() << std::endl;
        co_return;
    }

    // ---- setup ------------------------------------------------------------------------
    (void) co_await db.execute(std::string("DROP TABLE IF EXISTS ") + TABLE + ";");
    auto created = co_await db.execute(std::string("CREATE TABLE ") + TABLE
                                       + " (id SERIAL PRIMARY KEY, name TEXT NOT NULL, email TEXT, score DOUBLE PRECISION NOT NULL);");
    if (!created.ok()) {
        qb::io::cerr() << "CREATE TABLE failed: " << created.error().what() << std::endl;
        co_return;
    }

    // `query(sql, args...)` binds the values instead of pasting them into the string: one call,
    // no prepared-statement name to invent, and no way to build an injection by accident.
    for (auto const &[name, email, score] : std::vector<std::tuple<std::string, std::optional<std::string>, double>>{
             {"ada", std::optional<std::string>("ada@example.test"), 91.5},
             {"grace", std::optional<std::string>("grace@example.test"), 88.0},
             {"alan", std::nullopt, 95.25},
             {"edsger", std::optional<std::string>("edsger@example.test"), 79.75}
         }) {
        auto ins = co_await db.query(std::string("INSERT INTO ") + TABLE + " (name, email, score) VALUES ($1, $2, $3);", name, email, score);
        if (!ins.ok()) {
            qb::io::cerr() << "INSERT failed: " << ins.error().what() << std::endl;
            co_return;
        }
    }
    qb::io::cout() << "Connected — building a small table to read four different ways.\n"
                      "(4 rows, one of them with a NULL email, which is the row that decides everything below)\n\n";

    auto all_rows = co_await db.execute(std::string("SELECT id, name, email, score FROM ") + TABLE + " ORDER BY id;");
    if (!all_rows.ok()) {
        qb::io::cerr() << "SELECT failed: " << all_rows.error().what() << std::endl;
        co_return;
    }
    const qb::pg::results &rs = all_rows.result();

    // -----------------------------------------------------------------------------------
    // 1. THE BASELINE — by name, one column at a time
    // -----------------------------------------------------------------------------------
    int         counted = 0;
    std::string first_name;
    for (auto const &row : rs) {
        if (counted == 0)
            first_name = row["name"].as<std::string>();
        ++counted;
    }
    // The NULL column read into a plain type is the failure this loop invites. `as<optional<T>>`
    // is the fix, and it has to be written at EVERY site that touches a nullable column.
    const bool null_is_null = rs[2]["email"].is_null();
    const auto safe_email   = rs[2]["email"].as<std::optional<std::string>>();

    const bool loop_ok = counted == 4 && first_name == "ada" && null_is_null && !safe_email.has_value();
    qb::io::cout() << (loop_ok ? "[loop] the by-name loop is fine, and it is also where two runtime errors live: a\n"
                                 "       misspelled column name is a string that compiles, and as<T>() on a NULL throws\n"
                                 "       unless T is a std::optional. Both are per-column, per-loop decisions\n"
                               : "[loop] UNEXPECTED: the by-name loop did not read 4 rows with one NULL email\n");
    qb::io::cout() << "       (" << counted << " rows, first is '" << first_name
                   << "', row 3's email is_null() = " << (null_is_null ? "yes" : "no") << ")\n\n";

    // -----------------------------------------------------------------------------------
    // 2. A ROW AS A TUPLE
    // -----------------------------------------------------------------------------------
    // The nullable column is declared `std::optional<std::string>` ONCE, here, for the whole
    // query — instead of at each of the places that read it.
    using Person                      = std::tuple<int, std::string, std::optional<std::string>, double>;
    auto [id0, name0, email0, score0] = rs[0].as<Person>();

    const bool tuple_ok = id0 > 0 && name0 == "ada" && email0.has_value() && score0 == 91.5;
    qb::io::cout() << (tuple_ok ? "[tuple] row::as<std::tuple<...>> decodes the whole row in one call and binds it to\n"
                                  "        names with a structured binding — the column ORDER is the contract now, and\n"
                                  "        the nullability is declared once in the tuple rather than at each read\n"
                                : "[tuple] UNEXPECTED: the first row did not decode to (id, 'ada', <email>, 91.5)\n");
    qb::io::cout() << "        (" << id0 << ", " << name0 << ", " << email0.value_or("<null>") << ", " << score0 << ")\n\n";

    // -----------------------------------------------------------------------------------
    // 3. one<>() — the single-row query, without the ceremony
    // -----------------------------------------------------------------------------------
    auto found   = co_await db.query(std::string("SELECT id, name FROM ") + TABLE + " WHERE name = $1;", std::string("grace"));
    auto missing = co_await db.query(std::string("SELECT id, name FROM ") + TABLE + " WHERE name = $1;", std::string("nobody"));

    std::optional<std::tuple<int, std::string>> hit  = found.result().one<int, std::string>();
    std::optional<std::tuple<int, std::string>> miss = missing.result().one<int, std::string>();

    const bool one_ok = hit.has_value() && std::get<1>(*hit) == "grace" && !miss.has_value();
    qb::io::cout() << (one_ok ? "[one] one<>() is the single-row query without the ceremony: no empty() check, no [0],\n"
                                "      and 'no such row' is a nullopt you cannot forget to test rather than an\n"
                                "      out-of-range access on an empty result set\n"
                              : "[one] UNEXPECTED: one<>() did not find grace and miss 'nobody'\n");
    qb::io::cout() << "      (found: " << (hit ? std::get<1>(*hit) : "<none>") << ", missing: " << (miss ? "a row" : "nullopt") << ")\n\n";

    // -----------------------------------------------------------------------------------
    // 4. all<>() versus rows<>() — a copy you own against a view that borrows
    // -----------------------------------------------------------------------------------
    std::vector<std::tuple<int, std::string>> owned = rs.all<int, std::string>();

    // The lazy one. Nothing is decoded until the loop asks for it, and nothing is allocated at
    // all. It holds iterators INTO `rs`, so `rs` has to outlive it — which is why `owned` is the
    // right answer when the data must leave this scope.
    //
    // The tuple is POSITIONAL over the SELECT list, and the first N columns are the ones it
    // takes — there is no name matching. `rows<int, double>()` here would decode column 1
    // (`name`) as a double and throw at run time, not at compile time. Measured while writing
    // this file, and the throw was invisible: an exception escaping a task handed to
    // `coro_scheduler().spawn()` is stored on the promise and never observed, so the program
    // simply stopped printing (see 06-modules/redis/09-reliability).
    double sum  = 0;
    int    seen = 0;
    for (auto [id, name, email, score] : rs.rows<int, std::string, std::optional<std::string>, double>()) {
        (void) id;
        (void) name;
        (void) email;
        sum += score;
        ++seen;
    }

    const bool all_ok = owned.size() == 4 && std::get<1>(owned[3]) == "edsger" && seen == 4 && sum > 350.0;
    qb::io::cout() << (all_ok ? "[all] all<>() materialises a vector<tuple> you own; rows<>() is the same sequence as a\n"
                                "      lazy view that decodes on demand and BORROWS the result set. Return the vector,\n"
                                "      iterate the view — a view that outlives its result set is a dangling range\n"
                              : "[all] UNEXPECTED: all<>() and rows<>() did not agree on 4 rows\n");
    qb::io::cout() << "      (" << owned.size() << " owned tuples, view walked " << seen << " rows, total score " << sum << ")\n\n";

    // -----------------------------------------------------------------------------------
    // 5. field::text() — reading without copying
    // -----------------------------------------------------------------------------------
    // `as<std::string>()` allocates a copy per call. `text()` is a view of the bytes already in
    // the result set — the right thing for a comparison, a hash, or a parse you do immediately.
    std::string_view zero_copy = rs[1]["name"].text();
    std::string      a_copy    = rs[1]["name"].as<std::string>();
    const auto       raw_bytes = rs[1]["name"].view();

    const bool view_ok = zero_copy == "grace" && a_copy == zero_copy && raw_bytes.size() == zero_copy.size();
    qb::io::cout() << (view_ok ? "[view] field::text() is a string_view INTO the result set — no allocation, and no\n"
                                 "       lifetime of its own: the moment that result set goes, so does the view. Use it\n"
                                 "       to compare or parse in place; use as<std::string>() when the value must survive\n"
                               : "[view] UNEXPECTED: text() and as<std::string>() disagreed\n");
    qb::io::cout() << "       ('" << zero_copy << "' as a view of " << raw_bytes.size() << " byte(s), and the same value copied)\n\n";

    // -----------------------------------------------------------------------------------
    // 6. resultset::json() — the whole set, once
    // -----------------------------------------------------------------------------------
    // Every application-shaped example in this corpus hand-writes a per-model `to_json()`. For an
    // endpoint that returns rows as they are, this is that function, already written: an array of
    // objects keyed by COLUMN NAME, with SQL NULL as JSON null.
    qb::json body = rs.json();

    const bool json_ok = body.is_array() && body.size() == 4 && body[0]["name"] == "ada" && body[2]["email"].is_null();
    qb::io::cout() << (json_ok ? "[json] resultset::json() renders the whole set as a JSON array in one call, so a\n"
                                 "       read-only endpoint needs no per-model serialiser — and a SQL NULL arrives as a\n"
                                 "       JSON null rather than as an empty string\n"
                               : "[json] UNEXPECTED: json() did not produce a 4-element array with a null email\n");
    qb::io::cout() << "       (first element: " << body[0].dump() << ")\n";
    // Read that dump before you build an API on it: every value is a STRING, because json()
    // renders the text-format wire bytes rather than re-typing them. Only NULL is distinguished
    // (it becomes a JSON null). If a consumer needs numbers as numbers, cast in SQL or map the
    // row yourself — this is the fast path for a passthrough, not a typed serialiser.
    qb::io::cout() << "       (note: scalars come through as JSON strings; only NULL is typed)\n\n";

    // ---- cleanup ----------------------------------------------------------------------
    auto dropped = co_await db.execute(std::string("DROP TABLE IF EXISTS ") + TABLE + ";");

    ok = loop_ok && tuple_ok && one_ok && all_ok && view_ok && json_ok && dropped.ok();
    qb::io::cout() << "=== typed rows complete: tuple, one, all, rows, text and json — and the table is gone\n"
                      "    again, so a second run measures the same thing as the first ===\n";
    co_return;
}

int
main() {
    qb::io::async::init();

    bool running = true;
    bool ok      = false;
    qb::io::async::coro_scheduler().spawn(run_typed_rows(running, ok));
    qb::io::async::run_until(running);

    return ok ? 0 : 1;
}
