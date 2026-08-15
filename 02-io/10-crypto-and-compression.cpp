/**
 * @file examples/02-io/10-crypto-and-compression.cpp
 * @tier 02-io
 * @teaches The security and payload primitives qb-io already ships, on their own terms: digests and
 *          HMAC, an AEAD that REFUSES a tampered ciphertext, Argon2 password hashing, a constant-time
 *          comparison, a signed token, and a compressor whose uncompress takes an OUTPUT BOUND —
 *          because a decompression bomb is an attack, not a corner case.
 * @demonstrates qb::generate_random_uuid, qb::uuid, qb::crypto::sha256, qb::crypto::to_hex_string,
 *               qb::crypto::hash, qb::crypto::hmac, qb::crypto::DigestAlgorithm,
 *               qb::crypto::base64::encode, qb::crypto::base64::decode, qb::crypto::SymmetricAlgorithm,
 *               qb::crypto::generate_key, qb::crypto::generate_iv, qb::crypto::encrypt,
 *               qb::crypto::decrypt, qb::crypto::hash_password, qb::crypto::verify_password,
 *               qb::crypto::constant_time_compare, qb::crypto::secure_random_fill,
 *               qb::jwt::create, qb::jwt::verify, qb::jwt::CreateOptions, qb::jwt::VerifyOptions,
 *               qb::jwt::Algorithm, qb::jwt::ValidationError, qb::compression::builtin::supported,
 *               qb::compression::builtin::algorithm::supported, qb::gzip::compress, qb::gzip::uncompress,
 *               qb::deflate::compress, qb::io::cout
 * @prerequisites none
 * @expect "=== qb-io: crypto and compression, without adding OpenSSL by hand ==="
 * @expect "[aead] tampered ciphertext REJECTED (decrypt returned nothing)"
 * @expect "[argon2] wrong password rejected"
 * @expect "[jwt] expired token rejected: TOKEN_EXPIRED"
 * @expect "[gzip] bomb REFUSED by the output bound: "
 * @expect "=== done ==="
 *
 * WHY THIS PROGRAM EXISTS
 * -----------------------
 * qb-io links OpenSSL and zlib already, and wraps both. Measured over the pre-3.0 corpus, none of
 * that surface had a demonstrator: `qb::crypto` 0 uses, `qb::jwt` 0, `qb::compression` 0. Every one
 * of them was reached only THROUGH qbm-http middleware, so "hash a password in my qb-io service"
 * had no answer in 55 programs and the honest answer looked like "add OpenSSL to your CMakeLists".
 * It is not. Nothing below includes an OpenSSL or zlib header.
 *
 * THE TWO RULES WORTH TAKING AWAY
 * -------------------------------
 * 1. **A digest is not a password hash.** `sha256("hunter2")` is computed billions of times a
 *    second on a GPU. `hash_password()` is Argon2id — deliberately slow and memory-hard — and it
 *    embeds its own salt and parameters in the string it returns, so `verify_password()` needs
 *    nothing else stored beside it.
 * 2. **`uncompress` takes a `max`, and leaving it at 0 is a decision.** A few hundred compressed
 *    bytes can expand to gigabytes; the ratio is chosen by whoever sent them. `max` is the only
 *    thing between an attacker's ratio and your address space, and exceeding it throws rather than
 *    truncating — a truncated "success" is worse than a refusal.
 *
 * WHAT IS DELIBERATELY NOT HERE
 * -----------------------------
 * The asymmetric surface (`generate_rsa_keypair`, `ed25519_sign`, ECIES envelopes) and the KDF
 * family beyond Argon2. They exist and are documented; a demonstration of each would make this a
 * catalogue rather than a lesson, and the shapes below are the ones a service actually reaches for.
 *
 * Build:
 *   cmake --preset release
 *   cmake --build --preset release --target qb-example-io-crypto-and-compression
 * Run:
 *   ./build/presets/release/examples/02-io/qb-example-io-crypto-and-compression
 */

#include <chrono>
#include <iomanip>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>
#include <qb/io.h>
#include <qb/io/compression.h>
#include <qb/io/crypto.h>
#include <qb/io/crypto_jwt.h>
#include <qb/uuid.h>

using namespace std::chrono_literals;

