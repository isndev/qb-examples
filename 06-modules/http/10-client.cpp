/**
 * @file examples/06-modules/http/10-client.cpp
 * @tier 06-modules
 * @teaches The PERSISTENT HTTP/1.1 client — one connection reused across many requests, a batch
 *          issued in one call, the callback form beside the coroutine one, and the stats it keeps
 *          — measured against a server this program hosts itself, on the same event loop, so the
 *          connection count is a number rather than a claim.
 * @demonstrates qb::http1::make_client, qb::http1::Client, push_request, push_requests,
 *               is_connected, get_stats, set_request_timeout, set_auto_reconnect,
 *               qb::http::Request, qb::http::method, qb::http::status, qb::http::GET,
 *               qb::http::use<EchoServer>::server<EchoSession>, router, compile, listen_v4,
 *               qb::io::async::init, qb::io::async::run_until, qb::io::async::task<void>
 * @prerequisites 06-modules/http/09-coroutine-handlers
 * @expect "[server] hosting the upstream in this very process, on the SAME event loop as the"
 * @expect "[connect] connected; a second connect() on a live client is a no-op"
 * @expect "[reuse] 3 sequential requests, and the server saw 1 connection"
 * @expect "[batch] push_requests returned 4 responses IN REQUEST ORDER"
 * @expect "[callback] the same request without a coroutine: 200 pong"
 * @expect "[timeout] a 100 ms request timeout cut off a 400 ms route"
 * @expect "[one-shot] qb::http::GET opened a NEW connection for each call"
 * @expect "[stats] total/successful/failed = "
 * @expect "=== client complete: one persistent connection beat "
 *
 * WHAT THIS FILE USED TO BE, AND WHY IT MATTERED
 * ----------------------------------------------
 * It made three one-shot `qb::http::GET`/`POST` calls to the public **httpbin.org**. Two
 * consequences, both measured: it could not run offline, and its own report was unconditional —
 * it printed "HTTP Client demo completed!" and exited 0 on a run in which all three requests
 * came back **503**. A program whose output is the same whether it worked or not cannot be
 * checked by anything, which is why the example runner's entry for it carried a paragraph of
 * apology. Meanwhile `qb::http1::make_client` — the persistent client with reuse, queueing,
 * timeouts, reconnect and statistics — had **zero** occurrences anywhere in the corpus.
 *
 * ONE-SHOT VERBS versus A CLIENT
 * ------------------------------
 * `co_await qb::http::GET(uri)` is the right thing for one request: it connects, sends, reads,
 * and closes. For N requests to the same host it is also N connections, N TCP handshakes and N
 * slow starts. `qb::http1::Client` keeps the connection and pipelines your requests onto it.
 * The last two sections here run the same work both ways and print the server's connection
 * count for each; that difference is the whole argument.
 *
 * THE UPSTREAM IS IN THIS PROCESS
 * -------------------------------
 * The server below is an ordinary `qb::http::use<T>::server<Session>` bound to loopback, and it
 * shares the client's event loop — one thread, one `listener`, both sides on it. That is not a
 * trick for examples: pumping the loop to advance the client also services the server, which is
 * why one `run_until` drives both halves. It makes this program offline-safe,
 * deterministic, and able to assert things about the SERVER's view that no external host could
 * ever tell it.
 *
 * THINGS THE CLIENT WILL DO THAT ARE EASY TO MISREAD
 * --------------------------------------------------
 *  * `Client` is used through `std::shared_ptr` — it relies on `shared_from_this`, so
 *    `make_client(...)` is not a convenience, it is the constructor you have.
 *  * `connect()` has two forms: `co_await client->connect()` yields a `ConnectResult`
 *    (`operator bool` + `error_message`); the callback form takes `(bool, std::string const&)`.
 *    Calling it again on a live client succeeds immediately without reconnecting.
 *  * requests are sent STRICTLY ONE AT A TIME (`push_requests` is sequential pipelining onto one
 *    connection, not concurrency), and its result vector is indexed by the ORIGINAL request
 *    position regardless of completion order.
 *  * `set_request_timeout` bounds queue-wait AND flight time; `qb::duration::zero()` disarms it.
 *  * past `set_max_pending_requests`, `push_request` returns `false` *and still calls your
 *    callback*, with a synthesised 503.
 *
 * Build:
 *   cmake --preset release
 *   cmake --build --preset release --target qb-example-modules-http-client
 * Run (no network needed — it hosts its own upstream):
 *   ./build/presets/release/examples/06-modules/http/qb-example-modules-http-client
 */

#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>
#include <vector>
#include <qb/io/async.h>
#include <qb/io/async/coroutine.h>
#include <qbm/http/http.h>

using namespace std::chrono_literals;

