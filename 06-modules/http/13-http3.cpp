/**
 * @file examples/06-modules/http/13-http3.cpp
 * @tier 06-modules
 * @teaches One server on two transports: HTTP/2 over TCP and HTTP/3 over QUIC behind a single
 *          dual-stack object, with Alt-Svc advertising the upgrade.
 * @demonstrates qb::http::dual_stack_server<>, qb::http::make_dual_stack_server<>,
 *               qb::http2::DefaultSession, qb::http3::DefaultSession,
 *               qb::http::static_files_middleware<S>, qb::http::CorsMiddleware<S>,
 *               qb::http::LoggingMiddleware<S>, router(), use, compile, listen
 * @prerequisites 06-modules/http/12-http2
 * @expect "Initializing HTTP/2 + HTTP/3 dual-stack server..."
 * @expect "🚀 QB HTTP/2 + HTTP/3 dual-stack server"
 * @brief HTTP/3 (QUIC) server you can actually open in a browser — via a dual stack.
 *
 * The catch with HTTP/3 and browsers: a browser NEVER speaks h3 first. It connects over
 * TCP (HTTP/2 or HTTP/1.1), sees an `Alt-Svc: h3=...` header, and only THEN upgrades to
 * HTTP/3 for subsequent requests. A pure-h3 (UDP-only) server therefore can't even be
 * loaded by a browser — the initial TCP connection has nothing to talk to.
 *
 * So this example serves the SAME application over both stacks using
 * `qb::http::dual_stack_server`: HTTP/2 (TCP/TLS, ALPN h2 + http/1.1) and HTTP/3 (QUIC/UDP,
 * ALPN h3), with one set of routes mirrored onto both. The HTTP/2 responses advertise
 * `Alt-Svc: h3=":8444"`, so a browser:
 *   1. loads the page over HTTP/2 (TCP),
 *   2. reads Alt-Svc,
 *   3. transparently upgrades to HTTP/3 (QUIC) for the API calls.
 * Open DevTools → Network → the *Protocol* column flips to `h3`. This is exactly how h3 is
 * deployed in production (behind an Alt-Svc advertisement).
 *
 * Structurally it is still the HTTP/2 example's sibling: same routing engine, same
 * static-files middleware + interactive frontend. What it demonstrates is the h3 upgrade
 * path and the QUIC transport underneath.
 *
 * Build/run: needs qbm-http compiled with HTTP/3 (SSL + QUIC + nghttp3). When h3 is
 * unavailable the example still links and explains why (fallback main below).
 *
 * @author qb - C++ Actor Framework
 * @copyright Copyright (c) 2011-2026 qb - isndev (cpp.actor)
 * Licensed under the Apache License, Version 2.0 (http://www.apache.org/licenses/LICENSE-2.0)
 */

#include <qbm/http/http.h>

#ifdef QBM_HTTP_HAS_HTTP3

#include <qb/io/system/file.h> // qb::io::sys::resolve_resource
#include <qb/main.h>
#include <qb/system/parse.h> // qb::to_number

#include <qbm/http/2/http2.h>
#include <qbm/http/3/dual_stack.h>
#include <qbm/http/middleware/all.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <string>

using qb::json;

static long long
now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

// Strict, non-throwing bounded-int parse (qb::to_number): non-numeric / out-of-range /
// trailing garbage → false, so a handler answers 400 instead of a parse-throw-to-500.
static bool
parse_bounded_int(std::string_view s, int lo, int hi, int &out) {
    const std::optional<long> v = qb::to_number<long>(s);
    if (!v || *v < lo || *v > hi)
        return false;
    out = static_cast<int>(*v);
    return true;
}

template <typename Ctx>
static void
send_bad_request(Ctx &ctx, const char *message) {
    qb::json err;
    err["error"]             = message;
    ctx->response().status() = qb::http::Status::BAD_REQUEST;
    ctx->response().add_header("Content-Type", "application/json");
    ctx->response().body() = err;
    ctx->complete();
}

/**
 * @class Http3DualServer
 * @brief Actor owning a dual-stack (HTTP/2 over TCP + HTTP/3 over QUIC) server.
 *
 * Both stacks share one set of routes (via the dual-stack router facade) and the same
 * TLS certificate. Middleware is installed on each underlying router (they use distinct
 * session types). The HTTP/2 side advertises h3 with Alt-Svc so browsers upgrade.
 */
