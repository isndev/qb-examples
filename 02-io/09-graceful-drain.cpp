/**
 * @file examples/02-io/09-graceful-drain.cpp
 * @tier 02-io
 * @teaches The shutdown and backpressure vocabulary, which had ZERO coverage in the pre-3.0 corpus:
 *          `pending_read` / `input_drained` on the way in, `pending_write` / `eos` on the way out,
 *          `close_after_deliver()` for "answer, THEN hang up", `disconnected` and `dispose` in the
 *          order they really run, `extracted` for handing a live socket to another owner — and
 *          `async::defer`, the one primitive that lets a handler replace the object it is running on.
 * @demonstrates qb::io::async::event::pending_read, qb::io::async::event::input_drained,
 *               qb::io::async::event::pending_write, qb::io::async::event::eos,
 *               qb::io::async::event::disconnected, qb::io::async::event::dispose,
 *               qb::io::async::event::extracted, close_after_deliver, has_pending_write,
 *               bytes_written, messages_processed, extractSession, switch_protocol, publish,
 *               qb::io::async::defer, qb::io::async::callback, qb::io::use<T>::tcp::server<S>,
 *               qb::io::use<T>::tcp::client<S>, qb::protocol::text::command, qb::protocol::text::binary16,
 *               qb::io::async::init, qb::io::async::run_until, qb::io::cout
 * @prerequisites 02-io/06-framing-toolbox
 * @expect "=== qb-io: graceful drain ==="
 * @expect "[server] pending_write: "
 * @expect "[server] eos: the output buffer is empty; everything queued has reached the socket"
 * @expect "[client] input_drained: the buffer is empty — NOT a disconnect"
 * @expect "[server] close_after_deliver(): the farewell is queued, the close waits for it"
 * @expect "[client] farewell arrived BEFORE the close: "
 * @expect "[client] disconnected, then dispose — in that order, and dispose is the last hook"
 * @expect "[client] reconnecting from async::defer, not from the handler that was told to"
 * @expect "[server] extracted: this session is giving its socket away; watcher stopped"
 * @expect "[promoted] binary frame over the SAME connection: "
 * @expect "=== done ==="
 *
 * THE FOUR EVENTS, AND WHAT EACH ONE IS NOT
 * -----------------------------------------
 *   `pending_read{bytes}`  after the protocol extracted every COMPLETE message it could, this many
 *                          bytes are left over — a partial next message. Informational: they stay
 *                          in the buffer and are joined by the next read.
 *   `input_drained`        the input buffer is now empty. Its historical name is `eof`, and that
 *                          name was a lie: it fires on ANY successful read that empties the buffer,
 *                          on a perfectly healthy connection. `disconnected` is the closure event.
 *   `pending_write{bytes}` a write did not drain the output buffer — the kernel's send buffer is
 *                          full. THIS is your backpressure signal: stop producing.
 *   `eos`                  the output buffer is empty; everything published has reached the socket.
 *
 * THE ORDERING THAT MATTERS
 * -------------------------
 * `close_after_deliver()` does not close. It marks the protocol invalid, and the write path
 * disposes only once `pendingWrite()` reaches zero — so a farewell queued just before it is
 * guaranteed to go out first. That is the difference between a server that answers and hangs up
 * and one that hangs up on its own answer.
 *
 * Measured, and worth knowing: on that path `eos` does **not** fire. The write loop tests
 * `_reason || !_protocol->ok()` before it emits `eos`, and `close_after_deliver()` made the
 * protocol not-ok, so the disposal wins. `eos` is the event for a buffer that drained on a
 * connection that is staying up.
 *
 * Then `dispose()` runs, in this order: `on(disconnected)` first, `on(dispose)` last. That order is
 * load-bearing for a self-owned object: `dispose()` still touches `this` after `on(disconnected)`
 * returns, so `delete this` there is a use-after-free. `on(dispose)` is the last hook and nothing
 * in the framework touches the object after it returns. (An actor is exempt: `kill()` defers.)
 *
 * WHY defer() AND NOT callback(f) OR A DIRECT CALL
 * -----------------------------------------------
 * The client below reconnects when its connection drops, which means destroying and recreating the
 * very object whose handler noticed. Doing that inline frees memory the framework is still standing
 * on. `async::callback(f)` — with no delay — does NOT help: it calls `f` INLINE, right there
 * (`qb/src/qb/io/async/io.h:367`). `async::defer(f)` queues `f` to the tail of the current loop
 * turn, after every watcher for this turn has returned. It is the correct primitive, and it had
 * **zero** call sites in 55 programs.
 *
 * Build:
 *   cmake --preset release
 *   cmake --build --preset release --target qb-example-io-graceful-drain
 * Run:
 *   ./build/presets/release/examples/02-io/qb-example-io-graceful-drain
 */

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <qb/io.h>
#include <qb/io/async.h>
#include <qb/io/protocol/text.h>
#include <qb/io/tcp/socket.h>

