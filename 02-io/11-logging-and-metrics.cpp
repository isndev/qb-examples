/**
 * @file examples/02-io/11-logging-and-metrics.cpp
 * @tier 02-io
 * @teaches The two production surfaces a qb-io service needs and that nothing in the corpus used:
 *          the asynchronous logger behind QB_LOG_* — which every qb binary has ALREADY started
 *          before main() runs — and a fixed-capacity rolling window of measurements taken with
 *          the raw CPU counter, so a hot loop can report its own latency without allocating.
 * @demonstrates qb::io::log::init, qb::io::log::setLevel, qb::io::log::Level, QB_LOG_DEBUG,
 *               QB_LOG_VERB, QB_LOG_INFO, QB_LOG_WARN, QB_LOG_CRIT, qb::ring_buffer, push_back,
 *               size, capacity, full,
 *               qb::tsc_ticks, qb::CPU::Architecture, qb::CPU::LogicalCores, qb::CPU::PhysicalCores,
 *               qb::CPU::ClockSpeed, qb::CPU::HyperThreading, qb::CPU::ThreadPinningSupported,
 *               qb::io::async::init, qb::io::async::run_until, qb::io::async::with_timeout,
 *               qb::io::async::event::timer, setTimeout, qb::io::cout
 * @prerequisites 02-io/01-event-loop
 * @expect "=== qb-io: logging and metrics ==="
 * @expect "[log] a qb binary opens its log file BEFORE main() runs"
 * @expect "[log] read back from the file, so these really left the process:"
 * @expect "[log] the DEBUG line is absent: setLevel(INFO) dropped it at the call site"
 * @expect "[metrics] window full at 64 samples; the oldest is dropped, not the newest"
 * @expect "=== done ==="
 *
 * WHY qb::io::cout IS NOT THE PRODUCTION ANSWER
 * ---------------------------------------------
 * `qb/src/qb/io.h:101` says so in as many words, and the corpus ignored it: measured over the 55
 * pre-3.0 programs, `qb::io::cout` appeared in 59 files and `QB_LOG_*` in **zero**. The difference
 * is not taste:
 *
 *   * `qb::io::cout()` builds a `std::stringstream`, then takes a PROCESS-WIDE mutex in its
 *     destructor and writes through it. Two cores logging is two cores serialised, on the actor
 *     thread, inside whatever handler you were running.
 *   * `QB_LOG_*` encodes its arguments into a per-thread buffer and returns. A background thread
 *     owns the file. Formatting, `std::endl` and the write all happen off your loop.
 *
 * `qb::io::cout` stays exactly right for what this corpus uses it for — a program talking to the
 * person who ran it. It is the wrong tool for a service under load, which is why every example
 * below prints its NARRATION with `cout` and its LOG LINES with `QB_LOG_*`.
 *
 * THE PART NOBODY EXPECTS
 * -----------------------
 * With `QB_WITH_LOGGING=ON` (the default), `qb/src/qb/io/logger.cpp:52-64` runs a static
 * initialiser that calls `qb::io::log::init("./qb", 512)` and `setLevel(INFO)` — so **every**
 * executable linking qb-io creates `./qb.1.log` in its working directory and starts a logging
 * thread before `main()` is entered, whether or not it ever logs a line. Measured on this tree:
 * an empty `qb.1.log` sits next to every example binary that has ever been run. Two consequences
 * worth knowing: a service whose working directory is not writable gets a silently failed open
 * (the ofstream's state is not checked), and calling `log::init` yourself does not add a logger,
 * it REPLACES that one — which is what the first section below does, and why it is also the way
 * to flush: tearing the old logger down joins its thread and closes its file.
 *
 * TIMING WITH tsc_ticks()
 * -----------------------
 * `qb::tsc_ticks()` reads the CPU's cycle counter (`rdtsc` on x86-64, `cntvct_el0` on aarch64).
 * It is monotonic per core and extremely cheap, and it is NOT a clock: it is uncalibrated, it is
 * not comparable across cores, and there is no conversion to nanoseconds here on purpose. Use it
 * for deltas on one thread — "did this get slower?" — and `qb::mono_now()` for anything that has
 * to mean something to somebody else.
 *
 * Build:
 *   cmake --preset release
 *   cmake --build --preset release --target qb-example-io-logging-and-metrics
 * Run:
 *   ./build/presets/release/examples/02-io/qb-example-io-logging-and-metrics
 */

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
#include <qb/io.h>
#include <qb/io/async.h>
#include <qb/system/container/ring_buffer.h>
#include <qb/system/cpu.h>
#include <qb/system/time.h>

