/**
 * @file examples/06-modules/redis/15-locks.cpp
 * @tier 06-modules
 * @teaches A single Redis node can grant one time-limited lease: SET NX PX acquires it, and Lua
 *          compares the owner's random token before renewal or removal. Expiry permits takeover;
 *          an old owner must never delete a new owner's lease.
 * @demonstrates qb::redis::tcp::client, qb::redis::UpdateType::NOT_EXIST, qb::crypto::generate_random_bytes,
 *               set, pttl, get, eval<long long>, qb::io::async::sleep,
 *               qb::io::async::init, qb::io::async::run_until, qb::io::async::coro_scheduler,
 *               qb::io::async::task<void>
 * @prerequisites 06-modules/redis/07-scripting, 06-modules/redis/08-sorted-sets-and-ttl
 * @expect "[acquire] SET NX PX granted one lease and refused a second owner."
 * @expect "[renew] a different token could not extend the lease; its owner renewed it atomically."
 * @expect "[release] a different token could not remove the lease; its owner removed it atomically."
 * @expect "[expiry] an expired owner could not renew or remove the new owner's lease."
 * @expect "=== single-node lock complete: no key remains ==="
 *
 * The token is 128 unpredictable bits from qb::crypto's OpenSSL-backed CSPRNG, hex-encoded for
 * readable wire values. The example requires SSL support for that generator, even when its Redis
 * connection is plain TCP. Never use a fixed token, a PID, or a timestamp as an owner identity.
 *
 * SET NX PX is one atomic acquisition. Renew and release are Lua scripts because GET followed by
 * PEXPIRE or DEL as separate commands would race a lease expiry and a new owner. Lua acts on one
 * declared key; the qbm client does not route MOVED/ASK, so this example is for one Redis node,
 * not a distributed Redlock or Cluster service. The lease does not fence stale work: an expired
 * owner must stop acting, and external resources need a fencing protocol if overlap is unsafe.
 * No owner can guarantee cleanup after a process crash; the positive TTL bounds that case.
 *
 * The sliding-window rate limiter already lives in 08-sorted-sets-and-ttl as one atomic EVAL.
 * This lesson adds only the missing lock, not a second limiter.
 *
 * Build: cmake --build --preset release --target qb-example-modules-redis-locks
 * Run:   ./build/presets/release/examples/06-modules/redis/qb-example-modules-redis-locks
 *        (QB_EXAMPLE_REDIS_URI selects a Redis server; default localhost:6379)
 */

#include <array>
#include <chrono>
#include <cstdlib>
#include <string>
#include <qb/io/async.h>
#include <qb/io/async/coroutine.h>
#include <qb/io/crypto.h>
#include <qbm/redis/redis.h>

using namespace std::chrono_literals;

namespace {

constexpr const char *RENEW_SCRIPT = R"lua(
if redis.call('GET', KEYS[1]) ~= ARGV[1] then return 0 end
return redis.call('PEXPIRE', KEYS[1], ARGV[2])
)lua";

constexpr const char *UNLOCK_SCRIPT = R"lua(
if redis.call('GET', KEYS[1]) ~= ARGV[1] then return 0 end
return redis.call('DEL', KEYS[1])
)lua";

std::string
new_token() {
    const auto                     bytes = qb::crypto::generate_random_bytes(16);
    constexpr std::array<char, 16> digits{'0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};
    std::string                    token;
    token.reserve(bytes.size() * 2);
    for (const unsigned char byte : bytes) {
        token.push_back(digits[byte >> 4]);
        token.push_back(digits[byte & 0x0f]);
    }
    return token;
}

qb::io::async::task<long long>
renew(qb::redis::tcp::client &redis, std::string const &key, std::string const &token, long long ttl_ms) {
    auto reply = co_await redis.eval<long long>(RENEW_SCRIPT, {key}, {token, std::to_string(ttl_ms)});
    co_return reply.ok() ? reply.result() : -1;
}

qb::io::async::task<long long>
unlock(qb::redis::tcp::client &redis, std::string const &key, std::string const &token) {
    auto reply = co_await redis.eval<long long>(UNLOCK_SCRIPT, {key}, {token});
    co_return reply.ok() ? reply.result() : -1;
}

} // namespace