using namespace std::chrono_literals;

namespace {

constexpr std::size_t kBigReply = 300 * 1024; // large enough that one write() will not drain it

bool          g_running        = true; // `async::run_until` loops WHILE this is true
std::uint16_t g_port           = 0;
int           g_act            = 1;
int           g_pending_writes = 0;
int           g_pending_reads  = 0;

// ------------------------------------------------------------------- the promoted session
//
// Where an extracted socket ends up. It is a STANDALONE session (`client<>` with no server): the
// io_handler that used to own the connection has let go of it entirely.

class Promoted : public qb::io::use<Promoted>::tcp::client<> {
public:
    using Protocol = qb::protocol::text::binary16<Promoted>;

    /// Frame and send under the new protocol. `Header()` is the protocol's own writer, so the
    /// promotion changes the framing without this file spelling out an endian swap.
    void
    send_framed(std::string_view body) {
        const auto  header = Protocol::Header(body.size());
        std::string frame(reinterpret_cast<const char *>(&header), sizeof(header));
        frame.append(body);
        publish(frame);
    }

    void
    on(Protocol::message &&msg) {
        qb::io::cout() << "[promoted] client answered with " << msg.size << " binary bytes on the handed-over socket\n";
        g_running = false;
    }
};

std::unique_ptr<Promoted> g_promoted;

// ------------------------------------------------------------------------------ the server

class DrainServer;

class DrainSession : public qb::io::use<DrainSession>::tcp::client<DrainServer> {
public:
    using Protocol = qb::protocol::text::command<DrainSession>;

    explicit DrainSession(IOServer &server)
        : client(server) {}

    // Declared here, defined below `DrainServer`: it calls `server().extractSession(...)`, and the
    // server type is necessarily incomplete inside its own session.
    void on(Protocol::message &&msg);

    // --- the four flow-control events, on the server side ---

    void
    on(qb::io::async::event::pending_write &&e) {
        ++g_pending_writes;
        // Narrated once: this fires on every partial write, and a demo that printed all of them
        // would bury everything else.
        if (g_pending_writes == 1)
            qb::io::cout() << "[server] pending_write: " << e.bytes
                           << " byte(s) still unsent — this is the backpressure signal,\n"
                              "         and the moment to stop producing rather than to queue more\n";
    }

    void
    on(qb::io::async::event::eos &&) {
        qb::io::cout() << "[server] eos: the output buffer is empty; everything queued has reached the socket\n";
    }

    void
    on(qb::io::async::event::extracted &&) {
        // Exactly what qbm-http's session does on this event, and for the same reason: the fd is
        // about to belong to somebody else, so stop watching it here before it does.
        this->stop();
        qb::io::cout() << "[server] extracted: this session is giving its socket away; watcher stopped\n";
    }

