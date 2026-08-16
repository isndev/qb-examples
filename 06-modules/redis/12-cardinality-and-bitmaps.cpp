/**
 * @file examples/06-modules/redis/12-cardinality-and-bitmaps.cpp
 * @tier 06-modules
 * @teaches Two families for the same question — "how many DISTINCT things?" — and the trade between
 *          them. HyperLogLog answers it in a fixed 12 KB for any cardinality, approximately, and
 *          MERGES without double counting; a bitmap answers it exactly at one bit per id, and
 *          supports server-side set algebra (AND/OR/XOR/NOT) you would otherwise write a job for.
 * @demonstrates qb::redis::tcp::client, pfadd, pfcount, pfmerge,
 *               setbit, getbit, bitcount, bitop, bitpos, bitfield,
 *               strlen, memory_usage, sadd, scard, del, exists,
 *               qb::redis::Reply<T>, ok, result, error,
 *               qb::io::async::init, qb::io::async::run_until, qb::io::async::coro_scheduler,
 *               qb::io::async::task<void>
 * @prerequisites 06-modules/redis/02-data-types
 * @expect "Connected to Redis successfully!"
 * @expect "[hll] 5000 distinct visitors counted with an error of "
 * @expect "[hll] and that is the whole trade: the HyperLogLog is "
 * @expect "[merge] PFMERGE unioned three days into one week WITHOUT double-counting the overlap:"
 * @expect "[merge] you cannot do that with counters — adding three daily totals counts every"
 * @expect "[bits] a bitmap is EXACT at one bit per id: 6 of 4096 ids set, BITCOUNT says "
 * @expect "[bitop] BITOP ran set algebra INSIDE the server: AND = retained users, OR = reach,"
 * @expect "[bitpos] BITPOS finds the first 0 or 1 without reading the string back — the O(1)-ish"
 * @expect "[bitfield] BITFIELD packs SEVERAL counters into one string at sub-byte widths, and"
 * @expect "[choose] pick by the question, not by taste: HLL when you cannot enumerate the ids and"
 * @expect "=== cardinality and bitmaps complete: every key is deleted, on this path and on the"
 *
 * THE PROBLEM BOTH OF THESE SOLVE
 * -------------------------------
 * "How many unique visitors did we have?" is a set-cardinality question, and the obvious answer —
 * keep the set — costs memory proportional to the answer. Measured below, on this machine, for
 * 5000 ids: a Redis SET holding them costs ~247 KB. That is fine at 5000 and ruinous at 50 million,
 * and 50 million is exactly where somebody asks the question.
 *
 * HYPERLOGLOG: CONSTANT SPACE, APPROXIMATE ANSWER
 * -----------------------------------------------
 * A HyperLogLog is a fixed array of registers holding the longest run of leading zeros seen in the
 * hash of anything added to it. It never stores an element, so it cannot tell you WHICH ids it saw
 * — only roughly how many distinct ones there were, with a standard error of about 0.81%. Its size
 * is bounded at 12 KB no matter what you feed it. In Redis it is stored as an ordinary string, so
 * `STRLEN` reports its real byte size — which is how this program measures the claim instead of
 * repeating it. (Small cardinalities use a SPARSE encoding and are much smaller than 12 KB; the
 * conversion to the dense form is automatic and invisible.)
 *
 * PFMERGE IS THE FEATURE, NOT PFCOUNT
 * -----------------------------------
 * Any counter can count. What no counter can do is be COMBINED: three daily unique-visitor totals
 * added together count everyone who came on two days twice, and there is no correction for that
 * because the totals threw the identities away. `PFMERGE` unions the underlying registers, so a
 * weekly figure built from seven daily HLLs is a real distinct count. That is why you keep a
 * per-day HLL rather than a per-day integer.
 *
 * BITMAPS: EXACT, IF YOUR IDS ARE DENSE INTEGERS
 * ----------------------------------------------
 * A bitmap is a plain string used as an addressable array of bits. `SETBIT key 42 1` means "user
 * 42 was active"; `BITCOUNT` counts them. One million users is 125 KB and the answer is exact.
 * The precondition is the catch: the offset IS the id, so ids must be small dense integers. Setting
 * bit 4 billion allocates 500 MB.
 *
 * The payoff for accepting it is `BITOP`: AND, OR, XOR and NOT between whole bitmaps, executed
 * server-side, in one command. "Users active on Monday AND Tuesday" is retention. "Monday OR
 * Tuesday" is reach. Neither leaves the server, and neither needs a job.
 *
 * Every key this program writes is under `qb:example:card:` and is deleted on the way out, on the
 * failure path as well as the success one.
 *
 * Build:
 *   cmake --preset release
 *   cmake --build --preset release --target qb-example-modules-redis-cardinality-and-bitmaps
 * Run (needs a Redis on 127.0.0.1:6379):
 *   ./build/presets/release/examples/06-modules/redis/qb-example-modules-redis-cardinality-and-bitmaps
 */

