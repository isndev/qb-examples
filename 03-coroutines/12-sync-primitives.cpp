/**
 * @file examples/03-coroutines/12-sync-primitives.cpp
 * @tier 03-coroutines
 * @teaches That single-threaded does NOT mean synchronisation-free: every `co_await` is a place
 *          where another coroutine runs, so an invariant that spans one is exactly as broken as
 *          an invariant that spans a thread switch. The program produces a real lost update
 *          first, then fixes it, then covers the five other primitives that exist for the same
 *          reason.
 * @demonstrates qb::io::async::async_mutex, qb::io::async::semaphore,
 *               qb::io::async::async_rw_lock, qb::io::async::barrier,
 *               qb::io::async::async_event, qb::io::async::async_latch,
 *               qb::io::async::with_lock, qb::io::async::with_semaphore, scoped_lock,
 *               scoped_acquire, scoped_read_lock, scoped_write_lock, try_lock, try_acquire,
 *               is_locked, waiters_count, available_permits, total_permits, arrive_and_wait,
 *               count_down, current_count, is_ready, qb::io::async::coroutine_scope,
 *               qb::io::async::task<void>, qb::io::async::sleep, qb::io::async::run_sync
 * @prerequisites 03-coroutines/07-structured-concurrency
 * @expect "[race] five unsynchronised transfers of +10 left the balance at 10"
 * @expect "[async_mutex] the same five transfers under a lock left it at 50"
 * @expect "[async_mutex] try_lock/is_locked/waiters_count answer without suspending"
 * @expect "[semaphore] 5 workers, 2 permits: peak in flight was 2"
 * @expect "[semaphore] with_semaphore and with_lock take a SYNCHRONOUS callable"
 * @expect "[async_rw_lock] 3 readers held it at once; the writer waited"
 * @expect "[barrier] every arrival happened before any departure"
 * @expect "[async_event] manual-reset woke all 3 waiters and stayed set"
 * @expect "[async_event] auto-reset woke exactly 1 of 3"
 * @expect "[async_latch] count_down(3) released the waiters at zero"
 * @expect "=== sync primitives complete: 10 without a lock, 50 with one ==="
 *
 * THE MISCONCEPTION THIS FILE EXISTS TO KILL
 * ------------------------------------------
 * "These coroutines all run on one thread, so I do not need a mutex." Half right, and the
 * wrong half is expensive. There is no PREEMPTION here, so a stretch of code with no `co_await`
 * in it really is atomic. But every `co_await` is a scheduling point: your frame parks, another
 * frame runs, and it may be another instance of the very function you are in. So:
 *
 *     int current = account.balance;   // read
 *     co_await charge_the_card();      // <-- four other transfers run to completion here
 *     account.balance = current + n;   // write, based on a value that is now historical
 *
 * is a lost update, on one thread, with no data race and nothing for a thread sanitizer to
 * find. §1 runs exactly that and prints the wrong answer; §2 puts an `async_mutex` around it
 * and prints the right one. Both numbers gate the summary.
 *
 * WHY THESE ARE NOT std::mutex / std::counting_semaphore
 * ------------------------------------------------------
 * Blocking the thread is the one thing an event loop must never do: it would stop every OTHER
 * coroutine, every timer and every socket on this core. So each primitive below parks the
 * COROUTINE — `co_await mtx.lock()` suspends the frame and the loop keeps turning. That is
 * also why every one of them is non-copyable and single-thread: they coordinate frames on one
 * loop, never threads. To coordinate across cores, send an actor event.
 *
 * WHICH ONE TO REACH FOR
 * ----------------------
 *   async_mutex     one holder at a time. The default answer for "this invariant spans an await".
 *   semaphore       at most N holders. A connection pool, a rate limit, a queue depth.
 *   async_rw_lock   many readers OR one writer. Worth it only when reads dominate.
 *   barrier         N frames must all reach a point before any continues. REUSABLE via reset().
 *   async_event     a flag frames can wait on. Manual-reset latches; auto-reset wakes one.
 *   async_latch     a one-shot countdown. It cannot be reset — that is the difference from
 *                   barrier, and it is the reason to prefer it for "wait for startup".
 *
 * Build:
 *   cmake --preset release
 *   cmake --build --preset release --target qb-example-coroutines-sync-primitives
 * Run:
 *   ./build/presets/release/examples/03-coroutines/qb-example-coroutines-sync-primitives
 */

