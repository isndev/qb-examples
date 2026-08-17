/**
 * @file examples/02-io/12-quic.cpp
 * @tier 02-io
 * @teaches QUIC as a qb-io transport in its own right, not as something HTTP/3 happens to sit
 *          on: one `endpoint` type for both roles, ALPN chosen during the handshake rather than
 *          by the port, independent streams over one connection, unreliable datagrams alongside
 *          them, and the refusal that matters — a peer whose ALPN does not match never connects.
 * @demonstrates qb::io::async::quic::server, qb::io::async::quic::connector,
 *               qb::io::use<T>::quic::session, qb::io::quic::settings, qb::io::quic::tls_config,
 *               qb::io::async::quic::event::connected, qb::io::async::quic::event::stream_started,
 *               qb::io::async::quic::event::stream_data, qb::io::async::quic::event::datagram,
 *               qb::io::async::quic::event::connection_closed, open_bidirectional_stream,
 *               send_stream_data, send_datagram, current_state, local_endpoint, set_settings,
 *               qb::io::async::init, qb::io::async::run_for, qb::io::cout
 * @prerequisites 02-io/04-udp, 02-io/07-tls
 * @expect "=== qb-io: QUIC ==="
 * @expect "[server] listening on quic://127.0.0.1:"
 * @expect "[client] connected, ALPN negotiated: "
 * @expect "[server] the stream request arrived intact: ping"
 * @expect "[client] stream reply: "
 * @expect "[server] datagram (unreliable, unordered, no retransmit): "
 * @expect "[mismatch] REFUSED: the ALPN offered was not one the server advertises"
 * @expect "[server] the listener is still up after the refusal"
 * @expect "=== done ==="
 *
 * WHY QUIC IS NOT "TCP WITH TLS BUILT IN"
 * ---------------------------------------
 * It runs on UDP, so everything TCP gives you for free — ordering, retransmission, congestion
 * control, the handshake — QUIC implements itself, and having implemented it, offers choices TCP
 * cannot:
 *
 *   INDEPENDENT STREAMS   One connection carries many streams, each ordered and reliable on its
 *                         own. A lost packet stalls the stream that lost it and nothing else.
 *                         Over TCP a single lost segment stalls EVERY multiplexed exchange on the
 *                         socket, which is head-of-line blocking and the reason HTTP/2 over TCP
 *                         still queues behind itself.
 *   DATAGRAMS             The same connection also carries unreliable, unordered payloads that are
 *                         never retransmitted — telemetry, voice, a position update whose value is
 *                         gone by the time a retransmit arrives. On TCP you would open a second,
 *                         unencrypted UDP socket and secure it yourself.
 *   TLS IS NOT A LAYER    There is no plaintext QUIC. The transport handshake and the TLS 1.3
 *                         handshake are the same exchange, which is why `listen()` takes a
 *                         certificate and why ALPN is not optional here.
 *
 * ALPN IS THE CONTRACT, AND IT IS ENFORCED
 * ----------------------------------------
 * A TCP server can accept a connection and only then discover it does not speak the client's
 * protocol. QUIC settles it inside the handshake: the client offers a list, the server picks one
 * it advertises, and if the sets do not intersect the connection is never established. That is
 * the third act below — a client offering `h3` against a server advertising `qb-demo/1` is driven
 * to `closed` and the server's `on(connected)` never fires. It is worth demonstrating because it
 * is the failure people meet first and misread as a certificate problem.
 *
 * ONE LOOP, BOTH ROLES
 * --------------------
 * Server and client live on the same `qb::io::async::listener` here, which is a property of qb-io
 * rather than a shortcut for the demo: an endpoint is a watcher on a UDP socket, so a program can
 * hold as many as it likes on one thread. That is also what lets this file assert both sides of
 * every exchange instead of trusting one of them.
 *
 * Build (needs a QUIC-enabled build — the target is not created otherwise):
 *   cmake --preset release
 *   cmake --build --preset release --target qb-example-io-quic
 * Run:
 *   ./build/presets/release/examples/02-io/qb-example-io-quic
 */

#include <chrono>
#include <filesystem>
#include <string>
#include <qb/io.h>
#include <qb/io/async.h>

using namespace std::chrono_literals;
// Declarations below spell the full path once, where a reader looks for the type; the alias is
// for the handful of later mentions where the full path would only add noise.
namespace quic = qb::io::async::quic;

