/**
 * @file examples/06-modules/http/16-sse.cpp
 * @tier 06-modules
 * @teaches Server-Sent Events over a response stream: a `text/event-stream` route that writes each
 *          event as it happens with `begin_sse()`, a heartbeat that keeps the connection alive, a
 *          `Last-Event-ID` that resumes a client where it left off, and a client in the same process
 *          that reads the events back -- one handler, written once for HTTP/1.1, HTTP/2 and HTTP/3
 *          (this program serves it over HTTP/1.1).
 * @demonstrates qb::http::sse::Event, begin_sse, qb::http::sse::Options, send_async, send, comment,
 *               heartbeat, events_sent, end, qb::http::sse::last_event_id, qb::http::Server,
 *               router, get, compile, listen, spawn, qb::http::GET, qb::io::async::sleep,
 *               qb::io::async::task<void>
 * @prerequisites 06-modules/http/09-coroutine-handlers, 06-modules/http/14-streaming-and-cookies
 * @expect "[sse] event stream served on http://127.0.0.1:18085/events (curl -N to watch it)"
 * @expect "[sse] /events streamed 4 events 150 ms apart, each taken by the session as it was sent, then ended"
 * @expect "[client] 3 events received, last id 3"
 * @expect "[client] reconnected with Last-Event-ID: 3 -> the stream resumed at event 4"
 * @expect "[client] heartbeat comment lines arrived between the events (every 100 ms here; 15 s is a production value)"
 * @expect "=== sse up: Ctrl+C to stop ==="
 *
 * WHAT AN EVENT STREAM IS
 * -----------------------
 * A response that never ends, or ends when the server decides, whose body is a sequence of events in
 * a text format every browser's `EventSource` reads: `event: name`, `data: payload` (one line per line
 * of data), `id: 7`, `retry: 3000`, and a blank line that ends the event. A line starting with `:` is a
 * comment, which clients ignore -- the heartbeat form. It is HTTP: the same port, the same router, the
 * same middleware, no upgrade. What it needs from the server is to put each event on the wire WHEN IT
 * HAPPENS, which is exactly what `ctx->begin_stream()` does and what `ctx->begin_sse()` wraps with the
 * head of an event stream (`Content-Type: text/event-stream; charset=utf-8`, `Cache-Control: no-cache`,
 * and `Connection: keep-alive` on HTTP/1.1 alone).
 *
 * THREE THINGS THE HANDLER OWNS
 * -----------------------------
 * 1. The pieces: `send()` writes an event now and tells you whether it went (`taken`), must wait for
 *    the client to catch up (`wait`), or cannot go because the client left (`closed`). A coroutine
 *    handler writes `co_await events->send_async(event)` instead and gets the waiting for free.
 * 2. The end: `events->end()` closes the message and completes the context. Until then the context
 *    is the stream's: `ctx->complete()` is ignored, so the handler may hand the stream to a timer
 *    chain or a subscription and return. Dropping the handle before `end()` aborts the response.
 * 3. The life of the connection: a stream that writes nothing for the session's inactivity timeout
 *    ends like a client that left. `heartbeat(interval)` writes a comment line on a timer that holds
 *    the stream weakly, so it stops by itself when the stream is gone.
 *
 * THE CLIENT RESUMES
 * ------------------
 * A browser that loses the connection reconnects by itself, after the `retry:` the server last sent,
 * with the `id:` of the last event it saw in a `Last-Event-ID` header. `qb::http::sse::last_event_id()`
 * reads it; this route starts again right after it.
 *
 * The client below is the one-shot `qb::http::GET`, which hands the body over whole once the stream
 * ends -- enough to show the format and the resumption. A client that reads events as they arrive is
 * `EventSource` in a browser, or `curl -N` against the URL this program prints.
 *
 * Build:
 *   cmake --preset release
 *   cmake --build --preset release --target qb-example-modules-http-sse
 * Run:
 *   ./build/presets/release/examples/06-modules/http/qb-example-modules-http-sse
 */

