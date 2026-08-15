/**
 * @file examples/06-modules/http/14-streaming-and-cookies.cpp
 * @tier 06-modules
 * @teaches The parts of an HTTP message the other thirteen programs never touch: chunked framing
 *          and the `Chunk` builder, cookies with their attributes and a `CookieJar`, a url-encoded
 *          `Form`, a multipart body you BUILD rather than parse, `Body::compress`, and the HTTP
 *          date helpers that make a conditional GET answer 304.
 * @demonstrates qb::http::Chunk, add_chunk, add_final_chunk, qb::http::Cookie,
 *               qb::http::CookieJar, qb::http::SameSite, qb::http::parse_set_cookie, add_cookie,
 *               cookie, cookie_value, http_only, same_site, max_age, path, to_header,
 *               qb::http::Form, qb::http::Multipart, create_part, parts, boundary,
 *               qb::http::date::format_http_date, qb::http::date::parse_http_date,
 *               compress, uncompress, as<qb::http::Form>, as<qb::http::Multipart>,
 *               qb::http::GET, qb::http::POST, qb::http::status::NOT_MODIFIED,
 *               qb::http::use<AssetServer>::server<AssetSession>, router, compile, listen_v4,
 *               qb::io::async::init, qb::io::async::run_until, qb::io::async::task<void>
 * @prerequisites 06-modules/http/10-client
 * @expect "[server] one asset server on an ephemeral port, on this process's own event loop"
 * @expect "[chunked] Transfer-Encoding: chunked is a FRAMING, not a payload change — the server"
 * @expect "[chunk] ...and Chunk/add_chunk/add_final_chunk is the builder for when YOU are the one"
 * @expect "[cookie] a cookie is a header with attributes, and the attributes are the security:"
 * @expect "[jar] the client keeps a CookieJar, sends it back as one Cookie header, and the server"
 * @expect "[form] a url-encoded Form is a MULTI-map: one key can carry several values, which is"
 * @expect "[multipart] built rather than parsed: create_part() per part, the boundary is generated"
 * @expect "           (note: the one-shot verbs and qb::http1::Client compress the body THEMSELVES when"
 * @expect "[date] a conditional GET: Last-Modified out, If-Modified-Since back, and 304 when"
 * @expect "=== streaming and cookies complete ==="
 *
 * WHY THESE SIX ARE ONE PROGRAM
 * -----------------------------
 * They are the message-level surface. Every other HTTP example here is about ROUTING (who handles
 * this?) or about the CONNECTION (keep-alive, TLS, h2). These six are about what is actually in
 * the bytes, and each of them had zero demonstrators: `Chunk`, `Form`, `Cookie`/`CookieJar`,
 * building a `Multipart`, `Body::compress`, and the date helpers.
 *
 * THE TWO HALVES OF CHUNKED, WHICH ARE EASY TO CONFLATE
 * ------------------------------------------------------
 * Setting `Transfer-Encoding: chunked` on a message makes the SERIALISER emit the chunked wire
 * form for whatever body you assigned, and makes the parser at the other end put it back
 * together. Neither side's `body()` ever contains chunk headers — that is framing, and framing is
 * the layer's job.
 *
 * `Chunk` + `add_chunk` + `add_final_chunk` is the other half: it appends the chunk-encoded BYTES
 * to a body yourself, for when you are producing that wire form directly rather than handing a
 * message to the serialiser. Use one or the other. Doing both encodes twice, and the peer sees
 * chunk headers as payload.
 *
 * COOKIES ARE ATTRIBUTES, AND THE ATTRIBUTES ARE THE POINT
 * --------------------------------------------------------
 * `Set-Cookie: sid=abc` is a session that JavaScript can read, that is sent to every subdomain and
 * that survives no restart. `HttpOnly` + `Secure` + `SameSite=Strict` + a `Max-Age` are what turn
 * it into a session. This program sets them, serialises them (`to_header()`), parses them back
 * (`parse_set_cookie`), keeps them in a `CookieJar` and sends them again — the whole round trip,
 * because the parse half is the half everybody writes by hand and gets wrong.
 *
 * The server is hosted by this program on an ephemeral loopback port and shares the client's event
 * loop, so nothing here needs a network and every claim is measured end to end.
 *
 * Build:
 *   cmake --preset release
 *   cmake --build --preset release --target qb-example-modules-http-streaming-and-cookies
 * Run:
 *   ./build/presets/release/examples/06-modules/http/qb-example-modules-http-streaming-and-cookies
 */

