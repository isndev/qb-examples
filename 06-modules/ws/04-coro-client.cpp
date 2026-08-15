/**
 * @file examples/06-modules/ws/04-coro-client.cpp
 * @tier 06-modules
 * @teaches The WebSocket client as a coroutine: `coro_client`, `co_await connect/receive/
 *          close_async`, the tagged frame you must branch on, the ONE-awaiter rule, the buffer
 *          that catches frames nobody is waiting for, and a reconnect on the same object.
 * @demonstrates qb::http::ws::coro_client, qb::http::ws::coro_client_secure, connect, receive,
 *               close_async, set_pending_cap, negotiated_subprotocol,
 *               qb::http::ws::ConnectResult, qb::http::ws::CloseResult,
 *               qb::http::ws::IncomingFrame, qb::http::ws::MessageText, qb::http::ws::MessagePing,
 *               qb::http::ws::CloseStatus, qb::http::ws::coro_session<EchoSession, EchoServer>,
 *               next_frame, qb::io::use<EchoServer>::tcp::server<EchoSession>,
 *               qb::io::async::init, qb::io::async::run_until, qb::io::async::coro_scheduler,
 *               qb::io::async::sleep, qb::io::async::task<void>
 * @prerequisites 06-modules/ws/02-chat-client, 06-modules/ws/03-coro-session
 * @expect "[connect] one line and one bool: ConnectResult.ok covers BOTH the TCP connect and the"
 * @expect "[frames] receive() hands back a TAGGED frame, and Disconnected is one of the tags —"
 * @expect "[control] a Ping from the server arrived as a frame of its own and the protocol layer"
 * @expect "[buffer] frames that arrive with nobody parked are BUFFERED, in order, up to the cap"
 * @expect "[oneawaiter] two coroutines cannot park on the same client: the second receive() threw"
 * @expect "[close] the client closed and the server echoed the code and reason back"
 * @expect "[reconnect] connect() on the SAME client works: it clears the buffer, the disconnected"
 * @expect "=== coro-client complete: connect, receive, control frames, backpressure, the"
 *
 * WHAT THIS REPLACES
 * ------------------
 * `02-chat-client` is the callback client, and to have a conversation it keeps a state machine:
 * a flag for "connected", another for "welcomed", a queue for "things to send once we are", and
 * an `on(message&&)` that has to work out which of those it is looking at. Every one of those
 * members exists because the code cannot WAIT.
 *
 * `coro_client` can. `co_await connect(...)`, then `co_await receive()` where the protocol says
 * the server speaks next. The state machine is the coroutine, and the flags are gone.
 *
 * THE FOUR THINGS THAT ARE NOT OBVIOUS
 * ------------------------------------
 * 1. `receive()` yields a TAGGED frame, not a payload. `Message`, `Ping`, `Pong`, `Close` and
 *    `Disconnected` all arrive through it, and `Disconnected` is how a dead transport reaches you
 *    — a loop that only looks at `Message` spins for ever on a socket that is gone.
 * 2. ONE awaiter at a time. A second coroutine calling `receive()` while another is parked throws
 *    `std::logic_error` rather than silently stealing the first one's frame. Section 5 provokes it.
 * 3. Frames that arrive with nobody parked are BUFFERED, up to `set_pending_cap()` (1024 by
 *    default), and past the cap the OLDEST is dropped — so a slow consumer loses the head of a
 *    burst, not the tail. `set_pending_cap(0)` turns buffering off entirely.
 * 4. `connect()` on a live-but-dropped client RESETS it: the pending buffer is cleared and the
 *    disconnected flag is lowered. That is worth knowing because it is not universal in this
 *    codebase — `qb::pg::tcp::notify_co_consumer`'s `receive()` channel is closed permanently by
 *    a disconnect, so that type has to be rebuilt instead (see `06-modules/pgsql/07-listen-notify`).
 *
 * TLS IS ONE LINE
 * ---------------
 * `qb::http::ws::coro_client_secure` is `coro_client<qb::io::transport::stcp>` and takes a `wss://`
 * URI. Every call below is identical on it; nothing else in your code changes.
 *
 * As in `03-coro-session`, the server is hosted by this program on the same event loop, on an
 * ephemeral port — so the measurements are end to end and the program needs no network. Its
 * session type is in a NAMED namespace, which `coro_session` requires (see 03's header).
 *
 * Build:
 *   cmake --preset release
 *   cmake --build --preset release --target qb-example-modules-ws-coro-client
 * Run:
 *   ./build/presets/release/examples/06-modules/ws/qb-example-modules-ws-coro-client
 */

#include <cstdint>
#include <stdexcept>
#include <type_traits>
#include <string>
#include <qb/io/async.h>
#include <qb/io/async/coroutine.h>
#include <qbm/http/ws.h>
#include <qbm/http/ws/coro.h>

using namespace std::chrono_literals;

