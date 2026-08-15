/**
 * @file examples/02-io/07-tls.cpp
 * @tier 02-io
 * @teaches TLS on its own terms — a `qb::io::ssl::Context` built fluently, a secure server and a
 *          secure client over ONE event loop, ALPN negotiated, the peer certificate inspected, and
 *          the beat that matters most: a client that does not trust the certificate FAILS to
 *          connect, because verification is on by default and switching it off is a decision with
 *          a name.
 * @demonstrates qb::io::ssl::Context, qb::io::ssl::Context::server, qb::io::ssl::Context::client,
 *               alpn, trust, verify, min_version, session_cache, qb::io::ssl::VerifyMode,
 *               qb::io::ssl::TlsVersion, qb::io::ssl::Certificate, qb::io::tcp::ssl::socket,
 *               qb::io::use<T>::tcp::ssl::server<S>, qb::io::use<T>::tcp::ssl::client<S>,
 *               get_negotiated_tls_version, get_negotiated_cipher_suite, get_alpn_selected_protocol,
 *               get_peer_certificate_details, set_insecure, qb::io::async::tcp::connect,
 *               qb::protocol::text::command, qb::io::async::init, qb::io::async::run_until, qb::io::uri
 * @prerequisites 02-io/03-tcp
 * @expect "=== qb-io: TLS, without reaching for OpenSSL ==="
 * @expect "[server] listening with TLS on 127.0.0.1:"
 * @expect "[trusting] handshake complete"
 * @expect "[trusting] server said: "
 * @expect "[untrusting] REFUSED: the certificate is not signed by anything this client trusts"
 * @expect "[insecure] connected with verification switched OFF — this is what set_insecure() buys"
 * @expect "=== done ==="
 *
 * WHY THIS PROGRAM EXISTS
 * -----------------------
 * Measured over the pre-3.0 corpus, `tcp::ssl::socket`, `tcp::ssl::listener` and `ssl::Context` had
 * **zero** direct uses: TLS was only ever reached through qbm-http. So "make my qb-io server speak
 * TLS" had no answer anywhere, and the honest-looking answer — add OpenSSL to your CMakeLists and
 * write an `SSL_CTX` — is wrong. It is one type and one alias.
 *
 * THE ONE-ALIAS SWITCH
 * --------------------
 * A plaintext session and a secure one differ by a single path component:
 *
 *     qb::io::use<Self>::tcp::server<Session>          // plaintext
 *     qb::io::use<Self>::tcp::ssl::server<Session>     // TLS
 *
 * Everything else — the protocol, the handlers, `operator<<`, the events — is unchanged, because
 * TLS is a TRANSPORT here (`transport::stcp` / `transport::saccept`) and the protocol layer never
 * sees it. The only new object is the `Context`.
 *
 * THE CONTEXT IS VALUE-SEMANTIC AND SECURE BY DEFAULT
 * ---------------------------------------------------
 * `ssl::Context` is a reference-counted handle around an `SSL_CTX`, configured fluently and copied
 * freely — every accepted connection shares the server's one context, and the underlying `SSL_CTX`
 * is freed exactly once, when the last copy AND the last `SSL` minted from it are gone. There is no
 * `SSL_CTX_free` in user code, ever.
 *
 *   `Context::client()`  TLS 1.2+, the system trust store, `VerifyMode::peer`. Secure with no
 *                        arguments — a client that does nothing is not a client that checks nothing.
 *   `Context::server(cert, key)`  loads and CROSS-CHECKS the pair; a mismatch is a falsy context
 *                        carrying `error()`, not a crash on the first connection.
 *
 * A misconfigured context is falsy and every subsequent setter is a no-op, so one `if (!ctx)` after
 * the whole chain reports the first thing that went wrong. That is checked below, out loud.
 *
 * VERIFICATION IS THE POINT, AND IT IS THE EASY THING TO GET WRONG
 * ---------------------------------------------------------------
 * The certificate here is self-signed, which is exactly the case where examples usually reach for
 * `set_insecure()` and quietly teach a generation of readers to ship it. This one does not: the
 * cert carries `CA:TRUE`, so the client passes it to `trust()` and performs a REAL chain check.
 * The verification target comes from the URI the connector was given — a literal IP is matched
 * against the certificate's IP SANs, a name against its DNS SANs (`sni()` is the override for when
 * the two must differ). Section 2 then proves the check is live by connecting a client that
 * trusts only the system store and watching the handshake fail. Section 3 shows `set_insecure()`
 * working, so its cost is visible rather than implied.
 *
 * Build (REQUIRES ssl — in an SSL-off build this target is not created at all):
 *   cmake --preset release
 *   cmake --build --preset release --target qb-example-io-tls
 * Run (from the binary's own directory: it loads resources/ssl/ with a relative path):
 *   cd build/presets/release/examples/02-io && ./qb-example-io-tls
 */

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <qb/io.h>
#include <qb/io/async.h>
#include <qb/io/protocol/text.h>
#include <qb/io/tcp/ssl/socket.h>
#include <qb/io/uri.h>