class Http3DualServer : public qb::Actor {
    using DualServer = qb::http::dual_stack_server<>; // default h2 + h3 sessions

    std::unique_ptr<DualServer> _server;
    std::filesystem::path       _cert_file{"resources/ssl/cert.pem"};
    std::filesystem::path       _key_file{"resources/ssl/key.pem"};
    std::filesystem::path       _static_root{"resources/http3"};
    std::uint16_t               _port = 8444;

public:
    Http3DualServer() = default;

    qb::io::async::task<bool>
    onInit() override {
        // Shutdown wiring. Event dispatch is by SUBSCRIPTION, not by vtable: qb::Actor's
        // constructor already subscribed its own default handlers for these two, and
        // re-registering here is what replaces them with OURS. Without these two lines
        // the handlers below compile, are never called, and their cleanup is lost.
        registerEvent<qb::KillEvent>(*this);
        registerEvent<qb::SignalEvent>(*this);

        std::cout << "Initializing HTTP/2 + HTTP/3 dual-stack server..." << std::endl;
        if (!resolve_paths())
            co_return false;

        _server = qb::http::make_dual_stack_server<>();

        // Middleware is per-router (the two stacks have distinct session types).
        install_middleware<qb::http2::DefaultSession>(_server->http2_server().router(), /*advertise_h3=*/true);
        install_middleware<qb::http3::DefaultSession>(_server->http3_server().router(), /*advertise_h3=*/true);

        setup_routes();
        _server->router().compile();

        co_return start_listening();
    }

    // Ctrl+C / SIGTERM. qb::Main::start() installs both, so every actor receives a
    // qb::SignalEvent; routing it into the KillEvent below keeps ONE shutdown path.
    void
    on(const qb::SignalEvent &event) noexcept {
        std::cout << "Signal " << event.signum << " received." << std::endl;
        push<qb::KillEvent>(id());
    }

    void
    on(qb::KillEvent const &) noexcept {
        std::cout << "Dual-stack server shutting down (HTTP/3 GOAWAY + HTTP/2 close)..." << std::endl;
        if (_server)
            _server->close();
        qb::Actor::kill();
    }

private:
    bool
    resolve_paths() {
        _cert_file   = qb::io::sys::resolve_resource(_cert_file);
        _key_file    = qb::io::sys::resolve_resource(_key_file);
        _static_root = qb::io::sys::resolve_resource(_static_root);
        if (!std::filesystem::exists(_cert_file) || !std::filesystem::exists(_key_file)) {
            std::cerr << "SSL certificate not found (" << _cert_file
                      << "). Both stacks are TLS-only; run "
                         "from the build output directory (resources/ssl is staged there)."
                      << std::endl;
            return false;
        }
        std::cout << "TLS certificate: " << _cert_file << "\nStatic frontend: " << _static_root << std::endl;
        return true;
    }

    template <typename Ctx>
    void
    serve_file(Ctx ctx, std::filesystem::path const &path, char const *content_type) {
        std::error_code ec;
        if (!std::filesystem::exists(path, ec)) {
            ctx->response().status() = qb::http::Status::NOT_FOUND;
            ctx->response().add_header("Content-Type", "text/plain; charset=utf-8");
            ctx->response().body() = "Not found: " + path.filename().string();
            ctx->complete();
            return;
        }
        std::ifstream f(path, std::ios::binary);
        std::string   content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        ctx->response().body() = std::move(content);
        ctx->response().add_header("Content-Type", content_type);
        ctx->complete();
    }