    void
    on(qb::io::async::event::disconnected &&e) {
        qb::io::cout() << "[server] session disconnected, reason " << e.reason << ", " << bytes_written() << " byte(s) written in total\n";
    }
};

class DrainServer : public qb::io::use<DrainServer>::tcp::server<DrainSession> {
public:
    /// Called by `registerSession` once the accepted socket has been moved into the session — the
    /// earliest point at which there is an fd to configure. Doing this in the session's CONSTRUCTOR
    /// silently does nothing: the socket is moved in AFTER the constructor runs, so `set_optval`
    /// would be called on fd -1 and fail with no diagnostic. (Measured, not guessed: the first
    /// version of this file did exactly that and `pending_write` never fired.)
    void
    on(IOSession &session) {
        // BACKPRESSURE IS ONLY VISIBLE WHEN THE BUFFERS ARE SMALLER THAN THE MESSAGE. On a modern
        // loopback the kernel swallows a 300 KB write whole, so a demo with default buffers never
        // fires `pending_write` and teaches the opposite of the truth.
        session.transport().set_optval(SOL_SOCKET, SO_SNDBUF, 16 * 1024);
    }
};

void
DrainSession::on(Protocol::message &&msg) {
    if (msg.text == "big") {
        // One publish, far more than a socket send buffer holds. The framework writes what it can,
        // reports the remainder through `pending_write`, and finishes on later EV_WRITEs.
        std::string payload(kBigReply, 'x');
        payload += '\n';
        publish(payload);
        qb::io::cout() << "[server] queued " << payload.size()
                       << " bytes; still pending after the first write: " << (has_pending_write() ? "yes" : "no") << "\n";
        return;
    }

    if (msg.text == "bye") {
        *this << "farewell, and this reaches you before the socket closes" << Protocol::end;
        // NOT a close. The protocol is marked invalid and disposal waits until the output buffer is
        // empty — so the line above is delivered, in full, first.
        close_after_deliver();
        qb::io::cout() << "[server] close_after_deliver(): the farewell is queued, the close waits for it\n";
        return;
    }

    if (msg.text == "handoff") {
        // Give the live socket to somebody else. `extractSession` fires `on(extracted)` on this
        // session, removes it from the server's map and hands back the transport. It is called
        // INLINE on purpose: the io layer holds a `shared()` guard for the duration of this
        // handler, and deferring it would let the next EV_READ deliver the peer's bytes into THIS
        // session's buffer, where the promotion would then lose them.
        //
        // Nothing is published on this path, and that matters: after this returns the framework
        // would otherwise try to write through a moved-from transport.
        g_promoted      = std::make_unique<Promoted>();
        auto [sock, ok] = server().extractSession(id());
        if (!ok) {
            qb::io::cerr() << "[server] extractSession failed\n";
            return;
        }
        g_promoted->transport() = std::move(sock);
        g_promoted->start();
        g_promoted->send_framed("promoted"); // the first message of the NEW protocol
        return;
    }

    *this << "greeting: " << msg.text << " (" << messages_processed() << " message(s) processed on this session)" << Protocol::end;
}

// ------------------------------------------------------------------------------ the client

// Declared before the class so its `on(disconnected)` can ask for a reconnect, and DEFINED after
// the owning pointer so nothing here holds a `unique_ptr` to an incomplete type.
void schedule_reconnect();

class Drain : public qb::io::use<Drain>::tcp::client<> {
    bool _saw_farewell = false;
    bool _disconnected = false;

public:
    using Protocol = qb::protocol::text::command<Drain>;
    // The second protocol this connection speaks. Declared here so the switch below is a type,
    // not a re-parse: after the handoff both ends frame with a 16-bit length prefix.
    using Framed = qb::protocol::text::binary16<Drain>;

    bool
    connect_to(std::uint16_t port) {
        if (transport().connect_v4("127.0.0.1", port) != qb::io::SocketStatus::Done)
            return false;
        // The receiving half of the same deliberate squeeze: see DrainSession's constructor.
        transport().set_optval(SOL_SOCKET, SO_RCVBUF, 16 * 1024);
        start();
        return true;
    }

    void
    on(Protocol::message &&msg) {
        if (msg.text.rfind("farewell", 0) == 0) {
            _saw_farewell = true;
            qb::io::cout() << "[client] farewell arrived BEFORE the close: " << msg.text << "\n";
            return;
        }
        if (msg.size > 1024) {
            // Reassembled out of many reads, by the protocol, with no help from this handler.
            qb::io::cout() << "[client] the big reply arrived complete: " << msg.size << " bytes, in ONE message\n";
            return;
        }
        qb::io::cout() << "[client] " << msg.text << "\n";
    }

    void
    on(Framed::message &&msg) {
        qb::io::cout() << "[promoted] binary frame over the SAME connection: " << msg.size << " bytes\n";
        // Answer in the same framing, so the promoted session on the other end has something to
        // decode and the handover is proved in both directions.
        const auto  header = Framed::Header(4);
        std::string frame(reinterpret_cast<const char *>(&header), sizeof(header));
        frame += "done";
        publish(frame);
    }

