/**
 * @file examples/02-io/06-framing-toolbox.cpp
 * @tier 02-io
 * @teaches Most wire formats need NO parser. Six framings — a delimiter byte, a delimiter
 *          sequence, an 8-bit length prefix, a 32-bit length prefix, JSON and MessagePack — each
 *          expressed in a handful of lines by reusing a shipped archetype, and all six decoded out
 *          of ONE write per connection, because TCP has no message boundaries and the archetype is
 *          what supplies them.
 * @demonstrates qb::protocol::base::byte_terminated, qb::protocol::base::bytes_terminated,
 *               qb::protocol::base::size_as_header, qb::protocol::text::binary8,
 *               qb::protocol::json, qb::protocol::json_packed,
 *               onMessage, shiftSize, Header, not_ok,
 *               qb::io::use<T>::tcp::server<S>, qb::io::use<T>::tcp::client<S>, qb::io::tcp::socket,
 *               transport().listen_v4, local_endpoint, qb::io::async::event::disconnected,
 *               qb::io::async::init, qb::io::async::run_until, qb::io::async::callback, qb::json
 * @prerequisites 02-io/03-tcp
 * @expect "=== qb-io: the framing toolbox ==="
 * @expect "[pipe] record: "
 * @expect "[crlf] line: "
 * @expect " payload bytes, first byte 0x"
 * @expect "[size32] frame of "
 * @expect "[json] parsed object, id="
 * @expect "[msgpack] parsed object, id="
 * @expect "[pipe] empty record REFUSED by the protocol"
 * @expect "[json] over-nested payload REFUSED by the depth bound"
 * @expect "=== every framing decoded from a single write ==="
 *
 * THE PROBLEM EVERY TCP PROGRAM HAS
 * ---------------------------------
 * A stream socket delivers BYTES, not messages: two `write()`s can arrive as one `read()`, and one
 * `write()` can arrive as five. So every protocol needs a rule saying where a message ends, and
 * writing that rule by hand is where the corpus's largest single file came from — 745 lines of
 * hand-written parser in `02-io/05-custom-protocol.cpp`, most of it re-deriving `base.h`:
 *
 *   `base::byte_terminated<IO, C>`      end at the next `C`.        `\0`, `\n`, `|`, …
 *   `base::bytes_terminated<IO, Trait>` end at the next SEQUENCE.   `\r\n`, `\r\n\r\n`, …
 *   `base::size_as_header<IO, T>`       a `T` length prefix, in NETWORK byte order for 16/32 bits.
 *
 * Each supplies `getMessageSize()` — the framing — and nothing else. You supply `onMessage()`, the
 * MEANING: three lines handing the bytes to your handler as whatever type your program wants. That
 * split is why the three custom protocols below are eight lines each, and `qb/io/protocol/text.h`
 * and `json.h` are the same two halves pre-written — `text::command` is `byte_terminated<'\n'>`
 * yielding a `std::string`, `text::binary8/16/32` are `size_as_header` yielding a pointer and a
 * length, and `json`/`json_packed` are `byte_terminated<'\0'>` yielding a parsed document.
 *
 * FRAMING IS ALSO A BOUND, AND THAT IS THE PART TO REMEMBER
 * --------------------------------------------------------
 * A protocol may call `not_ok()`, and the session is then disposed with `reason == -1`. The shipped
 * ones use it: `size_as_header` refuses a zero-length frame, and `protocol::json` pre-scans
 * linearly and refuses anything nested deeper than 512 — nlohmann's parser is recursive-descent,
 * and a few kilobytes of `[[[[…` exhausts the stack, which no try/catch can recover. The last
 * section sends exactly that and watches the SESSION go down instead of the process.
 *
 * Build and run:
 *   cmake --build --preset release --target qb-example-io-framing-toolbox
 *   ./build/presets/release/examples/02-io/qb-example-io-framing-toolbox
 */

#include <chrono>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>
#include <qb/io.h>
#include <qb/io/async.h>
#include <qb/io/protocol/json.h>
#include <qb/io/protocol/text.h>
#include <qb/io/tcp/socket.h>
#include <qb/json.h>

using namespace std::chrono_literals;