#include <chrono>
#include <memory>
#include <string>
#include <vector>
#include <qb/io.h>
#include <qb/io/async/coroutine.h>

using namespace qb::io::async;
using namespace std::chrono_literals;

// The two balances the summary is gated on: the same five transfers, without and with a lock.
struct Balances {
    int unsynchronised = -1;
    int locked         = -1;
};
using Sheet = std::shared_ptr<Balances>;

// The shared mutable state. On ONE thread — which is exactly the point.
struct Account {
    int balance = 0;
};

// ---------------------------------------------------------------------------------------
// 1. The defect, produced rather than described.
// ---------------------------------------------------------------------------------------
task<void>
unsynchronised_transfer(std::shared_ptr<Account> acct, int amount) {
    const int current = acct->balance; // READ
    co_await sleep(10ms);              // every other transfer runs to completion right here
    acct->balance = current + amount;  // WRITE, using a value that is now stale
}

task<void>
locked_transfer(std::shared_ptr<Account> acct, int amount, async_mutex &mtx) {
    // `scoped_lock()` is a task<guard>; the guard releases in its destructor, so the critical
    // section is the rest of this frame. It is held ACROSS the await — which is the whole
    // reason a coroutine-aware mutex has to exist.
    auto guard = co_await mtx.scoped_lock();

    const int current = acct->balance;
    co_await sleep(10ms);
    acct->balance = current + amount;
}

task<void>
demo_race(Sheet sheet) {
    qb::io::cout() << "-- 1. the defect: an invariant that spans a co_await\n";

    {
        auto            acct = std::make_shared<Account>();
        coroutine_scope scope;
        for (int i = 0; i < 5; ++i)
            scope.spawn(unsynchronised_transfer(acct, 10));
        co_await scope.join_all();

        sheet->unsynchronised = acct->balance;
        if (acct->balance == 10)
            qb::io::cout() << "[race] five unsynchronised transfers of +10 left the balance at 10, not 50 — "
                              "all five read 0 before any of them wrote, on one thread, with no data race\n";
        else
            qb::io::cout() << "[race] UNEXPECTED: the unsynchronised balance is " << acct->balance << "\n";
    }

    qb::io::cout() << "\n-- 2. async_mutex: the same code, one line different\n";

    {
        auto            acct = std::make_shared<Account>();
        async_mutex     mtx;
        coroutine_scope scope;
        for (int i = 0; i < 5; ++i)
            scope.spawn(locked_transfer(acct, 10, mtx));
        co_await scope.join_all();

        sheet->locked = acct->balance;
        if (acct->balance == 50)
            qb::io::cout() << "[async_mutex] the same five transfers under a lock left it at 50 — the lock does not "
                              "make the code faster, it makes the answer correct\n";
        else
            qb::io::cout() << "[async_mutex] UNEXPECTED: the locked balance is " << acct->balance << "\n";
    }

    // The non-suspending queries. `try_lock` is the one to reach for when "I cannot get it now"
    // is a legitimate answer rather than a reason to park.
    {
        async_mutex mtx;
        const bool  first  = mtx.try_lock();
        const bool  second = mtx.try_lock();
        if (first && !second && mtx.is_locked() && mtx.waiters_count() == 0)
            qb::io::cout() << "[async_mutex] try_lock/is_locked/waiters_count answer without suspending: the second "
                              "try_lock returned false and nothing parked\n";
        else
            qb::io::cout() << "[async_mutex] UNEXPECTED: " << first << second << mtx.is_locked() << mtx.waiters_count() << "\n";
        mtx.unlock();
    }

    // `with_lock(mtx, f)` is the one-expression form — but read its signature: `f` is a plain
    // callable returning a value, NOT a task. The critical section it wraps therefore cannot
    // await anything. When it must, write `co_await mtx.scoped_lock()` as above.
    {
        async_mutex mtx;
        const int   doubled = co_await with_lock(mtx, [] { return 21 * 2; });
        qb::io::cout() << "    with_lock(mtx, f) ran a synchronous critical section and returned " << doubled << "\n\n";
    }
}

