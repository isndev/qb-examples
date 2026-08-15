/**
 * @file examples/06-modules/pgsql/08-tls-and-limits.cpp
 * @tier 06-modules
 * @teaches Encrypting a PostgreSQL connection — which is a STARTTLS negotiation on the same port,
 *          not a second port — what the default verification level really promises, and the two
 *          limits you set per connection and per transaction: connect_timeout and statement_timeout.
 * @demonstrates qb::pg::tcp::ssl::database, qb::pg::tcp::database, qb::pg::connection_options,
 *               qb::pg::ssl_verify_mode, connect, set_timeout, get_timeout, begin, rollback,
 *               execute, application_name, qb::pg::sqlstate::query_canceled,
 *               qb::io::async::init, qb::io::async::run_until, qb::io::async::coro_scheduler,
 *               qb::io::async::task<void>
 * @prerequisites 06-modules/pgsql/05-errors
 * @expect "[tls] the URI is still tcp:// — PostgreSQL has no TLS port. The client connects in"
 * @expect "[tls] measured against THIS server: "
 * @expect "[verify] the DEFAULT is ssl_verify_mode::none — ENCRYPTED BUT UNAUTHENTICATED. That"
 * @expect "[verify] measured against THIS server with ssl_verify_mode::full: "
 * @expect "[timeout] set_timeout() is TRANSACTION-scoped: it rides with the next BEGIN as SET"
 * @expect "[timeout] the 1s query was cancelled at 200ms with SQLSTATE 57014 (query_canceled), and"
 * @expect "[timeout] connect_timeout is the OTHER limit and it lives on connection_options —"
 * @expect "=== tls and limits complete ==="
 *
 * THERE IS NO TLS PORT
 * --------------------
 * Every other protocol in this corpus separates plaintext from TLS by port or by scheme —
 * `http://` 80 and `https://` 443, `tcp://` and `rediss://`. PostgreSQL does neither. A client
 * connects in CLEARTEXT to 5432, sends an 8-byte SSLRequest, and reads a single byte back: `S`
 * (proceed with the TLS handshake) or `N` (the server will not). Only after `S` does anything get
 * encrypted, and the PostgreSQL protocol handshake then starts over inside the tunnel.
 *
 * So the DSN below is `tcp://` in both halves of this program and the only difference is the
 * client TYPE: `qb::pg::tcp::ssl::database` instead of `qb::pg::tcp::database`. That type selects
 * the `stcp` transport, which the client hands to `qb::io::async::tcp::starttls_connect` together
 * with `qb::pg::detail::postgres_ssl_negotiator` — the small state machine that writes the
 * SSLRequest and reads the one-byte answer before the socket is upgraded.
 *
 * WHICH IS ALSO WHY THIS PROGRAM MEASURES INSTEAD OF ASSERTING
 * ------------------------------------------------------------
 * Whether TLS is available is the SERVER's decision (`ssl = on` in postgresql.conf), and it is not
 * on by default in every minimal container image even though every packaged distribution sets it.
 * So sections 1 and 2 print what they MEASURED against the server they were pointed at, in both
 * directions, rather than failing a run because the operator has not configured TLS. Section 3
 * needs no TLS at all and always runs.
 *
 * ENCRYPTED IS NOT AUTHENTICATED, AND THE DEFAULT IS THE WEAKER ONE
 * -----------------------------------------------------------------
 * `connection_options::ssl_verify` defaults to `ssl_verify_mode::none`, which matches libpq's
 * `sslmode=require`: the traffic is encrypted and the server's certificate is NOT checked. That
 * stops passive sniffing and does nothing at all about an active attacker, who simply presents
 * their own certificate. `ssl_verify_mode::full` is the one that verifies the chain and the
 * hostname — libpq's `verify-full` — and it is the level a production DSN should carry, with
 * `ssl_root_cert` naming your CA when the server's certificate is not publicly signed.
 * `ssl_cert` + `ssl_key` add a client certificate for mutual TLS.
 *
 * SCRAM RIDES ON TOP AND NEEDS NO CONFIGURATION
 * ---------------------------------------------
 * A modern PostgreSQL (`password_encryption = scram-sha-256`) asks for SCRAM-SHA-256 and this
 * client answers it, over TLS or not — the password never crosses the wire either way, and the
 * client verifies the SERVER's proof too, so a server that cannot prove it knows the password is
 * rejected. One thing to know: SCRAM and MD5 both need the OpenSSL-backed crypto, so a qb built
 * with `QB_WITH_SSL=OFF` cannot authenticate against either and refuses loudly rather than
 * hanging (`refuse_auth_without_ssl`). That is a BUILD requirement, not a TLS one.
 *
 * TWO LIMITS, TWO SCOPES
 * ----------------------
 *   connect_timeout      on `connection_options`, per CONNECTION. It bounds the TCP connect and
 *                        the handshake, and it defaults to 10 s.
 *   set_timeout(d)       on the transaction, per TRANSACTION. It is sent as `SET LOCAL
 *                        statement_timeout` in the SAME round trip as the next `BEGIN`, so it is
 *                        scoped to that block and cleared by COMMIT or ROLLBACK. Call it BEFORE
 *                        `begin()`; a query it cancels comes back as SQLSTATE 57014.
 *
 * Build:
 *   cmake --preset release
 *   cmake --build --preset release --target qb-example-modules-pgsql-tls-and-limits
 * Run (needs a PostgreSQL on 127.0.0.1:5432 with test:test@[test]):
 *   ./build/presets/release/examples/06-modules/pgsql/qb-example-modules-pgsql-tls-and-limits
 */