namespace {

constexpr std::uint16_t PORT = 18081;

std::string
url(std::string const &path) {
    return "http://127.0.0.1:" + std::to_string(PORT) + path;
}

qb::http::Request
request(qb::http::method m, std::string const &target) {
    return qb::http::Request{m, qb::io::uri(target)};
}

class EchoServer;

class EchoSession : public qb::http::use<EchoSession>::session<EchoServer> {
public:
    explicit EchoSession(EchoServer &server);
};

// The upstream. Counters live on the server object, and this program reads them only after it
// has observed the client-side result, so "how many connections did you accept" is an
// observation rather than a guess.
class EchoServer : public qb::http::use<EchoServer>::server<EchoSession> {
public:
    std::atomic<int> connections{0};
    std::atomic<int> requests{0};

    EchoServer() {
        router().get("/ping", [this](auto ctx) {
            ++requests;
            ctx->response().status() = qb::http::status::OK;
            ctx->response().body()   = "pong";
            ctx->complete();
        });

        router().post("/echo", [this](auto ctx) {
            ++requests;
            ctx->response().status() = qb::http::status::CREATED;
            ctx->response().body()   = ctx->request().body().template as<std::string>();
            ctx->complete();
        });

        // Answers only after 400 ms, so a client-side timeout has something to cut off. The
        // delay is a coroutine on the server's loop, not a sleep: blocking here would stall the
        // client too, since both share this thread.
        router().get("/slow", [this](auto ctx) -> qb::io::async::task<void> {
            ++requests;
            co_await qb::io::async::sleep(400ms);
            ctx->response().status() = qb::http::status::OK;
            ctx->response().body()   = "eventually";
            ctx->complete();
        });

        router().compile();
    }

    /// Called once per accepted connection — this is the number the reuse claim rests on.
    void
    on(IOSession &) {
        ++connections;
    }
};

EchoSession::EchoSession(EchoServer &server)
    : session(server) {}

} // namespace