#include <cmath>
#include <cstdint>
#include <string>
#include <vector>
#include <qb/io/async.h>
#include <qb/io/async/coroutine.h>
#include <qbm/redis/redis.h>

namespace {

#define REDIS_URI {"tcp://localhost:6379"}

constexpr const char *K_HLL_ALL = "qb:example:card:hll:all";
constexpr const char *K_SET_ALL = "qb:example:card:set:all";
constexpr const char *K_DAY1    = "qb:example:card:hll:mon";
constexpr const char *K_DAY2    = "qb:example:card:hll:tue";
constexpr const char *K_DAY3    = "qb:example:card:hll:wed";
constexpr const char *K_WEEK    = "qb:example:card:hll:week";
constexpr const char *K_BITS_A  = "qb:example:card:bits:mon";
constexpr const char *K_BITS_B  = "qb:example:card:bits:tue";
constexpr const char *K_BOTH    = "qb:example:card:bits:retained";
constexpr const char *K_EITHER  = "qb:example:card:bits:reach";
constexpr const char *K_SLOTS   = "qb:example:card:bits:slots";
constexpr const char *K_FIELDS  = "qb:example:card:bitfield";

constexpr int VISITORS = 5000; // distinct ids fed to both the HLL and the exact set
constexpr int BATCH    = 500;  // ids per PFADD/SADD — variadic, so this is one command each

// Every key the program touches, in one place, so the two cleanup calls cannot drift apart.
qb::io::async::task<long long>
purge(qb::redis::tcp::client &redis) {
    auto r = co_await redis.del(K_HLL_ALL, K_SET_ALL, K_DAY1, K_DAY2, K_DAY3, K_WEEK, K_BITS_A, K_BITS_B, K_BOTH, K_EITHER, K_SLOTS, K_FIELDS);
    co_return r.ok() ? r.result() : -1;
}

} // namespace