    // Install the shared middleware chain on one router. `Session` selects the concrete
    // session type of that stack (h2 vs h3). `advertise_h3` adds the Alt-Svc header so a
    // browser on the TCP/HTTP-2 side learns to upgrade to HTTP/3.
    template <typename Session, typename Router>
    void
    install_middleware(Router &router, bool advertise_h3) {
        router.use(qb::http::CorsMiddleware<Session>::dev());

        const std::string alt_svc = "h3=\":" + std::to_string(_port) + "\"; ma=86400";
        router.use([alt_svc, advertise_h3](auto ctx, auto next) {
            ctx->response().add_header("X-Powered-By", "QB Framework (HTTP/2 + HTTP/3)");
            ctx->response().add_header("X-Content-Type-Options", "nosniff");
            if (advertise_h3)
                ctx->response().add_header("Alt-Svc", alt_svc); // browsers upgrade to h3 after this
            next();
        });

        router.use(std::make_shared<qb::http::LoggingMiddleware<Session>>(
            [](qb::http::LogLevel, const std::string &message) { std::cout << "[dual] " << message << std::endl; }, qb::http::LogLevel::Info,
            qb::http::LogLevel::Info));

        qb::http::StaticFilesOptions static_options(_static_root);
        static_options.with_path_prefix_to_strip("/static").with_etags(true).with_last_modified(true).with_cache_control(
            true, "public, max-age=3600");
        router.use(qb::http::static_files_middleware<Session>(std::move(static_options)));
    }

    void
    setup_routes() {
        auto &r = _server->router(); // facade: mirrors every route onto both stacks

        r.get("/", [this](auto ctx) { serve_file(ctx, _static_root / "index.html", "text/html; charset=utf-8"); });
        r.get("/favicon.ico", [this](auto ctx) { serve_file(ctx, _static_root / "favicon.ico", "image/x-icon"); });

        r.get("/api/h3-features", [](auto ctx) {
            qb::json j;
            j["protocol"]  = "HTTP/3";
            j["transport"] = "QUIC (UDP)";
            j["timestamp"] = now_ms();
            j["features"]  = qb::json::array(
                {"QUIC transport over UDP — always TLS 1.3, no cleartext", "No head-of-line blocking — independent streams per request",
                 "1-RTT handshake, 0-RTT resumption (vs TCP+TLS 2–3 RTT)", "Connection migration — survives IP/port change via Connection ID",
                 "QPACK header compression (HOL-blocking-free HPACK successor)", "Discovered by browsers via Alt-Svc after an HTTP/2 request"});
            ctx->response().add_header("Content-Type", "application/json");
            ctx->response().body() = j;
            ctx->complete();
        });

        r.get("/api/transport-info", [](auto ctx) {
            qb::json j;
            j["note"]      = "This response may arrive over HTTP/2 (TCP) or HTTP/3 (QUIC) — check the browser's "
                             "DevTools Network 'Protocol' column, or the pill at the top of the page.";
            j["h3_alpn"]   = "h3";
            j["h2_alpn"]   = "h2 / http/1.1";
            j["tls"]       = "TLS 1.3";
            j["timestamp"] = now_ms();
            ctx->response().add_header("Content-Type", "application/json");
            ctx->response().body() = j;
            ctx->complete();
        });

        r.get("/api/connection-info", [](auto ctx) {
            qb::json j;
            j["feature"]   = "QUIC connection identity";
            j["note"]      = "A QUIC connection is keyed by a Connection ID (not the 4-tuple), so it survives a "
                             "client IP/port change — the basis of connection migration.";
            j["upgrade"]   = "Served from a dual stack: HTTP/2 on TCP advertises Alt-Svc h3, the browser then "
                             "moves to HTTP/3 over QUIC.";
            j["timestamp"] = now_ms();
            ctx->response().add_header("Content-Type", "application/json");
            ctx->response().body() = j;
            ctx->complete();
        });

        r.get("/api/no-hol-blocking", [](auto ctx) {
            qb::json j;
            j["feature"]     = "No head-of-line blocking";
            j["http2"]       = "One TCP byte stream: a single lost segment stalls ALL multiplexed streams until retransmit.";
            j["http3"]       = "Independent QUIC streams: a lost packet only stalls its own stream; the rest keep flowing.";
            j["your_stream"] = ctx->request().stream_id;
            j["timestamp"]   = now_ms();
            ctx->response().add_header("Content-Type", "application/json");
            ctx->response().body() = j;
            ctx->complete();
        });

        r.get("/api/stream-demo/:count", [](auto ctx) {
            int count = 0;
            if (!parse_bounded_int(ctx->path_param("count"), 1, 1000, count)) {
                send_bad_request(ctx, "count must be an integer in [1, 1000]");
                return;
            }
            qb::json j;
            j["feature"]   = "Independent streams";
            j["stream_id"] = ctx->request().stream_id;
            j["suggested"] = "Issue this " + std::to_string(count) + " times concurrently: over h3 each rides its own QUIC stream.";
            j["timestamp"] = now_ms();
            ctx->response().add_header("Content-Type", "application/json");
            ctx->response().body() = j;
            ctx->complete();
        });

        r.post("/api/echo", [](auto ctx) {
            qb::json j;
            j["feature"]    = "Echo";
            j["timestamp"]  = now_ms();
            const auto body = ctx->request().body().template as<std::string>();
            try {
                j["echoed"] = qb::json::parse(body);
            } catch (const std::exception &) {
                j["echoed"] = body;
                j["note"]   = "Non-JSON body echoed as string";
            }
            j["bytes"] = body.size();
            ctx->response().add_header("Content-Type", "application/json");
            ctx->response().body() = j;
            ctx->complete();
        });
    }