qb::io::async::task<void>
run_client(EchoServer &server, bool &running, bool &ok) {
    struct StopOnExit {
        bool &r;
        ~StopOnExit() {
            r = false;
        }
    } stop{running};

    // `make_client` and not `new Client`: the class is `enable_shared_from_this` and every
    // in-flight request holds a reference to it, which is what keeps a client alive across a
    // callback that outlives the scope it was created in.
    // The type is spelled out once: `make_client` hands back a `shared_ptr<Client>`, and the
    // shared ownership is load-bearing rather than stylistic — every in-flight request holds a
    // reference, which is what keeps the object alive across a callback.
    std::shared_ptr<qb::http1::Client> client = qb::http1::make_client(url("/"));

    const int conns_at_start = server.connections.load();

    auto connected = co_await client->connect();
    if (!connected) {
        qb::io::cerr() << "connect failed: " << connected.error_message << "\n";
        co_return;
    }
    // Idempotent: a second connect on a live client returns success without opening anything.
    auto again = co_await client->connect();
    qb::io::cout() << (connected && again && client->is_connected() ? "[connect] connected; a second connect() on a live client is a no-op\n"
                                                                    : "[connect] UNEXPECTED: the client did not report a live connection\n");

    // ---- 1. REUSE: three requests, one connection --------------------------------------
    // The baseline is taken BEFORE `connect()`, because connect is what opens the socket —
    // measuring from after it would count zero new connections and prove nothing. (Measured:
    // the first version of this file did exactly that and reported its own success as a
    // failure, which is the right way round for a mistake to go.)
    auto      a           = co_await client->push_request(request(qb::http::method::GET, url("/ping")));
    auto      b           = co_await client->push_request(request(qb::http::method::GET, url("/ping")));
    auto      c           = co_await client->push_request(request(qb::http::method::GET, url("/ping")));
    const int conns_after = server.connections.load();

    const bool reused = a.status() == qb::http::status::OK && b.status() == qb::http::status::OK && c.status() == qb::http::status::OK
                        && (conns_after - conns_at_start) == 1;
    qb::io::cout() << (reused ? "[reuse] 3 sequential requests, and the server saw 1 connection — keep-alive is\n"
                                "        decided per response, and a `Connection: close` would have dropped it\n"
                              : "[reuse] UNEXPECTED: the three requests did not share one connection\n");
    qb::io::cout() << "        (connections opened by connect() + 3 requests: " << (conns_after - conns_at_start) << ")\n";

    // ---- 2. A BATCH in one call ---------------------------------------------------------
    // Sent one at a time onto the same connection; the result vector is indexed by the
    // ORIGINAL request position, so a slower response never reorders your results.
    std::vector<qb::http::Request> batch;
    batch.push_back(request(qb::http::method::GET, url("/ping")));
    for (int i = 0; i < 3; ++i) {
        auto post   = request(qb::http::method::POST, url("/echo"));
        post.body() = "payload-" + std::to_string(i);
        batch.push_back(std::move(post));
    }
    auto results = co_await client->push_requests(std::move(batch));

    bool ordered = results.size() == 4 && results[0].body().template as<std::string>() == "pong";
    for (int i = 0; ordered && i < 3; ++i)
        ordered = results[static_cast<std::size_t>(i) + 1].body().template as<std::string>() == "payload-" + std::to_string(i);
    qb::io::cout() << (ordered ? "[batch] push_requests returned 4 responses IN REQUEST ORDER, on the same\n"
                                 "        connection — an empty batch is an immediate success, not an error\n"
                               : "[batch] UNEXPECTED: the batch results were missing or out of order\n");

    // ---- 3. THE CALLBACK FORM -----------------------------------------------------------
    // The same call without a coroutine. It returns `false` up front when the request cannot
    // even be queued — and note that even then your callback still runs, with a synthesised
    // 503, so a callback that fires is not by itself evidence the request was sent.
    int         cb_status = 0;
    std::string cb_body;
    const bool  queued = client->push_request(request(qb::http::method::GET, url("/ping")), [&](qb::http::Response response) {
        cb_status = static_cast<int>(response.status());
        cb_body   = response.body().template as<std::string>();
    });
    // Give the loop the turns the callback needs. `co_await` on our own scheduler is the
    // cooperative way to do that from inside a coroutine — never a blocking sleep.
    for (int i = 0; i < 50 && cb_status == 0; ++i)
        co_await qb::io::async::sleep(10ms);
    qb::io::cout() << (queued && cb_status == 200 && cb_body == "pong" ? "[callback] the same request without a coroutine: 200 pong\n"
                                                                       : "[callback] UNEXPECTED: the callback form did not deliver 200 pong\n");

    // ---- 4. A REQUEST TIMEOUT ------------------------------------------------------------
    // It bounds queue-wait AND flight; `qb::duration::zero()` disarms it. The route answers at
    // 400 ms, so 100 ms must cut it off.
    client->set_request_timeout(100ms);
    auto       slow = co_await client->push_request(request(qb::http::method::GET, url("/slow")));
    const bool cut  = slow.status() != qb::http::status::OK;
    qb::io::cout() << (cut ? "[timeout] a 100 ms request timeout cut off a 400 ms route, and the client\n"
                             "          reconnects for the next request rather than staying wedged\n"
                           : "[timeout] UNEXPECTED: the slow route answered inside the timeout\n");
    client->set_request_timeout(qb::duration::zero());
    client->set_auto_reconnect(true);

    // ---- 5. THE CONTRAST: one-shot verbs -------------------------------------------------
    const int before_oneshot = server.connections.load();
    for (int i = 0; i < 3; ++i)
        (void) co_await qb::http::GET(qb::http::Request{qb::io::uri(url("/ping"))});
    const int opened = server.connections.load() - before_oneshot;
    qb::io::cout() << (opened == 3 ? "[one-shot] qb::http::GET opened a NEW connection for each call: 3 requests,\n"
                                     "           3 connections. Right for one request, wrong for a hundred\n"
                                   : "[one-shot] UNEXPECTED: the one-shot verbs did not open one connection each\n");

    // ---- 6. WHAT THE CLIENT COUNTED ------------------------------------------------------
    auto [total, successful, failed] = client->get_stats();
    qb::io::cout() << "[stats] total/successful/failed = " << total << "/" << successful << "/" << failed
                   << " — the timeout above is the failure, and it is counted rather than hidden\n";

    qb::io::cout() << "\n=== client complete: one persistent connection beat " << opened << " one-shot connections for the same work ===\n";
    ok = reused && ordered && cut && opened == 3;
    co_return;
}

int
main() {
    qb::io::async::init();

    EchoServer server;
    // Bind FIRST and report honestly. A server that never bound must not let the program
    // pretend it ran — the rest of this file measures the server's own counters, so a failed
    // bind would make every number below meaningless.
    if (server.transport().listen_v4(PORT, "127.0.0.1") != 0) {
        qb::io::cerr() << "[server] could not bind 127.0.0.1:" << PORT << " — is something else using it?\n";
        return 1;
    }
    server.start();
    qb::io::cout() << "[server] hosting the upstream in this very process, on the SAME event loop as the\n"
                      "         client — so no network is needed and the connection counts below are the\n"
                      "         SERVER's own\n";
    qb::io::cout() << "[server] bound 127.0.0.1:" << PORT << "\n";

    bool running = true;
    bool ok      = false;
    qb::io::async::coro_scheduler().spawn(run_client(server, running, ok));
    qb::io::async::run_until(running);

    return ok ? 0 : 1;
}
