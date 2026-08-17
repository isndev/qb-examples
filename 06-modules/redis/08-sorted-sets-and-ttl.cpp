/**
 * @file examples/06-modules/redis/08-sorted-sets-and-ttl.cpp
 * @tier 06-modules
 * @teaches The sorted set as the structure that keeps the ORDER for you — a leaderboard and a
 *          sliding-window rate limiter — plus expiry (EXPIRE/TTL/PERSIST and which writes clear a
 *          TTL) and the cursor SCAN you must reach for instead of KEYS.
 * @demonstrates qb::redis::tcp::client, zadd, zincrby, zcard, zscore, zrevrange, zrevrank,
 *               zrangebyscore, zremrangebyscore, zrem, qb::redis::score_member,
 *               qb::redis::BoundedInterval<double>, qb::redis::LeftBoundedInterval<double>,
 *               qb::redis::LimitOptions,
 *               expire, ttl, persist, setex, incr, set, scan, qb::redis::scan<>,
 *               qb::redis::Reply<T>, ok, result, del,
 *               qb::io::async::init, qb::io::async::run_until, qb::io::async::coro_scheduler,
 *               qb::io::async::task<void>
 * @prerequisites 06-modules/redis/02-data-types
 * @expect "Connected to Redis successfully!"
 * @expect "[board] ZADD kept the set ORDERED as it was written, so 'top 3' is a range read and"
 * @expect "[board] ZREVRANK answers 'what place am I?' in O(log N), and ZSCORE the score itself"
 * @expect "[board] a score RANGE is its own query: everyone from 300 up, newest-first, LIMITed"
 * @expect "[limit] the sliding window is three commands: drop what aged out, count what is left,"
 * @expect "[limit] request 6 of 5 was REFUSED inside the same window, and the window key carries"
 * @expect "[ttl] INCR kept the expiry; a plain SET CLEARED it — a write is not a refresh, and"
 * @expect "[ttl] PERSIST removes an expiry outright: TTL goes from a countdown to -1 (no expiry)"
 * @expect "[scan] SCAN walked the keyspace in bounded steps and found all 5 keys; KEYS would"
 * @expect "=== sorted sets and expiry complete: leaderboard, sliding window, TTL rules and a"
 *
 * WHY A SORTED SET AND NOT A LIST WITH A SORT
 * -------------------------------------------
 * A sorted set stores a score per member and keeps the members ordered by it, all the time. So
 * "the top ten" is a RANGE READ (O(log N + 10)), not a sort of everything you have; "what rank is
 * this player" is a lookup, not a scan; and "everyone between 300 and 600 points" is a query the
 * server answers. Doing any of those with a list means shipping the whole list to the client and
 * sorting it there — every time, for every caller.
 *
 * THE TWO PATTERNS BELOW ARE THE TWO REASONS PEOPLE REACH FOR ONE
 * ---------------------------------------------------------------
 * A LEADERBOARD scores things you rank. A SLIDING-WINDOW RATE LIMITER scores things by TIME: each
 * request is a member whose score is its timestamp, so "how many requests in the last second" is
 * `ZREMRANGEBYSCORE` (drop what aged out) + `ZCARD` (count what is left). That is three commands
 * and no timer, and it is strictly more accurate than a fixed-window counter, which lets 2x the
 * limit through across a window boundary.
 *
 * EXPIRY HAS ONE RULE PEOPLE GET WRONG
 * ------------------------------------
 * A TTL belongs to the KEY, not to the value, and most writes leave it alone — `INCR`, `APPEND`,
 * `HSET`, `ZADD` all keep the countdown running. `SET` is the exception: it REPLACES the key, so
 * it clears the expiry unless you pass KEEPTTL. A cache that refreshes its entries with `SET` and
 * expects the original TTL to survive has an immortal key and does not know it. Section 3 measures
 * both halves.
 *
 * AND NEVER `KEYS` ON A SERVER THAT MATTERS
 * -----------------------------------------
 * `KEYS pattern` walks the entire keyspace in ONE command, and Redis runs commands one at a time —
 * so on a large database it is a stall for every other client. `SCAN` is the same walk in bounded
 * steps: it returns a cursor, you call again until the cursor comes back 0. The guarantee it gives
 * is weaker on purpose: an element present for the whole iteration IS returned, but an element may
 * be returned TWICE, and one added mid-walk may or may not appear. Section 4 does the loop.
 *
 * Every key this program writes is under `qb:example:zt:` and is deleted on the way out.
 *
 * Build:
 *   cmake --preset release
 *   cmake --build --preset release --target qb-example-modules-redis-sorted-sets-and-ttl
 * Run (needs a Redis on 127.0.0.1:6379):
 *   ./build/presets/release/examples/06-modules/redis/qb-example-modules-redis-sorted-sets-and-ttl
 */

