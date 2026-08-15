/**
 * @file examples/06-modules/redis/07-scripting.cpp
 * @tier 06-modules
 * @teaches Running your logic INSIDE Redis, in the three forms the server offers: an anonymous
 *          EVAL, a cached script called by SHA, and a named 7.0 Function. Includes the trap that
 *          decides whether an EVALSHA deployment survives a restart — NOSCRIPT.
 * @demonstrates qb::redis::tcp::client, eval<long long>, eval<bool>, eval<std::string>,
 *               evalRo<std::string>, script_load, script_exists, evalsha<bool>,
 *               function_load, function_list, function_delete, fcall<std::string>,
 *               fcallRo<std::string>, qb::redis::Reply<T>, ok, error, result, del, get, set,
 *               qb::io::async::init, qb::io::async::run_until, qb::io::async::coro_scheduler,
 *               qb::io::async::task<void>
 * @prerequisites 06-modules/redis/05-transactions
 * @expect "Connected to Redis successfully!"
 * @expect "[eval] the compare-and-set ran as ONE step: nobody could observe the value between"
 * @expect "[eval] and the same script refused the stale write, without a round trip to decide"
 * @expect "[sha] SCRIPT LOAD returned a 40-character SHA1; EVALSHA then sends 40 bytes instead"
 * @expect "[sha] an unknown SHA is NOSCRIPT — the script cache is NOT durable, so production"
 * @expect "[function] FUNCTION LOAD registered a NAMED library; FCALL calls it by function name"
 * @expect "[function] FCALL_RO is the read-only door: it is rejected on a replica-unsafe write"
 * @expect "=== scripting complete: EVAL, EVALSHA + NOSCRIPT, and a named FUNCTION library ==="
 *
 * WHY A SCRIPT AND NOT A TRANSACTION
 * ----------------------------------
 * `06-modules/redis/05-transactions` ends on a limit it cannot pass: inside a MULTI block you
 * cannot READ a value, because no reply is available until EXEC. So "read the version, and write
 * only if it is still the one I read" is not expressible as a transaction — WATCH can only make
 * the whole batch fail and be retried. A script has no such limit: it runs on the server, it can
 * read, branch and write, and the server executes it as one indivisible step.
 *
 * The guarantee is the same one that makes Redis simple: commands run one at a time. While your
 * script runs nothing else does — which is exactly why a SLOW script is an outage. Keep them
 * short, keep them O(1)-ish, and never loop over a whole keyspace inside one.
 *
 * THE THREE FORMS, AND WHEN EACH IS RIGHT
 * ---------------------------------------
 *   EVAL      the script text on every call. Correct, simple, and it re-sends the body every
 *             time — fine for a call you make once.
 *   EVALSHA   `SCRIPT LOAD` once, then call by SHA1. Same semantics, 40 bytes on the wire. The
 *             cache lives in the server's MEMORY: a restart, a failover or somebody else's
 *             `SCRIPT FLUSH` empties it and your next EVALSHA fails with NOSCRIPT. Code that
 *             does not handle that works perfectly until the day it matters.
 *   FCALL     Redis 7.0 Functions. A library is LOADED BY NAME, persists in the RDB/AOF and is
 *             replicated, so there is no NOSCRIPT to handle. This is the deployment answer;
 *             EVALSHA is the transport optimisation.
 *
 * REQUIREMENT: this program needs Redis 7.0 or newer for the FUNCTION section, and says so out
 * loud rather than skipping quietly if it is missing.
 *
 * WHAT THIS PROGRAM DELIBERATELY DOES NOT CALL
 * --------------------------------------------
 * `SCRIPT FLUSH` and `FUNCTION FLUSH`. Both are server-wide and would delete work belonging to
 * every other client of the same Redis, which an example run on a developer's box must never do.
 * The NOSCRIPT case below is provoked with a well-formed SHA that was never loaded — the same
 * error, reached without touching anyone else's cache.
 *
 * Build:
 *   cmake --preset release
 *   cmake --build --preset release --target qb-example-modules-redis-scripting
 * Run (needs a Redis 7.0+ on 127.0.0.1:6379):
 *   ./build/presets/release/examples/06-modules/redis/qb-example-modules-redis-scripting
 */

#include <string>
#include <vector>
#include <qb/io/async.h>
#include <qb/io/async/coroutine.h>
#include <qbm/redis/redis.h>

#define REDIS_URI {"tcp://localhost:6379"}