// ---------------------------------------------------------------------------------------
// 3. semaphore — at most N holders.
// ---------------------------------------------------------------------------------------
struct Gauge {
    int in_flight = 0;
    int peak      = 0;
};

task<void>
pooled_call(std::shared_ptr<semaphore> pool, std::shared_ptr<Gauge> gauge) {
    // `scoped_acquire()` mirrors `scoped_lock()`: a task<guard> whose destructor releases.
    auto permit = co_await pool->scoped_acquire();
    if (++gauge->in_flight > gauge->peak)
        gauge->peak = gauge->in_flight;
    co_await sleep(20ms);
    --gauge->in_flight;
}

task<void>
demo_semaphore() {
    qb::io::cout() << "-- 3. semaphore: a pool with N slots\n";

    auto pool  = std::make_shared<semaphore>(2);
    auto gauge = std::make_shared<Gauge>();

    qb::io::cout() << "    before: available_permits()=" << pool->available_permits() << " of total_permits()=" << pool->total_permits()
                   << "\n";

    {
        coroutine_scope scope;
        for (int i = 0; i < 5; ++i)
            scope.spawn(pooled_call(pool, gauge));
        co_await scope.join_all();
    }

    if (gauge->peak == 2)
        qb::io::cout() << "[semaphore] 5 workers, 2 permits: peak in flight was 2 — the other three parked on "
                          "acquire() instead of opening a third connection\n";
    else
        qb::io::cout() << "[semaphore] UNEXPECTED: peak in flight was " << gauge->peak << "\n";

    // try_acquire is the non-suspending sibling; it fails rather than parks.
    semaphore  tiny(1);
    const bool got  = tiny.try_acquire();
    const bool more = tiny.try_acquire();
    tiny.release();

    // Same caveat as with_lock: the callable is SYNCHRONOUS, so this form is for a critical
    // section that does no awaiting. It is not a defect, it is a different tool.
    const int guarded = co_await with_semaphore(tiny, [] { return 7; });
    if (got && !more && guarded == 7)
        qb::io::cout() << "[semaphore] with_semaphore and with_lock take a SYNCHRONOUS callable, so neither can "
                          "wrap a critical section that awaits — use scoped_acquire()/scoped_lock() for that\n\n";
    else
        qb::io::cout() << "[semaphore] UNEXPECTED: " << got << more << guarded << "\n\n";
}

// ---------------------------------------------------------------------------------------
// 4. async_rw_lock — many readers OR one writer.
// ---------------------------------------------------------------------------------------
struct RwLedger {
    int         readers_now  = 0;
    int         readers_peak = 0;
    std::string order;
};

task<void>
reader(async_rw_lock &lock, std::shared_ptr<RwLedger> led) {
    auto guard = co_await lock.scoped_read_lock();
    if (++led->readers_now > led->readers_peak)
        led->readers_peak = led->readers_now;
    co_await sleep(30ms);
    --led->readers_now;
}

task<void>
writer(async_rw_lock &lock, std::shared_ptr<RwLedger> led) {
    auto guard = co_await lock.scoped_write_lock();
    led->order += "W";
    co_await sleep(5ms);
}

