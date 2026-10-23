/**
 * @file examples/07-applications/04-observability/src/main.cpp
 * @tier 07-applications
 * @teaches Collect owner-core snapshots into a private Prometheus endpoint without polling client hot paths.
 * @demonstrates qb::metrics::Registry, qb::metrics::describe, qb::metrics::source_handle,
 *               qb::http::metrics_handler, qb::http::request_metrics, qb::ask, qb::Request,
 *               qb::pg::tcp::database, qb::redis::tcp::client
 * @prerequisites 04-patterns/04-scatter-gather, 06-modules/http/09-coroutine-handlers
 * @expect "Observability HTTP ready"
 * @expect "Observability metrics ready"
 * @expect "Observability backends ready"
 * @expect "Observability stopped"
 * @brief Two-core HTTP/PostgreSQL/Redis application, scraped by Prometheus and provisioned Grafana.
 * @author qb - C++ Actor Framework
 * @copyright Copyright (c) 2011-2026 qb - isndev (cpp.actor)
 * Licensed under the Apache License, Version 2.0 (http://www.apache.org/licenses/LICENSE-2.0)
 */
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <qb/main.h>
#include <qb/core/metrics_registry.h>
#include <qbm/http/http.h>
#include <qbm/http/middleware/metrics.h>
#include <qbm/pgsql/pgsql.h>
#include <qbm/pgsql/metrics.h>
#include <qbm/redis/redis.h>
#include <qbm/redis/metrics.h>

using namespace std::chrono_literals;
namespace observability {

std::string
env(const char *name, const char *fallback) {
    const auto *value = std::getenv(name);
    return value && *value ? value : fallback;
}

// Both the request and response are scalar values, safe for cross-core byte relocation.
struct WorkResult {
    bool pgsql = false;
    bool redis = false;
};
struct Work : qb::Request<WorkResult> {};

qb::io::async::task<bool>
select_one(std::shared_ptr<qb::pg::tcp::database> db) {
    auto row = co_await db->query("SELECT 1 AS value");
    co_return row.ok() && !row.result().empty() && row.result()[0][0].as<int>() == 1;
}

struct Startup {
    std::atomic_bool failed{false};
    void
    fail() noexcept {
        // Stop can cancel activation before Main records an onInit failure; retain its process exit status.
        failed.store(true, std::memory_order_relaxed);
        qb::Main::stop();
    }
};

class Backends final : public qb::Actor {
    std::shared_ptr<Startup>                _startup;
    std::string                             _pg_dsn;
    qb::io::uri                             _redis_uri;
    bool                                    _latency;
    std::shared_ptr<qb::pg::tcp::database>  _db;
    std::shared_ptr<qb::redis::tcp::client> _redis;
    // Declared last: unregister before the clients are destroyed, on this same core.
    qb::metrics::source_handle _metrics;

public:
    Backends(std::shared_ptr<Startup> startup, std::string pg_dsn, qb::io::uri redis_uri, bool latency)
        : _startup(std::move(startup))
        , _pg_dsn(std::move(pg_dsn))
        , _redis_uri(std::move(redis_uri))
        , _latency(latency) {}

    qb::io::async::task<bool>
    onInit() override {
        registerEvent<Work>(*this);
        registerEvent<qb::SignalEvent>(*this);
        _db                     = std::make_shared<qb::pg::tcp::database>();
        _redis                  = std::make_shared<qb::redis::tcp::client>(_redis_uri);
        auto       db           = _db;
        auto       redis        = _redis;
        auto       scope        = context();
        const auto startup      = _startup;
        auto       options      = qb::pg::connection_options::parse(_pg_dsn);
        options.connect_timeout = 3s;
        redis->set_command_timeout(3s);
        redis->set_latency_stats(_latency); // Explicit application opt-in, not registration side effect.
        const bool pg_connected = co_await db->connect(options);
        if (scope.cancelled())
            co_return false;
        if (!pg_connected) {
            std::cerr << "PostgreSQL connection failed (check QB_PG_DSN)\n";
            startup->fail();
            co_return false;
        }
        const bool redis_connected = co_await redis->connect(3s);
        if (scope.cancelled())
            co_return false;
        if (!redis_connected) {
            std::cerr << "Redis connection failed (check QB_REDIS_URI)\n";
            startup->fail();
            co_return false;
        }
        auto *registry = getService<qb::metrics::Registry>();
        if (!registry) {
            _startup->fail();
            co_return false;
        }
        _metrics = registry->add_source([db, redis](qb::metrics::snapshot &snapshot) {
            const qb::metrics::label_set labels{{"connection", "demo"}, {"core", "1"}};
            qb::metrics::describe(snapshot, db->stats(), labels);
            qb::metrics::describe(snapshot, redis->stats(), labels);
            if (redis->latency_stats()) {
                for (std::size_t i = 0; i < qb::redis::command_families; ++i) {
                    const auto family = static_cast<qb::redis::command_family>(i);
                    qb::metrics::describe(snapshot, redis->latency(family), family, labels);
                }
            }
        });
        std::cout << "Observability backends ready" << std::endl;
        co_return true;
    }