#include <chrono>
#include <string>
#include <vector>
#include <qb/io/async.h>
#include <qb/io/async/coroutine.h>
#include <qbm/redis/redis.h>

#define REDIS_URI {"tcp://localhost:6379"}

using namespace std::chrono_literals;

namespace {

constexpr const char *K_BOARD  = "qb:example:zt:leaderboard";
constexpr const char *K_WINDOW = "qb:example:zt:ratelimit:user42";
constexpr const char *K_TTL    = "qb:example:zt:session";
constexpr const char *K_SCAN   = "qb:example:zt:scan:"; // five keys share this prefix

// The rate limit, as one function, because that is how you would actually ship it: three commands
// on one key, no timer, no shared state on your side. `now_ms` is the score.
qb::io::async::task<bool>
allow(qb::redis::tcp::client &redis, std::string const &key, long long limit, std::chrono::milliseconds window) {
    const auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();

    // 1. Drop every request whose timestamp fell out of the window.
    (void) co_await redis.zremrangebyscore(
        key, qb::redis::BoundedInterval<double>(0, static_cast<double>(now_ms - window.count()), qb::redis::BoundType::CLOSED));
    // 2. Count what is left.
    auto used = co_await redis.zcard(key);
    if (!used.ok() || used.result() >= limit)
        co_return false;
    // 3. Record this one. The member must be unique or two requests in the same millisecond
    //    collapse into one entry — a counter suffix is enough here.
    (void) co_await redis.zadd(key, {{static_cast<double>(now_ms), std::to_string(now_ms) + ":" + std::to_string(used.result())}});
    // The key must not outlive the window, or an idle user leaks one key forever.
    (void) co_await redis.expire(key, std::chrono::seconds(1 + window.count() / 1000));
    co_return true;
}

} // namespace