task<void>
demo_rw_lock() {
    qb::io::cout() << "-- 4. async_rw_lock: concurrent reads, exclusive writes\n";

    async_rw_lock lock;
    auto          led = std::make_shared<RwLedger>();

    {
        coroutine_scope scope;
        // Three readers first, so they are all holding the lock when the writer asks.
        for (int i = 0; i < 3; ++i)
            scope.spawn(reader(lock, led));
        scope.spawn(writer(lock, led));
        co_await scope.join_all();
    }

    if (led->readers_peak == 3 && led->order == "W")
        qb::io::cout() << "[async_rw_lock] 3 readers held it at once; the writer waited for the last of them "
                          "and then had it alone — worth the extra state only when reads dominate\n\n";
    else
        qb::io::cout() << "[async_rw_lock] UNEXPECTED: peak readers " << led->readers_peak << ", order \"" << led->order << "\"\n\n";
}

// ---------------------------------------------------------------------------------------
// 5. barrier — nobody passes until everybody arrives.
// ---------------------------------------------------------------------------------------
task<void>
phase_worker(barrier &gate, std::shared_ptr<std::string> log, char id, qb::duration delay) {
    co_await sleep(delay);
    *log += std::string{"arrive-"} + id + " ";
    co_await gate.arrive_and_wait();
    *log += std::string{"pass-"} + id + " ";
}

task<void>
demo_barrier() {
    qb::io::cout() << "-- 5. barrier: a rendezvous for N frames, reusable\n";

    barrier gate(3);
    auto    log = std::make_shared<std::string>();

    {
        coroutine_scope scope;
        scope.spawn(phase_worker(gate, log, 'a', 10ms));
        scope.spawn(phase_worker(gate, log, 'b', 30ms));
        scope.spawn(phase_worker(gate, log, 'c', 60ms));
        co_await scope.join_all();
    }

    // The FIRST departure must come after the LAST arrival — that is the barrier's whole
    // contract, and it is a string comparison rather than a timing one so it cannot flake.
    const auto first_pass  = log->find("pass-");
    const auto last_arrive = log->rfind("arrive-");
    const bool ordered     = first_pass != std::string::npos && last_arrive != std::string::npos && first_pass > last_arrive;
    if (ordered)
        qb::io::cout() << "[barrier] every arrival happened before any departure: " << *log << "\n";
    else
        qb::io::cout() << "[barrier] UNEXPECTED order: " << *log << "\n";

    // `reset()` is what separates a barrier from a latch: the same object can gate phase two.
    gate.reset();
    auto second = std::make_shared<std::string>();
    {
        coroutine_scope scope;
        scope.spawn(phase_worker(gate, second, 'x', 5ms));
        scope.spawn(phase_worker(gate, second, 'y', 10ms));
        scope.spawn(phase_worker(gate, second, 'z', 15ms));
        co_await scope.join_all();
    }
    qb::io::cout() << "    after reset() the SAME barrier gated a second phase: " << *second << "\n\n";
}

// ---------------------------------------------------------------------------------------
// 6. async_event — a flag you can wait on.
// ---------------------------------------------------------------------------------------
task<void>
event_waiter(async_event &ev, std::shared_ptr<int> woken) {
    co_await ev.wait();
    ++(*woken);
}

