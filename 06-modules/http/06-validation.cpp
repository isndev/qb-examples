/**
 * @file examples/06-modules/http/06-validation.cpp
 * @tier 06-modules
 * @teaches The `qb::http::validation` namespace, which this file previously included five
 *          headers of and used zero times: a JSON-schema validator, typed query/path/header
 *          parameter rules, a sanitizer that runs BEFORE validation, the error shape you read
 *          them out of, and the middleware that wires all of it in front of a router.
 * @demonstrates qb::http::validation::RequestValidator, qb::http::validation::SchemaValidator,
 *               qb::http::validation::ParameterValidator, qb::http::validation::ParameterRuleSet,
 *               qb::http::validation::Sanitizer, qb::http::validation::PredefinedSanitizers,
 *               qb::http::validation::Result, qb::http::validation::Error,
 *               qb::http::validation::DataType, qb::http::validation::MinimumRule,
 *               qb::http::validation::MinLengthRule, qb::http::validation::PatternRule,
 *               qb::http::validation::EnumRule, qb::http::validation_middleware,
 *               qb::http::DefaultSession, qb::http::Server<>, router, use, compile, listen,
 *               qb::Actor, qb::Main
 * @prerequisites 06-modules/http/04-middleware, 06-modules/http/05-rest-api-json
 * @expect "[schema] a bad body produced 2 errors, and each names its field and its rule"
 * @expect "[schema] the two rules broken were 'minLength' on name and 'pattern' on email"
 * @expect "[params] a typed query parameter rejected a value below its minimum"
 * @expect "[params] strict mode rejected a parameter nobody declared"
 * @expect "[sanitize] trim + escape_html rewrote the value IN PLACE before validation"
 * @expect "[request] RequestValidator checked body, query, header and path in one call"
 * @expect "[middleware] validation_middleware installed; a bad body now gets 400 with a JSON"
 * @expect "Validation server listening on http://localhost:8080"
 *
 * WHAT THIS FILE USED TO BE
 * -------------------------
 * 1008 lines, five `#include <qbm/http/validation/...>` lines, and — measured — the string
 * `validation::` appearing **zero** times. Every check was a hand-written
 * `if (!json.contains("name"))`, and two comments described a middleware that was never
 * installed. It was the corpus's clearest case of a file named for a capability it did not
 * contain, and it is the reason `dev/agent/check-example-headers.py` exists at all.
 *
 * THE FOUR PIECES, AND WHICH ONE YOU ACTUALLY WANT
 * ------------------------------------------------
 *   `SchemaValidator`     one JSON schema against one `qb::json` value.
 *   `ParameterValidator`  a set of NAMED, TYPED parameters — query, header or path — each with
 *                         its own rules, an optional default, and an optional strict mode that
 *                         rejects anything undeclared.
 *   `Sanitizer`           rewrites string nodes in place (trim, escape_html, ...) BEFORE
 *                         anything is validated. Paths support dots, `[i]` and `[*]`.
 *   `RequestValidator`    all three at once, against a whole `qb::http::Request`: body schema,
 *                         query params, headers, path params, plus per-field sanitizers.
 *
 * For a route, you want the last one, wrapped in `qb::http::validation_middleware<Session>(...)`
 * and handed to `router().use(...)`. The first three are what it is made of, and they are worth
 * knowing because a hand-rolled check is exactly what this file used to be.
 *
 * FOUR THINGS THAT ARE EASY TO GET WRONG
 * --------------------------------------
 * 1. **There are no rule factory functions.** It is `std::make_shared<MinimumRule>(18)`, not
 *    `rules::minimum(18)`. And the interface is `IRule`; there is no class called `Rule`.
 * 2. **`Result` is an out-parameter, not a return value.** You default-construct one, pass it by
 *    reference, and read `success()` / `errors()`. Reusing one without `clear()` reports the
 *    union of every call.
 * 3. **Two of the sixteen rule types are inert placeholders.** `RequiredRule::validate` returns
 *    true unconditionally and `ItemsRule::validate` is a stub — presence comes from
 *    `ParameterRuleSet::set_required()` or from a schema's `required` array, and array item
 *    schemas are handled by `SchemaValidator` itself. Putting either in a rule set does nothing.
 * 4. **There is no `middleware::make<tags::validation>`.** The factory is the free function
 *    `qb::http::validation_middleware<SessionType>(std::shared_ptr<RequestValidator>)`, in
 *    namespace `qb::http` — not in `qb::http::middleware`. Keep the `shared_ptr` yourself if you
 *    want to keep configuring the validator after installing it.
 *
 * The self-check below runs before the server binds, so its verdicts are printed and asserted on
 * every run; the server then demonstrates the same validator as middleware, for a human with
 * curl. The `@expect` lines are whole literals chosen by the measurement, so a change in
 * behaviour makes the run RED rather than merely changing a number in the output.
 *
 * Build:
 *   cmake --preset release
 *   cmake --build --preset release --target qb-example-modules-http-validation
 * Run:
 *   ./build/presets/release/examples/06-modules/http/qb-example-modules-http-validation
 */