using namespace std::chrono_literals;

namespace {

// The rolling window. Fixed capacity, no allocation after construction, and `Overwrite = true`
// (the default) so the OLDEST sample is evicted when it is full — which is what "the last N" means
// and the opposite of a bounded queue that starts refusing writes.
constexpr std::size_t kWindow = 64;
using LatencyWindow           = qb::ring_buffer<std::uint64_t, kWindow>;

constexpr const char *kLogStem = "qb-example-io-logging";

void
section(const char *title) {
    qb::io::cout() << "\n--- " << title << " ---\n";
}

// `ring_buffer` publishes `size()` alongside `capacity()`, `empty()` and `full()`, so the live
// count is O(1). It did NOT until 3.0: `size_` was a private member that `empty()` and `full()`
// both read and nothing exposed, so this function walked the forward iterators instead — O(n),
// and the reason worth writing down is that `capacity()` was public while the occupancy it bounds
// was not, which reads as a deliberate omission and is not one.
std::size_t
live_count(const LatencyWindow &w) {
    return w.size();
}

// Percentiles over the window. Copied out and sorted because the ring is in arrival order and a
// percentile is a rank — this is the deliberate cost of asking, and it is paid at REPORT time
// rather than on every sample, which is the whole point of keeping a window instead of a histogram.
std::uint64_t
percentile(const LatencyWindow &w, double p) {
    std::vector<std::uint64_t> sorted(w.cbegin(), w.cend());
    if (sorted.empty())
        return 0;
    std::sort(sorted.begin(), sorted.end());
    const auto idx = static_cast<std::size_t>(p * static_cast<double>(sorted.size() - 1));
    return sorted[idx];
}

// A unit of work worth timing: something the optimiser cannot delete, and whose cost varies.
std::uint64_t
mix(std::uint64_t seed, int rounds) {
    for (int i = 0; i < rounds; ++i)
        seed = seed * 6364136223846793005ULL + 1442695040888963407ULL;
    return seed;
}

// A loop-driven sampler. `with_timeout` is the tier's periodic primitive (see 02-io/08): the
// watcher is one-shot, so the handler re-arms with `setTimeout`.
class Sampler : public qb::io::async::with_timeout<Sampler> {
    LatencyWindow &_window;
    bool          &_running;
    int            _ticks       = 0;
    const int      _max_ticks   = 0;
    std::uint64_t  _accumulator = 1;

public:
    Sampler(LatencyWindow &window, bool &running, int max_ticks)
        : with_timeout(2ms)
        , _window(window)
        , _running(running)
        , _max_ticks(max_ticks) {}

    [[nodiscard]] bool
    done() const noexcept {
        return _ticks >= _max_ticks;
    }

    void
    on(qb::io::async::event::timer const &) {
        // Each tick does a burst of work whose size grows, so the window has something to say.
        for (int i = 0; i < 8; ++i) {
            const auto before = qb::tsc_ticks();
            _accumulator      = mix(_accumulator, 200 + (_ticks % 5) * 400);
            const auto after  = qb::tsc_ticks();
            _window.push_back(after - before);
        }

        ++_ticks;
        // A log line per tick, on the logging thread's time rather than this one's.
        QB_LOG_INFO("sampler tick " << _ticks << " window=" << static_cast<std::uint64_t>(live_count(_window))
                                    << " accumulator=" << _accumulator);
        if (done())
            _running = false; // `run_until` loops WHILE its flag is true — it is a "keep going", not a "done"
        else
            setTimeout(2ms);
    }
};

} // namespace

