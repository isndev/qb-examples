/*
 * market-data-hub — the wire.
 *
 * A market feed does not speak JSON. This is the shape it speaks instead: a fixed 32-byte record
 * behind a 4-byte big-endian length prefix, framed by `qb::protocol::base::size_as_header`, which
 * means NO PARSER IS WRITTEN HERE — the archetype owns the length handling and hands `onMessage`
 * a complete frame. (`02-io/06-framing-toolbox` is the tier-2 lesson this reuses.)
 *
 * Two things are deliberate:
 *   * every multi-byte field goes through `qb::endian::to_big_endian` / `from_big_endian`, because
 *     a wire format that only works between two little-endian machines is not a wire format;
 *   * the price is an integer of micro-units, not a double. Floating point is fine INSIDE the
 *     process and wrong on the wire, where two implementations must agree bit for bit.
 */
#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <qb/io/protocol/base.h>
#include <qb/system/endian.h>

#include "./model.h"

namespace market_data {

/// The on-wire record. Read and written field by field — never memcpy'd as a struct, because
/// padding and endianness are exactly what a wire format must not inherit from the compiler.
struct WireQuote {
    char          symbol[8]{}; ///< NUL-padded, not NUL-terminated
    std::uint64_t last_micros{};
    std::uint64_t vwap_micros{};
    std::uint32_t volume{};
    std::uint32_t updates{};
};

inline constexpr std::size_t kWireQuoteSize = 8 + 8 + 8 + 4 + 4; // 32 bytes, stated rather than sizeof()
/// A record whose symbol is this one means "the stream is finished".
inline constexpr const char *kEndOfStream = "\0\0\0\0\0\0\0";

/// Serialise one record into `out` (32 bytes appended).
inline void
encode(WireQuote const &q, std::string &out) {
    out.append(q.symbol, sizeof(q.symbol));
    const auto last = qb::endian::to_big_endian(q.last_micros);
    const auto vwap = qb::endian::to_big_endian(q.vwap_micros);
    const auto vol  = qb::endian::to_big_endian(q.volume);
    const auto upd  = qb::endian::to_big_endian(q.updates);
    out.append(reinterpret_cast<const char *>(&last), sizeof(last));
    out.append(reinterpret_cast<const char *>(&vwap), sizeof(vwap));
    out.append(reinterpret_cast<const char *>(&vol), sizeof(vol));
    out.append(reinterpret_cast<const char *>(&upd), sizeof(upd));
}

/// Deserialise one record from `data` (which must hold at least `kWireQuoteSize` bytes).
inline WireQuote
decode(const char *data) {
    WireQuote q;
    std::memcpy(q.symbol, data, sizeof(q.symbol));
    std::uint64_t last{}, vwap{};
    std::uint32_t vol{}, upd{};
    std::memcpy(&last, data + 8, sizeof(last));
    std::memcpy(&vwap, data + 16, sizeof(vwap));
    std::memcpy(&vol, data + 24, sizeof(vol));
    std::memcpy(&upd, data + 28, sizeof(upd));
    q.last_micros = qb::endian::from_big_endian(last);
    q.vwap_micros = qb::endian::from_big_endian(vwap);
    q.volume      = qb::endian::from_big_endian(vol);
    q.updates     = qb::endian::from_big_endian(upd);
    return q;
}

/// Wrap a payload in the protocol's OWN length header. `size_as_header::Header()` is a static that
/// applies the network-order conversion and throws when the payload does not fit the width — using
/// it means the reader and the writer cannot disagree about the framing, because there is only one
/// implementation of it. Writing the payload alone (`*session << bytes`) sends an UNFRAMED stream
/// the peer will read as a length: measured, and it hangs rather than erroring.
template <typename Protocol>
inline std::string
framed(std::string_view payload) {
    const auto  header = Protocol::Header(payload.size());
    std::string out(reinterpret_cast<const char *>(&header), sizeof(header));
    out.append(payload);
    return out;
}

/// A whole frame may carry many records back to back — that is the point of batching, and it is
/// why the length prefix is 32 bits rather than 8.
template <typename IO_>
class QuoteFeed : public qb::protocol::base::size_as_header<IO_, std::uint32_t> {
    using base_t = qb::protocol::base::size_as_header<IO_, std::uint32_t>;

public:
    using base_t::base_t;

    /// What a decoded frame looks like to the session that receives it.
    struct message {
        const char *data;
        std::size_t size;
    };

    // The archetype has already consumed the 4-byte header by the time this runs, so `size` is
    // the PAYLOAD length and `in().cbegin()` is the first payload byte.
    void
    onMessage(std::size_t size) noexcept final {
        this->_io.on(message{this->_io.in().cbegin(), size});
    }
};

} // namespace market_data
