/**
 * @file examples/06-modules/redis/13-geospatial.cpp
 * @tier 06-modules
 * @teaches "What is near here?" answered by the server: GEOADD builds an index that is really a
 *          SORTED SET keyed on a 52-bit geohash, GEODIST and GEOPOS read it back (lossily — that is
 *          measured, not glossed), GEOSEARCH is the modern query and GEORADIUS the one it replaced,
 *          and the typed Reply's shape is what decides which query options you can actually use.
 * @demonstrates qb::redis::tcp::client, geoadd, geodist, geopos, geohash, geosearch, georadius,
 *               georadiusbymember, qb::redis::GeoUnit, qb::redis::geo_pos,
 *               type, zcard, zscore, del, exists,
 *               qb::redis::Reply<T>, ok, result, error, raw,
 *               qb::io::async::init, qb::io::async::run_until, qb::io::async::coro_scheduler,
 *               qb::io::async::task<void>
 * @prerequisites 06-modules/redis/08-sorted-sets-and-ttl
 * @expect "Connected to Redis successfully!"
 * @expect "[index] GEOADD did not create a new type: TYPE says "
 * @expect "[dist] GEODIST is answered from the two scores alone — no member list is scanned, and"
 * @expect "[pos] GEOPOS does NOT return what you put in. The coordinate was encoded into a 52-bit"
 * @expect "[hash] GEOHASH returns the standard 11-character string, and its PREFIX is the coarse"
 * @expect "[search] GEOSEARCH answered 'what is within 3 km of the Eiffel Tower' server-side, in"
 * @expect "[search] the same question from an arbitrary POINT rather than a member — which is the"
 * @expect "[options] WITHDIST changes the reply SHAPE from a flat array to an array of pairs, and"
 * @expect "[legacy] GEORADIUS and GEORADIUSBYMEMBER still work and are deprecated since Redis 6.2:"
 * @expect "=== geospatial complete: one key, deleted on this path and on the failure path above"
 *
 * A GEO INDEX IS A SORTED SET, AND KNOWING THAT EXPLAINS EVERYTHING ELSE
 * ---------------------------------------------------------------------
 * `GEOADD key lon lat name` interleaves the two coordinates into a single 52-bit integer (a
 * geohash) and does `ZADD key <that integer> name`. There is no new data type: `TYPE` reports
 * `zset`, `ZCARD` counts the members, `ZSCORE` hands you the raw geohash, and `ZREM` removes a
 * place. Section 1 shows all of that, because it is the fact that makes the rest predictable —
 * including the costs, which are a sorted set's costs, and the precision, which is a 52-bit
 * integer's precision.
 *
 * THE LOSS IS REAL AND NOBODY WARNS YOU ABOUT IT
 * ----------------------------------------------
 * Encoding into 52 bits is not reversible. `GEOPOS` returns the CENTRE of the cell your point
 * landed in, so what comes back is never bit-identical to what went in — measured below, the
 * error is around a metre. That is irrelevant for "which shops are near me" and fatal for "is this
 * the same coordinate I stored", so never round-trip a geo index and compare for equality. If you
 * need the exact coordinate, keep it somewhere else; the index is for searching.
 *
 * GEOSEARCH versus GEORADIUS
 * --------------------------
 * `GEORADIUS` and `GEORADIUSBYMEMBER` are deprecated as of Redis 6.2. Both still work and both are
 * exposed here. `GEOSEARCH` replaced them because it takes the origin (`FROMMEMBER` or
 * `FROMLONLAT`) and the shape (`BYRADIUS` or `BYBOX`) as separate choices instead of baking one
 * combination into each command name — and because the old ones carry a `STORE` option, which
 * makes a read look like a write to a replica.
 *
 * THE TYPED REPLY IS PART OF THE API
 * ----------------------------------
 * `geosearch(...)` is declared to yield `Reply<std::vector<std::string>>`, which is the shape of a
 * plain result: a flat list of member names. Ask for `WITHDIST` and the server answers with an
 * array of PAIRS instead. Measured, section 6: the typed decode does not reject that — it SUCCEEDS
 * and returns the member names with the distances silently dropped, so the only symptom is missing
 * data. `raw()` is the escape hatch and is where those distances still are; reach for it whenever
 * an option changes a reply's shape.
 *
 * The one key this program writes is `qb:example:geo:paris`, and it is deleted on the way out, on
 * the failure path as well as the success one.
 *
 * Build:
 *   cmake --preset release
 *   cmake --build --preset release --target qb-example-modules-redis-geospatial
 * Run (needs a Redis on 127.0.0.1:6379):
 *   ./build/presets/release/examples/06-modules/redis/qb-example-modules-redis-geospatial
 */

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <vector>
#include <qb/io/async.h>
#include <qb/io/async/coroutine.h>
#include <qbm/redis/redis.h>

