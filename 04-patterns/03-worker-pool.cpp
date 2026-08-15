/**
 * @file examples/04-patterns/03-worker-pool.cpp
 * @tier 04-patterns
 * @teaches The two routing decisions a pool of workers ever has to make, and the one line each
 *          costs: `next()` when any worker will do, `for_key(k)` when the same key must keep
 *          reaching the same worker — plus the caveat that makes the second one honest.
 * @demonstrates qb::WorkerPool, next, for_key, add, remove, size, empty, workers,
 *               registerEvent<E>, push<E>, qb::Main, addActor<T>
 * @prerequisites 01-actors/02-messaging, 01-actors/04-cores-and-placement
 * @expect "[dispatch] round-robin: "
 * @expect "[dispatch] round-robin spread: every worker got "
 * @expect "[dispatch] sticky: each of the 3 users landed on exactly ONE worker"
 * @expect "[dispatch] after remove(), users whose worker changed: "
 * @expect "=== worker pool complete: two routing policies, no balancer written by hand ==="
 *
 * WHAT THIS REPLACES
 * ------------------
 * The load balancer inside the pre-3.0 `example10_distributed_computing.cpp` (1320 lines, since
 * retired), which the example audit measured assigning **half the fleet no work at all**. A round-robin
 * cursor is four lines and it is very easy to write four wrong lines; `qb::WorkerPool` is the
 * four right ones, already tested.
 *
 * WHAT `WorkerPool` IS, AND — MORE USEFULLY — WHAT IT IS NOT
 * ---------------------------------------------------------
 * It is a `std::vector<qb::ActorId>` plus a cursor. It is not an actor, it holds no queue, it
 * sends nothing: YOU push, it only says where. That is why it can live as a member of any actor
 * and cost nothing. It does not own its workers and it does not track their liveness either —
 * a worker that dies stays in the pool and `next()` will keep handing out its id. Pair it with
 * `04-patterns/09-discovery` (to find workers) or `04-patterns/02-supervisor` (to keep them
 * alive) if your fleet changes; nothing in this header will notice on its own.
 *
 * THE ONE CAVEAT WORTH PRINTING
 * -----------------------------
 * `for_key(k)` is `workers[k % size()]`. It is stable for as long as `size()` is — and the
 * moment you `add()` or `remove()` a worker, most keys move. The final section measures exactly
 * that rather than describing it: it prints how many of three users changed worker when one
 * worker left. If your keys carry state on the worker (a session, a cache, a partition), that
 * number is your migration cost, and a consistent-hash ring is what you would reach for instead.
 *
 * Build:
 *   cmake --preset release
 *   cmake --build --preset release --target qb-example-patterns-worker-pool
 * Run:
 *   ./build/presets/release/examples/04-patterns/qb-example-patterns-worker-pool
 */

#include <cstdint>
#include <vector>
#include <qb/actor.h>
#include <qb/io.h>
#include <qb/main.h>
#include <qb/patterns.h>

constexpr int kWorkers = 4;
constexpr int kJobs    = 12; // 12 / 4 == 3 each, exactly
constexpr int kUsers   = 3;
constexpr int kPerUser = 3;

// One unit of work. `user_slot` is -1 for the round-robin half and 0..kUsers-1 for the sticky
// half, so a worker can report the two halves apart without a second event type.
struct Job : public qb::Event {
    int n;
    int user_slot;
    Job(int v, int u)
        : n(v)
        , user_slot(u) {}
};

// Dispatcher -> worker: report what you received. Pushed AFTER every job, and a source->
// destination pipe is ordered, so a worker cannot answer before its jobs have arrived.
struct Census : public qb::Event {};

// Worker -> dispatcher: the counts. Three ints, no allocation, trivially relocatable.
struct CensusReply : public qb::Event {
    int index;
    int round_robin;
    int per_user[kUsers];
    CensusReply(int i, int rr, int const (&u)[kUsers])
        : index(i)
        , round_robin(rr)
        , per_user{u[0], u[1], u[2]} {}
};

// ---------------------------------------------------------------------------
// A worker. It knows nothing about the pool — routing is entirely the sender's business.
// ---------------------------------------------------------------------------
class Worker : public qb::Actor {
    const int _index;
    int       _round_robin      = 0;
    int       _per_user[kUsers] = {0, 0, 0};

public:
    explicit Worker(int index)
        : _index(index) {}

    qb::io::async::task<bool>
    onInit() override {
        registerEvent<Job>(*this);
        registerEvent<Census>(*this);
        co_return true;
    }

    void
    on(Job const &j) {
        if (j.user_slot < 0)
            ++_round_robin;
        else
            ++_per_user[j.user_slot];
    }

    void
    on(Census const &c) {
        push<CensusReply>(c.getSource(), _index, _round_robin, _per_user);
    }
};