namespace {

int  g_pending = 0;    // messages still expected across every server
bool g_running = true; // `async::run_until` loops WHILE this is true
void
decoded() {
    if (--g_pending <= 0)
        g_running = false;
}

// ======================================================== three protocols, written here
//
// All three are the same shape: inherit an archetype, override ONE function. The framing, the
// buffer bookkeeping, the resume-where-we-left-off scan and `reset()` all come from the base.

/// Records separated by a single `|`. `shiftSize()` is the base's own "the payload is the frame
/// minus the delimiter" arithmetic. This one also shows a protocol's other job: `not_ok()` is how
/// the framing layer REFUSES input, and the session is then disposed with `reason == -1`.
template <typename IO_>
class PipeRecords : public qb::protocol::base::byte_terminated<IO_, '|'> {
public:
    using base_t = qb::protocol::base::byte_terminated<IO_, '|'>;
    using base_t::base_t;
    struct message {
        std::string_view record;
    };

    void
    onMessage(std::size_t size) noexcept final {
        const auto payload = this->shiftSize(size);
        if (payload == 0) {
            // An empty record (`||`) is malformed. Refusing ONE frame is not an option a stream
            // protocol has — both ends have already disagreed about the format — so the answer is
            // to invalidate the protocol and let the session go down.
            this->not_ok();
            return;
        }
        this->_io.on(message{std::string_view(this->_io.in().cbegin(), payload)});
    }
};

/// Lines ending in CRLF. `bytes_terminated` takes a TRAIT carrying the sequence, because a
/// multi-byte delimiter cannot be a template `char` parameter.
struct CRLF {
    static constexpr char _EndBytes[] = "\r\n";
};

template <typename IO_>
class CrlfLines : public qb::protocol::base::bytes_terminated<IO_, CRLF> {
public:
    using base_t = qb::protocol::base::bytes_terminated<IO_, CRLF>;
    using base_t::base_t;
    struct message {
        std::string_view line;
    };

    void
    onMessage(std::size_t size) noexcept final {
        this->_io.on(message{std::string_view(this->_io.in().cbegin(), this->shiftSize(size))});
    }
};

/// A 32-bit big-endian length prefix. The archetype consumed the header before `onMessage` runs,
/// so `size` is the PAYLOAD length and `in().cbegin()` is the payload.
template <typename IO_>
class Length32 : public qb::protocol::base::size_as_header<IO_, std::uint32_t> {
public:
    using base_t = qb::protocol::base::size_as_header<IO_, std::uint32_t>;
    using base_t::base_t;
    struct message {
        std::size_t size;
        const char *data;
    };

    void
    onMessage(std::size_t size) noexcept final {
        this->_io.on(message{size, this->_io.in().cbegin()});
    }
};

// ================================================================= one session per framing
//
// Every one is the same nine lines with a different `Protocol` alias, and the repetition IS the
// lesson: the framing is a TYPE, so changing wire format touches no handler.

class PipeServer;
class PipeSession : public qb::io::use<PipeSession>::tcp::client<PipeServer> {
public:
    using Protocol = PipeRecords<PipeSession>;
    explicit PipeSession(IOServer &server)
        : client(server) {}
    void
    on(Protocol::message &&msg) {
        qb::io::cout() << "[pipe] record: " << msg.record << "\n";
        decoded();
    }

