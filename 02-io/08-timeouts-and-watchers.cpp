/**
 * @file examples/02-io/08-timeouts-and-watchers.cpp
 * @tier 02-io
 * @teaches What `with_timeout` actually is — an INACTIVITY watchdog, not a periodic timer — proved
 *          by running the two re-arming calls side by side and counting how often each fires; a
 *          one-shot timer you can still cancel; a `file_watcher` that tails a growing file and
 *          frames its lines for you; and a `directory_watcher` whose real limit is the lesson: it
 *          polls, and it tells you THAT something changed, never WHAT.
 * @demonstrates qb::io::async::with_timeout, setTimeout, updateTimeout, getTimeout,
 *               qb::io::async::event::timer, qb::io::async::scoped_callback, qb::io::async::callback,
 *               qb::io::use<T>::file, qb::io::async::directory_watcher,
 *               qb::io::async::event::file, qb::protocol::text::command, qb::io::sys::file,
 *               qb::io::async::init, qb::io::async::run_until, qb::mono_now, qb::duration, qb::io::cout
 * @prerequisites 02-io/01-event-loop
 * @expect "=== qb-io: timeouts and watchers ==="
 * @expect "[watchdog] silence detected after "
 * @expect "[rearm] setTimeout() in the handler: fired "
 * @expect " time(s) — it stamps, it does not arm"
 * @expect "[deadline] cancelled before it fired: fired() = no"
 * @expect "[tail] line: "
 * @expect "[dir] a directory event says THAT something changed, never WHAT"
 * @expect "=== done ==="
 *
 * THE MISTAKE THIS PROGRAM EXISTS TO PREVENT
 * ------------------------------------------
 * `qb::io::async::with_timeout<Derived>` reads like a periodic timer and is not one. Its contract
 * is *inactivity*: it fires `Derived::on(event::timer&)` only when nothing has called
 * `updateTimeout()` for the whole budget. Look at the base's own handler
 * (`qb/src/qb/io/async/io.h:181`):
 *
 *     const ev_tstamp after = _last_activity - event.loop.now() + _timeout;
 *     if (after <= 0.)  Derived.on(event);          // the budget really elapsed
 *     else            { _async_event.set(after); _async_event.start(); }   // still busy: wait more
 *
 * So while activity keeps arriving the watcher keeps rescheduling ITSELF and your handler is never
 * called. When it finally does fire, the libev watcher is one-shot and is now STOPPED — and this is
 * the part that costs people an afternoon:
 *
 *     `setTimeout(d)`     sets the budget, stamps the activity time AND STARTS the watcher.
 *     `updateTimeout()`   stamps the activity time and NOTHING ELSE. It cannot re-arm a timer
 *                         that has already fired.
 *
 * Both are correct calls for different jobs — `updateTimeout()` is what you call on every byte
 * received, `setTimeout()` is what you call to (re)arm — and §1 below runs two otherwise identical
 * objects that differ only in which one their handler calls, then prints the two fire counts. The
 * counts are the proof; the paragraph above is only a claim.
 *
 * WHAT ev::stat CAN AND CANNOT TELL YOU
 * -------------------------------------
 * Both watchers are built on libev's `ev_stat`, which **polls** `stat()` — it is not inotify,
 * FSEvents or ReadDirectoryChangesW. Three consequences a design has to plan around:
 *   * the event carries `attr` and `prev` (two `struct stat`), so you learn size, mtime, inode and
 *     link count. For a directory that means "the directory changed"; the changed FILE's name is
 *     simply not in the data, and recovering it means diffing a listing yourself.
 *   * the interval is a floor, not a promise, and libev clamps very small values (~0.11 s). Two
 *     changes inside one interval are one event.
 *   * a file whose size and mtime both return to their previous values between two polls did not
 *     happen, as far as this API is concerned.
 * A reader who plans against inotify semantics plans wrongly, which is why the limit is stated here
 * rather than discovered later.
 *
 * Build:
 *   cmake --preset release
 *   cmake --build --preset release --target qb-example-io-timeouts-and-watchers
 * Run:
 *   ./build/presets/release/examples/02-io/qb-example-io-timeouts-and-watchers
 */

#include <chrono>
#include <filesystem>
#include <string>
#include <qb/io.h>
#include <qb/io/async.h>
#include <qb/io/protocol/text.h>
#include <qb/io/system/file.h>
#include <qb/system/time.h>

using namespace std::chrono_literals;

namespace {

bool g_running = true; // `async::run_until` loops WHILE this is true

// ============================================================== 1. the watchdog contract

/// Fires only after `budget` of silence, then RE-ARMS with `setTimeout()`.
class ReArming : public qb::io::async::with_timeout<ReArming> {
    int _fired = 0;

public:
    explicit ReArming(qb::duration budget)
        : with_timeout(budget) {}