    // --- the two input-side events ---

    void
    on(qb::io::async::event::pending_read &&e) {
        ++g_pending_reads;
        if (g_pending_reads == 1)
            qb::io::cout() << "[client] pending_read: " << e.bytes
                           << " byte(s) of a partial message are held for the next read —\n"
                              "         informational, the protocol will join them to what comes next\n";
    }

    void
    on(qb::io::async::event::input_drained &&) {
        qb::io::cout() << "[client] input_drained: the buffer is empty — NOT a disconnect, the peer merely paused\n";
    }

    void
    on(qb::io::async::event::disconnected &&) {
        _disconnected = true;
        qb::io::cout() << "[client] disconnected (saw the farewell first: " << (_saw_farewell ? "yes" : "NO") << ")\n";
        // NOT `delete this`, and NOT a reconnect here: `dispose()` still touches this object after
        // this handler returns. The reconnect is queued for the tail of the loop turn instead.
        if (g_act == 1)
            schedule_reconnect();
    }

    void
    on(qb::io::async::event::dispose &&) {
        qb::io::cout() << "[client] disconnected, then dispose — in that order, and dispose is the last hook\n";
    }
};

std::unique_ptr<Drain> g_client;

void
schedule_reconnect() {
    qb::io::async::defer([]() {
        qb::io::cout() << "[client] reconnecting from async::defer, not from the handler that was told to\n";
        g_act = 2;
        // This line frees the object whose handler asked for the reconnect. It is safe HERE and
        // nowhere earlier: every watcher for that turn has already returned.
        g_client = std::make_unique<Drain>();
        if (!g_client->connect_to(g_port)) {
            qb::io::cerr() << "[client] reconnect failed\n";
            g_running = false;
            return;
        }
        *g_client << "handoff" << Drain::Protocol::end;
        // From here the connection is length-prefixed in both directions. The server has not
        // answered yet, so switching now cannot cut a message in half.
        g_client->switch_protocol<Drain::Framed>(*g_client);
    });
}

} // namespace

int
main() {
    qb::io::cout() << "=== qb-io: graceful drain ===\n";
    qb::io::async::init();

    DrainServer server;
    if (server.transport().listen_v4(0, "127.0.0.1") != 0) {
        qb::io::cerr() << "[fatal] the server could not bind\n";
        return 1;
    }
    server.start();
    g_port = server.transport().local_endpoint().port();
    qb::io::cout() << "[server] listening on 127.0.0.1:" << g_port << "\n\n";

    g_client = std::make_unique<Drain>();
    if (!g_client->connect_to(g_port)) {
        qb::io::cerr() << "[fatal] the client could not connect\n";
        return 1;
    }

    // Act 1: a normal exchange, a large reply that exercises backpressure, then a graceful close.
    *g_client << "hello" << Drain::Protocol::end;
    qb::io::async::callback([]() { *g_client << "big" << Drain::Protocol::end; }, 120ms);
    qb::io::async::callback([]() { *g_client << "bye" << Drain::Protocol::end; }, 420ms);

    // A watchdog, so a lost step is a short run with a visible shortfall rather than a hang.
    qb::io::async::callback([]() { g_running = false; }, 4s);
    qb::io::async::run_until(g_running);

    qb::io::cout() << "\n--- what the flow-control events reported ---\n";
    qb::io::cout() << "[server] pending_write fired " << g_pending_writes << " time(s) while draining the big reply\n";
    qb::io::cout() << "[client] pending_read fired " << g_pending_reads << " time(s) while reassembling it\n";

    // Asserted rather than narrated: a run where neither event fired proved nothing about
    // backpressure, and would otherwise be indistinguishable from a run where it works.
    if (g_act != 2 || !g_promoted || g_pending_writes == 0 || g_pending_reads == 0) {
        qb::io::cerr() << "=== the drain script did not finish (act " << g_act << ", " << g_pending_writes << " pending_write, "
                       << g_pending_reads << " pending_read) ===\n";
        return 1;
    }
    qb::io::cout() << "\n=== done ===\n";
    return 0;
}