    bool
    start_listening() {
        const std::string tcp_uri  = "https://0.0.0.0:" + std::to_string(_port); // HTTP/2 (TCP/TLS)
        const std::string quic_uri = "https://0.0.0.0:" + std::to_string(_port); // HTTP/3 (QUIC/UDP)
        try {
            if (!_server->listen(tcp_uri, quic_uri, _cert_file, _key_file)) {
                std::cerr << "Failed to bind the dual stack on port " << _port << " (TCP+UDP). Is it already in use?" << std::endl;
                return false;
            }
            print_server_info();
            return true;
        } catch (const std::exception &e) {
            std::cerr << "Failed to start dual-stack server: " << e.what() << std::endl;
            return false;
        }
    }

    void
    print_server_info() {
        std::cout << "==================================================" << std::endl;
        std::cout << "🚀 QB HTTP/2 + HTTP/3 dual-stack server" << std::endl;
        std::cout << "==================================================" << std::endl;
        std::cout << "HTTP/2 : https://127.0.0.1:" << _port << "  (TCP/TLS, ALPN h2 + http/1.1)" << std::endl;
        std::cout << "HTTP/3 : https://127.0.0.1:" << _port << "  (QUIC/UDP, ALPN h3, advertised via Alt-Svc)" << std::endl;
        std::cout << "Frontend + endpoints:" << std::endl;
        std::cout << "  GET  /                      - interactive demo page" << std::endl;
        std::cout << "  GET  /static/*              - frontend assets" << std::endl;
        std::cout << "  GET  /api/h3-features | /api/transport-info | /api/connection-info" << std::endl;
        std::cout << "  GET  /api/no-hol-blocking | /api/stream-demo/:count | POST /api/echo" << std::endl;
        std::cout << "Open in a browser (self-signed cert → accept the warning once):" << std::endl;
        std::cout << "  https://127.0.0.1:" << _port << "/" << std::endl;
        std::cout << "  The page loads over HTTP/2, then upgrades to HTTP/3 via Alt-Svc." << std::endl;
        std::cout << "  DevTools > Network > Protocol column: it flips to 'h3' on the API calls." << std::endl;
        std::cout << "  (Chrome may need one reload before it moves to h3; the pill at the top of" << std::endl;
        std::cout << "   the page shows the negotiated protocol live.)" << std::endl;
        std::cout << "==================================================" << std::endl;
    }
};

int
main() {
    qb::Main engine;
    try {
        engine.addActor<Http3DualServer>(0);
        engine.start(false);
        engine.join();
        if (engine.hasError()) {
            std::cerr << "Engine encountered an error" << std::endl;
            return 1;
        }
    } catch (const std::exception &e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}

#else // QBM_HTTP_HAS_HTTP3

#include <iostream>

int
main() {
    std::cout << "This example requires HTTP/3 support. Rebuild qbm-http with SSL + QUIC + nghttp3\n"
                 "(QB_WITH_SSL=ON, QB_WITH_QUIC=ON/AUTO with ngtcp2 + nghttp3) so QBM_HTTP_HAS_HTTP3\n"
                 "is defined, then re-run qb-example-modules-http-http3."
              << std::endl;
    return 0;
}

#endif // QBM_HTTP_HAS_HTTP3
