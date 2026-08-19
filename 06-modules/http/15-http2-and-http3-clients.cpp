/**
 * @file examples/06-modules/http/15-http2-and-http3-clients.cpp
 * @tier 06-modules
 * @teaches The two clients this corpus documented and never used: qb::http2::Client and
 *          qb::http3::Client. The API is the SAME as http1::Client — make_client, connect,
 *          push_request, push_requests, get_stats — so the file is really about the one thing that
 *          differs, MULTIPLEXING, measured here against an HTTP/1.1 client doing identical work on
 *          the same event loop; plus the rules both clients enforce that http1 does not.
 * @demonstrates qb::http2::make_client, qb::http2::Client, qb::http2::make_server,
 *               qb::http1::make_client, qb::http1::Client,
 *               push_request, push_requests, connect, disconnect, is_connected, get_stats,
 *               set_verify_peer, set_connect_timeout, set_request_timeout, get_base_uri,
 *               qb::http::Request, qb::http::method, qb::http::status, qb::http::GET,
 *               qb::http::use<PlainServer>::server<PlainSession>, router, compile, listen, listen_v4,
 *               qb::io::async::init, qb::io::async::run_until, qb::io::async::sleep,
 *               qb::io::async::coro_scheduler, qb::io::async::task<void>
 * @prerequisites 06-modules/http/10-client, 06-modules/http/12-http2
 * @expect "[same] one API, three protocol versions: make_client(url) then push_request(req). The"
 * @expect "[https] both clients REFUSE a plaintext base URI at construction — there is no h2c and"
 * @expect "[verify] set_verify_peer(false) is the whole TLS setup on the client side: no context,"
 * @expect "[multiplex] the measurement this file exists for. Four requests to a route that sleeps"
 * @expect "[multiplex] HTTP/1.1 took "
 * @expect "[multiplex] HTTP/2 took "
 * @expect "[shape] the two client shapes return DIFFERENT things and it is easy to miss: a Client"
 * @expect "[h3] HTTP/3 is the same code again over QUIC — one origin, one connection, streams"
 * @expect "=== http2 and http3 clients complete: the same six calls over three transports, and"
 *
 * WHY THIS PROGRAM IS A MEASUREMENT AND NOT A TOUR
 * -----------------------------------------------
 * `06-modules/http/10-client.cpp` already teaches the persistent-client API, and it states a fact
 * about HTTP/1.1 that this file is the other half of: *`push_requests` is sequential pipelining
 * onto one connection, not concurrency*. HTTP/2 and HTTP/3 change exactly that and nothing else in
 * the API — so the honest way to demonstrate them is to run the SAME batch both ways against
 * servers hosted in this process and print the two elapsed times.
 *
 * The route the batch hits sleeps 150 ms server-side. Four requests: HTTP/1.1 pays 4 x 150 ms
 * because the second request cannot leave until the first response is fully read; HTTP/2 pays
 * roughly 150 ms once because the four live on four STREAMS of one connection and the server
 * interleaves them. That is head-of-line blocking, and it is the whole reason HTTP/2 exists.
 *
 * WHAT IS THE SAME, WHICH IS ALMOST EVERYTHING
 * --------------------------------------------
 *     auto c = qb::http1::make_client("http://host:port");   // or http2:: or http3::
 *     co_await c->connect();
 *     auto r  = co_await c->push_request(req);
 *     auto rs = co_await c->push_requests(reqs);
 *     auto [total, ok, failed] = c->get_stats();
 *     c->disconnect();
 *
 * Three namespaces, one vocabulary. Both new clients must be held through the `std::shared_ptr`
 * `make_client` returns — they use `weak_from_this()` for deferred timeout and connect callbacks —
 * and both have a callback overload beside every coroutine one, told apart by arity.
 *
 * THE RULES http1 DOES NOT HAVE
 * -----------------------------
 *  * **https only.** Both throw `std::invalid_argument` from `make_client` on a non-https base URI
 *    and synthesise a 400 for a non-https request URI. `http2::Client` has no h2c path at all: it
 *    mints its own TLS context with ALPN `h2` and fails the connection if the peer negotiates
 *    anything else. `http3::Client` does the same with ALPN `h3` over QUIC.
 *  * **`connect(nullptr)`, not `connect()`, for fire-and-forget.** The default argument was removed
 *    so the coroutine overload is unambiguous. A bare `connect()` selects the awaiter.
 *  * **you do not build a TLS context.** `set_verify_peer(false)` before connecting is the entire
 *    client-side setup for a self-signed certificate. There is no ALPN list to pass and no
 *    `ssl::Context` to construct — attempting either means you are on the server side by mistake.
 *
 * ABOUT THE HTTP/3 SECTION
 * ------------------------
 * It is compiled only when `QBM_HTTP_HAS_HTTP3` is defined — HTTP/3 needs OpenSSL *and* ngtcp2
 * *and* nghttp3, which the `REQUIRES` vocabulary (ssl/quic/compression) cannot express. The file
 * states which branch it took rather than skipping quietly, the same shape
 * `14-streaming-and-cookies` uses for compression and `13-http3` for the server side.
 *
 * All three servers are hosted IN THIS PROCESS on loopback, on the client's own event loop, so the
 * run needs no network and the numbers below are this machine's rather than an internet's.
 *
 * Build:
 *   cmake --preset release
 *   cmake --build --preset release --target qb-example-modules-http-http2-and-http3-clients
 * Run (no network needed — it hosts its own upstreams):
 *   ./build/presets/release/examples/06-modules/http/qb-example-modules-http-http2-and-http3-clients
 */

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>
#include <qb/io/async.h>
#include <qb/io/async/coroutine.h>
#include <qb/io/system/file.h> // qb::io::sys::resolve_resource
#include <qbm/http/http.h>