    void
    on(qb::io::async::event::disconnected &&e) {
        if (e.reason == -1) {
            qb::io::cout() << "[pipe] empty record REFUSED by the protocol; session closed with reason -1\n";
            decoded();
        }
    }
};
class PipeServer : public qb::io::use<PipeServer>::tcp::server<PipeSession> {};

class CrlfServer;
class CrlfSession : public qb::io::use<CrlfSession>::tcp::client<CrlfServer> {
public:
    using Protocol = CrlfLines<CrlfSession>;
    explicit CrlfSession(IOServer &server)
        : client(server) {}
    void
    on(Protocol::message &&msg) {
        qb::io::cout() << "[crlf] line: " << msg.line << "\n";
        decoded();
    }
};
class CrlfServer : public qb::io::use<CrlfServer>::tcp::server<CrlfSession> {};

// `text::command` has no session here on purpose: it is PipeRecords with a different delimiter and
// message type, and `02-io/03-tcp.cpp` already runs it end to end. (`text::string` is the
// NUL-terminated twin; `command_view`/`string_view` yield a `std::string_view` into the buffer.)

class Binary8Server;
class Binary8Session : public qb::io::use<Binary8Session>::tcp::client<Binary8Server> {
public:
    // `size_as_header<uint8_t>` + an `onMessage` yielding {size, data}. binary16/32 are the same
    // alias with a wider prefix, and a wider ceiling on one message.
    using Protocol = qb::protocol::text::binary8<Binary8Session>;
    explicit Binary8Session(IOServer &server)
        : client(server) {}
    void
    on(Protocol::message &&msg) {
        qb::io::cout() << "[binary8] " << msg.size << " payload bytes, first byte 0x" << std::hex
                       << (static_cast<unsigned>(msg.data[0]) & 0xFFu) << std::dec << "\n";
        decoded();
    }
};
class Binary8Server : public qb::io::use<Binary8Server>::tcp::server<Binary8Session> {};

class Size32Server;
class Size32Session : public qb::io::use<Size32Session>::tcp::client<Size32Server> {
public:
    using Protocol = Length32<Size32Session>;
    explicit Size32Session(IOServer &server)
        : client(server) {}
    void
    on(Protocol::message &&msg) {
        qb::io::cout() << "[size32] frame of " << msg.size << " bytes, no header left in the buffer\n";
        decoded();
    }
};
class Size32Server : public qb::io::use<Size32Server>::tcp::server<Size32Session> {};

class MsgpackServer;
class MsgpackSession : public qb::io::use<MsgpackSession>::tcp::client<MsgpackServer> {
public:
    using Protocol = qb::protocol::json_packed<MsgpackSession>;
    explicit MsgpackSession(IOServer &server)
        : client(server) {}
    void
    on(Protocol::message &&msg) {
        qb::io::cout() << "[msgpack] parsed object, id=" << msg.json.value("id", -1) << ", from " << msg.size << " binary bytes\n";
        decoded();
    }
};
class MsgpackServer : public qb::io::use<MsgpackServer>::tcp::server<MsgpackSession> {};

class JsonServer;
class JsonSession : public qb::io::use<JsonSession>::tcp::client<JsonServer> {
public:
    using Protocol = qb::protocol::json<JsonSession>;
    explicit JsonSession(IOServer &server)
        : client(server) {}
    void
    on(Protocol::message &&msg) {
        qb::io::cout() << "[json] parsed object, id=" << msg.json.value("id", -1) << ", " << msg.size << " bytes on the wire\n";
        decoded();
    }

    // `not_ok()` is a disposal, not an exception: reason -1, and the process is untouched.
    void
    on(qb::io::async::event::disconnected &&e) {
        if (e.reason == -1) {
            qb::io::cout() << "[json] over-nested payload REFUSED by the depth bound; session closed with reason -1 (protocol_error)\n";
            decoded();
        }
    }
};
class JsonServer : public qb::io::use<JsonServer>::tcp::server<JsonSession> {};

// ==================================================================== framing, on the wire
//
// The CLIENT is a raw `tcp::socket` on purpose: it writes the bytes literally, so each wire format
// is visible in this file rather than implied by a protocol on both ends.

std::string
delimited(std::string_view body, std::string_view delimiter) {
    return std::string(body) + std::string(delimiter);
}

/// Length-prefixed via the PROTOCOL's own header writer: `Header()` is a static of
/// `size_as_header`, applies `htons`/`htonl`, and throws when the payload does not fit the width.
template <typename Protocol>
std::string
length_prefixed(std::string_view body) {
    const auto  header = Protocol::Header(body.size());
    std::string out(reinterpret_cast<const char *>(&header), sizeof(header));
    out.append(body);
    return out;
}

struct Wire {
    std::uint16_t port{0};
    std::string   bytes;
    const char   *label{""};
};
std::vector<qb::io::tcp::socket> g_clients;

void
send_once(const Wire &w) {
    qb::io::tcp::socket sock;
    if (sock.connect_v4("127.0.0.1", w.port) != qb::io::SocketStatus::Done) {
        qb::io::cerr() << "[client] could not connect to the " << w.label << " server\n";
        return;
    }
    // ONE write for every message of this framing; without framing the server would see one blob
    // and decode nothing. Held open for the run, so this stays about framing, not about half-close.
    sock.write(w.bytes.data(), w.bytes.size());
    g_clients.push_back(std::move(sock));
}

/// Bind :0 and report the port the kernel chose — no fixed port, so nothing here can collide.
template <typename Server>
std::uint16_t
bind_ephemeral(Server &server, const char *label) {
    if (server.transport().listen_v4(0, "127.0.0.1") != 0) {
        qb::io::cerr() << "[fatal] the " << label << " server could not bind\n";
        return 0;
    }
    server.start();
    return server.transport().local_endpoint().port();
}

} // namespace

