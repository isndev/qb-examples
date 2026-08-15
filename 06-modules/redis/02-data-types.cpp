/**
 * @file examples/06-modules/redis/02-data-types.cpp
 * @tier 06-modules
 * @teaches The four everyday Redis structures — string, hash, list, set — and the one thing that
 *          tells them apart in this client: the C++ TYPE each command's Reply comes back as. An
 *          unordered_set is not a stylistic choice, it is what a Redis set IS.
 * @demonstrates qb::redis::tcp::client, set, get, append, strlen, incrby, mset, mget,
 *               hset, hget, hgetall, hincrby, hexists, hdel, hlen,
 *               rpush, lpush, lrange, llen, ltrim, lpop, rpop,
 *               sadd, smembers, sismember, scard, sinter, sunion, sdiff, srem,
 *               qb::redis::Reply<T>, ok, result, del,
 *               qb::io::async::init, qb::io::async::run_until, qb::io::async::coro_scheduler,
 *               qb::io::async::task<void>
 * @prerequisites 06-modules/redis/01-connect
 * @expect "Connected to Redis successfully!"
 * @expect "[string] a missing key is nullopt, an EMPTY key is a string of length 0 — and only"
 * @expect "[string] INCRBY parses the string as an integer server-side: 41 + 1 = 42"
 * @expect "[hash] one round trip returned the whole object as a map, and HINCRBY moved ONE field"
 * @expect "[hash] a missing field is nullopt while the hash itself exists — absence has two"
 * @expect "[list] RPUSH appends, LPUSH prepends, LRANGE reads the ORDER back — a list is a"
 * @expect "[list] LTRIM is how a list stays a bounded log: 6 entries in, newest 3 kept"
 * @expect "[set] SMEMBERS came back as an unordered_set: no duplicates, no order, membership"
 * @expect "[set] SINTER / SUNION / SDIFF ran INSIDE the server — the answer crossed the wire,"
 * @expect "=== data types complete: string, hash, list and set, each with the Reply type that"
 *
 * WHY THESE FOUR ARE ONE PROGRAM
 * ------------------------------
 * They were two programs — hashes in one, lists in another — and neither said why you would pick
 * one over the other, which is the only question a reader actually has. Sets had no demonstrator
 * anywhere in the corpus at all, and they are the structure that answers "have I already seen
 * this?" without shipping the answer's evidence to the client.
 *
 * THE RULE THAT MAKES THE CLIENT READABLE
 * ---------------------------------------
 * Every command here is `co_await`ed and hands back a `qb::redis::Reply<T>` whose `T` is chosen
 * by the command, not by you. Read the type and you have read the semantics:
 *
 *   get(k)            Reply<std::optional<std::string>>            may be ABSENT
 *   strlen(k)         Reply<long long>                             a length is never absent
 *   hgetall(k)        Reply<qb::unordered_map<string, string>>     an object, whole
 *   lrange(k, a, b)   Reply<std::vector<std::string>>              ORDER is part of the value
 *   smembers(k)       Reply<qb::unordered_set<std::string>>        no order, no duplicates
 *   sismember(k, m)   Reply<bool>                                  the question, not the set
 *
 * Always check `ok()` before `result()`: on the disconnect path `raw()` is `nullptr` and
 * `result()` hands back a default-constructed `T`, which for `optional` is an empty one — a
 * missing key and a dead connection then look identical if you skipped the check.
 *
 * THIS PROGRAM CLEANS UP AFTER ITSELF
 * -----------------------------------
 * Every key it writes is under `qb:example:dt:` and every one is deleted on the way out, on both
 * the success and the failure path. An example whose runtime depends on how many times it has
 * been run is not a passing example — this corpus has one that leaves a million stream entries
 * behind, and its runtime doubled between two consecutive runs on the same server.
 *
 * Build:
 *   cmake --preset release
 *   cmake --build --preset release --target qb-example-modules-redis-data-types
 * Run (needs a Redis on 127.0.0.1:6379):
 *   ./build/presets/release/examples/06-modules/redis/qb-example-modules-redis-data-types
 */