using namespace std::chrono_literals;

namespace {

constexpr std::uint16_t PLAIN_PORT = 18450; // HTTP/1.1, no TLS
constexpr std::uint16_t H2_PORT    = 18451; // HTTP/2 over TLS+ALPN
constexpr std::uint16_t H3_PORT    = 18452; // HTTP/3 over QUIC (UDP)

// The server-side delay that makes head-of-line blocking visible. Long enough that the difference
// is unambiguous, short enough that the whole program stays under two seconds.
constexpr auto ROUTE_DELAY = 150ms;
constexpr int  BATCH_SIZE  = 4;

std::string
plain_url(std::string const &path = "/") {
    return "http://127.0.0.1:" + std::to_string(PLAIN_PORT) + path;
}

std::string
h2_url(std::string const &path = "/") {
    return "https://127.0.0.1:" + std::to_string(H2_PORT) + path;
}

// Every call site sits inside `#ifdef QBM_HTTP_HAS_HTTP3`, so an HTTP/3-less build leaves this
// unused (gcc -Wunused-function). Marked rather than moved under the same #ifdef: the three URL
// helpers are one trio, and splitting the odd one out reads worse than saying why it may be idle.
[[maybe_unused]] std::string
h3_url(std::string const &path = "/") {
    return "https://127.0.0.1:" + std::to_string(H3_PORT) + path;
}

qb::http::Request
request(std::string const &url) {
    qb::http::Request req;
    req.method() = qb::http::method::GET;
    req.uri()    = qb::io::uri(url);
    return req;
}

long long
ms_since(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
}

// ---------------------------------------------------------------------------------------
// The HTTP/1.1 upstream. An ordinary qb-io server on the same loop as every client below —
// exactly the shape 06-modules/http/10-client.cpp uses, and for the same reason.
// ---------------------------------------------------------------------------------------
class PlainServer;

class PlainSession : public qb::http::use<PlainSession>::session<PlainServer> {
public:
    explicit PlainSession(PlainServer &server)
        : session(server) {}
};

class PlainServer : public qb::http::use<PlainServer>::server<PlainSession> {
public:
    PlainServer() {
        router().get("/slow", [](auto ctx) -> qb::io::async::task<void> {
            co_await qb::io::async::sleep(ROUTE_DELAY);
            ctx->response().status() = qb::http::status::OK;
            ctx->response().set_header("X-Protocol", "HTTP/1.1");
            ctx->response().body() = "slow-h1";
            ctx->complete();
            co_return;
        });
        router().get("/ping", [](auto ctx) {
            ctx->response().status() = qb::http::status::OK;
            ctx->response().body()   = "pong-h1";
            ctx->complete();
        });
        router().compile();
    }

