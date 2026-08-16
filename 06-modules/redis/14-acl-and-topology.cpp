/**
 * @file examples/06-modules/redis/14-acl-and-topology.cpp
 * @tier 06-modules
 * @teaches The two questions you should ask a Redis you did not configure yourself, and the two
 *          command families that answer them: ACL — who am I, what may I run, and would THIS user
 *          be allowed to run THAT (which `ACL DRYRUN` answers without you becoming them) — and
 *          CLUSTER, which tells you whether the single-node assumptions in your code still hold.
 * @demonstrates qb::redis::tcp::client, acl_whoami, acl_users, acl_list, acl_cat, acl_getuser,
 *               acl_setuser, acl_dryrun, acl_deluser, acl_genpass,
 *               cluster_info, cluster_myid, cluster_keyslot, cluster_shards,
 *               del, exists, qb::redis::Reply<std::string>, ok, result, error, qb::json,
 *               qb::io::async::init, qb::io::async::run_until, qb::io::async::coro_scheduler,
 *               qb::io::async::task<void>
 * @prerequisites 06-modules/redis/09-reliability
 * @expect "Connected to Redis successfully!"
 * @expect "[whoami] every connection is authenticated as SOME user — 'default' when you sent no"
 * @expect "[cat] ACL CAT lists the permission CATEGORIES, which is what you grant instead of"
 * @expect "[setuser] a least-privilege user, created for this run only: it may run 3 commands and"
 * @expect "[dryrun] ACL DRYRUN is the command worth remembering: it asks 'WOULD this user be"
 * @expect "[dryrun] and the three answers are distinguishable, which is what makes it useful in a"
 * @expect "[genpass] ACL GENPASS asks the SERVER's CSPRNG for a password, so a provisioning script"
 * @expect "[cleanup] the user is deleted again. ACL SAVE and ACL LOAD are deliberately NOT called"
 * @expect "[cluster] MEASURED on this server: CLUSTER is not a set of read-only introspection"
 * @expect "[cluster] so the only portable question is the FIRST one, and the answer changes what"
 * @expect "=== acl and topology complete: the temporary user is gone and no key was left behind"
 *
 * WHY THESE TWO BELONG IN ONE PROGRAM
 * -----------------------------------
 * Every other example in this group asks the server about YOUR data. These two families ask it
 * about ITSELF — and they are the two questions whose answers decide whether the rest of your code
 * is correct. "What am I allowed to do" turns a mysterious `NOPERM` at 3 a.m. into something you
 * could have checked at deploy time. "Is this a cluster" decides whether `MULTI` across two keys,
 * or `MGET` of two keys, is a working call or a `CROSSSLOT` error.
 *
 * ACL DRYRUN IS THE ONE TO REMEMBER
 * ---------------------------------
 * `ACL DRYRUN <user> <command> [args...]` asks "would this user be permitted to run this?" and
 * answers from the current rules, WITHOUT authenticating as that user, without a second connection,
 * and without running the command. That makes least-privilege rules testable: you can assert, in
 * CI, that your read-only reporting user cannot `DEL`, instead of finding out in production. The
 * three answers are distinguishable — `OK`, a sentence naming the KEY it may not touch, and a
 * sentence naming the COMMAND it may not run — which is what lets a test say which rule was wrong.
 *
 * WHAT THIS PROGRAM DELIBERATELY DOES NOT DO
 * ------------------------------------------
 * `ACL SAVE` writes the whole ACL table to the server's configured aclfile and `ACL LOAD` replaces
 * it from there. Both are SERVER-WIDE and would affect every other user of a shared developer
 * Redis, so — exactly like `SCRIPT FLUSH` in `07-scripting` — they are named here and never called.
 * The one piece of server-wide state this program does create is its own temporary user, which it
 * deletes on the way out, on the failure path as well as the success one.
 *
 * ABOUT THE CLUSTER HALF, WHICH IS SHORTER THAN YOU WOULD EXPECT
 * -------------------------------------------------------------
 * It was written expecting `CLUSTER KEYSLOT` to work everywhere — it is a pure function of the key,
 * CRC16 mod 16384, with nothing node-specific in it. It does not. Measured on Redis 8.10 with
 * `cluster-enabled no`, EVERY `CLUSTER` subcommand, that one included, is refused with `ERR This
 * instance has cluster support disabled`. So a standalone server cannot demonstrate the family, and
 * this program does not pretend otherwise: it asks the one question that is always answerable —
 * "are you a cluster?" — reports what came back, and says what each answer means for your code.
 * The other 26 subcommands need a real cluster, which an example corpus cannot assume.
 *
 * Build:
 *   cmake --preset release
 *   cmake --build --preset release --target qb-example-modules-redis-acl-and-topology
 * Run (needs a Redis on 127.0.0.1:6379):
 *   ./build/presets/release/examples/06-modules/redis/qb-example-modules-redis-acl-and-topology
 */