/// The ALPN this demo speaks. Both sides must name it or there is no connection at all.
static constexpr const char *kAlpn = "qb-demo/1";

// A stream session is what the server's io_handler spawns per remote stream. This one carries no
// state: the server below reads payloads straight off `event::stream_data`, which is the simplest
// shape and the right one when a stream is a single request rather than a byte pipe.
class DemoStreamSession : public qb::io::use<DemoStreamSession>::quic::session {
public:
    using Base = qb::io::use<DemoStreamSession>::quic::session;
    using Base::Base;
};

// ---------------------------------------------------------------------------------------
// The server. `quic::server<Derived, StreamSession>` is an endpoint plus a session table; every
// `on(event::…)` below is optional — the facade only dispatches the ones you declare.
// ---------------------------------------------------------------------------------------
class DemoServer : public qb::io::async::quic::server<DemoServer, DemoStreamSession> {
public:
    int         handshakes = 0;
    int         refusals   = 0;
    std::string last_stream;
    std::string last_datagram;

    void
    on(qb::io::async::quic::event::connected const &e) {
        ++handshakes;
        qb::io::cout() << "[server] handshake complete, connection " << e.connection_id << ", ALPN '" << e.negotiated_alpn << "'\n";
    }

    void
    on(qb::io::async::quic::event::stream_started const &e) {
        qb::io::cout() << "[server] stream " << e.id << " opened by the peer\n";
    }

    void
    on(qb::io::async::quic::event::stream_data const &e) {
        last_stream.assign(e.payload);
        qb::io::cout() << "[server] stream " << e.id << " carried: " << last_stream << (e.fin ? " (with FIN)" : "") << "\n";
        if (last_stream == "ping")
            qb::io::cout() << "[server] the stream request arrived intact: ping\n";
        // Answer on the SAME stream. Each stream is independently ordered, so this reply cannot be
        // delayed by loss on any other stream of this connection.
        send_stream_data(e.connection_id, e.id, "pong", true);
    }

    void
    on(qb::io::async::quic::event::datagram const &e) {
        last_datagram.assign(e.payload);
        qb::io::cout() << "[server] datagram (unreliable, unordered, no retransmit): " << last_datagram << "\n";
    }

    void
    on(qb::io::async::quic::event::connection_closed const &e) {
        qb::io::cout() << "[server] connection " << e.connection_id << " closed\n";
    }
};

// ---------------------------------------------------------------------------------------
// The client. `connector<Derived>` with no stream-session type is the plain endpoint facade:
// streams are opened and read by id, which is all this exchange needs.
// ---------------------------------------------------------------------------------------
class DemoClient : public qb::io::async::quic::connector<DemoClient> {
public:
    bool        connected = false;
    std::string alpn;
    std::string reply;

    void
    on(quic::event::connected const &e) {
        connected = true;
        alpn      = e.negotiated_alpn;
        qb::io::cout() << "[client] connected, ALPN negotiated: " << alpn << "\n";
    }

    void
    on(quic::event::stream_data const &e) {
        reply.assign(e.payload);
        qb::io::cout() << "[client] stream reply: " << reply << "\n";
    }
};

// A client that offers an ALPN the server does not advertise. Nothing else differs.
class MismatchedClient : public qb::io::async::quic::connector<MismatchedClient> {
public:
    bool connected = false;

    void
    on(quic::event::connected const &) {
        connected = true; // must never happen
    }
};

/// Drive the shared loop until `pred` holds or the budget runs out. Returns what pred said.
template <typename Pred>
static bool
pump_until(Pred pred, std::chrono::milliseconds budget = 5s) {
    const auto deadline = std::chrono::steady_clock::now() + budget;
    while (std::chrono::steady_clock::now() < deadline) {
        if (pred())
            return true;
        qb::io::async::run_for(5ms);
    }
    return pred();
}

