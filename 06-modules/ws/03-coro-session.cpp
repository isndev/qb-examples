/**
 * @file examples/06-modules/ws/03-coro-session.cpp
 * @tier 06-modules
 * @teaches A WebSocket session written as ONE coroutine instead of a bag of callbacks:
 *          `coro_session<Self, Server>`, `while (auto f = co_await next_frame())`, a handshake
 *          hook that negotiates a subprotocol or refuses the upgrade, and a graceful close.
 * @demonstrates qb::http::ws::coro_session<EchoSession, EchoServer>, next_frame, close_async,
 *               set_handshake_hook, set_pending_cap,
 *               qb::http::ws::IncomingFrame, qb::http::ws::MessageText, qb::http::ws::MessageBinary,
 *               qb::http::ws::CloseStatus, qb::http::ws::coro_client, connect, receive,
 *               negotiated_subprotocol, add_subprotocol,
 *               qb::io::use<EchoServer>::tcp::server<EchoSession>, local_endpoint,
 *               qb::io::async::init, qb::io::async::run_until, qb::io::async::coro_scheduler,
 *               qb::io::async::task<void>
 * @prerequisites 06-modules/ws/01-chat-server
 * @expect "[server] two servers up on ephemeral ports: one echo, one that refuses every upgrade"
 * @expect "[session] the whole conversation is ONE coroutine: next_frame() in a loop, a branch,"
 * @expect "[subproto] the handshake hook chose a subprotocol: the client offered two and the"
 * @expect "[reject] a hook that returns false REFUSES the upgrade, and the client's connect()"
 * @expect "[close] close_async() sent a Close and the client saw the code and the reason it was"
 * @expect "=== coro-session complete: one coroutine per connection, a handshake hook, and a"
 *
 * THE CALLBACK SESSION AND WHAT IT COSTS
 * --------------------------------------
 * `01-chat-server` writes a session the classical way: `on(message&&)`, `on(disconnected&&)`, and
 * whatever member variables are needed to remember where in the conversation this connection is.
 * That is the right shape when there IS no conversation — a pure echo, a broadcast relay. It is
 * the wrong shape the moment the protocol has an order: hello, then auth, then a stream, then a
 * goodbye. Every step of that order becomes a flag, and the flags become the protocol.
 *
 * `coro_session<Self, Server>` inverts it. You write `task<void> run()` and the base spawns it
 * once, when the upgrade succeeds; you `co_await next_frame()` where the protocol says "now the
 * client speaks". The order is the CONTROL FLOW, so there is nothing to keep in a member.
 *
 * THE THREE RULES OF THE BASE, AND ALL THREE MATTER
 * -------------------------------------------------
 * 1. `Self` MUST define `task<void> run()`. The base spawns it once and only once.
 * 2. The base holds a `shared_ptr` to the session for the whole life of that coroutine, so
 *    `*this` is valid at EVERY suspension point. The price of that guarantee is that `run()`
 *    must eventually return — a loop with no exit on `Close`/`Disconnected` leaks the session.
 * 3. Frames that arrive with nobody parked on `next_frame()` are BUFFERED, up to
 *    `set_pending_cap()` (1024 by default), and past that the OLDEST is dropped. A session that
 *    does slow work between frames is choosing to lose the beginning of a burst, not the end.
 *
 * THE FIXTURE LIVES IN A NAMED NAMESPACE, AND THAT IS A RULE
 * ----------------------------------------------------------
 * `coro_session` spawns a lambda into `CoroutineScheduler::invoke_owned_<F>`, whose frame is a
 * class DEFINED IN A QB HEADER. A session type declared in an anonymous namespace gives that
 * frame a field of no-linkage type, which g++-14 reports as `-Werror=subobject-linkage` — and
 * reports for only some of the instances, so the compiler is not the oracle here.
 * `qb/scripts/check-coro-fixture-linkage.py` is what makes it a rule; note that its scope today
 * is the four test trees and NOT `examples/`, so this file obeys the rule by hand.
 *
 * WHY THE CLIENT SIDE IS HERE AT ALL
 * -----------------------------------
 * A server example that nothing connects to proves only that it started. This program hosts both
 * servers and drives them with a `coro_client` on the SAME event loop, so every claim below is
 * measured end to end in one process with no network. The client is the subject of `04-coro-client`;
 * here it is the instrument.
 *
 * Build:
 *   cmake --preset release
 *   cmake --build --preset release --target qb-example-modules-ws-coro-session
 * Run:
 *   ./build/presets/release/examples/06-modules/ws/qb-example-modules-ws-coro-session
 */

#include <cstdint>
#include <string>
#include <qb/io/async.h>
#include <qb/io/async/coroutine.h>
#include <qbm/http/ws.h>
#include <qbm/http/ws/coro.h>