using namespace std::chrono_literals;

namespace {

bool          g_running  = true; // `async::run_until` loops WHILE this is true
std::uint16_t g_port     = 0;
int           g_done     = 0; // the three client attempts, counted
bool          g_verified = false;
bool          g_refused  = false;
bool          g_insecure = false;

constexpr const char *kCert = "resources/ssl/cert.pem";
constexpr const char *kKey  = "resources/ssl/key.pem";

void
step() {
    if (++g_done >= 3)
        g_running = false;
}

// ------------------------------------------------------------------------- the secure server

class TlsServer;

class TlsSession : public qb::io::use<TlsSession>::tcp::ssl::client<TlsServer> {
public:
    using Protocol = qb::protocol::text::command<TlsSession>;

    explicit TlsSession(IOServer &server)
        : client(server) {}

    void
    on(Protocol::message &&msg) {
        // By the time a framed message arrives the handshake is long finished, so this is the
        // natural place to look at what was negotiated.
        qb::io::cout() << "[server] session negotiated " << transport().get_negotiated_tls_version() << " / "
                       << transport().get_negotiated_cipher_suite() << ", alpn=" << transport().get_alpn_selected_protocol() << "\n";
        *this << "hello over " << transport().get_negotiated_tls_version() << Protocol::end;
    }
};

class TlsServer : public qb::io::use<TlsServer>::tcp::ssl::server<TlsSession> {};

// ------------------------------------------------------------------------- the secure client

class TlsClient : public qb::io::use<TlsClient>::tcp::ssl::client<> {
public:
    using Protocol = qb::protocol::text::command<TlsClient>;

    void
    on(Protocol::message &&msg) {
        qb::io::cout() << "[trusting] server said: " << msg.text << "\n";
        step();
    }
};

std::unique_ptr<TlsClient> g_client;

void
describe(const qb::io::ssl::Certificate &cert) {
    qb::io::cout() << "[trusting]   subject " << cert.subject << "\n";
    qb::io::cout() << "[trusting]   issuer  " << cert.issuer << (cert.subject == cert.issuer ? "   (self-signed: it is its own CA)" : "")
                   << "\n";
    qb::io::cout() << "[trusting]   sig     " << cert.signature_algorithm << ", serial " << cert.serial_number << "\n";
    qb::io::cout() << "[trusting]   names   ";
    for (const auto &name : cert.subject_alternative_names)
        qb::io::cout() << name << " ";
    qb::io::cout() << "\n";
}

} // namespace

