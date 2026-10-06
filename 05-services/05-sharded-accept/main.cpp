/**
 * @file examples/05-services/05-sharded-accept/main.cpp
 * @tier 05-services
 * @teaches One listener per core on ONE port, the accept sharded by the kernel: every core accepts and serves
 *          its own connections, with no acceptor handing sockets to a pool -- `listen_options{.reuse_port =
 *          true}` (3.3). Where the system cannot share a port, the program says so and serves from one core.
 * @demonstrates qb::io::tcp::listen_options, reuse_port, transport().listen_v4, local_endpoint,
 *               qb::io::use<T>::tcp::server<S>, qb::io::use<T>::tcp::client<S>, server(), getIndex,
 *               broadcast<E>, registerEvent<E>, addActor, hasError, qb::io::tcp::socket,
 *               qb::protocol::text::command
 * @prerequisites 05-services/01-tcp-chat
 * @expect "=== 05-services/05: one listener per core, the accept sharded by the kernel ==="
 * @expect "[shard] core 0 listening on 127.0.0.1:"
 * @expect "[load] every connection was answered by the core that accepted it"
 * @expect "=== done ==="
 *
 * THE HAND-OFF THIS REMOVES
 * -------------------------
 * `01-tcp-chat` accepts on one actor and hands every socket to a pool of server actors on other cores:
 * one core does all the accepting, and each connection crosses a core before its first byte is read.
 * Here each core runs its own listener on the same port, and the kernel decides which listener a new
 * connection lands on. The connection is accepted, read and answered on that core: nothing is handed
 * anywhere, and there is no single accepting core to saturate.
 *
 * WHAT THE OPTION DOES, SYSTEM BY SYSTEM
 * --------------------------------------
 *   Linux          `SO_REUSEPORT`: every listener that asks shares the port, and the kernel BALANCES
 *                  incoming connections across them (a hash of the connection's addresses).
 *   macOS, BSDs    the port is shared, but the accept is NOT balanced (FreeBSD's balancing variant is
 *                  `SO_REUSEPORT_LB`): one listener tends to get them all.
 *   Windows        no such option: a listen that asks for it FAILS with `ENOPROTOOPT`, by design --
 *                  qb never binds a port it cannot share honestly. This program then serves from core 0.
 * A listener that did not ask is refused the port, so two unrelated servers can still never share one by
 * accident (`run-examples.py` asserts that every server of this corpus refuses a second instance).
 *
 * WHO CHOOSES THE PORT
 * --------------------
 * Core 0's shard listens on port 0 -- the system picks a free one -- and BROADCASTS it; the shards of the
 * other cores join that port. The load comes from outside the engine, as real clients do: 64 blocking
 * connections, each asking "which core?" and reading the answer, so the distribution printed at the end
 * is measured, not assumed. On Linux the program fails unless at least two cores served (the chance
 * that the kernel's hash puts all 64 on one of four listeners is 4 * 4^-64).
 *
 * Run: cd build/presets/release/examples/05-services && ./qb-example-services-sharded-accept
 */

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>
#include <thread>
#include <qb/actor.h>
#include <qb/io.h>
#include <qb/io/async.h>
#include <qb/io/protocol/text.h>
#include <qb/io/tcp/socket.h>
#include <qb/main.h>

using namespace std::chrono_literals;

namespace {

constexpr int kCores       = 4;
constexpr int kConnections = 64;

std::atomic<std::uint16_t> g_port{0};
std::atomic<int>           g_listening{0};
std::atomic<bool>          g_shared{false};
std::atomic<bool>          g_join_failed{false};

// Core 0's shard tells the others which port to join.
struct PortChosen : qb::Event {
    std::uint16_t port = 0;
    explicit PortChosen(std::uint16_t p)
        : port(p) {}
};

class Shard;

// A connection, served on the core whose listener the kernel chose: it answers which core that is.
class ShardSession : public qb::io::use<ShardSession>::tcp::client<Shard> {
public:
    using Protocol = qb::protocol::text::command<ShardSession>;

    explicit ShardSession(IOServer &server)
        : client(server) {}