// ---------------------------------------------------------------------------
// The dispatcher owns the pool. Both routing policies are one call each.
// ---------------------------------------------------------------------------
class Dispatcher : public qb::Actor {
    std::vector<qb::ActorId> _worker_ids;
    qb::WorkerPool           _pool;
    int                      _replies = 0;
    int                      _rr[kWorkers]{};
    int                      _sticky[kWorkers][kUsers]{};

    // The three keys routed stickily. Real ones would be user ids, session ids, shard keys.
    static constexpr std::uint64_t kKeys[kUsers] = {1001, 4242, 777};

public:
    explicit Dispatcher(std::vector<qb::ActorId> workers)
        : _worker_ids(std::move(workers))
        , _pool(_worker_ids) {}

    qb::io::async::task<bool>
    onInit() override {
        registerEvent<CensusReply>(*this);

        if (_pool.empty()) // next()/for_key() have a non-empty precondition; say so, do not assert
            co_return false;

        // ---- policy 1: any worker will do -------------------------------------------------
        qb::io::cout() << "[dispatch] round-robin: " << kJobs << " jobs over " << _pool.size() << " workers\n";
        for (int i = 0; i < kJobs; ++i)
            push<Job>(_pool.next(), i, -1);

        // ---- policy 2: this key must keep reaching the same worker -------------------------
        for (int u = 0; u < kUsers; ++u)
            for (int r = 0; r < kPerUser; ++r)
                push<Job>(_pool.for_key(kKeys[u]), r, u);

        // The census rides the same ordered pipes as the jobs, so no delay is needed anywhere.
        for (auto const id : _pool.workers())
            push<Census>(id);
        co_return true;
    }

    void
    on(CensusReply const &r) {
        _rr[r.index] = r.round_robin;
        for (int u = 0; u < kUsers; ++u)
            _sticky[r.index][u] = r.per_user[u];
        if (++_replies < kWorkers)
            return;
        report();
        remap_after_removal();
        qb::io::cout() << "=== worker pool complete: two routing policies, no balancer written by hand ===\n";
        qb::Main::stop();
    }

private:
    void
    report() {
        int lo = _rr[0], hi = _rr[0];
        for (int i = 0; i < kWorkers; ++i) {
            qb::io::cout() << "[worker " << i << "] round-robin jobs: " << _rr[i] << ", sticky jobs: " << _sticky[i][0] << "/" << _sticky[i][1]
                           << "/" << _sticky[i][2] << "\n";
            lo = _rr[i] < lo ? _rr[i] : lo;
            hi = _rr[i] > hi ? _rr[i] : hi;
        }
        if (lo == hi)
            qb::io::cout() << "[dispatch] round-robin spread: every worker got " << lo << " — the fleet is used evenly\n";
        else
            qb::io::cout() << "[dispatch] round-robin spread: " << lo << " to " << hi
                           << " — UNEVEN, which is the defect this pattern exists to prevent\n";

        // Stickiness, measured: a user whose requests were split would show a non-zero count on
        // two workers. Anything but "exactly one worker per user" is a failure of the policy.
        int split = 0;
        for (int u = 0; u < kUsers; ++u) {
            int touched = 0;
            for (int i = 0; i < kWorkers; ++i)
                touched += _sticky[i][u] > 0 ? 1 : 0;
            split += touched == 1 ? 0 : 1;
        }
        if (split == 0) {
            // A CONDITIONAL print is a real oracle: if stickiness had failed, this line would
            // never appear and `dev/agent/run-examples.py` would report the @expect as a dead path.
            qb::io::cout() << "[dispatch] sticky: each of the 3 users landed on exactly ONE worker\n";
            qb::io::cout() << "[dispatch] note two users share worker 1 and two workers got none: "
                              "for_key is k % size(), so keys collide. Expected, and NOT the "
                              "round-robin defect above\n";
        } else
            qb::io::cout() << "[dispatch] sticky: " << split << " user(s) were SPLIT across workers — for_key did not hold\n";
    }

    // The caveat, measured rather than described.
    void
    remap_after_removal() {
        qb::ActorId before[kUsers];
        for (int u = 0; u < kUsers; ++u)
            before[u] = _pool.for_key(kKeys[u]);

        _pool.remove(_worker_ids.front()); // one worker leaves the fleet
        int moved = 0;
        for (int u = 0; u < kUsers; ++u)
            moved += _pool.for_key(kKeys[u]) == before[u] ? 0 : 1;
        qb::io::cout() << "[dispatch] after remove(), users whose worker changed: " << moved << " of " << kUsers << " (pool size "
                       << _pool.size() << ") — for_key is sticky only while the pool size holds\n";

        _pool.add(_worker_ids.front()); // and back again: add()/remove() are the whole lifecycle
    }
};

int
main() {
    qb::Main engine;

    std::vector<qb::ActorId> workers;
    for (int i = 0; i < kWorkers; ++i)
        workers.push_back(engine.addActor<Worker>(0, i));
    engine.addActor<Dispatcher>(0, workers);

    qb::io::cout() << "[main] one pool, four workers, two routing policies\n";

    engine.start();
    engine.join();
    return engine.hasError() ? 1 : 0;
}