    [[nodiscard]] int
    fired() const noexcept {
        return _fired;
    }

    void
    on(qb::io::async::event::timer const &) {
        ++_fired;
        // The watcher that just fired is stopped. This is the call that starts it again.
        setTimeout(getTimeout());
    }
};

/// Identical, except the handler calls `updateTimeout()`. It fires exactly once, and the count at
/// the end of the run is what proves it.
class Lapsed : public qb::io::async::with_timeout<Lapsed> {
    int _fired = 0;

public:
    explicit Lapsed(qb::duration budget)
        : with_timeout(budget) {}

    [[nodiscard]] int
    fired() const noexcept {
        return _fired;
    }

    void
    on(qb::io::async::event::timer const &) {
        ++_fired;
        // Stamps `_last_activity` and returns. Nothing is armed, so this handler is never
        // called again — which is correct behaviour for what the call means, and a silent
        // dead timer if you expected it to reschedule.
        updateTimeout();
    }
};

/// A connection-style watchdog: fed while traffic arrives, fires when it stops.
class Watchdog : public qb::io::async::with_timeout<Watchdog> {
    qb::mono_time _last_feed;
    bool          _fired = false;

public:
    explicit Watchdog(qb::duration budget)
        : with_timeout(budget)
        , _last_feed(qb::mono_now()) {}

    /// What a session calls on every byte it reads. Cheap on purpose: one timestamp store.
    void
    feed() {
        _last_feed = qb::mono_now();
        updateTimeout();
    }

    [[nodiscard]] bool
    fired() const noexcept {
        return _fired;
    }

    void
    on(qb::io::async::event::timer const &) {
        _fired         = true;
        const auto gap = std::chrono::duration_cast<std::chrono::milliseconds>(qb::mono_now() - _last_feed);
        qb::io::cout() << "[watchdog] silence detected after " << gap.count() << " ms with no feed() — the budget was 200 ms\n";
    }
};

// =========================================================== 3. tailing a file, framed
//
// `use<T>::file` is `file_watcher` (the ev::stat half) plus `transport::file` (the read half). Give
// it a `Protocol` and the bytes that appear at the end of the file arrive as framed messages — a
// `tail -f` that already knows where your records end.

class Tail : public qb::io::use<Tail>::file {
    int _lines = 0;

public:
    using Protocol = qb::protocol::text::command<Tail>;

    [[nodiscard]] int
    lines() const noexcept {
        return _lines;
    }

    void
    on(Protocol::message &&msg) {
        ++_lines;
        qb::io::cout() << "[tail] line: " << msg.text << "\n";
    }

    // Optional: the raw attribute event, forwarded before the read. `attr` is the current
    // `struct stat` and `prev` is the previous one, so the growth is a subtraction.
    void
    on(qb::io::async::event::file const &e) {
        qb::io::cout() << "[tail] file grew by " << (e.attr.st_size - e.prev.st_size) << " byte(s)\n";
    }
};

// ==================================================== 4. watching a directory, and its limit

class DirWatch : public qb::io::async::directory_watcher<DirWatch> {
    int _events = 0;

public:
    [[nodiscard]] int
    events() const noexcept {
        return _events;
    }

    void
    on(qb::io::async::event::file const &e) {
        ++_events;
        qb::io::cout() << "[dir] change #" << _events << ": size " << e.prev.st_size << " -> " << e.attr.st_size << ", mtime "
                       << (e.attr.st_mtime == e.prev.st_mtime ? "unchanged" : "moved") << ", links " << e.attr.st_nlink << "\n";
    }
};

void
append_line(const std::filesystem::path &path, const std::string &line) {
    qb::io::sys::file f;
    if (f.open(path, O_WRONLY | O_APPEND | O_CREAT, 0644) < 0)
        return;
    const std::string payload = line + "\n";
    f.write(payload.c_str(), payload.size());
    f.close();
}

} // namespace