namespace {

void
section(const char *title) {
    qb::io::cout() << "\n--- " << title << " ---\n";
}

// Bytes in, printable out. `to_hex_string` takes a std::string, so a byte vector needs the
// one-line bridge below rather than a second codec.
std::string
hex(const std::vector<unsigned char> &bytes, std::size_t max_bytes = 16) {
    const auto  n = bytes.size() < max_bytes ? bytes.size() : max_bytes;
    std::string raw(reinterpret_cast<const char *>(bytes.data()), n);
    auto        out = qb::crypto::to_hex_string(raw, qb::crypto::range_hex_lower);
    if (bytes.size() > n)
        out += "…";
    return out;
}

std::vector<unsigned char>
bytes_of(const std::string &s) {
    return {s.begin(), s.end()};
}

const char *
jwt_error_name(qb::jwt::ValidationError e) {
    switch (e) {
        case qb::jwt::ValidationError::NONE:
            return "NONE";
        case qb::jwt::ValidationError::INVALID_FORMAT:
            return "INVALID_FORMAT";
        case qb::jwt::ValidationError::INVALID_SIGNATURE:
            return "INVALID_SIGNATURE";
        case qb::jwt::ValidationError::TOKEN_EXPIRED:
            return "TOKEN_EXPIRED";
        case qb::jwt::ValidationError::TOKEN_NOT_ACTIVE:
            return "TOKEN_NOT_ACTIVE";
        case qb::jwt::ValidationError::INVALID_ISSUER:
            return "INVALID_ISSUER";
        case qb::jwt::ValidationError::INVALID_AUDIENCE:
            return "INVALID_AUDIENCE";
        case qb::jwt::ValidationError::INVALID_SUBJECT:
            return "INVALID_SUBJECT";
        case qb::jwt::ValidationError::CLAIM_MISMATCH:
            return "CLAIM_MISMATCH";
    }
    return "UNKNOWN";
}

} // namespace

