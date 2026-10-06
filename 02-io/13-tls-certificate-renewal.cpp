/**
 * @file examples/02-io/13-tls-certificate-renewal.cpp
 * @tier 02-io
 * @teaches A TLS server renews its certificate while it serves -- `reload_context()` on its listener -- with
 *          no restart and no dropped connection: the next handshake presents the renewed certificate, a
 *          session opened before it keeps talking, and a renewal left half-done is refused while the
 *          certificate in service goes on serving.
 * @demonstrates reload_context, qb::io::ssl::Context::server, qb::io::ssl::Context::client, trust, error,
 *               get_peer_certificate_details, qb::io::tcp::ssl::socket, qb::io::use<T>::tcp::ssl::server<S>,
 *               qb::io::use<T>::tcp::ssl::client<S>, qb::io::async::tcp::connect, qb::protocol::text::command,
 *               qb::io::async::run_until
 * @prerequisites 02-io/07-tls
 * @expect "=== qb-io: renewing a TLS certificate without a restart ==="
 * @expect "[server] serving the original certificate on 127.0.0.1:"
 * @expect "[early] connected; the server presented serial "
 * @expect "[strict] REFUSED before the renewal: it trusts only the renewed certificate"
 * @expect "[renewal] half-done (new certificate, old key): REFUSED -- "
 * @expect "[renewal] complete: installed for the next connections"
 * @expect "[strict] connected after the renewal; the server presented serial "
 * @expect "[early] the session opened before the renewal still answers: "
 * @expect "=== done ==="
 *
 * WHY THIS PROGRAM EXISTS
 * -----------------------
 * Every certificate expires. A server whose only way to present a renewed one is a restart drops every
 * connection it holds, every time -- so renewals get postponed, and postponed renewals are how
 * certificates expire in production. Since 3.3 the listener takes a replacement context while it
 * serves (Huly QB-205):
 *
 *     if (!server.transport().reload_context(qb::io::ssl::Context::server(cert, key)))
 *         ...   // the renewed files did not load: the certificate in service goes on serving
 *
 * WHAT THE RELOAD DOES, AND WHAT IT LEAVES ALONE
 * ----------------------------------------------
 * Each accept mints its connection's `SSL` from the listener's context, and an `SSL` holds a reference
 * on the `SSL_CTX` it came from. So the reload changes exactly one thing -- the context the NEXT accept
 * uses -- and the connections already open keep theirs, down to one whose handshake has not even run
 * yet. The old context is freed with the last of them. Nothing is dropped and nothing renegotiates.
 *
 * A RENEWAL RUNS ON DISK FIRST, AND CAN STOP HALF-WAY
 * --------------------------------------------------
 * Renewal tools replace the files where they are -- same paths, new content -- and a copy interrupted
 * between the certificate and the key leaves a pair that does not match. `Context::server()` cross-checks
 * the pair and comes back falsy, and `reload_context()` refuses a falsy context with `false`: the server
 * keeps presenting the certificate it had instead of failing every handshake that follows. That is the
 * difference with `init(Context)`, which installs whatever it is given and is the call for setup.
 * This program plays that sequence: it serves a working copy of the demo certificate from a scratch
 * directory, renews it there in two steps, and reloads after each.
 *
 * WHO PROVES WHAT
 * ---------------
 *   [early]   trusts the ORIGINAL certificate; connects before the renewal and keeps its session open
 *             across it, then talks again afterwards.
 *   [strict]  trusts only the RENEWED certificate; refused before the renewal, accepted after it. A
 *             verifying client is the honest witness: it fails on the wrong certificate instead of
 *             printing whichever one it got.
 *
 * Build the replacement WHOLE (the listener's raw setters wrote into the previous context and do not
 * carry over) and call `reload_context()` on the thread that accepts -- here, the one loop. Loading the
 * files may run on the offload pool (`03-coroutines/15-offloading-blocking-work`).
 *
 * Build (REQUIRES ssl -- in an SSL-off build this target is not created at all):
 *   cmake --preset release
 *   cmake --build --preset release --target qb-example-io-tls-certificate-renewal
 * Run (from the binary's own directory: it reads resources/ssl/ with a relative path):
 *   cd build/presets/release/examples/02-io && ./qb-example-io-tls-certificate-renewal
 */

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <system_error>
#include <qb/io.h>
#include <qb/io/async.h>
#include <qb/io/protocol/text.h>
#include <qb/io/tcp/ssl/socket.h>
#include <qb/io/uri.h>

using namespace std::chrono_literals;
namespace fs = std::filesystem;