    void
    on(IOSession &) {}
};

} // namespace

// ===========================================================================================
// The whole program, as one coroutine on the loop that also serves all three upstreams.
// ===========================================================================================
qb::io::async::task<void>
run_clients(bool &running, bool &ok) {
    struct StopOnExit {
        bool &r;
        ~StopOnExit() {
            r = false;
        }
    } stop{running};

    // -----------------------------------------------------------------------------------
    // 1. THE SAME SIX CALLS, over HTTP/1.1 and HTTP/2.
    // -----------------------------------------------------------------------------------
    // Spelled out rather than `auto`, because the TYPE is half the lesson: both are
    // `std::shared_ptr<...::Client>` and must stay that way — the clients use `weak_from_this()`
    // for deferred timeout and connect callbacks, so a stack-allocated one is a use-after-free
    // waiting for a timer.
    std::shared_ptr<qb::http1::Client> h1 = qb::http1::make_client(plain_url());
    std::shared_ptr<qb::http2::Client> h2 = qb::http2::make_client(h2_url());
    // The ONE piece of TLS setup a client needs for the bundled self-signed dev certificate.
    h2->set_verify_peer(false);
    // The default is 30 s. A demo that cannot reach its own loopback server should say so in
    // seconds, not spin for half a minute first.
    h2->set_connect_timeout(5s);
    // The other bound, and it means something different: connect_timeout is the handshake,
    // set_request_timeout covers queue-wait AND flight for each request. Generous here, because the
    // route below deliberately sleeps 150 ms.
    h2->set_request_timeout(5s);
    h1->set_request_timeout(5s);

    const bool h1_up      = static_cast<bool>(co_await h1->connect());
    auto       h2_connect = co_await h2->connect();
    const bool h2_up      = static_cast<bool>(h2_connect);

    if (!h1_up || !h2_up) {
        qb::io::cerr() << "[fatal] could not connect: h1=" << (h1_up ? "ok" : "failed") << ", h2=" << (h2_up ? "ok" : h2_connect.error_message)
                       << "\n";
        co_return;
    }

    auto h1_pong = co_await h1->push_request(request(plain_url("/ping")));
    auto h2_pong = co_await h2->push_request(request(h2_url("/ping")));

    const bool same_ok = h1_pong.status() == qb::http::status::OK && h2_pong.status() == qb::http::status::OK
                         && h2_pong.header("X-Protocol") == "HTTP/2" && h2->is_connected();

    qb::io::cout() << "[same] one API, three protocol versions: make_client(url) then push_request(req). The\n"
                      "       namespace is the only edit — qb::http1, qb::http2, qb::http3 — and connect(),\n"
                      "       push_requests(), get_stats(), disconnect() and is_connected() carry over unchanged\n";
    qb::io::cout() << "       (HTTP/1.1 said \"" << h1_pong.body().as<std::string>() << "\", HTTP/2 said \"" << h2_pong.body().as<std::string>()
                   << "\" and the server tagged it " << h2_pong.header("X-Protocol") << ")\n";
    qb::io::cout() << "       (the h2 client's own base URI: " << h2->get_base_uri().source() << ")\n\n";

    // -----------------------------------------------------------------------------------
    // 2. THE RULE http1 DOES NOT HAVE.
    // -----------------------------------------------------------------------------------
    bool        threw = false;
    std::string why;
    try {
        auto plaintext_h2 = qb::http2::make_client(plain_url()); // http:// — refused
        (void) plaintext_h2;
    } catch (std::invalid_argument const &e) {
        threw = true;
        why   = e.what();
    }

    qb::io::cout() << "[https] both clients REFUSE a plaintext base URI at construction — there is no h2c and\n"
                      "        no plaintext QUIC in this module, so an http:// origin is a programming error\n"
                      "        rather than a slower path. A non-https REQUEST uri is answered with a 400\n";
    qb::io::cout() << "        (make_client(\"" << plain_url() << "\") threw: " << (threw ? why : "NOTHING — UNEXPECTED") << ")\n\n";

    qb::io::cout() << "[verify] set_verify_peer(false) is the whole TLS setup on the client side: no context,\n"
                      "         no ALPN list, no certificate to load. The client mints its own context with the\n"
                      "         right ALPN baked in and FAILS the connection if the peer negotiates anything else,\n"
                      "         so a server offering only http/1.1 is a clear error and never a silent downgrade\n\n";

    // -----------------------------------------------------------------------------------
    // 3. THE MEASUREMENT.
    // -----------------------------------------------------------------------------------
    std::vector<qb::http::Request> h1_batch;
    std::vector<qb::http::Request> h2_batch;
    for (int i = 0; i < BATCH_SIZE; ++i) {
        h1_batch.push_back(request(plain_url("/slow")));
        h2_batch.push_back(request(h2_url("/slow")));
    }

    const auto h1_start     = std::chrono::steady_clock::now();
    auto       h1_responses = co_await h1->push_requests(std::move(h1_batch));
    const auto h1_ms        = ms_since(h1_start);

    const auto h2_start     = std::chrono::steady_clock::now();
    auto       h2_responses = co_await h2->push_requests(std::move(h2_batch));
    const auto h2_ms        = ms_since(h2_start);

    const long long serial_floor = static_cast<long long>(BATCH_SIZE) * ROUTE_DELAY.count();

    bool all_ok = h1_responses.size() == BATCH_SIZE && h2_responses.size() == BATCH_SIZE;
    for (auto const &r : h1_responses)
        all_ok = all_ok && r.status() == qb::http::status::OK;
    for (auto const &r : h2_responses)
        all_ok = all_ok && r.status() == qb::http::status::OK;

    // HTTP/1.1 must pay for every request in turn; HTTP/2 must not. Both bounds are generous —
    // the point is the SHAPE of the difference, and a machine under load must not fail the run.
    const bool multiplex_ok = all_ok && h1_ms >= serial_floor && h2_ms < serial_floor;

    qb::io::cout() << "[multiplex] the measurement this file exists for. Four requests to a route that sleeps\n"
                      "            150 ms server-side, issued as ONE push_requests() call on each client, against\n"
                      "            servers in this very process. Same code, same batch, same loop\n";
    qb::io::cout() << "[multiplex] HTTP/1.1 took " << h1_ms
                   << " ms — at least 4 x 150 ms, because request N+1\n"
                      "            cannot leave until response N has been read off the socket. That is head-of-line\n"
                      "            blocking, and it is a property of the FRAMING, not of the server\n";
    qb::io::cout() << "[multiplex] HTTP/2 took " << h2_ms
                   << " ms — the four requests are four STREAMS of one\n"
                      "            connection, interleaved on the wire, so the delays overlap instead of adding up.\n"
                      "            Nothing about the client code changed to get that\n";
    if (!multiplex_ok)
        qb::io::cerr() << "            UNEXPECTED: the two timings do not show the shape this section claims\n";
    qb::io::cout() << "\n";

    // -----------------------------------------------------------------------------------
    // 4. THE RETURN-SHAPE TRAP.
    // -----------------------------------------------------------------------------------
    auto one_shot = co_await qb::http::GET(request(plain_url("/ping")));
    // `qb::http::GET` yields a Reply — request AND response. `Client::push_request` yields the
    // Response alone. Reading `.body()` off the wrong one is a compile error; reading
    // `.response.body()` off a Response is too, which is the good outcome.
    const bool shape_ok = one_shot.response.status() == qb::http::status::OK && h1_pong.status() == qb::http::status::OK;

    qb::io::cout() << "[shape] the two client shapes return DIFFERENT things and it is easy to miss: a Client\n"
                      "        push_request yields a bare qb::http::Response, while the one-shot verbs\n"
                      "        (qb::http::GET and friends, HTTP/1.1 only) yield an async::Reply carrying BOTH the\n"
                      "        request and the response — so it is reply.response.body(), not reply.body()\n";
    qb::io::cout() << "        (one-shot: " << one_shot.response.body().as<std::string>() << " from " << one_shot.request.uri().source()
                   << "; client: " << h1_pong.body().as<std::string>() << ")\n\n";

    // -----------------------------------------------------------------------------------
    // 5. HTTP/3, when the build has it.
    // -----------------------------------------------------------------------------------
    bool h3_ok = true;
#ifdef QBM_HTTP_HAS_HTTP3
    {
        auto h3 = qb::http3::make_client(h3_url());
        h3->set_verify_peer(false);

        auto h3_connect = co_await h3->connect();
        if (!h3_connect) {
            qb::io::cerr() << "[h3] UNEXPECTED: connect failed: " << h3_connect.error_message << "\n";
            h3_ok = false;
        } else {
            auto h3_pong = co_await h3->push_request(request(h3_url("/ping")));

            std::vector<qb::http::Request> h3_batch;
            for (int i = 0; i < BATCH_SIZE; ++i)
                h3_batch.push_back(request(h3_url("/slow")));

            const auto h3_start     = std::chrono::steady_clock::now();
            auto       h3_responses = co_await h3->push_requests(std::move(h3_batch));
            const auto h3_ms        = ms_since(h3_start);

            const auto [total, succeeded, failed] = h3->get_stats();

            h3_ok = h3_pong.status() == qb::http::status::OK && h3_responses.size() == BATCH_SIZE && h3_ms < serial_floor && failed == 0;

            qb::io::cout() << "[h3] HTTP/3 is the same code again over QUIC — one origin, one connection, streams\n"
                              "     multiplexed like h2 but WITHOUT TCP underneath, so a lost packet stalls only the\n"
                              "     stream it belonged to instead of every stream sharing the socket\n";
            qb::io::cout() << "     (the same four-request batch took " << h3_ms << " ms against the same 150 ms route; " << total
                           << " requests total, " << succeeded << " succeeded, " << failed << " failed)\n";
            qb::io::cout() << "     (h3 alone also has push_request_with_id() and cancel_request(), which h2 does\n"
                              "     not — QUIC can RESET_STREAM one request without disturbing its neighbours)\n\n";
            h3->disconnect();
        }
    }
#else
    qb::io::cout() << "[h3] HTTP/3 is the same code again over QUIC — one origin, one connection, streams\n"
                      "     multiplexed like h2 but WITHOUT TCP underneath. This build has no QBM_HTTP_HAS_HTTP3,\n"
                      "     so the section did not run: HTTP/3 needs OpenSSL AND ngtcp2 AND nghttp3, and the\n"
                      "     REQUIRES vocabulary cannot express that third one\n\n";
#endif

    // -----------------------------------------------------------------------------------
    // Stats, and shutting the clients down.
    // -----------------------------------------------------------------------------------
    const auto [h1_total, h1_ok_n, h1_failed] = h1->get_stats();
    const auto [h2_total, h2_ok_n, h2_failed] = h2->get_stats();

    const bool stats_ok = h1_failed == 0 && h2_failed == 0 && h1_total >= 5 && h2_total >= 5;

    qb::io::cout() << "[stats] every client keeps the same three counters, and they are the cheapest health\n"
                      "        check you have: total / succeeded / failed since construction\n";
    qb::io::cout() << "        (HTTP/1.1 " << h1_total << "/" << h1_ok_n << "/" << h1_failed << ", HTTP/2 " << h2_total << "/" << h2_ok_n << "/"
                   << h2_failed << ")\n\n";

    h1->disconnect();
    h2->disconnect();

    ok = same_ok && threw && multiplex_ok && shape_ok && h3_ok && stats_ok;

    qb::io::cout() << "=== http2 and http3 clients complete: the same six calls over three transports, and\n"
                      "    the only thing that changed with the version was how long four requests took ===\n";
    co_return;
}