qb::io::async::task<void>
run_locks(bool &running, bool &ok) {
    struct StopOnExit {
        bool &flag;
        ~StopOnExit() {
            flag = false;
        }
    } stop{running};

    const char            *configured_uri = std::getenv("QB_EXAMPLE_REDIS_URI");
    const std::string      uri            = configured_uri && *configured_uri ? configured_uri : "tcp://localhost:6379";
    qb::redis::tcp::client redis{qb::io::uri{uri}};
    if (!co_await redis.connect()) {
        qb::io::cerr() << "Redis connection failed\n";
        co_return;
    }

    const std::string owner     = new_token();
    const std::string contender = new_token();
    const std::string successor = new_token();
    // Private run key: no startup DEL that could erase another process's live lease.
    const std::string key = "qb:example:lock:" + new_token();
    if (owner == contender || owner == successor || contender == successor) {
        qb::io::cerr() << "Random tokens unexpectedly collided\n";
        co_return;
    }

    auto       first     = co_await redis.set(key, owner, 30000LL, qb::redis::UpdateType::NOT_EXIST);
    auto       denied    = co_await redis.set(key, contender, 30000LL, qb::redis::UpdateType::NOT_EXIST);
    auto       first_ttl = co_await redis.pttl(key);
    const bool acquired  = first.ok() && static_cast<bool>(first.result()) && denied.ok() && !static_cast<bool>(denied.result())
                           && first_ttl.ok() && first_ttl.result() > 0;
    qb::io::cout() << (acquired ? "[acquire] SET NX PX granted one lease and refused a second owner.\n"
                                : "[acquire] UNEXPECTED: exclusive lease or TTL failed\n");

    const long long foreign_renew = co_await renew(redis, key, contender, 60000);
    const long long owner_renew   = co_await renew(redis, key, owner, 60000);
    auto            renewed_ttl   = co_await redis.pttl(key);
    const bool renewed = acquired && foreign_renew == 0 && owner_renew == 1 && renewed_ttl.ok() && renewed_ttl.result() > first_ttl.result();
    qb::io::cout() << (renewed ? "[renew] a different token could not extend the lease; its owner renewed it atomically.\n"
                               : "[renew] UNEXPECTED: compare-and-PEXPIRE or TTL failed\n");

    const long long foreign_unlock = co_await unlock(redis, key, contender);
    auto            still_owned    = co_await redis.get(key);
    const long long owner_unlock   = co_await unlock(redis, key, owner);
    const bool      released       = renewed && foreign_unlock == 0 && still_owned.ok() && still_owned.result().has_value()
                                     && *still_owned.result() == owner && owner_unlock == 1;
    qb::io::cout() << (released ? "[release] a different token could not remove the lease; its owner removed it atomically.\n"
                                : "[release] UNEXPECTED: compare-and-DEL or owner token failed\n");

    // Deliberately allow the next lease to expire, then prove stale operations are harmless.
    auto brief = co_await redis.set(key, contender, 200LL, qb::redis::UpdateType::NOT_EXIST);
    co_await qb::io::async::sleep(300ms);
    auto            replacement  = co_await redis.set(key, successor, 30000LL, qb::redis::UpdateType::NOT_EXIST);
    const long long stale_renew  = co_await renew(redis, key, contender, 60000);
    const long long stale_unlock = co_await unlock(redis, key, contender);
    auto            current      = co_await redis.get(key);
    const bool      expiry       = released && brief.ok() && static_cast<bool>(brief.result()) && replacement.ok()
                                   && static_cast<bool>(replacement.result()) && stale_renew == 0 && stale_unlock == 0 && current.ok()
                                   && current.result().has_value() && *current.result() == successor;
    qb::io::cout() << (expiry ? "[expiry] an expired owner could not renew or remove the new owner's lease.\n"
                              : "[expiry] UNEXPECTED: expiry or stale-owner check failed\n");

    // Release only a token this process created; never DEL an unowned lock.
    const long long successor_unlock = co_await unlock(redis, key, successor);
    auto            absent           = co_await redis.get(key);
    if (!expiry) {
        (void) co_await unlock(redis, key, owner);
        (void) co_await unlock(redis, key, contender);
    }
    ok = acquired && renewed && released && expiry && successor_unlock == 1 && absent.ok() && !absent.result().has_value();
    qb::io::cout() << (ok ? "=== single-node lock complete: no key remains ===\n"
                          : "=== single-node lock FAILED: check preceding verdicts ===\n");
}

int
main() {
    qb::io::async::init();
    bool running = true;
    bool ok      = false;
    qb::io::async::coro_scheduler().spawn(run_locks(running, ok));
    qb::io::async::run_until(running);
    return ok ? 0 : 1;
}