#include <memory>
#include <string>
#include <qb/main.h>
#include <qbm/http/http.h>
#include <qbm/http/middleware/validation.h>
#include <qbm/http/validation.h>

// A file-scope `using namespace` rather than an alias, deliberately: it is what lets the code
// below say `RequestValidator` while the header block claims the real, fully-qualified
// `qb::http::validation::RequestValidator` — one spelling for a reader, the other for the
// generated capability index, and the guard reconciles the two because this line is here.
using namespace qb::http::validation;

namespace {

/// The body schema, used by the self-check AND by the server's middleware — one definition, so
/// the thing demonstrated at startup is literally the thing installed on the router.
qb::json
user_schema() {
    return qb::json{
        {"type", "object"},
        {"properties",
         {{"name", {{"type", "string"}, {"minLength", 3}, {"maxLength", 40}}},
          {"email", {{"type", "string"}, {"pattern", "^[^@\\s]+@[^@\\s]+\\.[^@\\s]+$"}}},
          {"age", {{"type", "integer"}, {"minimum", 13}, {"maximum", 130}}},
          {"role", {{"type", "string"}, {"enum", qb::json::array({"user", "admin"})}}}}},
        {"required", qb::json::array({"name", "email", "age"})}
    };
}

/// The validator the middleware is built from. A `shared_ptr` because the middleware holds one
/// and you may want to keep configuring it afterwards.
std::shared_ptr<RequestValidator>
make_user_validator() {
    auto rv = std::make_shared<RequestValidator>();

    // The body: one JSON schema.
    rv->for_body(user_schema());

    // A query parameter, TYPED. The value arrives as a string and is converted before the rules
    // see it, so `MinimumRule` compares numbers rather than characters.
    rv->for_query_param("page",
                        ParameterRuleSet("page").set_type(DataType::INTEGER).set_default("1").add_rule(std::make_shared<MinimumRule>(1)));

    // A required header.
    rv->for_header("X-Api-Version", ParameterRuleSet("X-Api-Version")
                                        .set_type(DataType::STRING)
                                        .set_required(true)
                                        .add_rule(std::make_shared<EnumRule>(qb::json::array({"1", "2"}))));

    // A path parameter, for `/api/users/:id`.
    rv->for_path_param("id", ParameterRuleSet("id").set_type(DataType::INTEGER).add_rule(std::make_shared<MinimumRule>(1)));

    // Sanitizers run BEFORE validation and rewrite the request in place. That ordering is the
    // point: a name of "  Ada  " passes minLength either way, but "<b>Ada</b>" only stops being
    // markup because escape_html ran first.
    rv->add_body_sanitizer("name", PredefinedSanitizers::trim());
    rv->add_body_sanitizer("name", PredefinedSanitizers::escape_html());
    rv->add_body_sanitizer("email", PredefinedSanitizers::to_lower_case());

    return rv;
}

// ---------------------------------------------------------------------------------------
// The self-check. Runs before anything binds, so every verdict below is printed on every run.
// ---------------------------------------------------------------------------------------
bool
self_check() {
    bool all_ok = true;

    // ---- SchemaValidator: one schema, one value ---------------------------------------
    {
        SchemaValidator sv{user_schema()};
        Result          result; // an OUT parameter, default-constructed by you
        const qb::json  bad{{"name", "Al"}, {"email", "not-an-email"}, {"age", 30}};
        const bool      ok = sv.validate(bad, result);

        const bool two = !ok && result.errors().size() == 2;
        all_ok         = all_ok && two;
        qb::io::cout() << (two ? "[schema] a bad body produced 2 errors, and each names its field and its rule\n"
                               : "[schema] UNEXPECTED: the bad body did not produce exactly two errors\n");
        // `Error` carries `field_path`, `rule_violated`, `message` and an optional
        // `offending_value`. Note the JSON the middleware emits renames them to
        // field/rule/message/value — the C++ names and the wire names are not the same.
        // `Error` spelled out rather than `auto`, because its four fields are the API: the
        // path of the offending field, the rule it broke, a human message, and (subject to the
        // error-value policy) the value itself.
        bool saw_minlength = false, saw_pattern = false;
        for (Error const &e : result.errors()) {
            qb::io::cout() << "[schema]   " << e.field_path << " / " << e.rule_violated << " / " << e.message << "\n";
            saw_minlength = saw_minlength || e.rule_violated == "minLength";
            saw_pattern   = saw_pattern || e.rule_violated == "pattern";
        }
        all_ok = all_ok && saw_minlength && saw_pattern;
        qb::io::cout() << (saw_minlength && saw_pattern ? "[schema] the two rules broken were 'minLength' on name and 'pattern' on email\n"
                                                        : "[schema] UNEXPECTED: the reported rules were not minLength and pattern\n");

        Result         good_result;
        const qb::json good{{"name", "Ada"}, {"email", "ada@example.com"}, {"age", 36}, {"role", "admin"}};
        const bool     good_ok = sv.validate(good, good_result);
        all_ok                 = all_ok && good_ok && good_result.success();
        if (!good_ok)
            for (auto const &e : good_result.errors())
                qb::io::cerr() << "[schema] UNEXPECTED on the good body: " << e.field_path << " / " << e.rule_violated << " / " << e.message
                               << "\n";
    }

    // ---- ParameterValidator: named, typed, and optionally strict ------------------------
    {
        ParameterValidator pv{/*strict_mode*/ false};
        pv.add_param(ParameterRuleSet("age").set_type(DataType::INTEGER).add_rule(std::make_shared<MinimumRule>(18)));
        pv.add_param(ParameterRuleSet("nickname")
                         .set_type(DataType::STRING)
                         .add_rule(std::make_shared<MinLengthRule>(2))
                         // Rules STACK on one parameter and all of them run; a regex here is
                         // the same `PatternRule` a schema's "pattern" keyword builds for you.
                         .add_rule(std::make_shared<PatternRule>("^[A-Za-z][A-Za-z0-9_]*$")));

        qb::icase_unordered_map<std::string> params;
        params["age"]      = "12"; // below the minimum
        params["nickname"] = "Ada";

        Result     result;
        const bool ok       = pv.validate(params, result, "query");
        const bool rejected = !ok && !result.errors().empty() && result.errors().front().field_path.find("age") != std::string::npos;
        all_ok              = all_ok && rejected;
        qb::io::cout() << (rejected ? "[params] a typed query parameter rejected a value below its minimum — the string\n"
                                      "         \"12\" was converted to a number first, so the comparison is numeric\n"
                                    : "[params] UNEXPECTED: the out-of-range parameter was accepted\n");

        // Strict mode: anything not declared is itself an error. Useful for an API that would
        // rather fail than silently ignore a misspelled parameter.
        ParameterValidator strict{/*strict_mode*/ true};
        strict.add_param(ParameterRuleSet("age").set_type(DataType::INTEGER));
        qb::icase_unordered_map<std::string> extra;
        extra["age"]      = "21";
        extra["surprise"] = "hello";
        Result     strict_result;
        const bool strict_ok = strict.validate(extra, strict_result, "query");
        all_ok               = all_ok && !strict_ok;
        qb::io::cout() << (!strict_ok ? "[params] strict mode rejected a parameter nobody declared\n"
                                      : "[params] UNEXPECTED: strict mode accepted an undeclared parameter\n");
    }

    // ---- Sanitizer: in place, and before validation -------------------------------------
    {
        Sanitizer s;
        s.add_rule("name", PredefinedSanitizers::trim());
        s.add_rule("name", PredefinedSanitizers::escape_html()); // rules STACK, in order
        s.add_rule("tags[*]", PredefinedSanitizers::trim());     // array wildcard
        s.add_rule("profile.bio", PredefinedSanitizers::normalize_whitespace());

        qb::json data{
            {"name", "  <b>Ada</b>  "}, {"tags", qb::json::array({"  one ", " two  "})}, {"profile", {{"bio", "many    spaces   here"}}}
        };
        s.sanitize(data); // mutates `data`

        const bool cleaned = data["name"].get<std::string>() == "&lt;b&gt;Ada&lt;/b&gt;" && data["tags"][0].get<std::string>() == "one"
                             && data["profile"]["bio"].get<std::string>() == "many spaces here";
        all_ok             = all_ok && cleaned;
        qb::io::cout() << (cleaned ? "[sanitize] trim + escape_html rewrote the value IN PLACE before validation, and\n"
                                     "           tags[*] / profile.bio show the path grammar\n"
                                   : "[sanitize] UNEXPECTED: the sanitizer did not rewrite the document as described\n");
    }

    // ---- RequestValidator: all of it, against a real Request -----------------------------
    {
        auto              rv = make_user_validator();
        qb::http::Request req{qb::http::method::POST, qb::io::uri("http://localhost:8080/api/users?page=0")};
        req.set_header("X-Api-Version", "9"); // not in the enum
        req.body() = R"({"name":"Al","email":"NOT AN EMAIL","age":5})";

        // MEASURED: a path-parameter rule is NOT skipped when no `PathParameters` is supplied —
        // it FAILS, with rule `required` and the message "Path parameter context is required for
        // validation." So a validator carrying `for_path_param` must always be given the routing
        // context, which is exactly what the middleware does for you and what a hand-written
        // call has to remember. Here the bad request deliberately omits it (that is one of its
        // six errors) and the good one supplies it.
        Result     result;
        const bool ok = rv->validate(req, result); // NOTE: takes the request by NON-const ref
        // Body (name too short, email pattern, age minimum), query (page below 1) and header
        // (version not in the enum) are all checked by this ONE call.
        const bool caught = !ok && result.errors().size() >= 4;
        all_ok            = all_ok && caught;
        qb::io::cout() << (caught ? "[request] RequestValidator checked body, query, header and path in one call, and\n"
                                    "          reported every failure rather than stopping at the first\n"
                                  : "[request] UNEXPECTED: the invalid request did not produce at least four errors\n");
        qb::io::cout() << "[request] (" << result.errors().size() << " errors; the first was '" << result.errors().front().field_path << "' / '"
                       << result.errors().front().rule_violated << "')\n";

        // ...and the same validator on a good request, with the sanitizers doing their work.
        qb::http::Request good{qb::http::method::POST, qb::io::uri("http://localhost:8080/api/users?page=2")};
        good.set_header("X-Api-Version", "1");
        good.body() = R"({"name":"  Ada  ","email":"ADA@Example.COM","age":36,"role":"admin"})";
        qb::http::PathParameters path;
        path.set("id", "42"); // what the router would have extracted from /api/users/42
        Result     good_result;
        const bool good_ok = rv->validate(good, good_result, &path);
        all_ok             = all_ok && good_ok;
        if (!good_ok)
            for (auto const &e : good_result.errors())
                qb::io::cerr() << "[request] UNEXPECTED on the good request: " << e.field_path << " / " << e.rule_violated << " / " << e.message
                               << "\n";
    }

    return all_ok;
}

} // namespace