namespace {

constexpr const char *kOriginalCert = "resources/ssl/cert.pem";
constexpr const char *kOriginalKey  = "resources/ssl/key.pem";
constexpr const char *kRenewedCert  = "resources/ssl/renewed-cert.pem";
constexpr const char *kRenewedKey   = "resources/ssl/renewed-key.pem";

bool          g_running = true; // `async::run_until` loops WHILE this is true
std::uint16_t g_port    = 0;
fs::path      g_live_cert; // the files the server is configured with -- the ones a renewal replaces
fs::path      g_live_key;
std::string   g_original_serial;

bool g_early_before   = false;
bool g_strict_refused = false;
bool g_half_refused   = false;
bool g_installed      = false;
bool g_strict_after   = false;
bool g_early_after    = false;

void renew_and_retry();

// ------------------------------------------------------------------------- the server

class RenewingServer;

class Session : public qb::io::use<Session>::tcp::ssl::client<RenewingServer> {
public:
    using Protocol = qb::protocol::text::command<Session>;

    explicit Session(IOServer &server)
        : client(server) {}

    void
    on(Protocol::message &&msg) {
        *this << "you said " << msg.text << Protocol::end;
    }
};

class RenewingServer : public qb::io::use<RenewingServer>::tcp::ssl::server<Session> {};

RenewingServer *g_server = nullptr;

// ------------------------------------------------------------------------- the early client

class EarlyClient : public qb::io::use<EarlyClient>::tcp::ssl::client<> {
public:
    using Protocol = qb::protocol::text::command<EarlyClient>;

    void
    on(Protocol::message &&msg) {
        if (!g_early_before) {
            g_early_before = true;
            qb::io::cout() << "[early] server said: " << msg.text << " -- the session stays open\n\n";
            renew_and_retry();
            return;
        }
        g_early_after = true;
        qb::io::cout() << "[early] the session opened before the renewal still answers: " << msg.text << "\n";
        g_running = false;
    }
};

std::unique_ptr<EarlyClient> g_early;

qb::io::uri
remote() {
    return qb::io::uri{"tcp://127.0.0.1:" + std::to_string(g_port)};
}

// A client that trusts exactly one certificate, and verifies the server against it.
qb::io::tcp::ssl::socket
trusting(const char *certificate) {
    return qb::io::tcp::ssl::socket{qb::io::ssl::Context::client().trust(certificate)};
}

bool
replace_file(const char *from, const fs::path &to) {
    std::error_code ec;
    fs::copy_file(from, to, fs::copy_options::overwrite_existing, ec);
    if (ec)
        qb::io::cerr() << "[fatal] cannot copy " << from << " to " << to.string() << ": " << ec.message() << "\n";
    return !ec;
}

// ------------------------------------------------------------------------- 3. the strict client, after

void
strict_after_the_renewal() {
    qb::io::async::tcp::connect<qb::io::tcp::ssl::socket>(
        trusting(kRenewedCert), remote(),
        [](qb::io::tcp::ssl::socket &&sock) {
            if (!sock.is_open()) {
                qb::io::cerr() << "[strict] REFUSED after the renewal -- the next connection did not get the renewed certificate\n";
                g_running = false;
                return;
            }
            const auto serial = sock.get_peer_certificate_details().serial_number;
            g_strict_after    = serial != g_original_serial;
            qb::io::cout() << "[strict] connected after the renewal; the server presented serial " << serial << "\n";
            sock.disconnect();
            // The session the early client opened before any of this: it talks again.
            *g_early << "after" << EarlyClient::Protocol::end;
        },
        5s, /*verify_peer=*/true);
}

// ------------------------------------------------------------------------- 2. the renewal, in two steps

void
renew_and_retry() {
    // The strict client first: it trusts only the renewed certificate, which nobody presents yet.
    qb::io::async::tcp::connect<qb::io::tcp::ssl::socket>(
        trusting(kRenewedCert), remote(),
        [](qb::io::tcp::ssl::socket &&sock) {
            if (sock.is_open()) {
                qb::io::cerr() << "[strict] ACCEPTED before the renewal -- it trusts a certificate nobody presents yet\n";
                g_running = false;
                return;
            }
            g_strict_refused = true;
            qb::io::cout() << "[strict] REFUSED before the renewal: it trusts only the renewed certificate\n\n";

            // Step 1 of the renewal lands: the new certificate is on disk, the key is still the old one.
            if (!replace_file(kRenewedCert, g_live_cert)) {
                g_running = false;
                return;
            }
            const auto half = qb::io::ssl::Context::server(g_live_cert, g_live_key);
            if (g_server->transport().reload_context(half)) {
                qb::io::cerr() << "[renewal] a mismatched pair was INSTALLED -- every handshake from now on would fail\n";
                g_running = false;
                return;
            }
            g_half_refused = true;
            qb::io::cout() << "[renewal] half-done (new certificate, old key): REFUSED -- " << half.error() << "\n"
                           << "          the original certificate goes on serving\n";

            // Step 2 lands: the pair is whole again. Built WHOLE, then installed for the next accepts.
            if (!replace_file(kRenewedKey, g_live_key)) {
                g_running = false;
                return;
            }
            const auto renewed = qb::io::ssl::Context::server(g_live_cert, g_live_key);
            g_installed        = g_server->transport().reload_context(renewed);
            if (!g_installed) {
                qb::io::cerr() << "[renewal] the complete renewal was refused: " << renewed.error() << "\n";
                g_running = false;
                return;
            }
            qb::io::cout() << "[renewal] complete: installed for the next connections\n\n";
            strict_after_the_renewal();
        },
        5s, /*verify_peer=*/true);
}

} // namespace