#include <chrono>
#include <cstdint>
#include <string>
#include <qb/io/async.h>
#include <qb/io/async/coroutine.h>
#include <qbm/http/date.h>
#include <qbm/http/http.h>

using namespace std::chrono_literals;

namespace {

std::uint16_t g_port = 0;

std::string
url(std::string const &path) {
    return "http://127.0.0.1:" + std::to_string(g_port) + path;
}

qb::http::Request
request(qb::http::method m, std::string const &target) {
    return qb::http::Request{m, qb::io::uri(target)};
}

// A fixed instant, so the conditional-GET section compares against something this program owns
// rather than against the clock.
const std::chrono::system_clock::time_point ASSET_MTIME = std::chrono::system_clock::now() - 24h;

class AssetServer;

class AssetSession : public qb::http::use<AssetSession>::session<AssetServer> {
public:
    explicit AssetSession(AssetServer &server);
};

class AssetServer : public qb::http::use<AssetServer>::server<AssetSession> {
public:
    AssetServer() {
        // ---- chunked: a body assigned normally, framed as chunks by the serialiser -------
        router().get("/stream", [](auto ctx) {
            ctx->response().set_header("Transfer-Encoding", "chunked");
            ctx->response().body() = "one|two|three";
            ctx->complete();
        });

        // ---- cookies: set with attributes, read back on the next request -----------------
        router().get("/login", [](auto ctx) {
            qb::http::Cookie session{"sid", "s3cr3t-token"};
            session.path("/").http_only(true).same_site(qb::http::SameSite::Strict).max_age(1h);
            ctx->response().add_cookie(std::move(session));
            ctx->response().body() = "logged in";
            ctx->complete();
        });
        router().get("/whoami", [](auto ctx) {
            // The framework has already parsed the Cookie header into the request's jar.
            ctx->response().body() = "sid=" + ctx->request().cookie_value("sid");
            ctx->complete();
        });

        // ---- a url-encoded form ------------------------------------------------------------
        router().post("/search", [](auto ctx) {
            qb::http::Form form = ctx->request().body().template as<qb::http::Form>();
            // `get()` hands back a VECTOR (a copy) — a form key may legally repeat, and
            // `get_first()` is the accessor for the case where you know it does not.
            const std::vector<std::string> tags = form.get("tag");
            ctx->response().body()              = form.get_first("q").value_or("") + "/" + std::to_string(tags.size());
            ctx->complete();
        });

        // ---- a multipart upload, parsed --------------------------------------------------
        router().post("/upload", [](auto ctx) {
            qb::http::Multipart mp = ctx->request().body().template as<qb::http::Multipart>();
            std::string         summary;
            for (auto const &part : mp.parts())
                summary += part.body + ";";
            ctx->response().body() = std::to_string(mp.parts().size()) + ":" + summary;
            ctx->complete();
        });

        // ---- a compressed request body ----------------------------------------------------
        router().post("/ingest", [](auto ctx) {
#ifdef QB_HAS_COMPRESSION
            // The receiving side decodes explicitly. The server does NOT do this for you: a
            // Content-Encoding on an inbound request is a statement about the bytes, and it is
            // your handler that decides whether to spend the CPU on them.
            const std::string enc = ctx->request().header("Content-Encoding");
            if (!enc.empty())
                ctx->request().body().uncompress(enc);
#endif
            ctx->response().body() = std::to_string(ctx->request().body().template as<std::string>().size());
            ctx->complete();
        });

        // ---- a conditional GET ---------------------------------------------------------------
        router().get("/asset", [](auto ctx) {
            const std::string since = ctx->request().header("If-Modified-Since");
            // Second resolution: HTTP dates carry no sub-second part, so a comparison must be
            // "not newer than", never "equal to a time_point you still hold in memory".
            if (auto parsed = qb::http::date::parse_http_date(since);
                parsed && *parsed >= std::chrono::floor<std::chrono::seconds>(ASSET_MTIME)) {
                ctx->response().status() = qb::http::status::NOT_MODIFIED;
                ctx->complete();
                return;
            }
            ctx->response().set_header("Last-Modified", qb::http::date::format_http_date(ASSET_MTIME));
            ctx->response().body() = "the asset bytes";
            ctx->complete();
        });

        router().compile();
    }