#include <string>
#include <vector>
#include <qb/io/async.h>
#include <qb/io/async/coroutine.h>
#include <qbm/redis/redis.h>

#define REDIS_URI {"tcp://localhost:6379"}

namespace {

// One prefix, so the cleanup below is exhaustive by construction rather than by memory.
constexpr const char *K_STR   = "qb:example:dt:greeting";
constexpr const char *K_EMPTY = "qb:example:dt:empty";
constexpr const char *K_COUNT = "qb:example:dt:counter";
constexpr const char *K_HASH  = "qb:example:dt:user:1";
constexpr const char *K_LIST  = "qb:example:dt:eventlog";
constexpr const char *K_SET_A = "qb:example:dt:tags:a";
constexpr const char *K_SET_B = "qb:example:dt:tags:b";

} // namespace

qb::io::async::task<void>
run_data_types(bool &running, bool &ok) {
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

    (void) co_await redis.del(K_STR, K_EMPTY, K_COUNT, K_HASH, K_LIST, K_SET_A, K_SET_B);

    // -----------------------------------------------------------------------------------
    // 1. STRING — the flat value, and the three states it can be in
    // -----------------------------------------------------------------------------------
    // Redis has no "empty" concept distinct from absent at the KEY level, but the client's
    // type does: `optional` distinguishes them, and getting that wrong is how a cache reports
    // a hit on a key it never wrote.
    auto missing = co_await redis.get(K_STR);
    (void) co_await redis.set(K_EMPTY, "");
    auto empty = co_await redis.get(K_EMPTY);

    (void) co_await redis.set(K_STR, "hello");
    auto grew   = co_await redis.append(K_STR, ", world");
    auto length = co_await redis.strlen(K_STR);
    auto value  = co_await redis.get(K_STR);

    const bool three_states = missing.ok() && !missing.result().has_value() && empty.ok() && empty.result().has_value()
                              && empty.result()->empty() && value.ok() && value.result().value_or("") == "hello, world";
    qb::io::cout() << (three_states ? "[string] a missing key is nullopt, an EMPTY key is a string of length 0 — and only\n"
                                      "         the optional tells them apart. APPEND returned the NEW length, not the added\n"
                                    : "[string] UNEXPECTED: absent, empty and present did not come back as three states\n");
    qb::io::cout() << "         (append -> " << grew.result() << " bytes, strlen -> " << length.result() << ", value '"
                   << value.result().value_or("?") << "')\n";

    // A counter is a string. INCRBY parses it on the server, so two clients incrementing the
    // same key never lose an update — the read-modify-write happens where the value lives.
    (void) co_await redis.set(K_COUNT, "41");
    auto bumped = co_await redis.incrby(K_COUNT, 1);
    qb::io::cout() << (bumped.ok() && bumped.result() == 42
                           ? "[string] INCRBY parses the string as an integer server-side: 41 + 1 = 42, with no\n"
                             "         read-modify-write on your side to lose a concurrent update\n"
                           : "[string] UNEXPECTED: INCRBY did not reach 42\n");

    // MSET/MGET: one round trip for N keys. MGET's reply is a vector of OPTIONALS — the holes
    // are where the missing keys were, positionally.
    (void) co_await redis.mset({{"qb:example:dt:m1", "one"}, {"qb:example:dt:m2", "two"}});
    auto many = co_await redis.mget({"qb:example:dt:m1", "qb:example:dt:missing", "qb:example:dt:m2"});
    qb::io::cout() << "[string] MGET of 3 keys in one round trip; the middle one does not exist and comes\n"
                      "         back as a hole in the vector: "
                   << (many.result().size() == 3 && !many.result()[1].has_value() ? "yes" : "no") << "\n\n";
    (void) co_await redis.del("qb:example:dt:m1", "qb:example:dt:m2");

    // -----------------------------------------------------------------------------------
    // 2. HASH — the object, whose fields move independently
    // -----------------------------------------------------------------------------------
    (void) co_await redis.hset(K_HASH, "name", "ada");
    (void) co_await redis.hset(K_HASH, "role", "engineer");
    (void) co_await redis.hset(K_HASH, "logins", "7");
    auto one_more = co_await redis.hincrby(K_HASH, "logins", 1);
    // Spelled out once, because the type IS the lesson: HGETALL does not hand back a list of
    // pairs you have to fold, it hands back the map.
    qb::redis::Reply<qb::unordered_map<std::string, std::string>> whole = co_await redis.hgetall(K_HASH);

    const bool object_ok = whole.ok() && whole.result().size() == 3 && whole.result().at("name") == "ada" && one_more.result() == 8;
    qb::io::cout() << (object_ok ? "[hash] one round trip returned the whole object as a map, and HINCRBY moved ONE field\n"
                                   "       without reading or rewriting the other two — that is the reason to prefer a hash\n"
                                   "       over a JSON blob in a string\n"
                                 : "[hash] UNEXPECTED: the hash did not read back as a 3-field object\n");

    auto no_field  = co_await redis.hget(K_HASH, "nickname");
    auto has_field = co_await redis.hexists(K_HASH, "role");
    auto fields    = co_await redis.hlen(K_HASH);
    qb::io::cout() << (no_field.ok() && !no_field.result().has_value() && has_field.result()
                           ? "[hash] a missing field is nullopt while the hash itself exists — absence has two\n"
                             "       levels here (no key, no field) and HEXISTS answers the second without a copy\n"
                           : "[hash] UNEXPECTED: field presence did not behave as documented\n");
    (void) co_await redis.hdel(K_HASH, "logins");
    qb::io::cout() << "       (fields before HDEL: " << fields.result() << ", after: " << (co_await redis.hlen(K_HASH)).result() << ")\n\n";

    // -----------------------------------------------------------------------------------
    // 3. LIST — the sequence, where position is part of the value
    // -----------------------------------------------------------------------------------
    (void) co_await redis.rpush(K_LIST, "boot", "connect", "ready");
    (void) co_await redis.lpush(K_LIST, "power-on"); // prepends: this is now entry 0
    auto ordered = co_await redis.lrange(K_LIST, 0, -1);

    const bool list_ok =
        ordered.ok() && ordered.result().size() == 4 && ordered.result().front() == "power-on" && ordered.result().back() == "ready";
    qb::io::cout() << (list_ok ? "[list] RPUSH appends, LPUSH prepends, LRANGE reads the ORDER back — a list is a\n"
                                 "       vector because the order is data, and -1 means 'to the end'\n"
                               : "[list] UNEXPECTED: the four entries did not come back in push order\n");

    // A list used as a bounded log: push, then LTRIM to the window you want to keep. Doing it
    // on every write is what stops an event log becoming an outage.
    (void) co_await redis.rpush(K_LIST, "work", "work-again");
    auto trimmed = co_await redis.ltrim(K_LIST, -3, -1);
    auto window  = co_await redis.lrange(K_LIST, 0, -1);
    qb::io::cout() << (trimmed.ok() && window.result().size() == 3
                           ? "[list] LTRIM is how a list stays a bounded log: 6 entries in, newest 3 kept, and the\n"
                             "       trim is O(removed) rather than O(list)\n"
                           : "[list] UNEXPECTED: LTRIM did not leave a 3-entry window\n");

    // Both ends pop. One entry -> optional; a count -> vector. Two different questions, two
    // different reply types, and the count form is what a batching worker wants.
    auto head = co_await redis.lpop(K_LIST);
    auto tail = co_await redis.rpop(K_LIST, 2);
    qb::io::cout() << "       (LPOP one -> '" << head.result().value_or("?") << "', RPOP 2 -> " << tail.result().size()
                   << " entries, list is now " << (co_await redis.llen(K_LIST)).result() << " long)\n\n";

    // -----------------------------------------------------------------------------------
    // 4. SET — membership, and algebra that never crosses the wire
    // -----------------------------------------------------------------------------------
    auto added = co_await redis.sadd(K_SET_A, "c++", "actors", "async", "c++"); // the duplicate is dropped
    (void) co_await redis.sadd(K_SET_B, "async", "sql", "redis");
    auto members = co_await redis.smembers(K_SET_A);

    // `count(...)`, not `contains(...)`: qb::unordered_set is the vendored ska flat hash set and
    // it predates the C++20 member, so `contains` does not compile. Measured, not assumed.
    const bool set_ok =
        added.ok() && added.result() == 3 && members.ok() && members.result().size() == 3 && members.result().count("actors") == 1;
    qb::io::cout() << (set_ok ? "[set] SMEMBERS came back as an unordered_set: no duplicates, no order, membership\n"
                                "      only. SADD returned 3 for 4 arguments because the repeat was not new\n"
                              : "[set] UNEXPECTED: the set did not deduplicate to 3 members\n");

    auto both   = co_await redis.sinter({K_SET_A, K_SET_B});
    auto either = co_await redis.sunion({K_SET_A, K_SET_B});
    auto only_a = co_await redis.sdiff({K_SET_A, K_SET_B});
    auto is_in  = co_await redis.sismember(K_SET_A, "actors");
    auto count  = co_await redis.scard(K_SET_A);

    const bool algebra_ok =
        both.result().size() == 1 && either.result().size() == 5 && only_a.result().size() == 2 && is_in.result() && count.result() == 3;
    qb::io::cout() << (algebra_ok ? "[set] SINTER / SUNION / SDIFF ran INSIDE the server — the answer crossed the wire,\n"
                                    "      not the two sets. SISMEMBER is O(1) and answers without sending any member\n"
                                  : "[set] UNEXPECTED: the set algebra did not produce 1 / 5 / 2\n");
    qb::io::cout() << "      (inter " << both.result().size() << ", union " << either.result().size() << ", diff " << only_a.result().size()
                   << ", card " << count.result() << ")\n";
    (void) co_await redis.srem(K_SET_A, "async");
    qb::io::cout() << "      (after SREM 'async', A and B no longer intersect: "
                   << ((co_await redis.sinter({K_SET_A, K_SET_B})).result().empty() ? "yes" : "no") << ")\n\n";

    // -----------------------------------------------------------------------------------
    // 5. Choosing one — the whole lesson in four lines
    // -----------------------------------------------------------------------------------
    qb::io::cout() << "Which structure answers which question:\n"
                      "  string  one value, or a counter two writers share safely (INCRBY)\n"
                      "  hash    an object whose fields change independently (HINCRBY, HDEL)\n"
                      "  list    a sequence where position matters — a queue, or a bounded log (LTRIM)\n"
                      "  set     'have I seen this?' and set algebra the server does for you\n\n";

    // Cleanup. Not optional, and not only on the happy path — see the header.
    (void) co_await redis.del(K_STR, K_EMPTY, K_COUNT, K_HASH, K_LIST, K_SET_A, K_SET_B);

    ok = three_states && object_ok && list_ok && set_ok && algebra_ok;
    qb::io::cout() << "=== data types complete: string, hash, list and set, each with the Reply type that\n"
                      "    states what it is; every key this program wrote has been deleted ===\n";
    co_return;
}

int
main() {
    qb::io::async::init();

    bool running = true;
    bool ok      = false;
    qb::io::async::coro_scheduler().spawn(run_data_types(running, ok));
    qb::io::async::run_until(running);

    return ok ? 0 : 1;
}