namespace {

constexpr const char *K_DOC = "qb:example:script:doc";
constexpr const char *K_VER = "qb:example:script:doc:version";
constexpr const char *LIB   = "qbexamplelib";

// Compare-and-set: write only if the caller's expected version is the current one, and bump the
// version in the same breath. Reading, deciding and writing all happen here, which is the thing a
// MULTI block cannot do.
constexpr const char *CAS_SCRIPT = "local current = redis.call('GET', KEYS[2])\n"
                                   "if current == false then current = '0' end\n"
                                   "if current ~= ARGV[1] then return 0 end\n"
                                   "redis.call('SET', KEYS[1], ARGV[2])\n"
                                   "redis.call('SET', KEYS[2], tonumber(current) + 1)\n"
                                   "return 1\n";

// A named library. The `#!lua name=` shebang is not decoration — it is how the server knows what
// to call this library, and FUNCTION LOAD refuses code without it.
constexpr const char *LIB_CODE = "#!lua name=qbexamplelib\n"
                                 "redis.register_function('qbexample_bump', function(keys, args)\n"
                                 "  return tostring(redis.call('INCRBY', keys[1], tonumber(args[1])))\n"
                                 "end)\n"
                                 "redis.register_function{function_name='qbexample_peek',\n"
                                 "  callback=function(keys, args) return redis.call('GET', keys[1]) or '0' end,\n"
                                 "  flags={'no-writes'}}\n";

} // namespace