#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <qb/io/async/coroutine.h>
#include <qb/main.h>
#include <qbm/http/http.h>

using namespace std::chrono_literals;

namespace {

constexpr std::uint16_t kPort = 18085;

std::string
url(std::string_view path) {
    return "http://127.0.0.1:" + std::to_string(kPort) + std::string(path);
}

/// The events of a `text/event-stream` body, counted, the `id:` of the first and the last, the comment lines seen.
struct ParsedEvents {
    int         count    = 0;
    int         comments = 0;
    std::string first_id;
    std::string last_id;
};

/// The value of the first `name:` line of `block`, read line by line (a `data:` line may hold "id: " inside it),
/// or nothing when the block has no such line.
std::optional<std::string_view>
field(std::string_view block, std::string_view name) {
    while (!block.empty()) {
        const auto       eol  = block.find('\n');
        std::string_view line = block.substr(0, eol);
        block.remove_prefix(eol == std::string_view::npos ? block.size() : eol + 1);
        if (line.size() > name.size() && line.substr(0, name.size()) == name && line[name.size()] == ':') {
            line.remove_prefix(name.size() + 1);
            if (!line.empty() && line.front() == ' ')
                line.remove_prefix(1); // one space after the colon is not part of the value
            return line;
        }
    }
    return std::nullopt;
}

ParsedEvents
parse_events(std::string_view body) {
    ParsedEvents parsed;
    while (!body.empty()) {
        const auto       end   = body.find("\n\n");
        std::string_view block = body.substr(0, end);
        body.remove_prefix(end == std::string_view::npos ? body.size() : end + 2);
        if (block.empty())
            continue; // a blank gap
        if (block.front() == ':') {
            ++parsed.comments; // a comment line: the heartbeat, or a note; a client ignores it
            continue;
        }
        if (!field(block, "data"))
            continue; // a `retry:` line alone dispatches nothing
        ++parsed.count;
        if (const auto id = field(block, "id"); id && !id->empty()) {
            parsed.last_id = std::string(*id);
            if (parsed.first_id.empty())
                parsed.first_id = parsed.last_id;
        }
    }
    return parsed;
}

/// Printed once, from the first run of /events: every event was taken by the session as it was sent (a measurement
/// of the stream, not a promise: a buffered stream would read `buffered` and gather them until the end).
bool g_streaming_reported = false;

class SseServer
    : public qb::Actor
    , public qb::http::Server<> {
public:
    qb::io::async::task<bool>
    onInit() override {
        registerEvent<qb::KillEvent>(*this);
        registerEvent<qb::SignalEvent>(*this);

        // ---- the event stream -------------------------------------------------------------------
        router().get("/events", [](auto ctx) -> qb::io::async::task<void> {
            // Resume after the id the client last saw, or start at 1: a browser reconnects with it. The value is
            // the client's: parse it without throwing (std::from_chars leaves `last_seen` at 0 on anything else).
            const std::string &last      = qb::http::sse::last_event_id(ctx->request());
            std::uint32_t      last_seen = 0; // bounded: a signed `+ 1` on a client's INT_MAX would overflow
            std::from_chars(last.data(), last.data() + last.size(), last_seen);
            std::uint64_t next = std::uint64_t{last_seen} + 1;

            // The head leaves here: 200, text/event-stream, the retry the client keeps, a heartbeat. 100 ms here,
            // so that a run of half a second shows it; 15 s is a production value (under every proxy's idle timeout).
            auto events = ctx->begin_sse(qb::http::sse::Options{.retry = 3000ms, .heartbeat = 100ms});
            if (!events)
                co_return; // the response cannot be streamed (answered 500), or the request had already ended

            bool every_event_taken = events->stream()->framing() != qb::http::stream_framing::buffered;
            for (int i = 0; i < 3; ++i, ++next) {
                qb::http::sse::Event event{.name = "tick", .data = "{\"n\": " + std::to_string(next) + "}", .id = std::to_string(next)};
                // send_async: written now; waits for the client to catch up when the stream asks for it;
                // false once the client is gone or the stream ended -- the only thing left to do is stop.
                if (!co_await events->send_async(event))
                    co_return;
                co_await qb::io::async::sleep(150ms);
            }
            // send(): the same write, with the stream's verdict instead of the wait -- for a producer without a coroutine.
            const auto verdict =
                events->send(qb::http::sse::Event{.name = "batch-end", .data = std::to_string(events->events_sent()) + " events sent"});
            every_event_taken = every_event_taken && verdict == qb::http::ResponseStream<qb::http::DefaultSession>::write_result::taken;
            (void) events->comment("end of this batch");
            if (every_event_taken && events->events_sent() == 4 && !g_streaming_reported) {
                g_streaming_reported = true;
                std::cout << "[sse] /events streamed 4 events 150 ms apart, each taken by the session as it was sent, then ended" << std::endl;
            }
            events->end(); // completes the context; never ctx->complete() for a stream
        });

        router().get("/health", [](auto ctx) { ctx->text("ok"); });
        router().compile();

        // A fixed port: a second instance refuses it, which the corpus asserts.
        if (!listen(qb::io::uri(url("")))) {
            std::cerr << "[sse] port " << kPort << " is taken: another instance is up" << std::endl;
            co_return false;
        }
        std::cout << "[sse] event stream served on http://127.0.0.1:18085/events (curl -N to watch it)" << std::endl;

        // ---- a client in the same process, on the same loop ---------------------------------
        spawn([](qb::ScopedCoroContext) -> qb::io::async::task<void> {
            auto first = co_await qb::http::GET(qb::http::Request{qb::io::uri(url("/events"))}, 5s);
            if (first.response.status() != qb::http::status::OK
                || first.response.header("Content-Type").find("text/event-stream") == std::string::npos) {
                std::cerr << "[client] unexpected answer: " << first.response.status().code() << std::endl;
                co_return;
            }
            const auto got = parse_events(first.response.body().template as<std::string_view>());
            // Gated on the measurement: three ticks and the batch-end event, the last id the third tick's.
            if (got.count == 4 && got.last_id == "3")
                std::cout << "[client] 3 events received, last id 3" << std::endl;
            else
                std::cout << "[client] UNEXPECTED: " << got.count << " events received, last id " << got.last_id << std::endl;

            qb::http::Request resume{qb::io::uri(url("/events"))};
            resume.set_header("Last-Event-ID", got.last_id);
            auto       second  = co_await qb::http::GET(std::move(resume), 5s);
            const auto resumed = parse_events(second.response.body().template as<std::string_view>());
            if (resumed.first_id == "4")
                std::cout << "[client] reconnected with Last-Event-ID: 3 -> the stream resumed at event 4" << std::endl;
            else
                std::cout << "[client] UNEXPECTED: the stream resumed at event " << resumed.first_id << std::endl;
            // Gated on the measurement: the stream lived ~450 ms with a 100 ms heartbeat, so comment lines came
            // between the events (each stream ends with one `: end of this batch` note; a dead timer would leave only those two).
            if (got.comments + resumed.comments >= 4)
                std::cout << "[client] heartbeat comment lines arrived between the events (every 100 ms here; 15 s is a production value)"
                          << std::endl;
            else
                std::cout << "[client] UNEXPECTED: " << got.comments + resumed.comments << " comment lines in two streams" << std::endl;
            std::cout << "=== sse up: Ctrl+C to stop ===" << std::endl;
        });
        co_return true;
    }

    void
    on(qb::SignalEvent const &) noexcept {
        push<qb::KillEvent>(id());
    }
    void
    on(qb::KillEvent const &) noexcept {
        kill();
    }
};

} // namespace

int
main() {
    qb::Main engine;
    engine.addActor<SseServer>(0);
    engine.start(false);
    engine.join();
    return engine.hasError() ? 1 : 0;
}