int
main() {
    qb::io::cout() << "=== qb-io: TLS, without reaching for OpenSSL ===\n";
    qb::io::async::init();

    // --------------------------------------------------------------- the server's context
    // Fluent, and every setter is a no-op once the context has recorded an error — so ONE check
    // after the whole chain reports the first real problem instead of the last symptom.
    auto server_ctx =
        qb::io::ssl::Context::server(kCert, kKey).min_version(qb::io::ssl::TlsVersion::v1_2).alpn({"qb-demo/1", "http/1.1"}).session_cache(64);
    if (!server_ctx) {
        qb::io::cerr() << "[fatal] server context: " << server_ctx.error() << "\n"
                       << "        (run this from the binary's own directory — it loads " << kCert << " relatively)\n";
        return 1;
    }

    TlsServer server;
    server.transport().init(server_ctx); // one context, shared by reference count with every session
    if (server.transport().listen_v4(0, "127.0.0.1") != 0) {
        qb::io::cerr() << "[fatal] the TLS server could not bind\n";
        return 1;
    }
    server.start();
    g_port = server.transport().local_endpoint().port();
    qb::io::cout() << "[server] listening with TLS on 127.0.0.1:" << g_port << ", offering alpn qb-demo/1 and http/1.1\n\n";

    const qb::io::uri remote{"tcp://127.0.0.1:" + std::to_string(g_port)};

    // --------------------------------------------------- 1. a client that verifies, and passes
    // `trust()` adds the self-signed certificate as a trust anchor, which is what makes a REAL
    // chain check possible against a development certificate. `verify()` is spelled out even
    // though `peer` is already the client default: a security setting that matters is worth
    // reading in the code rather than inferring from a doc.
    auto client_ctx = qb::io::ssl::Context::client().trust(kCert).verify(qb::io::ssl::VerifyMode::peer).alpn({"qb-demo/1"});
    if (!client_ctx) {
        qb::io::cerr() << "[fatal] client context: " << client_ctx.error() << "\n";
        return 1;
    }

    // The connector runs the TCP connect AND the TLS handshake on this loop, so server and client
    // live in one thread. (A blocking `connect_v4()` on a secure socket would deadlock here: it
    // waits for a handshake the server cannot answer while this thread is inside it.)
    qb::io::async::tcp::connect<qb::io::tcp::ssl::socket>(
        qb::io::tcp::ssl::socket{client_ctx}, remote,
        [](qb::io::tcp::ssl::socket &&sock) {
            if (!sock.is_open()) {
                qb::io::cerr() << "[trusting] handshake FAILED — the trust anchor did not take\n";
                step();
                return;
            }
            g_verified = true;
            qb::io::cout() << "[trusting] handshake complete: " << sock.get_negotiated_tls_version() << " / "
                           << sock.get_negotiated_cipher_suite() << "\n";
            qb::io::cout() << "[trusting] alpn selected: " << sock.get_alpn_selected_protocol() << "\n";
            describe(sock.get_peer_certificate_details());

            g_client              = std::make_unique<TlsClient>();
            g_client->transport() = std::move(sock);
            g_client->start();
            *g_client << "ping" << TlsClient::Protocol::end;
        },
        5s, /*verify_peer=*/true);

    // ------------------------------------------- 2. a client that verifies, and is REFUSED
    // Same server, same certificate — only the trust store differs. This is the section that makes
    // section 1 mean something: without it, "it connected" proves only that something answered.
    qb::io::async::callback(
        []() {
            auto strict = qb::io::ssl::Context::client(); // system trust store only
            qb::io::async::tcp::connect<qb::io::tcp::ssl::socket>(
                qb::io::tcp::ssl::socket{strict}, qb::io::uri{"tcp://127.0.0.1:" + std::to_string(g_port)},
                [](qb::io::tcp::ssl::socket &&sock) {
                    if (sock.is_open()) {
                        qb::io::cerr() << "[untrusting] ACCEPTED an untrusted certificate — verification is not doing its job\n";
                    } else {
                        g_refused = true;
                        qb::io::cout() << "[untrusting] REFUSED: the certificate is not signed by anything this client trusts\n";
                    }
                    step();
                },
                5s, /*verify_peer=*/true);
        },
        250ms);

    // ------------------------------------------------ 3. the escape hatch, and what it costs
    // `set_insecure()` (and the connector's `verify_peer=false`) turns the check off for ONE
    // connection. It is the right call for a trusted channel you control end to end, and it is the
    // wrong call everywhere else — an attacker who can answer the connection can now be the server.
    qb::io::async::callback(
        []() {
            qb::io::tcp::ssl::socket loose;
            loose.set_insecure();
            qb::io::async::tcp::connect<qb::io::tcp::ssl::socket>(
                std::move(loose), qb::io::uri{"tcp://127.0.0.1:" + std::to_string(g_port)},
                [](qb::io::tcp::ssl::socket &&sock) {
                    if (sock.is_open()) {
                        g_insecure = true;
                        qb::io::cout() << "[insecure] connected with verification switched OFF — this is what set_insecure() buys,\n"
                                          "           and the whole of what it costs: nothing checked who answered\n";
                    } else {
                        qb::io::cerr() << "[insecure] could not connect at all\n";
                    }
                    step();
                },
                5s, /*verify_peer=*/false);
        },
        500ms);

    // A watchdog, so a stalled handshake is a short run with a visible shortfall rather than a hang.
    qb::io::async::callback([]() { g_running = false; }, 8s);
    qb::io::async::run_until(g_running);

    qb::io::cout() << "\n--- what the three attempts proved ---\n";
    qb::io::cout() << "[1] a trusting client verified the chain and exchanged a message: " << (g_verified ? "yes" : "NO") << "\n";
    qb::io::cout() << "[2] an untrusting client was refused by the handshake:            " << (g_refused ? "yes" : "NO") << "\n";
    qb::io::cout() << "[3] set_insecure() connected anyway:                              " << (g_insecure ? "yes" : "NO") << "\n";

    // All three are asserted. The second is the one that must never quietly stop holding: a run in
    // which the untrusted client CONNECTED would still print plenty and mean the opposite.
    if (!g_verified || !g_refused || !g_insecure) {
        qb::io::cerr() << "=== the TLS contract did not reproduce ===\n";
        return 1;
    }

    qb::io::cout() << "\n=== done ===\n";
    return 0;
}