// NAMED, not anonymous — see the header block. `<file>_example` is the convention this tree uses.
namespace ws_coro_session_example {

using qb::http::ws::CloseStatus;
using qb::http::ws::IncomingFrame;

class EchoServer;

/**
 * The whole protocol, as one function. There is no member holding "where we are": the position in
 * the conversation IS the position in this coroutine.
 */
class EchoSession : public qb::http::ws::coro_session<EchoSession, EchoServer> {
public:
    using base = qb::http::ws::coro_session<EchoSession, EchoServer>;

    explicit EchoSession(EchoServer &server)
        : base(server) {
        // A slow session would rather drop the head of a burst than grow without bound. Set the
        // cap where the protocol's own burst size is, not where it feels safe.
        set_pending_cap(64);

        // Runs once, on the upgrade request, before the 101 is written. Returning false refuses
        // the upgrade; mutating `response` is how a subprotocol or an extra header is negotiated.
        set_handshake_hook([](EchoSession &, qb::http::Request &req, qb::http::Response &res) {
            const std::string &offered = req.header("Sec-WebSocket-Protocol");
            if (offered.find("qb.demo.v1") != std::string::npos)
                res.set_header("Sec-WebSocket-Protocol", "qb.demo.v1");
            return true;
        });
    }

    qb::io::async::task<void>
    run() {
        int seen = 0;
        while (true) {
            IncomingFrame frame = co_await next_frame();

            // Rule 2: this loop MUST be able to end, or the session's shared_ptr is never
            // released. Both peer-initiated endings arrive here.
            if (frame.kind == IncomingFrame::Kind::Disconnected || frame.kind == IncomingFrame::Kind::Close)
                co_return;
            if (frame.kind != IncomingFrame::Kind::Message)
                continue; // Ping/Pong: the protocol layer already answered the ping.

            ++seen;
            if (!frame.is_text) {
                qb::http::ws::MessageBinary echo;
                echo << frame.payload;
                *this << echo;
                continue;
            }
            if (frame.payload == "bye") {
                // The server-initiated close, awaited: it resumes once the peer echoes the Close
                // or the transport drops, so the reason really did reach the other end.
                // The awaiter is [[nodiscard]]: it yields a CloseResult saying whether the
                // handshake completed, and dropping it silently is exactly the mistake the
                // attribute exists to catch.
                [[maybe_unused]] auto closed = co_await close_async(CloseStatus::Normal, "as you wish");
                co_return;
            }
            qb::http::ws::MessageText reply;
            reply << (frame.payload == "count" ? std::to_string(seen) : "echo:" + frame.payload);
            *this << reply;
        }
    }
};

class EchoServer : public qb::io::use<EchoServer>::tcp::server<EchoSession> {
public:
    void
    on(IOSession &) {}
};

// The second server: its hook refuses everything, which is the other half of the hook's contract.
class ClosedServer;

class ClosedSession : public qb::http::ws::coro_session<ClosedSession, ClosedServer> {
public:
    using base = qb::http::ws::coro_session<ClosedSession, ClosedServer>;

    explicit ClosedSession(ClosedServer &server)
        : base(server) {
        set_handshake_hook([](ClosedSession &, qb::http::Request &, qb::http::Response &res) {
            // Set the status BEFORE returning false: the base sends this response as-is and then
            // drops the connection, so this is the only chance to say why.
            res.status() = qb::http::status::FORBIDDEN;
            res.body()   = "this endpoint is closed";
            return false;
        });
    }

    qb::io::async::task<void>
    run() {
        // Never spawned: the base only starts `run()` after a SUCCESSFUL upgrade.
        co_return;
    }
};

class ClosedServer : public qb::io::use<ClosedServer>::tcp::server<ClosedSession> {
public:
    void
    on(IOSession &) {}
};

} // namespace ws_coro_session_example

using namespace ws_coro_session_example;