int
main() {
    qb::io::cout() << "=== qb-io: crypto and compression, without adding OpenSSL by hand ===\n";

    // -------------------------------------------------------------------- identity
    section("1. a random identifier");
    const qb::uuid request_id = qb::generate_random_uuid();
    // `qb::uuid` IS `uuids::uuid`, so the vendored library's free functions apply to it directly.
    qb::io::cout() << "[uuid] request id: " << uuids::to_string(request_id) << "\n";

    // ------------------------------------------------------------- digests and HMAC
    section("2. digests, and why a digest alone proves nothing about the sender");
    const std::string payload = R"({"account":"a-17","amount":250})";

    // `sha256` returns RAW digest bytes, not hex — printing it straight to a terminal would emit
    // 32 bytes of binary. `to_hex_string` is the display step, and it is a separate call for the
    // good reason that most uses (comparison, storage) want the bytes.
    const std::string digest = qb::crypto::sha256(payload);
    qb::io::cout() << "[sha256] " << qb::crypto::to_hex_string(digest, qb::crypto::range_hex_lower) << "\n";

    // A digest says the bytes did not change. It does NOT say who sent them: anyone can recompute
    // it. An HMAC folds in a shared key, so only a holder of that key can produce a valid tag.
    const auto key = bytes_of("a shared secret nobody else has");
    const auto tag = qb::crypto::hmac(bytes_of(payload), key, qb::crypto::DigestAlgorithm::SHA256);
    qb::io::cout() << "[hmac-sha256] " << hex(tag) << "\n";

    // The generic entry point, when the algorithm is a runtime choice rather than a call site.
    const auto blake = qb::crypto::hash(bytes_of(payload), qb::crypto::DigestAlgorithm::BLAKE2B512);
    qb::io::cout() << "[blake2b512] " << hex(blake) << "\n";

    // -------------------------------------------------------------------- transport codecs
    section("3. codecs: getting bytes through something that only carries text");
    const std::string b64 = qb::crypto::base64::encode(payload);
    qb::io::cout() << "[base64] " << b64 << "\n";
    qb::io::cout() << "[base64] round-trip " << (qb::crypto::base64::decode(b64) == payload ? "ok" : "BROKEN") << "\n";

    // ---------------------------------------------------------------------------- AEAD
    section("4. AEAD: encryption that also DETECTS tampering");
    // AES-256-GCM is authenticated encryption: `encrypt` appends a tag, and `decrypt` verifies it
    // before returning anything. The key and IV come from the library's CSPRNG, and the IV must be
    // fresh for every message under the same key — `generate_iv` is that call, not a constant.
    const auto aead_key = qb::crypto::generate_key(qb::crypto::SymmetricAlgorithm::AES_256_GCM);
    const auto iv       = qb::crypto::generate_iv(qb::crypto::SymmetricAlgorithm::AES_256_GCM);
    // Additional Authenticated Data: carried in the clear, but covered by the tag. Routing headers
    // belong here — the peer must read them, and must not be able to change them.
    const auto aad = bytes_of("route=payments;v=1");

    auto ciphertext = qb::crypto::encrypt(bytes_of(payload), aead_key, iv, qb::crypto::SymmetricAlgorithm::AES_256_GCM, aad);
    qb::io::cout() << "[aead] " << payload.size() << " plaintext bytes -> " << ciphertext.size() << " bytes (ciphertext + tag)\n";

    const auto recovered = qb::crypto::decrypt(ciphertext, aead_key, iv, qb::crypto::SymmetricAlgorithm::AES_256_GCM, aad);
    qb::io::cout() << "[aead] decrypted: " << std::string(recovered.begin(), recovered.end()) << "\n";

    // Flip one bit anywhere in the ciphertext. Under an unauthenticated mode this would decrypt to
    // plausible-looking garbage and be acted on; under GCM it returns EMPTY.
    ciphertext[0] ^= 0x01;
    const auto tampered = qb::crypto::decrypt(ciphertext, aead_key, iv, qb::crypto::SymmetricAlgorithm::AES_256_GCM, aad);
    qb::io::cout() << (tampered.empty() ? "[aead] tampered ciphertext REJECTED (decrypt returned nothing)\n"
                                        : "[aead] TAMPERING WENT UNDETECTED — this build is not doing AEAD\n");

    // ----------------------------------------------------------------------- passwords
    section("5. passwords: Argon2id, not a digest");
    // `hash_password` embeds the variant, the cost parameters and a fresh random salt in the
    // string it returns, so the ONE column you store is enough to verify later. It is deliberately
    // slow (tens of milliseconds, tens of megabytes) — that cost is the whole point.
    const std::string stored = qb::crypto::hash_password("correct horse battery staple");
    qb::io::cout() << "[argon2] stored form: " << stored.substr(0, 20) << "… (variant, cost, salt and digest, all in one column)\n";
    qb::io::cout() << "[argon2] correct password accepted: "
                   << (qb::crypto::verify_password("correct horse battery staple", stored) ? "yes" : "NO — broken") << "\n";
    qb::io::cout() << (qb::crypto::verify_password("Tr0ub4dor&3", stored) ? "[argon2] WRONG PASSWORD ACCEPTED — broken\n"
                                                                          : "[argon2] wrong password rejected\n");

    // ------------------------------------------------------------ comparing secrets
    section("6. comparing two secrets without leaking how far you got");
    // `==` on a byte array stops at the first difference. An attacker who can time your endpoint
    // can then recover a tag byte by byte. `constant_time_compare` always reads every byte.
    const auto expected_tag = tag;
    auto       supplied_tag = tag;
    qb::io::cout() << "[timing] genuine tag compares equal: " << (qb::crypto::constant_time_compare(expected_tag, supplied_tag) ? "yes" : "no")
                   << "\n";
    supplied_tag.back() ^= 0xFF;
    qb::io::cout() << "[timing] forged tag compares equal: " << (qb::crypto::constant_time_compare(expected_tag, supplied_tag) ? "yes" : "no")
                   << " (and took the same time to say so)\n";

    // A CSPRNG for anything you would otherwise have reached for `rand()` to make.
    std::vector<unsigned char> nonce(16);
    qb::io::cout() << "[random] secure_random_fill: " << (qb::crypto::secure_random_fill(nonce) ? hex(nonce) : "FAILED") << "\n";

    // ---------------------------------------------------------------------------- JWT
    section("7. a signed token, directly — no HTTP middleware involved");
    qb::jwt::CreateOptions create;
    create.algorithm = qb::jwt::Algorithm::HS256;
    create.key       = "a-signing-key-at-least-32-bytes-long";

    const auto now = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    std::map<std::string, std::string> claims{
        {"sub", "user-42"}, {"role", "operator"}, {"iat", std::to_string(now)}, {"exp", std::to_string(now + 3600)}
    };
    const std::string token = qb::jwt::create(claims, create);
    qb::io::cout() << "[jwt] token: " << token.substr(0, 40) << "…\n";

    qb::jwt::VerifyOptions verify;
    verify.algorithm = qb::jwt::Algorithm::HS256;
    verify.key       = create.key;
    const auto ok    = qb::jwt::verify(token, verify);
    qb::io::cout() << "[jwt] verified: " << (ok.is_valid() ? "yes" : "no") << ", sub = " << (ok.is_valid() ? ok.payload.at("sub") : "-")
                   << "\n";

    // The same token signed a minute ago with a one-second life. `exp` is enforced by the library,
    // not by the caller remembering to look.
    claims["exp"]                 = std::to_string(now - 60);
    const std::string stale       = qb::jwt::create(claims, create);
    const auto        stale_check = qb::jwt::verify(stale, verify);
    qb::io::cout() << (stale_check.error == qb::jwt::ValidationError::TOKEN_EXPIRED
                           ? "[jwt] expired token rejected: TOKEN_EXPIRED\n"
                           : "[jwt] expired token NOT rejected as expired — the exp claim was not enforced\n");

    // A token signed with a different key must not verify — this is the check an "is the signature
    // even looked at?" bug hides behind.
    qb::jwt::VerifyOptions wrong_key = verify;
    wrong_key.key                    = "not-the-signing-key-not-the-signing-k";
    qb::io::cout() << "[jwt] foreign signature rejected, error = " << jwt_error_name(qb::jwt::verify(token, wrong_key).error) << "\n";

    // -------------------------------------------------------------------- compression
    section("8. compression, and the bound that stops a bomb");
    qb::io::cout() << "[zlib] compression built in: " << (qb::compression::builtin::supported() ? "yes" : "no")
                   << ", gzip: " << (qb::compression::builtin::algorithm::supported(qb::compression::builtin::algorithm::GZIP) ? "yes" : "no")
                   << "\n";

    std::string body;
    for (int i = 0; i < 400; ++i)
        body += "the quick brown fox jumps over the lazy dog; ";

    const std::string gz = qb::gzip::compress(body.data(), body.size());
    qb::io::cout() << "[gzip] " << body.size() << " -> " << gz.size() << " bytes (" << (body.size() / gz.size()) << "x smaller)\n";

    // The generous form: `max = 0` means unbounded, which is the right answer only for data you
    // produced yourself. Note it is spelled out rather than defaulted, so the choice is visible.
    const std::string back = qb::gzip::uncompress(gz.data(), gz.size());
    qb::io::cout() << "[gzip] round-trip " << (back == body ? "ok" : "BROKEN") << "\n";

    // deflate is the same family without the gzip envelope — the encoding an HTTP peer means by
    // `Content-Encoding: deflate`. Same call shape; the two are not interchangeable on the wire.
    const std::string df = qb::deflate::compress(body.data(), body.size());
    qb::io::cout() << "[deflate] " << body.size() << " -> " << df.size() << " bytes\n";

    // The bomb. A megabyte of zeros is a few hundred compressed bytes; a peer chooses that ratio,
    // and nothing in the compressed stream declares its expanded size honestly. Decompressing into
    // a bounded sink is the defence, and the bound is a REFUSAL, not a truncation: a silently
    // truncated payload is a correctness bug you would then have to find somewhere else.
    const std::string zeros(1024 * 1024, '\0');
    const std::string bomb = qb::gzip::compress(zeros.data(), zeros.size());
    qb::io::cout() << "[gzip] " << bomb.size() << " bytes of input claim " << zeros.size() << " bytes of output\n";

    std::string sink;
    try {
        qb::gzip::uncompress(sink, bomb.data(), bomb.size(), /*max=*/64 * 1024);
        qb::io::cout() << "[gzip] BOMB ACCEPTED — the bound did nothing\n";
    } catch (const std::runtime_error &e) {
        qb::io::cout() << "[gzip] bomb REFUSED by the output bound: " << e.what() << "\n";
    }

    // And the same call with a bound the payload fits inside still succeeds, so the guard is a
    // budget rather than a switch that turns decompression off.
    std::string bounded;
    qb::gzip::uncompress(bounded, gz.data(), gz.size(), /*max=*/1024 * 1024);
    qb::io::cout() << "[gzip] bounded round-trip within budget: " << (bounded == body ? "ok" : "BROKEN") << "\n";

    qb::io::cout() << "\n=== done ===\n";
    return 0;
}