task<void>
demo_event() {
    qb::io::cout() << "-- 6. async_event: manual-reset latches, auto-reset wakes one\n";

    {
        async_event     manual(false); // auto_reset = false
        auto            woken = std::make_shared<int>(0);
        coroutine_scope scope;
        for (int i = 0; i < 3; ++i)
            scope.spawn(event_waiter(manual, woken));

        co_await sleep(10ms); // let all three park
        const size_t parked = manual.waiters_count();
        manual.set();
        co_await scope.join_all();

        // Still set afterwards, so a LATE waiter does not block at all — that is what
        // "manual-reset" means and why it models "initialisation is finished".
        const bool late_is_free = manual.is_set();
        co_await manual.wait();
        manual.reset();

        if (*woken == 3 && parked == 3 && late_is_free)
            qb::io::cout() << "[async_event] manual-reset woke all 3 waiters and stayed set, so a fourth waiter "
                              "arriving afterwards never parked at all\n";
        else
            qb::io::cout() << "[async_event] UNEXPECTED: woke " << *woken << " of " << parked << "\n";
    }

    {
        async_event     auto_reset(true); // auto_reset = true
        auto            woken = std::make_shared<int>(0);
        coroutine_scope scope;
        for (int i = 0; i < 3; ++i)
            scope.spawn(event_waiter(auto_reset, woken));

        co_await sleep(10ms);
        auto_reset.set(); // one signal, one waiter
        co_await sleep(10ms);
        const int after_one = *woken;

        // Release the other two so the scope can drain: an auto-reset event is a
        // one-signal-one-wake queue, so it takes one set() per waiter.
        auto_reset.set();
        auto_reset.set();
        co_await scope.join_all();

        if (after_one == 1 && *woken == 3)
            qb::io::cout() << "[async_event] auto-reset woke exactly 1 of 3 per set(), so it models a handoff "
                              "rather than a broadcast\n\n";
        else
            qb::io::cout() << "[async_event] UNEXPECTED: " << after_one << " woke on the first set(), " << *woken << " in total\n\n";
    }
}

// ---------------------------------------------------------------------------------------
// 7. async_latch — a countdown that happens once.
// ---------------------------------------------------------------------------------------
task<void>
latch_waiter(async_latch &latch, std::shared_ptr<int> released) {
    co_await latch.wait();
    ++(*released);
}

task<void>
subsystem_ready(async_latch &latch, qb::duration after) {
    co_await sleep(after);
    latch.count_down();
}

task<void>
demo_latch() {
    qb::io::cout() << "-- 7. async_latch: one-shot startup gate\n";

    async_latch latch(3);
    auto        released = std::make_shared<int>(0);

    {
        coroutine_scope scope;
        scope.spawn(latch_waiter(latch, released));
        scope.spawn(latch_waiter(latch, released));
        scope.spawn(subsystem_ready(latch, 10ms));
        scope.spawn(subsystem_ready(latch, 20ms));
        scope.spawn(subsystem_ready(latch, 30ms));

        co_await sleep(15ms);
        const auto mid = latch.current_count();

        co_await scope.join_all();

        if (*released == 2 && mid == 2 && latch.is_ready())
            qb::io::cout() << "[async_latch] count_down(3) released the waiters at zero, and current_count() read 2 "
                              "half way through — a latch cannot be reset, which is why it models startup\n";
        else
            qb::io::cout() << "[async_latch] UNEXPECTED: released " << *released << ", mid-count " << mid << "\n";
    }

    // `arrive_and_wait()` is count_down + wait in one, for a participant that is also a waiter.
    async_latch pair(1);
    co_await pair.arrive_and_wait();
    qb::io::cout() << "    arrive_and_wait() counted down and returned immediately once the count hit " << pair.current_count() << "\n\n";
}

task<void>
run_all(Sheet sheet) {
    co_await demo_race(sheet);
    co_await demo_semaphore();
    co_await demo_rw_lock();
    co_await demo_barrier();
    co_await demo_event();
    co_await demo_latch();
}

int
main() {
    qb::io::cout() << "=== sync primitives: one thread is not the same as no synchronisation ===\n\n";

    init();

    auto sheet = std::make_shared<Balances>();
    run_sync(run_all(sheet));

    // Gated on the two balances. They are the lesson: identical code, one line apart.
    qb::io::cout() << "    measured: balance " << sheet->unsynchronised << " without a lock, " << sheet->locked << " with one\n";
    if (sheet->unsynchronised == 10 && sheet->locked == 50)
        qb::io::cout() << "=== sync primitives complete: 10 without a lock, 50 with one ===\n";
    else
        qb::io::cout() << "=== sync primitives INCONCLUSIVE: the two balances are not 10 and 50 ===\n";
    return 0;
}