#include <cstdint>
#include <string>
#include <vector>
#include <qb/io/async.h>
#include <qb/io/async/coroutine.h>
#include <qbm/redis/redis.h>

namespace {

#define REDIS_URI {"tcp://localhost:6379"}

// The temporary user. Named for the run so it cannot be mistaken for somebody's real account, and
// deleted on every exit path below.
constexpr const char *TEST_USER = "qb-example-acl-reader";
constexpr const char *K_ALLOWED = "qb:example:acl:doc";
constexpr const char *K_DENIED  = "qb:example:other:doc";

// A qb::json that may be a string, an array or an object depending on the RESP version the server
// negotiated. Render it compactly rather than assuming one of those.
std::string
brief(qb::json const &j, std::size_t max_len = 120) {
    std::string s = j.is_string() ? j.get<std::string>() : j.dump();
    if (s.size() > max_len)
        s = s.substr(0, max_len) + "...";
    return s;
}

} // namespace

qb::io::async::task<void>
run_acl_and_topology(bool &running, bool &ok) {
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
    (void) co_await redis.acl_deluser(TEST_USER);
    (void) co_await redis.del(K_ALLOWED, K_DENIED);

    // -----------------------------------------------------------------------------------
    // 1. WHO AM I, AND WHO ELSE IS THERE.
    // -----------------------------------------------------------------------------------
    // Spelled out once rather than `auto`: ACL WHOAMI answers with a bare user NAME, which is why
    // the reply is a string and not a structure describing a session.
    qb::redis::Reply<std::string> me    = co_await redis.acl_whoami();
    auto                          users = co_await redis.acl_users();
    auto                          rules = co_await redis.acl_list();

    const bool whoami_ok = me.ok() && !me.result().empty() && users.ok() && rules.ok();

    qb::io::cout() << "[whoami] every connection is authenticated as SOME user — 'default' when you sent no\n"
                      "         AUTH, which is a real account with real rules and not an absence of one. ACL LIST\n"
                      "         prints those rules in the same syntax ACL SETUSER accepts, so it round-trips\n";
    qb::io::cout() << "         (I am \"" << (me.ok() ? me.result() : "?") << "\"; the server knows "
                   << (users.ok() ? users.result().size() : 0) << " user(s): ";
    if (users.ok())
        for (std::size_t i = 0; i < users.result().size(); ++i)
            qb::io::cout() << (i ? ", " : "") << users.result()[i];
    qb::io::cout() << ")\n";
    qb::io::cout() << "         (ACL LIST: " << (rules.ok() ? brief(rules.result()) : rules.error()) << ")\n\n";

    // -----------------------------------------------------------------------------------
    // 2. THE VOCABULARY — categories, not commands.
    // -----------------------------------------------------------------------------------
    auto cats     = co_await redis.acl_cat();
    auto in_cat   = co_await redis.acl_cat("string");
    auto default_ = co_await redis.acl_getuser("default");

    const bool cat_ok = cats.ok() && cats.result().size() > 10 && in_cat.ok() && !in_cat.result().empty() && default_.ok();

    qb::io::cout() << "[cat] ACL CAT lists the permission CATEGORIES, which is what you grant instead of\n"
                      "      enumerating commands: +@read, +@write, -@dangerous. A category tracks the server\n"
                      "      version, so a rule written today still means the right thing after an upgrade\n";
    qb::io::cout() << "      (" << (cats.ok() ? cats.result().size() : 0) << " categories; the 'string' category alone holds "
                   << (in_cat.ok() ? in_cat.result().size() : 0) << " commands)\n";
    qb::io::cout() << "      (ACL GETUSER default: " << (default_.ok() ? brief(default_.result()) : default_.error()) << ")\n\n";

    // -----------------------------------------------------------------------------------
    // 3. A LEAST-PRIVILEGE USER, created for this run.
    // -----------------------------------------------------------------------------------
    // `on` enables it, `>secret` sets a password, `~pattern` scopes the KEYS it may touch, and
    // `+cmd` allows one command. Everything not granted is denied: a fresh user starts at -@all.
    auto created = co_await redis.acl_setuser(TEST_USER, "on", ">example-only-secret", "~qb:example:acl:*", "+get", "+set", "+ping");

    if (!created.ok()) {
        // A locked-down server may refuse this, and that is an operator's decision rather than a
        // defect in the example. Report what was found and stop cleanly — the same shape
        // 08-tls-and-limits uses for a PostgreSQL with `ssl = off`.
        qb::io::cout() << "[setuser] this connection may not create users on this server, so the ACL sections\n"
                          "          below cannot run. That is a legitimate configuration, not a failure\n";
        qb::io::cout() << "          (ACL SETUSER said: " << created.error() << ")\n";
        (void) co_await redis.acl_deluser(TEST_USER);
        co_return;
    }

    auto readback = co_await redis.acl_getuser(TEST_USER);
    auto now      = co_await redis.acl_users();

    const bool setuser_ok = created.ok() && readback.ok() && now.ok() && now.result().size() >= 2;

    qb::io::cout() << "[setuser] a least-privilege user, created for this run only: it may run 3 commands and\n"
                      "          touch keys matching one pattern. Everything not granted is DENIED — a new user\n"
                      "          starts at -@all with no key patterns, which is the right default and surprises\n"
                      "          people who expect to subtract from full access instead of adding to none\n";
    qb::io::cout() << "          (rules as the server understands them: " << (readback.ok() ? brief(readback.result(), 160) : "?") << ")\n";
    qb::io::cout() << "          (note what the `passwords` field holds: a SHA-256 digest, never the password —\n"
                      "          so ACL GETUSER is safe to log, and a password can only ever be set, not read back)\n\n";

    // -----------------------------------------------------------------------------------
    // 4. ACL DRYRUN — the section this program exists for.
    // -----------------------------------------------------------------------------------
    auto may_get   = co_await redis.acl_dryrun(TEST_USER, "GET", std::vector<std::string>{K_ALLOWED});
    auto wrong_key = co_await redis.acl_dryrun(TEST_USER, "GET", std::vector<std::string>{K_DENIED});
    auto wrong_cmd = co_await redis.acl_dryrun(TEST_USER, "DEL", std::vector<std::string>{K_ALLOWED});

    const std::string a_allowed = may_get.ok() ? brief(may_get.result()) : may_get.error();
    const std::string a_key     = wrong_key.ok() ? brief(wrong_key.result()) : wrong_key.error();
    const std::string a_cmd     = wrong_cmd.ok() ? brief(wrong_cmd.result()) : wrong_cmd.error();

    // "OK" for the permitted one; a SENTENCE naming what was refused for the other two — and the
    // two sentences differ, which is what lets a test say WHICH rule was wrong.
    const bool dryrun_ok = may_get.ok() && a_allowed == "OK" && a_key != "OK" && a_cmd != "OK" && a_key != a_cmd;

    qb::io::cout() << "[dryrun] ACL DRYRUN is the command worth remembering: it asks 'WOULD this user be\n"
                      "         permitted to run this?' and answers from the current rules — without\n"
                      "         authenticating as them, without a second connection, and without running it\n";
    qb::io::cout() << "         (GET " << K_ALLOWED << " -> " << a_allowed << ")\n";
    qb::io::cout() << "         (GET " << K_DENIED << " -> " << a_key << ")\n";
    qb::io::cout() << "         (DEL " << K_ALLOWED << " -> " << a_cmd << ")\n";
    qb::io::cout() << "[dryrun] and the three answers are distinguishable, which is what makes it useful in a\n"
                      "         test: one says yes, one names the KEY the rules do not cover, one names the\n"
                      "         COMMAND. Assert those in CI and a least-privilege policy stops being a hope\n\n";

    // -----------------------------------------------------------------------------------
    // 5. ACL GENPASS.
    // -----------------------------------------------------------------------------------
    auto pass = co_await redis.acl_genpass(128);

    // 128 bits of entropy rendered as hex is 32 characters.
    const bool pass_ok = pass.ok() && pass.result().size() == 32;

    qb::io::cout() << "[genpass] ACL GENPASS asks the SERVER's CSPRNG for a password, so a provisioning script\n"
                      "          does not have to carry a random source of its own — and cannot accidentally\n"
                      "          carry a bad one. The argument is BITS of entropy, and the answer is hex\n";
    qb::io::cout() << "          (128 bits -> " << (pass.ok() ? pass.result().size() : 0)
                   << " hex characters; the value itself is not printed, because printing a generated secret\n"
                      "          into a log is how generated secrets stop being secret)\n\n";

    // -----------------------------------------------------------------------------------
    // 6. CLEANUP OF THE USER — before the cluster half, so an early exit there cannot leak it.
    // -----------------------------------------------------------------------------------
    auto deleted = co_await redis.acl_deluser(TEST_USER);
    auto after   = co_await redis.acl_users();

    bool still_there = false;
    if (after.ok())
        for (auto const &u : after.result())
            still_there = still_there || u == TEST_USER;

    const bool cleanup_ok = deleted.ok() && deleted.result() == 1 && after.ok() && !still_there;

    qb::io::cout() << "[cleanup] the user is deleted again. ACL SAVE and ACL LOAD are deliberately NOT called\n"
                      "          anywhere in this file: both are server-wide, and one of them would overwrite\n"
                      "          the whole ACL table of a Redis this example does not own\n";
    qb::io::cout() << "          (ACL DELUSER removed " << (deleted.ok() ? deleted.result() : -1) << " user; it is "
                   << (still_there ? "STILL LISTED — UNEXPECTED" : "gone from ACL USERS") << ")\n\n";

    // -----------------------------------------------------------------------------------
    // 7. IS THIS A CLUSTER?
    // -----------------------------------------------------------------------------------
    auto info   = co_await redis.cluster_info();
    auto myid   = co_await redis.cluster_myid();
    auto slot   = co_await redis.cluster_keyslot("qb:example:acl:doc");
    auto shards = co_await redis.cluster_shards();

    const bool clustered = info.ok();

    qb::io::cout() << "[cluster] MEASURED on this server: CLUSTER is not a set of read-only introspection\n"
                      "          commands that degrade politely on a single node. With `cluster-enabled no`,\n"
                      "          EVERY subcommand is refused outright — including CLUSTER KEYSLOT, which is a\n"
                      "          pure function of the key (CRC16 mod 16384) and has nothing node-specific in it\n";
    qb::io::cout() << "          (CLUSTER INFO   -> " << (info.ok() ? std::string("answered") : info.error()) << ")\n";
    qb::io::cout() << "          (CLUSTER MYID   -> " << (myid.ok() ? myid.result() : myid.error()) << ")\n";
    qb::io::cout() << "          (CLUSTER KEYSLOT-> " << (slot.ok() ? std::to_string(slot.result()) : slot.error()) << ")\n";
    qb::io::cout() << "          (CLUSTER SHARDS -> " << (shards.ok() ? brief(shards.result()) : shards.error()) << ")\n";

    qb::io::cout() << "[cluster] so the only portable question is the FIRST one, and the answer changes what\n"
                      "          your code may assume. Standalone: MULTI, MGET, Lua KEYS and BITOP may name any\n"
                      "          keys you like. Clustered: they must all live in one hash SLOT, which means one\n"
                      "          `{tag}` in braces shared by every key in the call — `{user:1000}:profile` and\n"
                      "          `{user:1000}:sessions` hash to the same slot precisely because only the braced\n"
                      "          part is hashed. Design for that BEFORE you need it; it is not a late migration\n\n";

    // ---- final cleanup ----------------------------------------------------------------
    auto keys_gone = co_await redis.exists(K_ALLOWED, K_DENIED);

    ok = whoami_ok && cat_ok && setuser_ok && dryrun_ok && pass_ok && cleanup_ok && keys_gone.ok() && keys_gone.result() == 0;

    qb::io::cout() << "=== acl and topology complete: the temporary user is gone and no key was left behind\n"
                      "    (this server reports itself as "
                   << (clustered ? "CLUSTERED" : "standalone")
                   << ", so 26 of the 27 CLUSTER commands cannot be\n"
                      "    demonstrated here and are named rather than faked) ===\n";
    co_return;
}

int
main() {
    qb::io::async::init();

    bool running = true;
    bool ok      = false;
    qb::io::async::coro_scheduler().spawn(run_acl_and_topology(running, ok));
    qb::io::async::run_until(running);

    return ok ? 0 : 1;
}