qb::io::async::task<void>
drive(std::uint16_t echo_port, std::uint16_t closed_port, bool &running, bool &ok) {
    struct StopOnExit {
        bool &r;
        ~StopOnExit() {
            r = false;
        }
    } stop{running};

    const std::string echo_uri   = "ws://127.0.0.1:" + std::to_string(echo_port) + "/";
    const std::string closed_uri = "ws://127.0.0.1:" + std::to_string(closed_port) + "/";

    // ---- 1. the conversation ------------------------------------------------------------
    qb::http::ws::coro_client ws;
    ws.add_subprotocol("chat.v9"); // one the server does not know
    ws.add_subprotocol("qb.demo.v1");

    // `qb::io::uri(...)` spelled out: `connect` is overloaded on `uri const&` and on
    // `std::string_view`, and a `std::string` converts to BOTH by a user-defined conversion, so
    // passing one directly is ambiguous and does not compile. Build the uri, or pass a literal.
    auto connected = co_await ws.connect(qb::io::uri(echo_uri));
    if (!connected.ok) {
        qb::io::cerr() << "could not connect to the echo server\n";
        co_return;
    }

    auto say = [&ws](std::string const &text) {
        qb::http::ws::MessageText msg;
        msg << text;
        ws << msg;
    };

    say("hello");
    IncomingFrame first = co_await ws.receive();
    say("world");
    IncomingFrame second = co_await ws.receive();
    say("count");
    IncomingFrame counted = co_await ws.receive();

    const bool session_ok =
        first.kind == IncomingFrame::Kind::Message && first.payload == "echo:hello" && second.payload == "echo:world" && counted.payload == "3";
    qb::io::cout() << (session_ok ? "[session] the whole conversation is ONE coroutine: next_frame() in a loop, a branch,\n"
                                    "          a reply — and the message COUNT lives in a local of run(), not in a member,\n"
                                    "          because the coroutine frame is the session's state\n"
                                  : "[session] UNEXPECTED: the echo/count exchange did not come back as expected\n");
    qb::io::cout() << "          ('" << first.payload << "', '" << second.payload << "', count -> " << counted.payload << ")\n";

    const bool subproto_ok = ws.negotiated_subprotocol() == "qb.demo.v1";
    qb::io::cout() << (subproto_ok ? "[subproto] the handshake hook chose a subprotocol: the client offered two and the\n"
                                     "           server echoed back the ONE it understands, which is how a protocol version\n"
                                     "           is agreed before the first frame rather than negotiated in the first frame\n"
                                   : "[subproto] UNEXPECTED: the negotiated subprotocol was not qb.demo.v1\n");
    qb::io::cout() << "           (offered chat.v9 + qb.demo.v1, negotiated '" << ws.negotiated_subprotocol() << "')\n";

    // ---- 2. the graceful close, initiated by the SERVER ---------------------------------
    say("bye");
    IncomingFrame closing = co_await ws.receive();

    const bool close_ok = closing.kind == IncomingFrame::Kind::Close && closing.close_code == 1000 && closing.close_reason == "as you wish";
    qb::io::cout() << (close_ok ? "[close] close_async() sent a Close and the client saw the code and the reason it was\n"
                                  "        given — a WebSocket close is a FRAME with a payload, not a socket teardown, and\n"
                                  "        awaiting it is how you know the peer received it\n"
                                : "[close] UNEXPECTED: the server's close did not arrive with code 1000 and its reason\n");
    qb::io::cout() << "        (code " << closing.close_code << ", reason '" << closing.close_reason << "')\n\n";

    // ---- 3. the hook that refuses --------------------------------------------------------
    qb::http::ws::coro_client rejected;
    auto                      refused = co_await rejected.connect(qb::io::uri(closed_uri));

    const bool reject_ok = !refused.ok;
    qb::io::cout() << (reject_ok ? "[reject] a hook that returns false REFUSES the upgrade, and the client's connect()\n"
                                   "         reports ok == false rather than opening a socket that is not a WebSocket. Set\n"
                                   "         the response status inside the hook: the base sends it before it drops the link\n"
                                 : "[reject] UNEXPECTED: the rejecting server accepted the upgrade\n");

    ok = session_ok && subproto_ok && close_ok && reject_ok;
    qb::io::cout() << "\n=== coro-session complete: one coroutine per connection, a handshake hook, and a\n"
                      "    close whose reason reached the other end ===\n";
    co_return;
}

int
main() {
    qb::io::async::init();

    EchoServer   echo;
    ClosedServer closed;
    // Ephemeral ports: this program is its own client, so nothing outside needs to guess a
    // number, and two runs cannot collide.
    if (echo.transport().listen_v4(0, "127.0.0.1") != 0 || closed.transport().listen_v4(0, "127.0.0.1") != 0) {
        qb::io::cerr() << "[fatal] could not bind a loopback port\n";
        return 1;
    }
    echo.start();
    closed.start();

    const std::uint16_t echo_port   = echo.transport().local_endpoint().port();
    const std::uint16_t closed_port = closed.transport().local_endpoint().port();
    qb::io::cout() << "[server] two servers up on ephemeral ports: one echo, one that refuses every upgrade\n"
                   << "         (echo on " << echo_port << ", refusing on " << closed_port << ")\n\n";

    bool running = true;
    bool ok      = false;
    qb::io::async::coro_scheduler().spawn(drive(echo_port, closed_port, running, ok));
    qb::io::async::run_until(running);

    return ok ? 0 : 1;
}