int
main() {
    qb::io::cout() << "=== qb-io: QUIC ===\n";

    const std::filesystem::path cert = "resources/ssl/cert.pem";
    const std::filesystem::path key  = "resources/ssl/key.pem";
    if (!std::filesystem::exists(cert) || !std::filesystem::exists(key)) {
        qb::io::cout() << "[fatal] run this from the directory holding resources/ssl/ — the build stages it "
                          "next to the binary\n";
        return 1;
    }

    qb::io::async::init();

    // Datagrams are OFF by default and are negotiated in the handshake, so BOTH ends have to ask
    // for them: a client that enables them against a server that did not simply never gets any.
    qb::io::quic::settings settings;
    settings.enable_datagrams        = true;
    settings.max_datagram_frame_size = 1200;

    // ---- 1. the handshake -------------------------------------------------------------
    DemoServer server;
    server.set_settings(settings);
    if (!server.listen(qb::io::uri{"quic://127.0.0.1:0"}, cert, key, {kAlpn})) {
        qb::io::cout() << "[fatal] the QUIC endpoint could not bind\n";
        return 1;
    }
    const auto port = server.local_endpoint().port();
    qb::io::cout() << "[server] listening on quic://127.0.0.1:" << port << ", advertising ALPN '" << kAlpn << "'\n";

    // The certificate here is self-signed, so the demo client does not verify the chain. In a real
    // client `verify_peer` stays true and `server_name` is what the certificate must match.
    qb::io::quic::tls_config tls;
    tls.server_name = "localhost";
    tls.verify_peer = false;

    const auto uri = std::string{"quic://127.0.0.1:"} + std::to_string(port);

    DemoClient client;
    client.set_settings(settings);
    if (!client.connect(qb::io::uri{uri}, tls, {kAlpn})) {
        qb::io::cout() << "[fatal] the client could not start its handshake\n";
        return 1;
    }
    if (!pump_until([&] { return client.connected && server.handshakes == 1; })) {
        qb::io::cout() << "[fatal] the handshake never completed\n";
        return 1;
    }

    // ---- 2. a stream, then a datagram, on the same connection --------------------------
    const auto stream = client.open_bidirectional_stream();
    client.send_stream_data(stream.id(), "ping", true);
    if (!pump_until([&] { return server.last_stream == "ping" && client.reply == "pong"; })) {
        qb::io::cout() << "[fatal] the stream round-trip did not complete\n";
        return 1;
    }

    client.send_datagram("telemetry");
    if (!pump_until([&] { return server.last_datagram == "telemetry"; })) {
        qb::io::cout() << "[fatal] the datagram never arrived — is enable_datagrams set on BOTH ends?\n";
        return 1;
    }
    qb::io::cout() << "    the stream was retransmitted until acknowledged; the datagram was not, and would "
                      "simply be gone had it been lost — that choice is the point of having both\n";
    qb::io::cout() << "    client sent " << client.stats().datagrams_sent << " datagram(s), server received "
                   << server.stats().datagrams_received << "\n";

    // ---- 3. THE BEAT THAT MATTERS: an ALPN mismatch never connects ---------------------
    // The server advertises `qb-demo/1`. This client offers `h3` and nothing else, so the sets do
    // not intersect and the handshake is refused INSIDE itself — there is no connection to accept
    // and then reject. Read as: the protocol is agreed before the first byte of application data.
    MismatchedClient wrong;
    wrong.set_settings(settings);
    if (!wrong.connect(qb::io::uri{uri}, tls, {"h3"})) {
        qb::io::cout() << "[fatal] the mismatched client could not even start\n";
        return 1;
    }
    const bool refused = pump_until([&] { return wrong.current_state() == quic::endpoint::state::closed; });
    if (!refused || wrong.connected) {
        qb::io::cout() << "[fatal] an ALPN-mismatched client was NOT refused — that would mean the protocol is "
                          "negotiable after the fact, which it is not\n";
        return 1;
    }
    qb::io::cout() << "[mismatch] REFUSED: the ALPN offered was not one the server advertises, so the "
                      "connection was never established\n";

    // And the refusal is local to that peer: the listener and the first connection are untouched.
    if (server.handshakes != 1) {
        qb::io::cout() << "[fatal] the server counted a handshake it should never have seen\n";
        return 1;
    }
    qb::io::cout() << "[server] the listener is still up after the refusal, with " << server.stats().active_connections
                   << " active connection(s) — one bad peer is not a server outage\n";

    // ---- 4. teardown ------------------------------------------------------------------
    wrong.close();
    client.close();
    server.close();
    qb::io::async::run_for(50ms);
    qb::io::async::listener::current.clear();

    qb::io::cout() << "=== done ===\n";
    return 0;
}