// NAMED namespace: `coro_session` spawns a lambda into a frame type defined in a qb header, and an
// anonymous-namespace session would give that frame a field of no-linkage type.
namespace ws_coro_client_example {

using qb::http::ws::CloseStatus;
using qb::http::ws::IncomingFrame;

class EchoServer;

/// The fixture: echo text, send a Ping on demand, close on "bye". Written with `coro_session`
/// because that is the shortest correct server there is; it is the subject of `03-coro-session`.
class EchoSession : public qb::http::ws::coro_session<EchoSession, EchoServer> {
public:
    using base = qb::http::ws::coro_session<EchoSession, EchoServer>;
    using base::base;

    qb::io::async::task<void>
    run() {
        while (true) {
            IncomingFrame frame = co_await next_frame();
            if (frame.kind == IncomingFrame::Kind::Disconnected || frame.kind == IncomingFrame::Kind::Close)
                co_return;
            if (frame.kind != IncomingFrame::Kind::Message)
                continue;

            if (frame.payload == "ping-me") {
                qb::http::ws::MessagePing ping;
                ping << "are you there";
                *this << ping;
                continue;
            }
            if (frame.payload == "burst") {
                // Three replies with nothing asking for them yet: the client's buffer is what
                // catches these.
                for (int i = 1; i <= 3; ++i) {
                    qb::http::ws::MessageText part;
                    part << "part-" << i;
                    *this << part;
                }
                continue;
            }
            qb::http::ws::MessageText reply;
            reply << "echo:" << frame.payload;
            *this << reply;
        }
    }
};

class EchoServer : public qb::io::use<EchoServer>::tcp::server<EchoSession> {
public:
    void
    on(IOSession &) {}
};

} // namespace ws_coro_client_example

using namespace ws_coro_client_example;

namespace {

// `coro_client` is a class TEMPLATE with a defaulted transport parameter, so `coro_client x;`
// works by deduction while a function PARAMETER of that type does not — it needs the `<>`.
// This alias is the readable way to say it once.
using WsClient = qb::http::ws::coro_client<>;

// ...and the wss:// one, named here because naming it IS the change: `coro_client_secure` is the
// same class over the TLS transport, so every call in this file compiles unchanged against it.
using SecureWsClient = qb::http::ws::coro_client_secure;
static_assert(!std::is_same_v<WsClient, SecureWsClient>, "the secure client is a distinct instantiation, not an alias of the plain one");

void
say(WsClient &ws, std::string const &text) {
    qb::http::ws::MessageText msg;
    msg << text;
    ws << msg;
}

// A named coroutine, not an immediately-invoked lambda: `CoroutineScheduler::spawn` takes a
// `task<void>&&`, so a lambda would have to be CALLED, and the temporary closure would die while
// its frame still referred to it.
qb::io::async::task<void>
second_receiver(WsClient &ws, bool &threw, std::string &what) {
    try {
        [[maybe_unused]] auto frame = co_await ws.receive();
    } catch (std::logic_error const &e) {
        threw = true;
        what  = e.what();
    }
}

} // namespace