int
main() {
    qb::io::cout() << "=== qb-io: renewing a TLS certificate without a restart ===\n";
    qb::io::async::init();

    // A working copy of the demo pair, in a scratch directory: the files a renewal will replace in place.
    std::error_code ec;
    const fs::path  live =
        fs::temp_directory_path() / ("qb-example-tls-renewal-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(live, ec);
    g_live_cert = live / "cert.pem";
    g_live_key  = live / "key.pem";
    if (ec || !replace_file(kOriginalCert, g_live_cert) || !replace_file(kOriginalKey, g_live_key)) {
        qb::io::cerr() << "[fatal] cannot stage the working copy (run this from the binary's own directory)\n";
        return 1;
    }

    const auto original = qb::io::ssl::Context::server(g_live_cert, g_live_key);
    if (!original) {
        qb::io::cerr() << "[fatal] server context: " << original.error() << "\n";
        return 1;
    }

    RenewingServer server;
    g_server = &server;
    server.transport().init(original);
    if (server.transport().listen_v4(0, "127.0.0.1") != 0) {
        qb::io::cerr() << "[fatal] the TLS server could not bind\n";
        return 1;
    }
    server.start();
    g_port = server.transport().local_endpoint().port();
    qb::io::cout() << "[server] serving the original certificate on 127.0.0.1:" << g_port << "\n\n";

    // ------------------------------------------------------------------- 1. the early client
    qb::io::async::tcp::connect<qb::io::tcp::ssl::socket>(
        trusting(kOriginalCert), remote(),
        [](qb::io::tcp::ssl::socket &&sock) {
            if (!sock.is_open()) {
                qb::io::cerr() << "[early] handshake FAILED against the original certificate\n";
                g_running = false;
                return;
            }
            g_original_serial = sock.get_peer_certificate_details().serial_number;
            qb::io::cout() << "[early] connected; the server presented serial " << g_original_serial << "\n";
            g_early              = std::make_unique<EarlyClient>();
            g_early->transport() = std::move(sock);
            g_early->start();
            *g_early << "before" << EarlyClient::Protocol::end;
        },
        5s, /*verify_peer=*/true);

    // A watchdog, so a stalled step is a short run with a visible shortfall rather than a hang.
    qb::io::async::callback([]() { g_running = false; }, 10s);
    qb::io::async::run_until(g_running);

    g_early.reset();
    fs::remove_all(live, ec);

    qb::io::cout() << "\n--- what the run proved ---\n";
    qb::io::cout() << "[1] a session opened before the renewal talked before and after it:   "
                   << (g_early_before && g_early_after ? "yes" : "NO") << "\n";
    qb::io::cout() << "[2] a client trusting only the renewed certificate was refused before: " << (g_strict_refused ? "yes" : "NO") << "\n";
    qb::io::cout() << "[3] a half-done renewal was refused, the original kept serving:       " << (g_half_refused ? "yes" : "NO") << "\n";
    qb::io::cout() << "[4] the complete renewal reached the next connection, new serial:     " << (g_installed && g_strict_after ? "yes" : "NO")
                   << "\n";
    if (!(g_early_before && g_early_after && g_strict_refused && g_half_refused && g_installed && g_strict_after)) {
        qb::io::cerr() << "=== the renewal contract did not reproduce ===\n";
        return 1;
    }

    qb::io::cout() << "\n=== done ===\n";
    return 0;
}