int
main() {
    qb::io::cout() << "=== qb-io: timeouts and watchers ===\n";
    qb::io::async::init();

    // --------------------------------------------------------------- 1. the two re-arms
    qb::io::cout() << "\n--- 1. with_timeout is an inactivity watchdog ---\n";
    // 150 ms budget over a ~800 ms window: a handler that re-arms gets 3 or more firings, a handler
    // that only stamps gets exactly one. Nothing feeds either object, so every firing is a genuine
    // "the budget elapsed".
    ReArming rearming{150ms};
    Lapsed   lapsed{150ms};

    // The watchdog IS fed, five times at 60 ms — well inside its 200 ms budget — so it must stay
    // quiet until the feeding stops, and then fire once.
    Watchdog watchdog{200ms};
    for (int i = 1; i <= 5; ++i)
        qb::io::async::callback([&watchdog]() { watchdog.feed(); }, std::chrono::milliseconds(60 * i));

    // --------------------------------------------------------- 2. a cancellable one-shot
    qb::io::cout() << "\n--- 2. a deadline you can take back ---\n";
    // `async::callback(f, d)` is fire-and-forget: it heap-allocates a self-deleting `Timeout` and
    // hands you nothing, so there is no way to say "never mind". `scoped_callback` returns the
    // timer, and letting the handle die (or calling cancel()) is the cancellation.
    auto abandoned = qb::io::async::scoped_callback([]() { qb::io::cout() << "[deadline] THIS MUST NOT PRINT\n"; }, 400ms);
    auto honoured  = qb::io::async::scoped_callback([]() { qb::io::cout() << "[deadline] honoured deadline fired on time\n"; }, 300ms);
    qb::io::async::callback([&abandoned]() { abandoned->cancel(); }, 100ms);

    // ------------------------------------------------------------ 3 & 4. the two watchers
    const auto dir  = std::filesystem::temp_directory_path() / "qb-example-io-watchers";
    const auto file = dir / "stream.log";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    append_line(file, "first line, present before the watcher starts");

    qb::io::cout() << "\n--- 3. tailing a file, with the lines already framed ---\n";
    Tail tail;
    if (tail.transport().open(file, O_RDONLY) < 0) {
        qb::io::cerr() << "[fatal] could not open " << file.string() << "\n";
        return 1;
    }
    // The interval is a FLOOR: libev clamps `ev_stat` to about 0.11 s, so asking for 10 ms would
    // not make this poll faster. 100 ms is honest about what it will actually do.
    tail.start(file, 100ms);

    qb::io::cout() << "\n--- 4. watching a directory ---\n";
    DirWatch dirwatch;
    dirwatch.start(dir, 100ms);

    // Append to the tailed file and create files in the watched directory, spread out so each
    // change lands in its own polling interval.
    for (int i = 1; i <= 3; ++i)
        qb::io::async::callback([file, i]() { append_line(file, "appended line " + std::to_string(i)); }, std::chrono::milliseconds(250 * i));
    for (int i = 1; i <= 2; ++i)
        qb::io::async::callback([dir, i]() { append_line(dir / ("extra-" + std::to_string(i) + ".txt"), "x"); },
                                std::chrono::milliseconds(300 * i + 120));

    // One budget for the whole run: long enough for three appends at 250 ms and their polls.
    qb::io::async::callback([]() { g_running = false; }, 1600ms);
    qb::io::async::run_until(g_running);

    // ------------------------------------------------------------------------- the counts
    qb::io::cout() << "\n--- what actually happened ---\n";
    qb::io::cout() << "[rearm] setTimeout() in the handler: fired " << rearming.fired() << " time(s) — a watchdog that keeps watching\n";
    qb::io::cout() << "[rearm] updateTimeout() in the handler: fired " << lapsed.fired() << " time(s) — it stamps, it does not arm\n";
    qb::io::cout() << "[watchdog] fired after the feeding stopped: " << (watchdog.fired() ? "yes" : "NO — it should have") << "\n";
    qb::io::cout() << (abandoned->fired() ? "[deadline] cancel() did NOTHING — the abandoned deadline fired anyway\n"
                                          : "[deadline] cancelled before it fired: fired() = no\n");
    qb::io::cout() << "[deadline] honoured deadline fired: " << (honoured->fired() ? "yes" : "no") << "\n";
    qb::io::cout() << "[tail] framed " << tail.lines() << " line(s) out of a file that was being written to\n";
    qb::io::cout() << "[dir] " << dirwatch.events() << " directory event(s) observed\n";
    qb::io::cout() << "[dir] a directory event says THAT something changed, never WHAT: no filename is carried,\n"
                      "      because ev::stat is two struct stats and a poll, not an inotify queue\n";

    // The stop is explicit. Both watchers keep the loop alive otherwise, and a program that leaves
    // its watchers running is a program that never returns from `run()`.
    tail.disconnect();
    dirwatch.disconnect();
    std::filesystem::remove_all(dir);

    // The two counts are the measured claim of this program, so a run in which they came out wrong
    // must not report success.
    if (rearming.fired() < 2 || lapsed.fired() != 1 || tail.lines() == 0) {
        qb::io::cerr() << "=== the timer contract did not reproduce on this host ===\n";
        return 1;
    }

    qb::io::cout() << "\n=== done ===\n";
    return 0;
}