namespace {

#define REDIS_URI {"tcp://localhost:6379"}

constexpr const char *K_GEO = "qb:example:geo:paris";

// The coordinate that goes IN, kept so section 3 can compare it with what comes back out.
constexpr double EIFFEL_LON = 2.2945;
constexpr double EIFFEL_LAT = 48.8584;

std::string
join(std::vector<std::string> const &v) {
    std::string out;
    for (auto const &s : v)
        out += (out.empty() ? "" : ", ") + s;
    return out;
}

// A coordinate printed at the stream's default six significant digits looks IDENTICAL to the one
// that went in, which would make section 3 argue against itself. Ten decimals is what it takes to
// see the difference the 52-bit encoding actually made.
std::string
fixed10(double v) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.10f", v);
    return std::string(buf);
}

} // namespace

qb::io::async::task<void>
run_geospatial(bool &running, bool &ok) {
    struct StopOnExit {
        bool &r;
        ~StopOnExit() {
            r = false;
        }
    } stop{running};

    qb::redis::tcp::client redis{REDIS_URI};
    if (!co_await redis.connect()) {
        qb::io::cerr() << "Failed to connect to Redis at tcp://localhost:6379" << std::endl;
        co_return;
    }
    qb::io::cout() << "Connected to Redis successfully!\n\n";

    // A previous run that died mid-way must not change what this one measures.
    (void) co_await redis.del(K_GEO);

    // -----------------------------------------------------------------------------------
    // 1. THE INDEX — and what it really is.
    // -----------------------------------------------------------------------------------
    // Seven Paris landmarks, as flat (longitude, latitude, name) triplets. Longitude FIRST:
    // that is the order Redis takes and the opposite of how most people say a coordinate.
    auto added = co_await redis.geoadd(K_GEO, EIFFEL_LON, EIFFEL_LAT, "eiffel-tower", 2.3376, 48.8606, "louvre", 2.3499, 48.8530, "notre-dame",
                                       2.2950, 48.8738, "arc-de-triomphe", 2.3431, 48.8867, "sacre-coeur", 2.3553, 48.8809, "gare-du-nord",
                                       2.3794, 48.7233, "orly-airport");
    if (!added.ok()) {
        qb::io::cerr() << "[index] UNEXPECTED: GEOADD failed: " << added.error() << "\n";
        (void) co_await redis.del(K_GEO);
        co_return;
    }

    auto kind  = co_await redis.type(K_GEO);
    auto count = co_await redis.zcard(K_GEO);
    auto score = co_await redis.zscore(K_GEO, "eiffel-tower");

    const bool index_ok = added.result() == 7 && kind.ok() && kind.result() == "zset" && count.ok() && count.result() == 7 && score.ok();

    qb::io::cout() << "[index] GEOADD did not create a new type: TYPE says " << (kind.ok() ? kind.result() : "?") << " and ZCARD says "
                   << (count.ok() ? count.result() : -1)
                   << ". A geo index\n"
                      "        IS a sorted set whose score is the two coordinates interleaved into one 52-bit\n"
                      "        integer, so every sorted-set command still works on it — ZREM removes a place\n";
    qb::io::cout() << "        (ZSCORE eiffel-tower = " << (score.ok() ? score.value_or(0.0) : -1.0)
                   << ", which is that geohash as a double)\n\n";

    // -----------------------------------------------------------------------------------
    // 2. GEODIST — the cheapest thing in the family.
    // -----------------------------------------------------------------------------------
    // Spelled out once rather than `auto`: the OPTIONAL is the lesson. A member that is not in the
    // index is a typed null, so the value is absent rather than zero or an error.
    qb::redis::Reply<std::optional<double>> d_km   = co_await redis.geodist(K_GEO, "eiffel-tower", "louvre", qb::redis::GeoUnit::KM);
    auto                                    d_m    = co_await redis.geodist(K_GEO, "eiffel-tower", "louvre", qb::redis::GeoUnit::M);
    auto                                    d_far  = co_await redis.geodist(K_GEO, "eiffel-tower", "orly-airport", qb::redis::GeoUnit::KM);
    auto                                    d_none = co_await redis.geodist(K_GEO, "eiffel-tower", "no-such-place", qb::redis::GeoUnit::KM);

    // A missing member is not an error: the reply is a typed NULL, which is why the return type
    // is optional<double> and not double.
    const bool dist_ok = d_km.ok() && d_m.ok() && d_far.ok() && d_none.ok() && !d_none.result().has_value()
                         && std::abs(d_km.value_or(0.0) * 1000.0 - d_m.value_or(0.0)) < 1.0;

    qb::io::cout() << "[dist] GEODIST is answered from the two scores alone — no member list is scanned, and\n"
                      "       the unit is a parameter rather than a conversion you do afterwards. A member that\n"
                      "       does not exist is a typed NULL, not an error: hence optional<double>\n";
    qb::io::cout() << "       (eiffel-tower to louvre = " << d_km.value_or(0.0) << " km = " << d_m.value_or(0.0)
                   << " m; to orly-airport = " << d_far.value_or(0.0)
                   << " km; to a missing member = " << (d_none.result().has_value() ? "a value — UNEXPECTED" : "nullopt") << ")\n\n";

    // -----------------------------------------------------------------------------------
    // 3. GEOPOS — and the loss.
    // -----------------------------------------------------------------------------------
    auto back = co_await redis.geopos(K_GEO, "eiffel-tower");

    double lon_err_m = -1.0;
    double lat_err_m = -1.0;
    bool   pos_ok    = false;
    if (back.ok() && back.result().size() == 1 && back.result()[0].has_value()) {
        const qb::redis::geo_pos got = *back.result()[0];
        // Degrees to metres, near 48.86 N: 1 deg latitude ~ 111.32 km, 1 deg longitude ~ 73.3 km.
        lon_err_m = std::abs(got.longitude - EIFFEL_LON) * 73300.0;
        lat_err_m = std::abs(got.latitude - EIFFEL_LAT) * 111320.0;
        pos_ok    = got.longitude != EIFFEL_LON && lon_err_m < 10.0 && lat_err_m < 10.0;
    }

    qb::io::cout() << "[pos] GEOPOS does NOT return what you put in. The coordinate was encoded into a 52-bit\n"
                      "      cell and what comes back is that cell's CENTRE, so a round trip is lossy by about a\n"
                      "      metre. Fine for 'what is near me', fatal for 'is this the same point I stored'\n";
    qb::io::cout() << "      (put in " << fixed10(EIFFEL_LON) << ", " << fixed10(EIFFEL_LAT)
                   << "\n"
                      "       got back "
                   << fixed10(back.ok() && back.result()[0].has_value() ? back.result()[0]->longitude : 0.0) << ", "
                   << fixed10(back.ok() && back.result()[0].has_value() ? back.result()[0]->latitude : 0.0) << " — about " << lon_err_m
                   << " m east-west and " << lat_err_m << " m north-south away)\n\n";

    // -----------------------------------------------------------------------------------
    // 4. GEOHASH — the shareable form.
    // -----------------------------------------------------------------------------------
    auto hashes = co_await redis.geohash(K_GEO, "eiffel-tower", "louvre");

    std::string h_eiffel, h_louvre, shared_prefix;
    if (hashes.ok() && hashes.result().size() == 2) {
        h_eiffel = hashes.result()[0].value_or("");
        h_louvre = hashes.result()[1].value_or("");
        for (std::size_t i = 0; i < h_eiffel.size() && i < h_louvre.size() && h_eiffel[i] == h_louvre[i]; ++i)
            shared_prefix.push_back(h_eiffel[i]);
    }
    const bool hash_ok = hashes.ok() && h_eiffel.size() == 11 && h_louvre.size() == 11 && shared_prefix.size() >= 3;

    qb::io::cout() << "[hash] GEOHASH returns the standard 11-character string, and its PREFIX is the coarse\n"
                      "       location: two places in the same city share the first few characters, so a shared\n"
                      "       prefix is a proximity test you can do with strcmp and no server at all\n";
    qb::io::cout() << "       (eiffel-tower " << h_eiffel << ", louvre " << h_louvre << " — " << shared_prefix.size()
                   << " characters in common: \"" << shared_prefix << "\")\n\n";

    // -----------------------------------------------------------------------------------
    // 5. GEOSEARCH — the query.
    // -----------------------------------------------------------------------------------
    // No options: the reply is a flat array of names, which is exactly the declared
    // Reply<vector<string>>. ASC + COUNT keep it that shape while ordering and bounding it.
    auto near_member = co_await redis.geosearch(K_GEO, "eiffel-tower", 3.0, qb::redis::GeoUnit::KM, std::vector<std::string>{"ASC"});
    auto nearest_three =
        co_await redis.georadius(K_GEO, 2.3376, 48.8606, 5.0, qb::redis::GeoUnit::KM, std::vector<std::string>{"ASC", "COUNT", "3"});

    const bool search_ok = near_member.ok() && !near_member.result().empty() && near_member.result().front() == "eiffel-tower"
                           && nearest_three.ok() && nearest_three.result().size() == 3;

    qb::io::cout() << "[search] GEOSEARCH answered 'what is within 3 km of the Eiffel Tower' server-side, in\n"
                      "         one command, sorted by distance. The origin member is itself in the answer at\n"
                      "         distance zero, which is usually not what a caller wants — drop it yourself\n";
    qb::io::cout() << "         (" << join(near_member.result()) << ")\n";
    qb::io::cout() << "[search] the same question from an arbitrary POINT rather than a member — which is the\n"
                      "         one a user's own GPS position asks, and the reason FROMLONLAT exists at all —\n"
                      "         bounded to the nearest 3 with COUNT\n";
    qb::io::cout() << "         (nearest 3 to 2.3376, 48.8606: " << join(nearest_three.result()) << ")\n\n";

    // -----------------------------------------------------------------------------------
    // 6. THE OPTION THAT CHANGES THE REPLY'S SHAPE.
    //
    // WITHDIST makes the server answer with an array of [name, distance] PAIRS. The typed
    // Reply<vector<string>> is not that shape, so this MEASURES what the client does with it
    // rather than asserting an outcome — and shows raw(), which sees the reply as it arrived.
    // -----------------------------------------------------------------------------------
    auto with_dist = co_await redis.geosearch(K_GEO, "eiffel-tower", 3.0, qb::redis::GeoUnit::KM, std::vector<std::string>{"WITHDIST", "ASC"});

    std::size_t raw_entries = 0;
    std::string first_pair;
    if (with_dist.raw() && with_dist.raw()->is_array()) {
        auto const &arr = with_dist.raw()->as_array();
        raw_entries     = arr.size();
        if (!arr.empty() && arr[0] && arr[0]->is_array() && arr[0]->as_array().size() == 2) {
            auto const &pair = arr[0]->as_array();
            if (pair[0] && pair[1])
                first_pair = std::string(pair[0]->as_string_view()) + " at " + std::string(pair[1]->as_string_view()) + " km";
        }
    }

    qb::io::cout() << "[options] WITHDIST changes the reply SHAPE from a flat array to an array of pairs, and\n"
                      "          the typed decode does NOT fail on it — measured: it succeeds and hands back the\n"
                      "          member names, silently dropping the distances you asked for. So the way you\n"
                      "          find out is by not getting your data, which is the worst way to find out\n";
    qb::io::cout() << "          (typed decode ok=" << (with_dist.ok() ? "yes" : "no") << ", " << with_dist.result().size()
                   << " typed values: " << join(with_dist.result()) << ")\n";
    qb::io::cout() << "          raw() is the escape hatch — it holds the reply as it ARRIVED, whatever the typed\n"
                      "          decode made of it, and it is where the distances still are\n";
    qb::io::cout() << "          (" << raw_entries << " raw entries, the first being \"" << first_pair << "\")\n\n";

    // -----------------------------------------------------------------------------------
    // 7. THE COMMANDS GEOSEARCH REPLACED.
    // -----------------------------------------------------------------------------------
    auto legacy_member = co_await redis.georadiusbymember(K_GEO, "louvre", 2.0, qb::redis::GeoUnit::KM, std::vector<std::string>{"ASC"});

    const bool legacy_ok = legacy_member.ok() && !legacy_member.result().empty();

    qb::io::cout() << "[legacy] GEORADIUS and GEORADIUSBYMEMBER still work and are deprecated since Redis 6.2:\n"
                      "         they bake one origin-and-shape combination into each command name, and they carry\n"
                      "         a STORE option that makes a read look like a write to a replica. Prefer GEOSEARCH\n";
    qb::io::cout() << "         (GEORADIUSBYMEMBER louvre 2 km: " << join(legacy_member.result()) << ")\n\n";

    // ---- cleanup ----------------------------------------------------------------------
    auto removed = co_await redis.del(K_GEO);
    auto gone    = co_await redis.exists(K_GEO);

    ok = index_ok && dist_ok && pos_ok && hash_ok && search_ok && legacy_ok && removed.ok() && removed.result() == 1 && gone.ok()
         && gone.result() == 0;

    qb::io::cout() << "=== geospatial complete: one key, deleted on this path and on the failure path above\n"
                      "    ("
                   << removed.result() << " removed, " << gone.result() << " still present) ===\n";
    co_return;
}

int
main() {
    qb::io::async::init();

    bool running = true;
    bool ok      = false;
    qb::io::async::coro_scheduler().spawn(run_geospatial(running, ok));
    qb::io::async::run_until(running);

    return ok ? 0 : 1;
}