int
main() {
    qb::io::cout() << "=== qb-io: the framing toolbox ===\n"
                   << "six wire formats, no parser written; each server gets ONE write holding every message\n\n";
    qb::io::async::init();

    PipeServer    pipe_server;
    CrlfServer    crlf_server;
    Binary8Server binary8_server;
    Size32Server  size32_server;
    JsonServer    json_server;
    MsgpackServer msgpack_server;

    std::vector<Wire> wires;
    // 1. a single delimiter byte. Three frames in one write, and the third is empty — refused.
    wires.push_back({bind_ephemeral(pipe_server, "pipe"), delimited("alpha", "|") + delimited("beta", "|") + "|", "pipe"});

    // 2. a delimiter SEQUENCE.
    wires.push_back({bind_ephemeral(crlf_server, "crlf"), delimited("GET /health", "\r\n") + delimited("GET /ready", "\r\n"), "crlf"});

    // 3. an 8-bit length prefix (payloads up to 255 bytes).
    wires.push_back(
        {bind_ephemeral(binary8_server, "binary8"),
         length_prefixed<Binary8Session::Protocol>("\x01\x02\x03") + length_prefixed<Binary8Session::Protocol>("\x04\x05"), "binary8"});

    // 4. a 32-bit big-endian length prefix. The 300-byte payload does not fit an 8-bit one.
    wires.push_back(
        {bind_ephemeral(size32_server, "size32"),
         length_prefixed<Size32Session::Protocol>(std::string(300, 'x')) + length_prefixed<Size32Session::Protocol>("short"), "size32"});

    // 5. JSON, NUL-terminated, and 6. the SAME document as MessagePack — same framing, same
    // handler shape: the payload encoding and the framing are independent choices.
    const qb::json doc = {{"id", 7}, {"symbol", "QBX"}, {"qty", 120}};
    wires.push_back({bind_ephemeral(json_server, "json"), delimited(doc.dump(), std::string_view("\0", 1)), "json"});
    const auto       packed = qb::json::to_msgpack(doc);
    std::string_view packed_view(reinterpret_cast<const char *>(packed.data()), packed.size());
    wires.push_back({bind_ephemeral(msgpack_server, "msgpack"), delimited(packed_view, std::string_view("\0", 1)), "msgpack"});

    // 2+2+2+2+1+1 decoded messages, plus the pipe refusal above and the JSON depth refusal below.
    // Counted, not assumed: reporting success having decoded nothing is the failure to avoid.
    g_pending = 12;

    for (const auto &w : wires) {
        if (w.port == 0) {
            qb::io::cerr() << "=== a server failed to bind; aborting ===\n";
            return 1;
        }
        qb::io::cout() << "[wire] " << w.label << " -> 127.0.0.1:" << w.port << ", " << w.bytes.size() << " bytes in one write\n";
        send_once(w);
    }

    // 7. the bound the toolbox gives free: 600 nested arrays, well under any message-size limit and
    // fatal to a recursive parser that does not check. `protocol::json` pre-scans and refuses.
    {
        std::string nested(600, '[');
        nested.append(600, ']');
        Wire bomb{wires[4].port, delimited(nested, std::string_view("\0", 1)), "json-depth"};
        qb::io::cout() << "[wire] json-depth -> 127.0.0.1:" << bomb.port << ", " << nested.size() << " bytes nested 600 deep\n";
        send_once(bomb);
    }

    // A watchdog, so a lost frame is a short run with a visible shortfall rather than a hang.
    qb::io::async::callback([]() { g_running = false; }, 5s);
    qb::io::async::run_until(g_running);

    qb::io::cout() << "\n";
    if (g_pending > 0) {
        qb::io::cerr() << "=== " << g_pending << " message(s) never decoded — the watchdog fired ===\n";
        return 1;
    }
    qb::io::cout() << "=== every framing decoded from a single write ===\n";
    return 0;
}