qb::io::async::task<void>
run_cardinality(bool &running, bool &ok) {
    struct StopOnExit {
        bool &r;
        ~StopOnExit() {
            r = false;
        }
    } stop{running};

    qb::redis::tcp::client redis{REDIS_URI};
    if (!co_await redis.connect()) {
        qb::io::cerr() << "Failed to connect to Redis at tcp://localhost:6379" << std::endl;
        co_return;
    }
    qb::io::cout() << "Connected to Redis successfully!\n\n";

    // A previous run that died mid-way must not change what this one measures.
    (void) co_await purge(redis);

    // -----------------------------------------------------------------------------------
    // 1. HYPERLOGLOG — the same 5000 ids into an HLL and into an exact set, then compare
    //    the answers AND the bytes.
    // -----------------------------------------------------------------------------------
    for (int base = 0; base < VISITORS; base += BATCH) {
        std::vector<std::string> ids;
        ids.reserve(BATCH);
        for (int i = base; i < base + BATCH; ++i)
            ids.push_back("visitor:" + std::to_string(i));

        // Both commands are variadic, so a batch of 500 is ONE round trip, not 500.
        auto added = co_await redis.pfadd(K_HLL_ALL, ids);
        auto exact = co_await redis.sadd(K_SET_ALL, ids);
        if (!added.ok() || !exact.ok()) {
            qb::io::cerr() << "[hll] UNEXPECTED: bulk insert failed: " << added.error() << exact.error() << "\n";
            (void) co_await purge(redis);
            co_return;
        }
    }

    // Spelled out once rather than `auto`, because the TYPE is the thing to notice: PFCOUNT is a
    // cardinality ESTIMATE and comes back as a plain integer, not as anything approximate-looking.
    qb::redis::Reply<long long> approx    = co_await redis.pfcount(K_HLL_ALL);
    auto                        truth     = co_await redis.scard(K_SET_ALL);
    auto                        hll_bytes = co_await redis.strlen(K_HLL_ALL); // an HLL IS a string, so STRLEN is its size
    auto                        set_bytes = co_await redis.memory_usage(K_SET_ALL);

    const long long estimated = approx.ok() ? approx.result() : -1;
    const long long counted   = truth.ok() ? truth.result() : -1;
    const double    error_pct = counted > 0 ? 100.0 * std::abs(static_cast<double>(estimated - counted)) / static_cast<double>(counted) : -1.0;
    const bool      hll_ok    = approx.ok() && truth.ok() && counted == VISITORS && error_pct >= 0.0 && error_pct < 5.0;

    qb::io::cout() << "[hll] 5000 distinct visitors counted with an error of " << error_pct
                   << "% — a HyperLogLog\n"
                      "      never stores an element, so it can tell you HOW MANY and never WHICH. The standard\n"
                      "      error is ~0.81%, which is a property of the structure and not of the data\n";
    qb::io::cout() << "      (PFCOUNT " << estimated << " against an exact SCARD of " << counted << ")\n";
    if (!hll_ok)
        qb::io::cerr() << "      UNEXPECTED: the estimate is not within 5% of the truth\n";

    const long long hll_size = hll_bytes.ok() ? hll_bytes.result() : -1;
    const long long set_size = set_bytes.ok() ? set_bytes.result() : -1;
    qb::io::cout() << "[hll] and that is the whole trade: the HyperLogLog is " << hll_size
                   << " bytes and stops growing\n"
                      "      there for ANY cardinality, while the exact set holding the same ids costs "
                   << set_size
                   << "\n"
                      "      and grows for ever. At 5000 ids that ratio is already the argument; at 50 million it\n"
                      "      is the difference between a key and an outage\n\n";

    // -----------------------------------------------------------------------------------
    // 2. PFMERGE — the reason to keep a per-day HLL instead of a per-day integer.
    // -----------------------------------------------------------------------------------
    // Three overlapping days. Monday 0-999, Tuesday 500-1499, Wednesday 1000-1999.
    // The true union is 0-1999, i.e. 2000 people; the SUM of the three daily counts is 3000.
    const struct {
        const char *key;
        int         from;
        int         to;
    } days[] = {{K_DAY1, 0, 1000}, {K_DAY2, 500, 1500}, {K_DAY3, 1000, 2000}};

    long long naive_sum = 0;
    for (auto const &day : days) {
        std::vector<std::string> ids;
        ids.reserve(static_cast<std::size_t>(day.to - day.from));
        for (int i = day.from; i < day.to; ++i)
            ids.push_back("visitor:" + std::to_string(i));
        (void) co_await redis.pfadd(std::string(day.key), ids);
        auto n = co_await redis.pfcount(std::string(day.key));
        naive_sum += n.ok() ? n.result() : 0;
    }

    auto merged = co_await redis.pfmerge(K_WEEK, K_DAY1, K_DAY2, K_DAY3);
    auto weekly = co_await redis.pfcount(K_WEEK);

    const long long week_count = weekly.ok() ? weekly.result() : -1;
    const bool      merge_ok   = merged.ok() && weekly.ok() && week_count > 1800 && week_count < 2200;

    qb::io::cout() << "[merge] PFMERGE unioned three days into one week WITHOUT double-counting the overlap:\n"
                      "        Mon 0-999, Tue 500-1499, Wed 1000-1999. The true union is 2000 people\n";
    qb::io::cout() << "        (weekly PFCOUNT " << week_count << ", and the naive sum of the three daily counts is " << naive_sum << ")\n";
    qb::io::cout() << "[merge] you cannot do that with counters — adding three daily totals counts every\n"
                      "        returning visitor once per day they came, and nothing in the totals can undo it\n"
                      "        because they threw the identities away. The HLL kept the registers, not the ids\n\n";
    if (!merge_ok)
        qb::io::cerr() << "        UNEXPECTED: the merged count is not near the true union\n";

    // -----------------------------------------------------------------------------------
    // 3. BITMAPS — exact, one bit per id.
    // -----------------------------------------------------------------------------------
    // Monday: users 1, 5, 9, 4090, 4095, 100.  Tuesday: 5, 9, 200, 4095.
    const std::vector<long long> monday{1, 5, 9, 100, 4090, 4095};
    const std::vector<long long> tuesday{5, 9, 200, 4095};
    for (auto id : monday)
        (void) co_await redis.setbit(K_BITS_A, id, true);
    for (auto id : tuesday)
        (void) co_await redis.setbit(K_BITS_B, id, true);

    auto mon_count = co_await redis.bitcount(K_BITS_A);
    auto bit_len   = co_await redis.strlen(K_BITS_A);
    auto was_set   = co_await redis.getbit(K_BITS_A, 9);
    auto was_clear = co_await redis.getbit(K_BITS_A, 10);

    const bool bits_ok = mon_count.ok() && mon_count.result() == static_cast<long long>(monday.size()) && was_set.ok() && was_set.result() == 1
                         && was_clear.ok() && was_clear.result() == 0;

    qb::io::cout() << "[bits] a bitmap is EXACT at one bit per id: 6 of 4096 ids set, BITCOUNT says "
                   << (mon_count.ok() ? mon_count.result() : -1)
                   << ", and\n"
                      "       the whole day costs "
                   << (bit_len.ok() ? bit_len.result() : -1)
                   << " bytes because the HIGHEST offset decides the length. That is the\n"
                      "       precondition people miss: the offset IS the id, so ids must be dense integers —\n"
                      "       SETBIT at 4,000,000,000 allocates 500 MB for one user\n";
    qb::io::cout() << "       (GETBIT 9 = " << (was_set.ok() ? was_set.result() : -1)
                   << ", GETBIT 10 = " << (was_clear.ok() ? was_clear.result() : -1)
                   << " — absence and presence, no allocation either way)\n\n";

    // -----------------------------------------------------------------------------------
    // 4. BITOP — set algebra, server-side.
    // -----------------------------------------------------------------------------------
    auto and_len  = co_await redis.bitop("AND", K_BOTH, std::vector<std::string>{K_BITS_A, K_BITS_B});
    auto or_len   = co_await redis.bitop("OR", K_EITHER, std::vector<std::string>{K_BITS_A, K_BITS_B});
    auto retained = co_await redis.bitcount(K_BOTH);
    auto reach    = co_await redis.bitcount(K_EITHER);

    const bool bitop_ok = and_len.ok() && or_len.ok() && retained.ok() && retained.result() == 3 && reach.ok() && reach.result() == 7;

    qb::io::cout() << "[bitop] BITOP ran set algebra INSIDE the server: AND = retained users, OR = reach,\n"
                      "        and the result is another bitmap you can keep, BITCOUNT, or feed to the next\n"
                      "        BITOP. Nothing crossed the wire except the two key names\n";
    qb::io::cout() << "        (retained Mon AND Tue = " << (retained.ok() ? retained.result() : -1)
                   << " users {5, 9, 4095}, reach Mon OR Tue = " << (reach.ok() ? reach.result() : -1) << ")\n\n";

    // -----------------------------------------------------------------------------------
    // 5. BITPOS — the allocator's question.
    // -----------------------------------------------------------------------------------
    // A slot table: bits 0..4 taken, 5 free. "Which is the first free slot?" is one command.
    for (long long i = 0; i < 5; ++i)
        (void) co_await redis.setbit(K_SLOTS, i, true);
    auto first_free = co_await redis.bitpos(K_SLOTS, false);
    auto first_used = co_await redis.bitpos(K_SLOTS, true);

    const bool pos_ok = first_free.ok() && first_free.result() == 5 && first_used.ok() && first_used.result() == 0;

    qb::io::cout() << "[bitpos] BITPOS finds the first 0 or 1 without reading the string back — the O(1)-ish\n"
                      "         answer to 'which slot is free?' and to 'where does the used range start?'\n";
    qb::io::cout() << "         (first free bit = " << (first_free.ok() ? first_free.result() : -1)
                   << ", first used bit = " << (first_used.ok() ? first_used.result() : -1) << ")\n\n";

    // -----------------------------------------------------------------------------------
    // 6. BITFIELD — several counters in one string, at widths you choose.
    // -----------------------------------------------------------------------------------
    // Two unsigned 8-bit counters at positions #0 and #1 of the same key. OVERFLOW SAT clamps
    // instead of wrapping, which is what a saturating rate counter wants.
    auto fields =
        co_await redis.bitfield(K_FIELDS, std::vector<std::string>{"INCRBY", "u8", "#0", "10", "INCRBY", "u8", "#1", "3", "GET", "u8", "#0"});
    auto saturated = co_await redis.bitfield(K_FIELDS, std::vector<std::string>{"OVERFLOW", "SAT", "INCRBY", "u8", "#0", "250"});
    auto packed    = co_await redis.strlen(K_FIELDS);

    const bool field_ok = fields.ok() && fields.result().size() == 3 && fields.result()[2].value_or(-1) == 10 && saturated.ok()
                          && saturated.result().size() == 1 && saturated.result()[0].value_or(-1) == 255 && packed.ok() && packed.result() == 2;

    qb::io::cout() << "[bitfield] BITFIELD packs SEVERAL counters into one string at sub-byte widths, and\n"
                      "           OVERFLOW SAT clamps rather than wraps — which is the behaviour a rate counter\n"
                      "           wants and the behaviour plain INCR cannot give you\n";
    qb::io::cout() << "           (two u8 counters in " << (packed.ok() ? packed.result() : -1) << " bytes: #0 read back as "
                   << fields.result()[2].value_or(-1) << ", then +250 SATURATED at " << saturated.result()[0].value_or(-1)
                   << " instead of wrapping to 4)\n\n";

    qb::io::cout() << "[choose] pick by the question, not by taste: HLL when you cannot enumerate the ids and\n"
                      "         a fraction of a percent is fine; a bitmap when the ids are dense integers and you\n"
                      "         need an exact answer or set algebra; and the plain SET only when you will actually\n"
                      "         ask WHICH ones — that is the one question neither of the other two can answer\n\n";

    // ---- cleanup ----------------------------------------------------------------------
    const long long removed = co_await purge(redis);
    auto            gone    = co_await redis.exists(K_HLL_ALL, K_SET_ALL, K_WEEK, K_BITS_A, K_FIELDS);

    ok = hll_ok && merge_ok && bits_ok && bitop_ok && pos_ok && field_ok && removed == 12 && gone.ok() && gone.result() == 0;

    qb::io::cout() << "=== cardinality and bitmaps complete: every key is deleted, on this path and on the\n"
                      "    failure path above ("
                   << removed << " removed, " << gone.result() << " of the probed keys still present) ===\n";
    co_return;
}

int
main() {
    qb::io::async::init();

    bool running = true;
    bool ok      = false;
    qb::io::async::coro_scheduler().spawn(run_cardinality(running, ok));
    qb::io::async::run_until(running);

    return ok ? 0 : 1;
}