int
main() {
    qb::io::cout() << "=== qb-io: logging and metrics ===\n";

    // ------------------------------------------------------------------------ the logger
    section("1. the logger you already have");

    const auto preexisting = std::filesystem::current_path() / "qb.1.log";
    qb::io::cout() << "[log] a qb binary opens its log file BEFORE main() runs — " << preexisting.filename().string()
                   << " exists here already: " << (std::filesystem::exists(preexisting) ? "yes" : "no") << "\n";
    qb::io::cout() << "[log] that is qb/src/qb/io/logger.cpp's static LogInitializer, not this program\n";

    // Replacing it. `init` does not add a second logger — it destroys the running one, which joins
    // its thread and closes its file, then starts a new one writing `<stem>.1.log`.
    const auto log_path = std::filesystem::current_path() / (std::string(kLogStem) + ".1.log");
    std::filesystem::remove(log_path);
    qb::io::log::init(kLogStem, /*roll_MB=*/8);

    // Everything strictly below the level is dropped AT THE CALL SITE: the macro tests
    // `is_logged(level)` first, so a filtered-out DEBUG line does not even format its arguments.
    qb::io::log::setLevel(qb::io::log::Level::INFO);

    QB_LOG_DEBUG("this DEBUG line is below the level and never reaches the file");
    QB_LOG_VERB("this VERBOSE line is below the level too");
    QB_LOG_INFO("service starting, window capacity " << static_cast<std::uint64_t>(kWindow));
    QB_LOG_WARN("a warning carries the same cost as an info line: an encode and a return");
    QB_LOG_CRIT("and a critical line is not special either — the LEVEL is a filter, not a channel");

    // --------------------------------------------------------------------- the loop
    section("2. sampling from the event loop");
    qb::io::async::init();

    LatencyWindow window;
    bool          running = true;
    Sampler       sampler{window, running, 40};

    // A watchdog, so a lost tick is a short run rather than a hang. It clears the same flag.
    qb::io::async::callback([&running]() { running = false; }, 3s);
    qb::io::async::run_until(running);

    qb::io::cout() << "[metrics] " << (sampler.done() ? "sampler completed its ticks" : "watchdog fired first") << "\n";

    // ---------------------------------------------------------------------- the window
    section("3. what a fixed window can and cannot tell you");
    qb::io::cout() << "[metrics] capacity " << window.capacity() << ", live " << live_count(window)
                   << ", full: " << (window.full() ? "yes" : "no") << "\n";
    if (window.full())
        qb::io::cout() << "[metrics] window full at 64 samples; the oldest is dropped, not the newest\n";

    qb::io::cout() << "[metrics] oldest sample " << window.front() << " ticks, newest " << window.back() << " ticks\n";
    qb::io::cout() << "[metrics] p50 " << percentile(window, 0.50) << " ticks, p99 " << percentile(window, 0.99) << " ticks\n";
    qb::io::cout() << "[metrics] these are RAW CPU ticks: comparable to each other on this thread, and to nothing else\n";

    // ------------------------------------------------------------------ what you ran on
    section("4. the machine the numbers came from");
    qb::io::cout() << "[cpu] " << qb::CPU::Architecture() << ", " << qb::CPU::PhysicalCores() << " physical / " << qb::CPU::LogicalCores()
                   << " logical cores, hyper-threading: " << (qb::CPU::HyperThreading() ? "yes" : "no") << "\n";
    // `ClockSpeed()` is a NOMINAL figure the OS may simply not publish — it returns -1 on Apple
    // silicon, measured. A metric that is not available must read as unavailable, not as a number.
    const auto hz = qb::CPU::ClockSpeed();
    qb::io::cout() << "[cpu] nominal clock " << (hz > 0 ? std::to_string(hz) + " Hz" : std::string("not reported by this OS")) << "\n";
    qb::io::cout() << "[cpu] thread pinning supported: "
                   << (qb::CPU::ThreadPinningSupported() ? "yes" : "no — here a successful setAffinity() does not imply pinning") << "\n";

    // -------------------------------------------------------- proving the lines landed
    section("5. proving the log lines really left the process");
    // The logger is asynchronous, so "I called QB_LOG_INFO" is not evidence a line is on disk. Each
    // line IS flushed by the writer thread (it ends with std::endl), so a brief wait is enough for
    // a demonstration; a service that must not lose its last lines replaces the logger at shutdown,
    // which joins the thread and drains the queue.
    std::this_thread::sleep_for(150ms);

    std::vector<std::string> lines;
    {
        std::ifstream in(log_path);
        for (std::string line; std::getline(in, line);)
            lines.push_back(line);
    }

    qb::io::cout() << "[log] read back from the file, so these really left the process:\n";
    qb::io::cout() << "[log] " << log_path.filename().string() << " holds " << lines.size() << " line(s)\n";
    for (std::size_t i = 0; i < lines.size() && i < 3; ++i)
        qb::io::cout() << "[log]   " << lines[i] << "\n";
    if (lines.size() > 3)
        qb::io::cout() << "[log]   … and " << (lines.size() - 3) << " more\n";

    const bool debug_present =
        std::any_of(lines.begin(), lines.end(), [](const std::string &l) { return l.find("[DEBUG]") != std::string::npos; });
    qb::io::cout() << (debug_present ? "[log] a DEBUG line is present — the level was not applied\n"
                                     : "[log] the DEBUG line is absent: setLevel(INFO) dropped it at the call site\n");
    qb::io::cout() << "[log] the file is left in place on purpose: " << log_path.string() << "\n";

    qb::io::cout() << "\n=== done ===\n";
    return 0;
}