qb::io::async::task<void>
run_sorted_sets_and_ttl(bool &running, bool &ok) {
    struct StopOnExit {
        bool &r;
        ~StopOnExit() {
            r = false;
        }
    } stop{running};

    qb::redis::tcp::client redis{REDIS_URI};
    if (!co_await redis.connect()) {
        qb::io::cerr() << "Failed to connect to Redis\n";
        co_return;
    }
    qb::io::cout() << "Connected to Redis successfully!\n\n";

    (void) co_await redis.del(K_BOARD, K_WINDOW, K_TTL);
    for (int i = 0; i < 5; ++i)
        (void) co_await redis.del(K_SCAN + std::to_string(i));

    // -----------------------------------------------------------------------------------
    // 1. THE LEADERBOARD
    // -----------------------------------------------------------------------------------
    // Named, not built inside the co_await: a temporary in the operand must be promoted into
    // the coroutine frame to survive the suspension.
    const std::vector<qb::redis::score_member> board{{420.0, "ada"}, {310.0, "grace"}, {615.0, "alan"}, {180.0, "linus"}, {520.0, "edsger"}};
    auto                                       added = co_await redis.zadd(K_BOARD, board);

    // ZINCRBY is the update: it returns the NEW score, and it creates the member if it is not
    // there — so "add points" is one command whether or not the player has played before.
    auto ada_now = co_await redis.zincrby(K_BOARD, 250.0, "ada");

    auto       top3     = co_await redis.zrevrange(K_BOARD, 0, 2);
    const bool board_ok = added.ok() && added.result() == 5 && ada_now.ok() && ada_now.result() == 670.0 && top3.ok()
                          && top3.result().size() == 3 && top3.result()[0].member == "ada" && top3.result()[1].member == "alan";
    qb::io::cout() << (board_ok ? "[board] ZADD kept the set ORDERED as it was written, so 'top 3' is a range read and\n"
                                  "        never a sort: ZREVRANGE 0..2 is O(log N + 3) however many players there are\n"
                                : "[board] UNEXPECTED: the top three were not ada, alan, edsger\n");
    // `score_member` spelled out: a sorted set's element is a PAIR, and the reply says so.
    for (qb::redis::score_member const &sm : top3.result())
        qb::io::cout() << "        " << sm.member << "  " << sm.score << "\n";

    auto       rank    = co_await redis.zrevrank(K_BOARD, "grace");
    auto       score   = co_await redis.zscore(K_BOARD, "grace");
    auto       total   = co_await redis.zcard(K_BOARD);
    const bool rank_ok = rank.ok() && rank.result().has_value() && *rank.result() == 3 && score.result().value_or(0) == 310.0;
    qb::io::cout() << (rank_ok ? "[board] ZREVRANK answers 'what place am I?' in O(log N), and ZSCORE the score itself —\n"
                                 "        neither reads the board. A missing member is nullopt, not rank 0\n"
                               : "[board] UNEXPECTED: grace was not in 4th place with 310\n");
    qb::io::cout() << "        (grace: rank " << (rank.result().value_or(-1) + 1) << " of " << total.result() << ", score "
                   << score.result().value_or(0) << "; an unknown player -> "
                   << ((co_await redis.zrevrank(K_BOARD, "nobody")).result().has_value() ? "a rank" : "nullopt") << ")\n";

    // A score RANGE, with LIMIT for pagination. The interval carries its own inclusivity, which
    // is why it is a type and not two doubles.
    //
    // `RIGHT_OPEN` and NOT `CLOSED`, and this one is worth stopping on. A BoundType names the
    // whole interval, not one endpoint: for `[300, +inf)` the OPEN side is the right one, so
    // RIGHT_OPEN leaves the lower bound inclusive. `LeftBoundedInterval<double>` accepts only
    // OPEN and RIGHT_OPEN and THROWS qb::redis::Error on the other two (redis.cpp:127-141) —
    // and `CLOSED` is the reading that "from 300 upwards, 300 included" invites. Measured here:
    // the throw came from an ARGUMENT of a co_await, inside a task spawned on the standalone
    // scheduler, and the program printed nothing at all about it — it simply stopped two
    // sections in and exited. See 09-reliability for why that silence is its own lesson.
    auto strong = co_await redis.zrangebyscore(K_BOARD, qb::redis::LeftBoundedInterval<double>(300.0, qb::redis::BoundType::RIGHT_OPEN),
                                               qb::redis::LimitOptions{0, 3});
    qb::io::cout() << (strong.ok() && strong.result().size() == 3
                           ? "[board] a score RANGE is its own query: everyone from 300 up, newest-first, LIMITed to\n"
                             "        3 — pagination happens on the server, not by fetching everything and slicing\n"
                           : "[board] UNEXPECTED: the score range did not return 3 members\n");
    (void) co_await redis.zrem(K_BOARD, {"linus"});
    qb::io::cout() << "        (after ZREM linus, " << (co_await redis.zcard(K_BOARD)).result() << " players remain)\n\n";

    // -----------------------------------------------------------------------------------
    // 2. THE SLIDING-WINDOW RATE LIMIT — the same structure, scored by TIME
    // -----------------------------------------------------------------------------------
    constexpr long long LIMIT   = 5;
    int                 allowed = 0, refused = 0;
    for (int i = 0; i < 6; ++i)
        (co_await allow(redis, K_WINDOW, LIMIT, 1000ms)) ? ++allowed : ++refused;

    auto       window_ttl = co_await redis.ttl(K_WINDOW);
    const bool limit_ok   = allowed == 5 && refused == 1 && window_ttl.ok() && window_ttl.result() > 0;
    qb::io::cout() << (limit_ok ? "[limit] the sliding window is three commands: drop what aged out, count what is left,\n"
                                  "        record this one. No timer, no state on the client, and it does not let 2x the\n"
                                  "        limit through at a window boundary the way a fixed-window counter does\n"
                                : "[limit] UNEXPECTED: 6 requests against a limit of 5 did not give 5 allowed / 1 refused\n");
    qb::io::cout() << (limit_ok ? "[limit] request 6 of 5 was REFUSED inside the same window, and the window key carries\n"
                                  "        an EXPIRE so an idle caller does not leak a key forever\n"
                                : "[limit] UNEXPECTED: the sixth request was not refused\n");
    qb::io::cout() << "        (allowed " << allowed << ", refused " << refused << ", window key expires in " << window_ttl.result()
                   << "s)\n\n";

    // -----------------------------------------------------------------------------------
    // 3. EXPIRY — which writes keep a TTL, and which clear it
    // -----------------------------------------------------------------------------------
    (void) co_await redis.setex(K_TTL, std::chrono::seconds(60), "1");
    auto ttl_fresh = co_await redis.ttl(K_TTL);
    (void) co_await redis.incr(K_TTL);
    auto ttl_after_incr = co_await redis.ttl(K_TTL);
    (void) co_await redis.set(K_TTL, "reset"); // no KEEPTTL -> the key is replaced
    auto ttl_after_set = co_await redis.ttl(K_TTL);

    // -1 means "the key exists and has no expiry"; -2 means "there is no key". Two different
    // answers that a plain integer return would let you confuse.
    const bool ttl_ok = ttl_fresh.result() > 0 && ttl_after_incr.result() > 0 && ttl_after_set.result() == -1;
    qb::io::cout() << (ttl_ok ? "[ttl] INCR kept the expiry; a plain SET CLEARED it — a write is not a refresh, and\n"
                                "      SET is the one that replaces the key. Pass KEEPTTL when you meant to keep it\n"
                              : "[ttl] UNEXPECTED: INCR and SET did not differ over the TTL\n");
    qb::io::cout() << "      (after SETEX 60: " << ttl_fresh.result() << "s, after INCR: " << ttl_after_incr.result()
                   << "s, after SET: " << ttl_after_set.result() << ")\n";

    (void) co_await redis.expire(K_TTL, std::chrono::seconds(30));
    auto       persisted  = co_await redis.persist(K_TTL);
    auto       ttl_gone   = co_await redis.ttl(K_TTL);
    auto       ttl_nokey  = co_await redis.ttl("qb:example:zt:no-such-key");
    const bool persist_ok = persisted.result() && ttl_gone.result() == -1 && ttl_nokey.result() == -2;
    qb::io::cout() << (persist_ok ? "[ttl] PERSIST removes an expiry outright: TTL goes from a countdown to -1 (no expiry),\n"
                                    "      while -2 is the different answer 'no such key' — never treat them as one\n"
                                  : "[ttl] UNEXPECTED: PERSIST / -1 / -2 did not behave as documented\n");
    qb::io::cout() << "      (persisted: " << (persisted.result() ? "yes" : "no") << ", TTL now " << ttl_gone.result()
                   << ", TTL of a missing key " << ttl_nokey.result() << ")\n\n";

    // -----------------------------------------------------------------------------------
    // 4. SCAN — the bounded walk
    // -----------------------------------------------------------------------------------
    for (int i = 0; i < 5; ++i)
        (void) co_await redis.set(K_SCAN + std::to_string(i), "v");

    // The loop is the API: start at cursor 0, call again with whatever came back, stop when it
    // is 0 again. COUNT is a HINT about work per call, not a page size — a step may return more
    // or fewer, and an empty step with a non-zero cursor is normal, not the end.
    long long                      cursor = 0;
    int                            steps  = 0;
    qb::unordered_set<std::string> seen;
    do {
        qb::redis::Reply<qb::redis::scan<>> step = co_await redis.scan(cursor, std::string(K_SCAN) + "*", 2);
        if (!step.ok()) {
            qb::io::cerr() << "SCAN failed: " << step.error() << "\n";
            break;
        }
        ++steps;
        for (auto const &k : step.result().items)
            seen.insert(k);
        cursor = static_cast<long long>(step.result().cursor);
    } while (cursor != 0);

    const bool scan_ok = seen.size() == 5 && steps >= 1;
    qb::io::cout() << (scan_ok ? "[scan] SCAN walked the keyspace in bounded steps and found all 5 keys; KEYS would\n"
                                 "       have done it in one command that blocks every other client for its duration.\n"
                                 "       COUNT is a hint, an empty step is not the end, and a key may arrive twice —\n"
                                 "       which is why the results go into a set\n"
                               : "[scan] UNEXPECTED: the cursor walk did not find the 5 keys\n");
    qb::io::cout() << "       (" << steps << " step(s), " << seen.size() << " distinct key(s))\n\n";

    // Cleanup, on the success path and — via the guard at the top — with `running` cleared on
    // every other one too.
    (void) co_await redis.del(K_BOARD, K_WINDOW, K_TTL);
    for (int i = 0; i < 5; ++i)
        (void) co_await redis.del(K_SCAN + std::to_string(i));

    ok = board_ok && rank_ok && limit_ok && ttl_ok && persist_ok && scan_ok;
    qb::io::cout() << "=== sorted sets and expiry complete: leaderboard, sliding window, TTL rules and a\n"
                      "    cursor walk; every key written above has been deleted ===\n";
    co_return;
}

int
main() {
    qb::io::async::init();

    bool running = true;
    bool ok      = false;
    qb::io::async::coro_scheduler().spawn(run_sorted_sets_and_ttl(running, ok));
    qb::io::async::run_until(running);

    return ok ? 0 : 1;
}