// ---------------------------------------------------------------------------------------
// The server: the same validator, installed as middleware in front of the router.
// ---------------------------------------------------------------------------------------
class ValidationServer
    : public qb::Actor
    , public qb::http::Server<> {
public:
    qb::io::async::task<bool>
    onInit() override {
        // Dispatch is by SUBSCRIPTION, not by vtable: without these two the handlers below
        // compile, never run, and the server's teardown is silently lost.
        registerEvent<qb::KillEvent>(*this);
        registerEvent<qb::SignalEvent>(*this);

        auto validator = make_user_validator();

        // THE factory. A free function in `qb::http` (not in `qb::http::middleware`), taking
        // the session type as its template argument. On failure it answers 400 with
        // `{"message":"Validation failed.","errors":[{field,rule,message,value?}]}` — note the
        // wire names differ from the C++ member names.
        router().use(qb::http::validation_middleware<qb::http::DefaultSession>(validator));

        router().post("/api/users", [](auto ctx) {
            // Reaching this handler MEANS the body, the query and the header were valid, and
            // that the sanitizers already rewrote them. That is the whole value of doing this
            // as middleware rather than at the top of every handler.
            ctx->response().status() = qb::http::status::CREATED;
            ctx->response().set_header("Content-Type", "application/json");
            ctx->response().body() =
                qb::json{{"created", true}, {"echo", qb::json::parse(ctx->request().body().template as<std::string>())}}.dump(2);
            ctx->complete();
        });

        router().get("/api/users/:id", [](auto ctx) {
            ctx->response().status() = qb::http::status::OK;
            ctx->response().set_header("Content-Type", "application/json");
            ctx->response().body() = qb::json{{"id", std::string(ctx->path_param("id"))}}.dump(2);
            ctx->complete();
        });

        router().compile();

        if (!listen({"tcp://0.0.0.0:8080"})) {
            qb::io::cerr() << "Failed to bind 0.0.0.0:8080 — a server that never bound must not report success\n";
            co_return false;
        }
        start();

        qb::io::cout() << "[middleware] validation_middleware installed; a bad body now gets 400 with a JSON\n"
                          "             error list, and the handler is never entered\n";
        qb::io::cout() << "\nValidation server listening on http://localhost:8080\n";
        qb::io::cout() << "  # rejected: name too short, email malformed, age below the minimum\n"
                          "  curl -i -X POST http://localhost:8080/api/users -H 'X-Api-Version: 1' \\\n"
                          "       -H 'Content-Type: application/json' -d '{\"name\":\"Al\",\"email\":\"x\",\"age\":5}'\n"
                          "  # accepted, and the name is trimmed and HTML-escaped on the way in\n"
                          "  curl -i -X POST 'http://localhost:8080/api/users?page=2' -H 'X-Api-Version: 1' \\\n"
                          "       -H 'Content-Type: application/json' \\\n"
                          "       -d '{\"name\":\"  <b>Ada</b>  \",\"email\":\"ADA@Example.COM\",\"age\":36}'\n";
        co_return true;
    }

    void
    on(qb::KillEvent const &) {
        kill();
    }

    void
    on(qb::SignalEvent const &) {
        kill();
    }
};

int
main() {
    if (!self_check()) {
        qb::io::cerr() << "\nThe validation self-check did not behave as documented; refusing to start.\n";
        return 1;
    }

    qb::Main engine;
    engine.addActor<ValidationServer>(0);

    engine.start();
    engine.join();
    return engine.hasError() ? 1 : 0;
}