    void on(Protocol::message &&);
};

// One per core: a listener on the shared port, and the sessions it accepted.
class Shard
    : public qb::Actor
    , public qb::io::use<Shard>::tcp::server<ShardSession> {
    const bool _chooses_port;

public:
    explicit Shard(bool chooses_port)
        : _chooses_port(chooses_port) {}

    qb::io::async::task<bool>
    onInit() override {
        registerEvent<PortChosen>(*this);
        if (!_chooses_port)
            co_return true; // joins once core 0 has broadcast the port

        const qb::io::tcp::listen_options share{.reuse_port = true};
        const bool                        shared = transport().listen_v4(0, "127.0.0.1", share) == 0;
        if (!shared && transport().listen_v4(0, "127.0.0.1") != 0) {
            qb::io::cerr() << "[shard] core 0 could not bind 127.0.0.1\n";
            co_return false; // a failed bind must not look like a healthy start
        }
        start();
        const auto port = transport().local_endpoint().port();
        qb::io::cout() << "[shard] core 0 listening on 127.0.0.1:" << port
                       << (shared ? " (port shared)\n" : " (this system cannot share a port: core 0 serves alone)\n");
        g_shared = shared;
        g_port   = port;
        ++g_listening;
        if (shared)
            broadcast<PortChosen>(port);
        co_return true;
    }

    void
    on(PortChosen const &e) {
        if (_chooses_port)
            return; // the broadcast reaches core 0 too
        if (transport().listen_v4(e.port, "127.0.0.1", qb::io::tcp::listen_options{.reuse_port = true}) != 0) {
            qb::io::cerr() << "[shard] core " << getIndex() << " could not join port " << e.port << "\n";
            g_join_failed = true;
            return;
        }
        start();
        qb::io::cout() << "[shard] core " << getIndex() << " joined 127.0.0.1:" << e.port << "\n";
        ++g_listening;
    }
};

void
ShardSession::on(Protocol::message &&) {
    *this << "core " << server().getIndex() << Protocol::end;
}

// A client outside the engine: connect, ask, read the answer "core N\n". Returns N, or -1.
int
ask_which_core(std::uint16_t port) {
    qb::io::tcp::socket s;
    if (s.connect(qb::io::endpoint{"127.0.0.1", port}) != 0)
        return -1;
    if (s.write("who\n", 4) != 4)
        return -1;
    std::string reply;
    char        c = 0;
    while (reply.size() < 16 && s.read(&c, 1) == 1 && c != '\n')
        reply.push_back(c);
    s.disconnect();
    if (reply.rfind("core ", 0) != 0)
        return -1;
    return std::stoi(reply.substr(5));
}

} // namespace

int
main() {
    qb::io::cout() << "=== 05-services/05: one listener per core, the accept sharded by the kernel ===\n";

    qb::Main engine;
    for (int core = 0; core < kCores; ++core)
        engine.addActor<Shard>(static_cast<qb::CoreId>(core), core == 0);
    engine.start();

    // Wait for the listeners: core 0's, then, when the port can be shared, the three that join it.
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (std::chrono::steady_clock::now() < deadline && !g_join_failed && !(g_port != 0 && g_listening == (g_shared ? kCores : 1)))
        std::this_thread::sleep_for(5ms);
    if (g_port == 0 || g_join_failed || g_listening != (g_shared ? kCores : 1)) {
        qb::io::cerr() << "=== the listeners did not come up (" << g_listening.load() << " listening) ===\n";
        engine.stop();
        engine.join();
        return 1;
    }

    // The load: every connection asks which core served it.
    std::array<int, kCores> served{};
    int                     answered = 0;
    for (int i = 0; i < kConnections; ++i) {
        const int core = ask_which_core(g_port);
        if (core >= 0 && core < kCores) {
            ++served[static_cast<std::size_t>(core)];
            ++answered;
        }
    }
    engine.stop();
    engine.join();

    qb::io::cout() << "[load] " << answered << " of " << kConnections << " connections answered\n";
    if (answered == kConnections)
        qb::io::cout() << "[load] every connection was answered by the core that accepted it\n";
    int cores_used = 0;
    for (int core = 0; core < kCores; ++core) {
        qb::io::cout() << "[load]   core " << core << " served " << served[static_cast<std::size_t>(core)] << "\n";
        cores_used += served[static_cast<std::size_t>(core)] > 0;
    }
#if defined(__linux__)
    const bool balanced_here = true;
#else
    const bool balanced_here = false;
#endif
    if (g_shared && balanced_here)
        qb::io::cout() << "[kernel] the accept was spread over " << cores_used << " of " << kCores << " cores, no hand-off\n";
    else if (g_shared)
        qb::io::cout() << "[kernel] the port is shared, but this system does not balance the accept\n";
    else
        qb::io::cout() << "[kernel] no SO_REUSEPORT on this system: one listener, on core 0\n";

    if (engine.hasError() || answered != kConnections || (g_shared && balanced_here && cores_used < 2)) {
        qb::io::cerr() << "=== the sharded accept did not reproduce ===\n";
        return 1;
    }
    qb::io::cout() << "=== done ===\n";
    return 0;
}