    void
    on(Work &request) {
        const auto asker = request.getSource();
        // Copies retain the clients until both bounded operations settle, even after actor teardown.
        spawn([db = _db, redis = _redis, asker, reply = request](qb::ScopedCoroContext scope) mutable -> qb::io::async::task<void> {
            try {
                reply.response.pgsql = co_await qb::io::async::coro_with_timeout(select_one(db), 3s);
            } catch (const qb::io::async::timeout_error &) {
                // A timeout stops our wait, not PostgreSQL. Settle its queue on the owner thread.
                db->disconnect();
            }
            if (scope.cancelled())
                co_return;
            const auto pong = co_await redis->ping();
            if (scope.cancelled())
                co_return;
            reply.response.redis = pong && pong.result() == "PONG";
            scope.push_to<Work>(asker, reply);
        });
    }

    void
    on(const qb::SignalEvent &) noexcept {
        _metrics.reset();
        if (_db)
            _db->disconnect();
        if (_redis)
            _redis->disconnect();
        kill();
    }
};

class Application final
    : public qb::Actor
    , public qb::http::Server<> {
    std::shared_ptr<Startup>                                            _startup;
    qb::ActorId                                                         _backends;
    qb::io::uri                                                         _listen;
    std::shared_ptr<qb::http::RequestMetrics<qb::http::DefaultSession>> _requests;
    qb::metrics::source_handle                                          _metrics;

public:
    Application(std::shared_ptr<Startup> startup, qb::ActorId backends, qb::io::uri listen)
        : _startup(std::move(startup))
        , _backends(backends)
        , _listen(std::move(listen)) {}
    qb::io::async::task<bool>
    onInit() override {
        registerEvent<Work>(*this); // Replies must reach resolve_ask().
        _requests = qb::http::request_metrics<qb::http::DefaultSession>();
        router().add_observer(_requests);
        auto *registry = getService<qb::metrics::Registry>();
        if (!registry) {
            _startup->fail();
            co_return false;
        }
        _metrics = registry->add_source([requests = _requests](auto &snapshot) { requests->collect(snapshot); });
        router().get("/", [](auto ctx) { ctx->text("GET /work runs SELECT 1 and Redis PING; metrics use their own listener.\n"); });
        router().get("/work", [scope = context(), backend = _backends](auto ctx) -> qb::io::async::task<void> {
            try {
                const auto result = co_await qb::ask(scope, backend, Work{}, 7s);
                const auto status =
                    result.response.pgsql && result.response.redis ? qb::http::status::OK : qb::http::status::SERVICE_UNAVAILABLE;
                ctx->json(qb::json{{"pgsql", result.response.pgsql}, {"redis", result.response.redis}}, status);
            } catch (const std::exception &) {
                ctx->text("Backend unavailable\n", qb::http::status::SERVICE_UNAVAILABLE);
            }
        });
        router().compile();
        if (!listen(_listen)) {
            std::cerr << "HTTP bind failed\n";
            _startup->fail();
            co_return false;
        }
        std::cout << "Observability HTTP ready" << std::endl;
        co_return true;
    }
    void
    on(Work &reply) {
        resolve_ask(reply);
    }
};

class MetricsEndpoint final
    : public qb::Actor
    , public qb::http::Server<> {
    std::shared_ptr<Startup> _startup;
    qb::io::uri              _listen;

public:
    MetricsEndpoint(std::shared_ptr<Startup> startup, qb::io::uri listen)
        : _startup(std::move(startup))
        , _listen(std::move(listen)) {}
    qb::io::async::task<bool>
    onInit() override {
        router().get("/metrics",
                     qb::http::metrics_handler<qb::http::DefaultSession>(*this, qb::http::MetricsEndpointOptions{}.with_timeout(500ms)));
        router().compile();
        if (!listen(_listen)) {
            std::cerr << "Metrics bind failed\n";
            _startup->fail();
            co_return false;
        }
        std::cout << "Observability metrics ready" << std::endl;
        co_return true;
    }
};
} // namespace observability

int
main() {
    using namespace observability;
    try {
        const auto startup = std::make_shared<Startup>();
        qb::Main   engine;
        for (qb::CoreId core : {0, 1}) {
            engine.core(core).setLatency(500us);
            engine.addActor<qb::metrics::Registry>(core);
        }
        const auto backend =
            engine.addActor<Backends>(1, startup, env("QB_PG_DSN", "tcp://test:test@127.0.0.1:5432[test]"),
                                      qb::io::uri{env("QB_REDIS_URI", "tcp://127.0.0.1:6379")}, env("QB_REDIS_LATENCY", "0") == "1");
        engine.addActor<Application>(0, startup, backend, qb::io::uri{env("QB_HTTP_LISTEN", "tcp://127.0.0.1:18080")});
        engine.addActor<MetricsEndpoint>(0, startup, qb::io::uri{env("QB_METRICS_LISTEN", "tcp://127.0.0.1:19090")});
        engine.start();
        engine.join();
        if (engine.hasError() || startup->failed.load(std::memory_order_relaxed))
            return 1;
        std::cout << "Observability stopped" << std::endl;
        return 0;
    } catch (const std::exception &) {
        std::cerr << "Observability startup failed; check configuration\n";
        return 1;
    }
}
