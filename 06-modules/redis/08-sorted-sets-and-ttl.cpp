/**
 * @file examples/06-modules/redis/08-sorted-sets-and-ttl.cpp
 * @tier 06-modules
 * @teaches The sorted set as the structure that keeps the ORDER for you — a leaderboard and a
 *          sliding-window rate limiter with one atomic EVAL — plus expiry (EXPIRE/TTL/PERSIST and which writes clear a
 *          TTL) and the cursor SCAN you must reach for instead of KEYS.
 * @demonstrates qb::redis::tcp::client, zadd, zincrby, zcard, zscore, zrevrange, zrevrank,
 *               zrangebyscore, zrem, qb::redis::score_member,
 *               qb::redis::LeftBoundedInterval<double>,
 *               qb::redis::LimitOptions,
 *               eval<long long>, expire, ttl, persist, setex, incr, set, scan, qb::redis::scan<>,
 *               qb::redis::Reply<T>, ok, result, del,
 *               qb::io::async::init, qb::io::async::run_until, qb::io::async::coro_scheduler,
 *               qb::io::async::task<void>
 * @prerequisites 06-modules/redis/02-data-types
 * @expect "Connected to Redis successfully!"
 * @expect "[board] ZADD kept the set ORDERED as it was written, so 'top 3' is a range read and"
 * @expect "[board] ZREVRANK answers 'what place am I?' in O(log N), and ZSCORE the score itself"
 * @expect "[board] a score RANGE is its own query: everyone from 300 up, newest-first, LIMITed"
 * @expect "[limit] one EVAL atomically drops aged requests, counts survivors, records one, and"
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
 * `ZREMRANGEBYSCORE` (drop what aged out) + `ZCARD` (count what is left) + `ZADD NX` (record one).
 * Those steps MUST be one atomic decision: separate awaited commands let two callers count the
 * same free slot and a same-millisecond member can collapse their entries. The script below runs
 * them as one EVAL, takes time from Redis, and checks ZADD and PEXPIRE before it admits. An error
 * fails closed; if expiry fails after the add, it tries to remove that add before returning an
 * error. If cleanup is itself denied, the request is still refused but its entry may remain.
 * One EVAL is one client/server round trip. An admission runs five short Redis commands inside
 * (TIME, purge, count, add, expiry); a quota refusal runs the first three. A rollback adds ZREM.
 * EVAL sends its script body on every call. A hot production path could cache its SHA and handle
 * NOSCRIPT after restart or failover.
 * The script declares just KEYS[1], so Cluster slot rules are satisfied. The qbm-redis client
 * does not follow MOVED/ASK; a Cluster caller must reach the key's owning node.
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
 * Run (defaults to Redis on 127.0.0.1:6379; QB_EXAMPLE_REDIS_URI overrides it):
 *   ./build/presets/release/examples/06-modules/redis/qb-example-modules-redis-sorted-sets-and-ttl
 */

#include <chrono>
#include <cstdlib>
#include <string>
#include <vector>
#include <qb/io/async.h>
#include <qb/io/async/coroutine.h>
#include <qbm/redis/redis.h>

using namespace std::chrono_literals;