#include <chrono>
#include <string>
#include <qb/io/async.h>
#include <qb/io/async/coroutine.h>
#include <qbm/pgsql/pgsql.h>

using namespace std::chrono_literals;

namespace {

const char *PG_CONNECTION_STRING = "tcp://test:test@localhost:5432[test]";

} // namespace

qb::io::async::task<void>
run_tls_and_limits(bool &running, bool &ok) {
    struct StopOnExit {
        bool &r;
        ~StopOnExit() {
            r = false;
        }
    } stop{running};

    // -----------------------------------------------------------------------------------
    // 1. STARTTLS — the same URI, a different client type
    // -----------------------------------------------------------------------------------
    qb::io::cout() << "[tls] the URI is still tcp:// — PostgreSQL has no TLS port. The client connects in\n"
                      "      cleartext, sends an SSLRequest, reads one byte back, and only then upgrades the\n"
                      "      socket. The single change from the plaintext example is the client TYPE\n";

    qb::pg::tcp::ssl::database secure;
    secure.application_name("qb-example-tls"); // visible in pg_stat_activity; set before connect()

    const bool  tls_connected = co_await secure.connect(PG_CONNECTION_STRING);
    bool        encrypted     = false;
    std::string tls_version, cipher;
    if (tls_connected) {
        // Do not take the client's word for it. `pg_stat_ssl` is the server's own view of THIS
        // backend, so this is the encryption confirmed by the other end of the socket.
        auto proof = co_await secure.execute("SELECT ssl, version, cipher FROM pg_stat_ssl WHERE pid = pg_backend_pid();");
        if (proof.ok() && !proof.result().empty()) {
            encrypted   = proof.result()[0]["ssl"].as<bool>();
            tls_version = proof.result()[0]["version"].as<std::optional<std::string>>().value_or("?");
            cipher      = proof.result()[0]["cipher"].as<std::optional<std::string>>().value_or("?");
        }
    }
    qb::io::cout() << "[tls] measured against THIS server: "
                   << (encrypted ? "pg_stat_ssl reports this backend ENCRYPTED (" + tls_version + ", " + cipher + ")"
                                 : std::string("no TLS — the server answered N to the SSLRequest, or refused the handshake. "
                                               "It needs ssl = on"))
                   << "\n\n";

    // -----------------------------------------------------------------------------------
    // 2. WHAT THE DEFAULT VERIFICATION LEVEL DOES NOT DO
    // -----------------------------------------------------------------------------------
    qb::pg::connection_options opts            = qb::pg::connection_options::parse(PG_CONNECTION_STRING);
    const bool                 default_is_none = opts.ssl_verify == qb::pg::ssl_verify_mode::none;
    qb::io::cout() << (default_is_none ? "[verify] the DEFAULT is ssl_verify_mode::none — ENCRYPTED BUT UNAUTHENTICATED. That\n"
                                         "         is libpq's `sslmode=require`, and it defeats a passive listener and nothing\n"
                                         "         else: an active attacker presents their own certificate and you accept it\n"
                                       : "[verify] UNEXPECTED: the parsed options did not default to ssl_verify_mode::none\n");

    // The same server, the same DSN, one field changed. Against a stock PostgreSQL — which ships
    // a SELF-SIGNED certificate no public CA vouches for — this must be refused, and that refusal
    // is the whole point of the setting. Where the CA IS installed it connects, which is the
    // outcome a production deployment wants; both are reported.
    opts.ssl_verify      = qb::pg::ssl_verify_mode::full;
    opts.connect_timeout = 3s;
    qb::pg::tcp::ssl::database verified;
    const bool                 verify_connected = co_await verified.connect(opts);

    qb::io::cout() << "[verify] measured against THIS server with ssl_verify_mode::full: "
                   << (verify_connected ? std::string("ACCEPTED — this host already trusts the server's certificate")
                                        : std::string("REFUSED (") + verified.error().what() + ")")
                   << "\n"
                      "         Point ssl_root_cert at your CA to make `full` succeed against a private cert;\n"
                      "         ssl_cert + ssl_key present a client certificate for mutual TLS\n\n";

    // -----------------------------------------------------------------------------------
    // 3. statement_timeout — per TRANSACTION, and it rides with the BEGIN
    // -----------------------------------------------------------------------------------
    // On a PLAINTEXT connection, deliberately: this limit has nothing to do with TLS, and the
    // section must run on a server that has none.
    qb::pg::tcp::database db;
    if (!co_await db.connect(PG_CONNECTION_STRING)) {
        qb::io::cerr() << "Failed to connect to PostgreSQL: " << db.error().what() << std::endl;
        co_return;
    }

    // Order matters: set_timeout() only affects the NEXT begin(). Calling it after BEGIN sets a
    // limit for the transaction after this one, which is the kind of mistake that shows up as
    // "the timeout does not work" long after it was written.
    db.set_timeout(200ms);
    const bool armed = db.get_timeout() == std::chrono::milliseconds(200);
    (void) co_await db.begin();

    const auto slow_start = std::chrono::steady_clock::now();
    auto       cancelled  = co_await db.execute("SELECT pg_sleep(1);");
    const auto slow_took  = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - slow_start);
    const bool by_timeout = !cancelled.ok() && cancelled.error().sqlstate == qb::pg::sqlstate::query_canceled;
    (void) co_await db.rollback();

    qb::io::cout() << (armed ? "[timeout] set_timeout() is TRANSACTION-scoped: it rides with the next BEGIN as SET\n"
                               "          LOCAL statement_timeout, in the SAME round trip, and COMMIT or ROLLBACK clears\n"
                               "          it. It must be called BEFORE begin(), or it arms the transaction after this one\n"
                             : "[timeout] UNEXPECTED: get_timeout() did not report the 200ms that was set\n");
    qb::io::cout() << (by_timeout ? "[timeout] the 1s query was cancelled at 200ms with SQLSTATE 57014 (query_canceled), and\n"
                                    "          it is a normal error you catch by sqlstate — not a dropped connection\n"
                                  : "[timeout] UNEXPECTED: pg_sleep(1) was not cancelled by the 200ms statement timeout\n");
    qb::io::cout() << "          (cancelled after " << slow_took.count() << "ms: " << cancelled.error().what() << ")\n";

    // ...and the scope: outside that transaction the limit is gone, so the same shape runs.
    db.set_timeout(qb::duration::zero());
    auto       unbounded  = co_await db.execute("SELECT pg_sleep(0.3);");
    const bool cleared_ok = unbounded.ok();
    qb::io::cout() << "          (after ROLLBACK the limit is gone — the same shape of query now " << (cleared_ok ? "completes" : "still fails")
                   << ")\n";

    qb::io::cout() << "[timeout] connect_timeout is the OTHER limit and it lives on connection_options —\n"
                      "          per connection, bounding the TCP connect and the handshake, default 10s. A DSN\n"
                      "          string cannot express it, which is why the options overload of connect() exists\n";
    qb::io::cout() << "          (the verify-full attempt above used connect_timeout = "
                   << std::chrono::duration_cast<std::chrono::seconds>(opts.connect_timeout).count() << "s)\n\n";

    // The TLS half is a MEASUREMENT of the server, not an assertion about it, so it does not
    // decide this program's exit status. The three things that are this client's own behaviour
    // do: the default verify level, the transaction-scoped timeout, and its clearing.
    ok = default_is_none && armed && by_timeout && cleared_ok;
    qb::io::cout() << "=== tls and limits complete ===\n"
                      "(this program creates no schema, so there is nothing to clean up)\n";
    co_return;
}

int
main() {
    qb::io::async::init();

    bool running = true;
    bool ok      = false;
    qb::io::async::coro_scheduler().spawn(run_tls_and_limits(running, ok));
    qb::io::async::run_until(running);

    return ok ? 0 : 1;
}