qb::io::async::task<void>
run_client(std::uint16_t port, bool &running, bool &ok) {
    struct StopOnExit {
        bool &r;
        ~StopOnExit() {
            r = false;
        }
    } stop{running};

    const qb::io::uri uri{"ws://127.0.0.1:" + std::to_string(port) + "/"};

    // ---- 1. connect ---------------------------------------------------------------------
    WsClient ws;
    // The buffer that section 4 measures. 64 is chosen from the protocol's own burst size (3
    // here); the default is 1024 and 0 disables buffering entirely.
    ws.set_pending_cap(64);
    qb::http::ws::ConnectResult connected = co_await ws.connect(uri);

    const bool connect_ok = connected.ok;
    qb::io::cout() << (connect_ok ? "[connect] one line and one bool: ConnectResult.ok covers BOTH the TCP connect and the\n"
                                    "          HTTP upgrade, because a socket that connected and did not upgrade is not a\n"
                                    "          WebSocket and there is nothing useful to do with it\n"
                                  : "[connect] UNEXPECTED: could not connect to the loopback server\n");
    if (!connect_ok)
        co_return;
    qb::io::cout() << "          (negotiated subprotocol: '" << ws.negotiated_subprotocol() << "' — empty, because none was offered)\n";

    // ---- 2. the tagged frame -------------------------------------------------------------
    say(ws, "hello");
    IncomingFrame first = co_await ws.receive();

    const bool frames_ok = first.kind == IncomingFrame::Kind::Message && first.is_text && first.payload == "echo:hello";
    qb::io::cout() << (frames_ok ? "[frames] receive() hands back a TAGGED frame, and Disconnected is one of the tags —\n"
                                   "         a loop that only ever looks at Kind::Message spins for ever once the peer is\n"
                                   "         gone, because nothing else will ever arrive\n"
                                 : "[frames] UNEXPECTED: the echo did not come back as a text Message\n");
    qb::io::cout() << "         (kind=Message, is_text=" << (first.is_text ? "true" : "false") << ", payload '" << first.payload << "')\n";

    // ---- 3. a control frame ---------------------------------------------------------------
    say(ws, "ping-me");
    IncomingFrame ping = co_await ws.receive();

    const bool control_ok = ping.kind == IncomingFrame::Kind::Ping && ping.payload == "are you there";
    qb::io::cout() << (control_ok ? "[control] a Ping from the server arrived as a frame of its own and the protocol layer\n"
                                    "          had already answered it with a Pong — you SEE control frames, you do not have\n"
                                    "          to service them\n"
                                  : "[control] UNEXPECTED: the server's Ping did not arrive as Kind::Ping\n");
    qb::io::cout() << "          (payload '" << ping.payload << "')\n";

    // ---- 4. the buffer ---------------------------------------------------------------------
    say(ws, "burst");
    // Deliberately do NOT park: give the loop time to deliver all three while nobody is waiting.
    co_await qb::io::async::sleep(120ms);
    IncomingFrame b1 = co_await ws.receive();
    IncomingFrame b2 = co_await ws.receive();
    IncomingFrame b3 = co_await ws.receive();

    const bool buffer_ok = b1.payload == "part-1" && b2.payload == "part-2" && b3.payload == "part-3";
    qb::io::cout() << (buffer_ok ? "[buffer] frames that arrive with nobody parked are BUFFERED, in order, up to the cap\n"
                                   "         set by set_pending_cap() — and past it the OLDEST is dropped, so a slow reader\n"
                                   "         loses the head of a burst rather than the newest state\n"
                                 : "[buffer] UNEXPECTED: the three buffered parts did not arrive in order\n");
    qb::io::cout() << "         ('" << b1.payload << "', '" << b2.payload << "', '" << b3.payload << "')\n";

    // ---- 5. the one-awaiter rule ------------------------------------------------------------
    // Two consumers on one client is a design error, and the client says so instead of letting
    // one of them silently steal the other's frame.
    bool        threw = false;
    std::string message;
    qb::io::async::coro_scheduler().spawn(second_receiver(ws, threw, message)); // parks first
    co_await qb::io::async::sleep(30ms);
    bool        threw2 = false;
    std::string message2;
    try {
        [[maybe_unused]] auto stolen = co_await ws.receive(); // must not park: one is already there
    } catch (std::logic_error const &e) {
        threw2   = true;
        message2 = e.what();
    }
    // Release the parked receiver by sending it something to take.
    say(ws, "release");
    co_await qb::io::async::sleep(120ms);

    const bool one_awaiter_ok = threw2 && message2.find("already waiting") != std::string::npos;
    qb::io::cout() << (one_awaiter_ok ? "[oneawaiter] two coroutines cannot park on the same client: the second receive() threw\n"
                                        "             std::logic_error rather than silently taking the first one's frame. One\n"
                                        "             consumer per client — fan out AFTER you have read, not by reading twice\n"
                                      : "[oneawaiter] UNEXPECTED: a second parked receive() was allowed\n");
    qb::io::cout() << "             (" << (message2.empty() ? "<no message>" : message2) << ")\n";

    // ---- 6. closing ---------------------------------------------------------------------------
    qb::http::ws::CloseResult closed = co_await ws.close_async(CloseStatus::Normal, "done for now");

    const bool close_ok = closed.ok;
    qb::io::cout() << (close_ok ? "[close] the client closed and the server echoed the code and reason back, which is what\n"
                                  "        close_async() waits for. It does NOT tear the TCP stream down — call disconnect()\n"
                                  "        if you want that too\n"
                                : "[close] UNEXPECTED: the close handshake did not complete\n");

    // ---- 7. reconnect on the SAME object ------------------------------------------------------
    qb::http::ws::ConnectResult again  = co_await ws.connect(uri);
    bool                        reused = false;
    if (again.ok) {
        say(ws, "second life");
        IncomingFrame after = co_await ws.receive();
        reused              = after.kind == IncomingFrame::Kind::Message && after.payload == "echo:second life";
    }

    qb::io::cout() << (reused ? "[reconnect] connect() on the SAME client works: it clears the buffer, the disconnected\n"
                                "            flag and any half-finished close before it dials again — so a client is\n"
                                "            reusable, unlike the pgsql notify consumer, whose receive() channel a\n"
                                "            disconnect closes for good\n"
                              : "[reconnect] UNEXPECTED: the client could not be reused after a close\n");

    ok = connect_ok && frames_ok && control_ok && buffer_ok && one_awaiter_ok && close_ok && reused;
    qb::io::cout() << "\n=== coro-client complete: connect, receive, control frames, backpressure, the\n"
                      "    one-awaiter rule, a close and a reuse ===\n"
                      "(for wss://, the only change is qb::http::ws::coro_client_secure)\n";
    co_return;
}

int
main() {
    qb::io::async::init();

    EchoServer server;
    if (server.transport().listen_v4(0, "127.0.0.1") != 0) {
        qb::io::cerr() << "[fatal] could not bind a loopback port\n";
        return 1;
    }
    server.start();

    const std::uint16_t port = server.transport().local_endpoint().port();
    qb::io::cout() << "[server] echo server up on 127.0.0.1:" << port << " (ephemeral, in this process)\n\n";

    bool running = true;
    bool ok      = false;
    qb::io::async::coro_scheduler().spawn(run_client(port, running, ok));
    qb::io::async::run_until(running);

    return ok ? 0 : 1;
}