qb::io::async::task<void>
run_scripting(bool &running, bool &ok) {
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

    (void) co_await redis.del(K_DOC, K_VER);

    // -----------------------------------------------------------------------------------
    // 1. EVAL — read, decide and write, atomically
    // -----------------------------------------------------------------------------------
    (void) co_await redis.set(K_DOC, "draft");
    (void) co_await redis.set(K_VER, "1");

    // KEYS are declared so the server (and a cluster) knows which slots the script touches;
    // ARGV is everything else. Passing a key through ARGV works on a single node and breaks on
    // a cluster — this is the one convention worth obeying from the first script you write.
    // The reply type is the one you asked EVAL for: a Lua `return 1` becomes Reply<long long>.
    qb::redis::Reply<long long> applied = co_await redis.eval<long long>(CAS_SCRIPT, {K_DOC, K_VER}, {"1", "published"});
    auto                        now_doc = co_await redis.get(K_DOC);
    auto                        now_ver = co_await redis.get(K_VER);

    const bool cas_ok =
        applied.ok() && applied.result() == 1 && now_doc.result().value_or("") == "published" && now_ver.result().value_or("") == "2";
    qb::io::cout() << (cas_ok ? "[eval] the compare-and-set ran as ONE step: nobody could observe the value between\n"
                                "       the GET and the SET, because the server runs commands one at a time and a\n"
                                "       script is one command\n"
                              : "[eval] UNEXPECTED: the guarded write did not apply\n");

    // The same script with a now-stale expected version. It returns 0 and writes nothing — the
    // decision was made where the data is, so no second round trip was needed to make it.
    auto stale     = co_await redis.eval<long long>(CAS_SCRIPT, {K_DOC, K_VER}, {"1", "clobbered"});
    auto unchanged = co_await redis.get(K_DOC);
    qb::io::cout() << (stale.ok() && stale.result() == 0 && unchanged.result().value_or("") == "published"
                           ? "[eval] and the same script refused the stale write, without a round trip to decide:\n"
                             "       the branch is evaluated on the server, so a lost race costs one call, not two\n"
                           : "[eval] UNEXPECTED: the stale write was not refused\n");

    // A script's return value is converted by the type you ask for. Lua's `true`/`false` become
    // 1 / nil on the wire, which is why `eval<bool>` is a distinct instantiation and not a cast.
    auto flag = co_await redis.eval<bool>("return redis.call('EXISTS', KEYS[1]) == 1", {K_DOC});
    auto text = co_await redis.evalRo<std::string>("return redis.call('GET', KEYS[1])", {K_DOC});
    qb::io::cout() << "       (eval<bool> -> " << (flag.result() ? "true" : "false") << ", EVAL_RO read '" << text.result()
                   << "' — the _RO forms are rejected if the script writes, which is what makes\n"
                      "        them safe to route to a replica)\n\n";

    // -----------------------------------------------------------------------------------
    // 2. SCRIPT LOAD + EVALSHA — and the NOSCRIPT that ends careless deployments
    // -----------------------------------------------------------------------------------
    auto loaded = co_await redis.script_load(CAS_SCRIPT);
    if (!loaded.ok()) {
        qb::io::cerr() << "SCRIPT LOAD failed: " << loaded.error() << "\n";
        co_return;
    }
    const std::string sha    = loaded.result();
    auto              cached = co_await redis.script_exists(sha);

    // Same script, same semantics, 40 bytes instead of 200-odd on every call.
    auto by_sha = co_await redis.evalsha<bool>(sha, {K_DOC, K_VER}, {"2", "published-again"});
    qb::io::cout() << (loaded.ok() && sha.size() == 40 && !cached.result().empty() && cached.result()[0] && by_sha.ok() && by_sha.result()
                           ? "[sha] SCRIPT LOAD returned a 40-character SHA1; EVALSHA then sends 40 bytes instead\n"
                             "      of the whole body, and SCRIPT EXISTS is how you ask before you call\n"
                           : "[sha] UNEXPECTED: the load/exists/evalsha round did not complete\n");
    qb::io::cout() << "      (sha " << sha << ")\n";

    // The trap. This SHA is well-formed and was never loaded — exactly what every cached SHA
    // looks like after a restart or a failover. The reply is an ERROR, not an empty result, so
    // `ok()` is false and `error()` names it.
    const std::string never_loaded = std::string(40, 'a');
    auto              missing      = co_await redis.evalsha<bool>(never_loaded, {K_DOC, K_VER}, {"3", "x"});
    const bool        noscript     = !missing.ok() && missing.error().find("NOSCRIPT") != std::string::npos;
    qb::io::cout() << (noscript ? "[sha] an unknown SHA is NOSCRIPT — the script cache is NOT durable, so production\n"
                                  "      code is 'EVALSHA, and on NOSCRIPT send the body once with EVAL and retry'.\n"
                                  "      Code without that fallback works until the first restart\n"
                                : "[sha] UNEXPECTED: an unknown SHA did not report NOSCRIPT\n");
    qb::io::cout() << "      (the error was: " << missing.error() << ")\n";

    // ...and the fallback itself, in the two lines it actually takes.
    auto recovered = co_await redis.eval<long long>(CAS_SCRIPT, {K_DOC, K_VER}, {"3", "recovered"});
    qb::io::cout() << "      (fallback: the same call re-sent as EVAL -> "
                   << (recovered.ok() && recovered.result() == 1 ? "applied" : "refused")
                   << ", and the server has re-cached it under the same SHA)\n\n";

    // -----------------------------------------------------------------------------------
    // 3. FUNCTION — the 7.0 answer, where the deployment has a NAME
    // -----------------------------------------------------------------------------------
    // REPLACE so a second run of this example is not an error: loading a library whose name is
    // already registered fails otherwise, and an example must be runnable twice.
    auto lib = co_await redis.function_load(LIB_CODE, "REPLACE");
    if (!lib.ok()) {
        qb::io::cerr() << "FUNCTION LOAD failed: " << lib.error()
                       << "\n(this section needs Redis 7.0 or newer — FUNCTION does not exist before it)\n";
        co_return;
    }

    auto bumped = co_await redis.fcall<std::string>("qbexample_bump", {K_VER}, {"10"});
    auto peeked = co_await redis.fcallRo<std::string>("qbexample_peek", {K_VER}, {});
    auto listed = co_await redis.function_list(std::string{LIB});

    const bool fn_ok = bumped.ok() && peeked.ok() && bumped.result() == peeked.result() && listed.ok() && !listed.result().empty();
    qb::io::cout() << (fn_ok ? "[function] FUNCTION LOAD registered a NAMED library; FCALL calls it by function name\n"
                               "           rather than by hash, and the library is persisted and replicated — so there\n"
                               "           is no NOSCRIPT case to handle at all\n"
                             : "[function] UNEXPECTED: the library did not load and answer\n");
    qb::io::cout() << "           (qbexample_bump -> " << bumped.result() << ", qbexample_peek -> " << peeked.result() << ")\n";

    // The `no-writes` flag is not advisory. FCALL_RO refuses any function not declared with it,
    // which is what lets a read-only caller be routed to a replica safely.
    auto refused = co_await redis.fcallRo<std::string>("qbexample_bump", {K_VER}, {"1"});
    qb::io::cout() << (!refused.ok() ? "[function] FCALL_RO is the read-only door: it is rejected on a replica-unsafe write\n"
                                       "           function, so 'no-writes' is enforced by the server rather than trusted\n"
                                     : "[function] UNEXPECTED: FCALL_RO accepted a writing function\n");
    qb::io::cout() << "           (the refusal was: " << refused.error() << ")\n\n";

    // Cleanup: this library and these keys only. FUNCTION FLUSH / SCRIPT FLUSH would take every
    // other client's work with them — see the header.
    auto removed = co_await redis.function_delete(LIB);
    (void) co_await redis.del(K_DOC, K_VER);

    ok = cas_ok && noscript && fn_ok && removed.ok();
    qb::io::cout() << "=== scripting complete: EVAL, EVALSHA + NOSCRIPT, and a named FUNCTION library ===\n"
                      "    (the library and both keys have been removed; no server-wide flush was issued)\n";
    co_return;
}

int
main() {
    qb::io::async::init();

    bool running = true;
    bool ok      = false;
    qb::io::async::coro_scheduler().spawn(run_scripting(running, ok));
    qb::io::async::run_until(running);

    return ok ? 0 : 1;
}