namespace {

constexpr const char *K_BOARD  = "qb:example:zt:leaderboard";
constexpr const char *K_WINDOW = "qb:example:zt:ratelimit:user42";
constexpr const char *K_TTL    = "qb:example:zt:session";
constexpr const char *K_SCAN   = "qb:example:zt:scan:"; // five keys share this prefix

// Redis serializes this whole read/decide/write step, including competing clients. The optional
// third argument fixes the clock only in the disposable-server test; normal calls use Redis TIME.
constexpr const char *SLIDING_WINDOW_SCRIPT = R"lua(
local limit = tonumber(ARGV[1])
local window_ms = tonumber(ARGV[2])
if not limit or limit < 1 or not window_ms or window_ms < 1 then
  return redis.error_reply('invalid rate limit or window')
end
local now_ms
if ARGV[3] then
  now_ms = tonumber(ARGV[3])
else
  local clock = redis.call('TIME')
  now_ms = tonumber(clock[1]) * 1000 + math.floor(tonumber(clock[2]) / 1000)
end
if not now_ms then return redis.error_reply('invalid clock') end
redis.call('ZREMRANGEBYSCORE', KEYS[1], '-inf', now_ms - window_ms)
local used = redis.call('ZCARD', KEYS[1])
if used >= limit then return 0 end
-- The count changes on every admission at this timestamp. NX still refuses any collision.
local member = tostring(now_ms) .. ':' .. tostring(used)
local added = redis.pcall('ZADD', KEYS[1], 'NX', now_ms, member)
if type(added) == 'table' and added.err then return redis.error_reply(added.err) end
if added ~= 1 then return redis.error_reply('ZADD member collision') end
local expires = redis.pcall('PEXPIRE', KEYS[1], window_ms + 1000)
if type(expires) == 'table' and expires.err then
  redis.call('ZREM', KEYS[1], member)
  return redis.error_reply(expires.err)
end
if expires ~= 1 then
  redis.call('ZREM', KEYS[1], member)
  return redis.error_reply('PEXPIRE did not set a TTL')
end
return 1
)lua";

enum class Admission { allowed, limited, failed };

qb::io::async::task<Admission>
allow(qb::redis::tcp::client &redis, std::string const &key, long long limit, std::chrono::milliseconds window) {
    if (limit < 1 || window.count() < 1)
        co_return Admission::failed;
    qb::redis::Reply<long long> decision =
        co_await redis.eval<long long>(SLIDING_WINDOW_SCRIPT, {key}, {std::to_string(limit), std::to_string(window.count())});
    // Redis errors (including ZADD / PEXPIRE failure) are Reply values, not exceptions.
    if (!decision.ok())
        co_return Admission::failed;
    if (decision.result() == 1)
        co_return Admission::allowed;
    co_return decision.result() == 0 ? Admission::limited : Admission::failed;
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

    const char            *configured_uri = std::getenv("QB_EXAMPLE_REDIS_URI");
    const std::string      redis_uri      = configured_uri ? configured_uri : "tcp://localhost:6379";
    qb::redis::tcp::client redis{qb::io::uri{redis_uri}};
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
    int                 allowed = 0, refused = 0, failed = 0;
    for (int i = 0; i < 6; ++i) {
        switch (co_await allow(redis, K_WINDOW, LIMIT, 1000ms)) {
            case Admission::allowed:
                ++allowed;
                break;
            case Admission::limited:
                ++refused;
                break;
            case Admission::failed:
                ++failed;
                break;
        }
    }

    auto       window_ttl = co_await redis.ttl(K_WINDOW);
    const bool limit_ok   = allowed == 5 && refused == 1 && failed == 0 && window_ttl.ok() && window_ttl.result() > 0;
    qb::io::cout() << (limit_ok ? "[limit] one EVAL atomically drops aged requests, counts survivors, records one, and\n"
                                  "        sets expiry. One round trip, no client timer; it does not let 2x the\n"
                                  "        limit through at a window boundary the way a fixed-window counter does\n"
                                : "[limit] UNEXPECTED: 6 requests against a limit of 5 did not give 5 allowed / 1 refused\n");
    qb::io::cout() << (limit_ok ? "[limit] request 6 of 5 was REFUSED inside the same window, and the window key carries\n"
                                  "        an EXPIRE so an idle caller does not leak a key forever\n"
                                : "[limit] UNEXPECTED: the sixth request was not refused\n");
    qb::io::cout() << "        (allowed " << allowed << ", refused " << refused << ", errors " << failed << ", window key expires in "
                   << (window_ttl.ok() ? std::to_string(window_ttl.result()) + "s" : "n/a") << ")\n\n";

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