int
main() {
    qb::io::async::init();

    // The self-signed dev certificate, staged next to this binary by the build.
    const std::filesystem::path cert = qb::io::sys::resolve_resource("resources/ssl/cert.pem");
    const std::filesystem::path key  = qb::io::sys::resolve_resource("resources/ssl/key.pem");
    if (!std::filesystem::exists(cert) || !std::filesystem::exists(key)) {
        qb::io::cerr() << "[fatal] the dev certificate is not next to this binary (" << cert << ")\n";
        return 1;
    }

    // ---- the HTTP/1.1 upstream ---------------------------------------------------------
    PlainServer plain;
    if (plain.transport().listen_v4(PLAIN_PORT, "127.0.0.1") != 0) {
        qb::io::cerr() << "[fatal] could not bind 127.0.0.1:" << PLAIN_PORT << "\n";
        return 1;
    }
    plain.start();

    // ---- the HTTP/2 upstream -----------------------------------------------------------
    auto h2_server = qb::http2::make_server();
    h2_server->router().get("/slow", [](auto ctx) -> qb::io::async::task<void> {
        co_await qb::io::async::sleep(ROUTE_DELAY);
        ctx->response().status() = qb::http::status::OK;
        ctx->response().set_header("X-Protocol", "HTTP/2");
        ctx->response().body() = "slow-h2";
        ctx->complete();
        co_return;
    });
    h2_server->router().get("/ping", [](auto ctx) {
        ctx->response().status() = qb::http::status::OK;
        ctx->response().set_header("X-Protocol", "HTTP/2");
        ctx->response().body() = "pong-h2";
        ctx->complete();
    });
    h2_server->router().compile();
    if (!h2_server->listen(qb::io::uri("https://127.0.0.1:" + std::to_string(H2_PORT)), cert, key)) {
        qb::io::cerr() << "[fatal] could not bind the HTTP/2 upstream on 127.0.0.1:" << H2_PORT << "\n";
        return 1;
    }
    // Redundant since 3.0 and kept on purpose: listen() now binds AND arms the accept watcher,
    // on the h2 server exactly as on the h3 one below, so the two stacks finally agree. Before
    // 3.0 this call was required here and its absence did not fail — it made every connect()
    // time out with nothing in any log naming the cause. A second start() is a no-op (libev's
    // ev_io_start returns early on an active watcher), which is what lets code written against
    // the old contract keep working unchanged.
    h2_server->start();

    // ---- the HTTP/3 upstream -----------------------------------------------------------
#ifdef QBM_HTTP_HAS_HTTP3
    auto h3_server = qb::http3::make_server();
    h3_server->router().get("/slow", [](auto ctx) -> qb::io::async::task<void> {
        co_await qb::io::async::sleep(ROUTE_DELAY);
        ctx->response().status() = qb::http::status::OK;
        ctx->response().set_header("X-Protocol", "HTTP/3");
        ctx->response().body() = "slow-h3";
        ctx->complete();
        co_return;
    });
    h3_server->router().get("/ping", [](auto ctx) {
        ctx->response().status() = qb::http::status::OK;
        ctx->response().body()   = "pong-h3";
        ctx->complete();
    });
    h3_server->router().compile();
    if (!h3_server->listen(qb::io::uri("https://127.0.0.1:" + std::to_string(H3_PORT)), cert, key)) {
        qb::io::cerr() << "[fatal] could not bind the HTTP/3 upstream on UDP 127.0.0.1:" << H3_PORT << "\n";
        return 1;
    }
#endif

    qb::io::cout() << "[servers] three upstreams in this process, on the client's own event loop: HTTP/1.1 on " << PLAIN_PORT
                   << ",\n"
                      "          HTTP/2 on "
                   << H2_PORT << " (TLS+ALPN), HTTP/3 on UDP " << H3_PORT
                   << " (QUIC) — so no network is needed\n"
                      "          and the timings below are this machine's\n\n";

    bool running = true;
    bool ok      = false;
    qb::io::async::coro_scheduler().spawn(run_clients(running, ok));
    qb::io::async::run_until(running);

    return ok ? 0 : 1;
}