    void
    on(IOSession &) {}
};

AssetSession::AssetSession(AssetServer &server)
    : session(server) {}

} // namespace

qb::io::async::task<void>
run_message_surface(bool &running, bool &ok) {
    struct StopOnExit {
        bool &r;
        ~StopOnExit() {
            r = false;
        }
    } stop{running};

    // ---- 1. chunked framing, both halves --------------------------------------------------
    auto       streamed = co_await qb::http::GET(request(qb::http::method::GET, url("/stream")));
    const bool framed_ok =
        streamed.response.status() == qb::http::status::OK && streamed.response.body().template as<std::string>() == "one|two|three";
    qb::io::cout() << (framed_ok ? "[chunked] Transfer-Encoding: chunked is a FRAMING, not a payload change — the server\n"
                                   "          assigned a plain body, the serialiser emitted chunk headers, and the client's\n"
                                   "          body() holds the reassembled bytes with no trace of them\n"
                                 : "[chunked] UNEXPECTED: the chunked response did not reassemble\n");
    qb::io::cout() << "          (received '" << streamed.response.body().template as<std::string>() << "')\n";

    // The other half: build the chunk-encoded bytes by hand. `5\r\nhello\r\n` then the terminator.
    qb::http::Body handmade;
    handmade.add_chunk(qb::http::Chunk("hello", 5));
    handmade.add_final_chunk();
    const std::string wire     = handmade.template as<std::string>();
    const bool        chunk_ok = wire == "5\r\nhello\r\n0\r\n\r\n";
    qb::io::cout() << (chunk_ok ? "[chunk] ...and Chunk/add_chunk/add_final_chunk is the builder for when YOU are the one\n"
                                  "        producing that wire form. Never both: a chunked body plus the header encodes\n"
                                  "        twice, and the peer reads the chunk headers as payload\n"
                                : "[chunk] UNEXPECTED: the hand-built chunked body was not 5\\r\\nhello\\r\\n0\\r\\n\\r\\n\n");
    qb::io::cout() << "        (" << wire.size() << " bytes, size-line first, zero-length chunk last)\n\n";

    // ---- 2. cookies, out and back ------------------------------------------------------------
    auto login = co_await qb::http::GET(request(qb::http::method::GET, url("/login")));

    // Parse the Set-Cookie header the way a client must: the header is a STRING with attributes,
    // and parse_set_cookie is what turns it back into a Cookie you can inspect.
    std::optional<qb::http::Cookie> parsed = qb::http::parse_set_cookie(login.response.header("Set-Cookie"));
    const bool attrs_ok = parsed && parsed->name() == "sid" && parsed->http_only() && parsed->same_site() == qb::http::SameSite::Strict
                          && parsed->path() == "/" && parsed->max_age().has_value();
    qb::io::cout() << (attrs_ok ? "[cookie] a cookie is a header with attributes, and the attributes are the security:\n"
                                  "         HttpOnly keeps it away from script, SameSite=Strict keeps it off cross-site\n"
                                  "         requests, Max-Age gives it an end. A bare name=value has none of that\n"
                                : "[cookie] UNEXPECTED: the Set-Cookie header did not carry the attributes that were set\n");
    qb::io::cout() << "         (" << (parsed ? parsed->to_header() : std::string("<unparsed>")) << ")\n";

    // A jar is the client's store. Sending it back is one header with every applicable cookie.
    qb::http::CookieJar jar;
    if (parsed)
        jar.add(*parsed);
    std::string cookie_header;
    for (auto const &[name, cookie] : jar.all())
        cookie_header += (cookie_header.empty() ? "" : "; ") + name + "=" + cookie.value();

    auto whoami = request(qb::http::method::GET, url("/whoami"));
    whoami.set_header("Cookie", cookie_header);
    auto identified = co_await qb::http::GET(std::move(whoami));

    const bool jar_ok = jar.size() == 1 && jar.has("sid") && identified.response.body().template as<std::string>() == "sid=s3cr3t-token";
    qb::io::cout() << (jar_ok ? "[jar] the client keeps a CookieJar, sends it back as one Cookie header, and the server\n"
                                "      reads it with request().cookie_value(name) — the attributes are NOT sent back, only\n"
                                "      the name and value, because they were instructions to the client\n"
                              : "[jar] UNEXPECTED: the cookie did not survive the round trip\n");
    qb::io::cout() << "      (sent '" << cookie_header << "', server answered '" << identified.response.body().template as<std::string>()
                   << "')\n\n";

    // ---- 3. a url-encoded form ----------------------------------------------------------------
    qb::http::Form outgoing;
    outgoing.add("q", "actor framework");
    outgoing.add("tag", "cpp");
    outgoing.add("tag", "async"); // the SAME key twice — legal, and the reason get() returns a list
    auto form_post = request(qb::http::method::POST, url("/search"));
    form_post.set_header("Content-Type", "application/x-www-form-urlencoded");
    form_post.body() = outgoing;

    auto       searched = co_await qb::http::POST(std::move(form_post));
    const bool form_ok  = searched.response.body().template as<std::string>() == "actor framework/2";
    qb::io::cout() << (form_ok ? "[form] a url-encoded Form is a MULTI-map: one key can carry several values, which is\n"
                                 "       why get() hands back a list and get_first() exists for the common case. Encoding\n"
                                 "       and decoding are the Body's job — the spaces above never became %20 by hand\n"
                               : "[form] UNEXPECTED: the form did not round-trip as 'actor framework' + 2 tags\n");
    qb::io::cout() << "       (server saw q + " << searched.response.body().template as<std::string>() << " tag(s))\n\n";

    // ---- 4. a multipart body, BUILT --------------------------------------------------------------
    qb::http::Multipart upload;
    auto               &field = upload.create_part();
    field.set_header("Content-Disposition", "form-data; name=\"title\"");
    field.body = "holiday photo";
    auto &file = upload.create_part();
    file.set_header("Content-Disposition", "form-data; name=\"image\"; filename=\"beach.txt\"");
    file.set_header("Content-Type", "text/plain");
    file.body = "not really a jpeg";

    auto multi = request(qb::http::method::POST, url("/upload"));
    multi.set_header("Content-Type", "multipart/form-data; boundary=" + upload.boundary());
    multi.body() = upload;

    auto       uploaded = co_await qb::http::POST(std::move(multi));
    const bool multi_ok = uploaded.response.body().template as<std::string>() == "2:holiday photo;not really a jpeg;";
    qb::io::cout() << (multi_ok ? "[multipart] built rather than parsed: create_part() per part, the boundary is generated\n"
                                  "            for you (and must also go in the Content-Type header, which is the step\n"
                                  "            everybody forgets), and the far side gets it back with body().as<Multipart>()\n"
                                : "[multipart] UNEXPECTED: the two parts did not arrive intact\n");
    qb::io::cout() << "            (boundary " << upload.boundary().substr(0, 16) << "…, server read '"
                   << uploaded.response.body().template as<std::string>() << "')\n\n";

    // ---- 5. compression ---------------------------------------------------------------------------
#ifdef QB_HAS_COMPRESSION
    constexpr bool have_compression = true;
#else
    constexpr bool have_compression = false;
#endif
    bool        compress_ok = false;
    std::size_t plain_size = 0, gz_size = 0, restored = 0;
    std::string server_saw;
#ifdef QB_HAS_COMPRESSION
    // A payload that actually compresses. Random bytes would not, and an example that shrinks
    // nothing teaches the opposite of its subject.
    std::string payload;
    for (int i = 0; i < 200; ++i)
        payload += "the quick brown fox jumps over the lazy dog. ";
    plain_size = payload.size();

    // (a) the API on its own, with no wire involved: compress() replaces the body and returns the
    //     new size; uncompress() puts it back. Neither one writes a header.
    qb::http::Body local;
    local    = payload;
    gz_size  = local.compress("gzip");
    restored = local.uncompress("gzip");

    // (b) over the wire — and here the body is assigned PLAIN. See the warning below.
    auto ingest   = request(qb::http::method::POST, url("/ingest"));
    ingest.body() = payload;
    ingest.set_header("Content-Encoding", "gzip");
    auto ingested = co_await qb::http::POST(std::move(ingest));
    server_saw    = ingested.response.body().template as<std::string>();

    compress_ok = gz_size < plain_size && restored == plain_size && server_saw == std::to_string(plain_size);
#endif
    if (!have_compression)
        qb::io::cout() << "[compress] SKIPPED: this build has QB_HAS_COMPRESSION off, so Body::compress() does not\n"
                          "           exist in it. The five sections around this one do not depend on it\n";
    else
        qb::io::cout() << (compress_ok ? "[compress] Body::compress(\"gzip\") replaces the body with the compressed bytes and\n"
                                         "           returns the new size; uncompress() puts it back. Neither writes a header —\n"
                                         "           Content-Encoding is yours to set, and it is what the peer decodes by\n"
                                       : "[compress] UNEXPECTED: the gzip round trip did not come back as the original size\n");
    if (have_compression)
        qb::io::cout() << "           (" << plain_size << " bytes -> " << gz_size << " -> " << restored
                       << " locally; over the wire the server decoded " << server_saw << ")\n";

    // MEASURED, and it is the one thing to get right here: the ONE-SHOT VERBS COMPRESS FOR YOU.
    // `qb::http::POST` (1.1/http.h:726-731) and `qb::http1::Client` (1.1/client.cpp:122) both do
    // `if (has_header("Content-Encoding")) body().compress(...)` just before sending. So the
    // body above is assigned PLAIN on purpose: calling compress() yourself as well encodes it
    // TWICE. Measured on this tree — 9000 bytes -> 108 gzip -> 109 on the wire (gzip of gzip),
    // and the server's single uncompress() then hands back 108 bytes of gzip that look like a
    // corrupt payload. There is no diagnostic; the only symptom is a body the peer cannot read.
    qb::io::cout() << "           (note: the one-shot verbs and qb::http1::Client compress the body THEMSELVES when\n"
                      "            Content-Encoding is set, so do not also call compress() — that encodes twice)\n";
    qb::io::cout() << "           (and always bound what you decompress: a small compressed body can expand to\n"
                      "            gigabytes — see 02-io/10-crypto-and-compression for the bounded form)\n\n";

    // ---- 6. HTTP dates and a conditional GET --------------------------------------------------------
    auto              fresh      = co_await qb::http::GET(request(qb::http::method::GET, url("/asset")));
    const std::string last_mod   = fresh.response.header("Last-Modified");
    auto              round_trip = qb::http::date::parse_http_date(last_mod);

    auto conditional = request(qb::http::method::GET, url("/asset"));
    conditional.set_header("If-Modified-Since", last_mod);
    auto not_modified = co_await qb::http::GET(std::move(conditional));

    const bool date_ok = fresh.response.status() == qb::http::status::OK && !last_mod.empty() && round_trip.has_value()
                         && not_modified.response.status() == qb::http::status::NOT_MODIFIED;
    qb::io::cout() << (date_ok ? "[date] a conditional GET: Last-Modified out, If-Modified-Since back, and 304 when\n"
                                 "       nothing changed — the body is not sent twice. format_http_date/parse_http_date are\n"
                                 "       the RFC 7231 pair; never std::format a date into a header by hand\n"
                               : "[date] UNEXPECTED: the conditional GET did not answer 304\n");
    qb::io::cout() << "       (Last-Modified: " << last_mod << " -> second request: " << static_cast<int>(not_modified.response.status())
                   << ")\n\n";

    // The compression half is only asserted where the build HAS compression; everything else
    // is unconditional.
    ok = framed_ok && chunk_ok && attrs_ok && jar_ok && form_ok && multi_ok && date_ok && (!have_compression || compress_ok);
    qb::io::cout() << "=== streaming and cookies complete ===\n";
    co_return;
}

int
main() {
    qb::io::async::init();

    AssetServer server;
    if (server.transport().listen_v4(0, "127.0.0.1") != 0) {
        qb::io::cerr() << "[fatal] could not bind a loopback port\n";
        return 1;
    }
    server.start();
    g_port = server.transport().local_endpoint().port();
    qb::io::cout() << "[server] one asset server on an ephemeral port, on this process's own event loop\n"
                   << "         (127.0.0.1:" << g_port << ")\n\n";

    bool running = true;
    bool ok      = false;
    qb::io::async::coro_scheduler().spawn(run_message_surface(running, ok));
    qb::io::async::run_until(running);

    return ok ? 0 : 1;
}
