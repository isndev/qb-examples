# Owner-core observability

This application uses two virtual cores: HTTP and a separate metrics listener on core 0,
PostgreSQL and Redis on core 1. Every `GET /work` asks the worker to execute `SELECT 1`
and Redis `PING`. A `qb::metrics::Registry` on each core collects its own values only
when a scrape asks. There is no polling thread or cross-core client pointer.

The example uses the explicit `<qbm/pgsql/metrics.h>` and `<qbm/redis/metrics.h>`
adapters. Neither header is automatically included by the client umbrella, installs a
completion hook, or enables timing. The Redis duration distribution is absent unless
`QB_REDIS_LATENCY=1`. HTTP request observation is explicitly installed by this application.

## Build and run against existing services

From the qb-dev superproject (CMake 3.25+, C++20):

```sh
cmake --preset dev
cmake --build --preset dev -j4 --target qb-example-applications-observability
export QB_PG_DSN='tcp://test:test@127.0.0.1:5432[test]'
export QB_REDIS_URI='tcp://127.0.0.1:6379'
./build/presets/dev/examples/07-applications/04-observability/qb-example-applications-observability
```

Windows builds the same target; run its `.exe` and set the same environment variables
with PowerShell `$env:QB_PG_DSN='...'`. Connection strings are not printed.
Wait for all three `Observability ... ready` lines, then from another terminal:

```sh
curl --fail http://127.0.0.1:18080/work
curl --fail http://127.0.0.1:19090/metrics
python3 examples/07-applications/04-observability/smoke.py
```

The first response is `{"pgsql":true,"redis":true}`. `/metrics` on port 18080 is a 404.
Ctrl+C (SIGINT) or SIGTERM stops the whole engine and prints `Observability stopped`.
A failed backend connection or either failed bind stops startup with a nonzero exit.
No schema or Redis key is created. A query wait is bounded to three seconds; a timed-out
PostgreSQL wait disconnects that connection to settle the underlying queue. Redis's
three-second command watchdog also drops the connection and fails pending commands.
The HTTP ask allows seven seconds for both operations. This intentionally small example
does not reconnect after such a failure: restart it after fixing the backend.

## Prometheus and Grafana

The application remains a native qb binary. Compose starts only its four dependencies;
there is no second Docker build of the C++ application. Docker Compose v2 is required.
All published dependency/UI ports are bound to host loopback. The development credentials
in the compose file are for these disposable services only.

```sh
docker compose -f examples/07-applications/04-observability/compose.yaml up -d --wait
# In the terminal running the native application:
export QB_PG_DSN='tcp://qb:qb@127.0.0.1:55432[qb]'
export QB_REDIS_URI='tcp://127.0.0.1:56379'
export QB_METRICS_LISTEN='tcp://0.0.0.0:19090'
export QB_REDIS_LATENCY=1
./build/presets/dev/examples/07-applications/04-observability/qb-example-applications-observability
```

`QB_METRICS_LISTEN` is **loopback by default** (`tcp://127.0.0.1:19090`). The explicit
`0.0.0.0` override above lets the Prometheus container reach the host, through
`host.docker.internal` (`host-gateway` on Linux). It also makes this unauthenticated
endpoint reachable on other host interfaces: use it only on a development network,
or select an interface reachable exclusively by the monitoring network. Do not publish
this endpoint on an untrusted interface. With WSL, run the native app on the host network
Docker Desktop forwards to, or adjust the scrape target to that WSL instance's reachable
address; loopback inside a container does not refer to the native process.

After the three ready lines, from another terminal:

```sh
python3 examples/07-applications/04-observability/smoke.py --latency \
  --prometheus http://127.0.0.1:19091 --grafana http://127.0.0.1:13000
```

Prometheus is at <http://127.0.0.1:19091>. Grafana is at <http://127.0.0.1:13000>, login
`admin` / `qb-local-demo`; the provisioned dashboard is **qb owner-core observability**.
Send more `/work` requests to populate the rate panels, then allow two or more scrapes.
The smoke check verifies actual HTTP responses, both core series, concurrent scrapes,
PostgreSQL/Redis counter increases, latency opt-in, Prometheus ingestion and a query
through Grafana's provisioned datasource. It is a correctness check, not a benchmark.

Stop the native application first, then remove only this demo's Compose resources:

```sh
docker compose -f examples/07-applications/04-observability/compose.yaml down -v
```

The fixed project name is `qb330-observabilitytest`; do not reuse it for other work.
The four exact image tags were checked against their upstream releases and registry
manifests on 2026-10-23: [PostgreSQL 16.15](https://www.postgresql.org/support/versioning/),
[Redis 8.2.10](https://github.com/redis/redis/releases/tag/8.2.10),
[Prometheus 3.13.5 LTS](https://github.com/prometheus/prometheus/releases/tag/v3.13.5),
[Grafana OSS 13.2.4](https://grafana.com/grafana/download/13.2.4?edition=oss).
They are pinned tags, not immutable image digests. Container setup follows the upstream
[Prometheus installation](https://prometheus.io/docs/prometheus/latest/installation/)
and [Grafana provisioning](https://grafana.com/docs/grafana/latest/administration/provisioning/)
contracts.

## Configuration and interpretation

| Environment | Default | Meaning |
|---|---|---|
| `QB_PG_DSN` | `tcp://test:test@127.0.0.1:5432[test]` | PostgreSQL connection; bracket database syntax |
| `QB_REDIS_URI` | `tcp://127.0.0.1:6379` | Redis connection |
| `QB_HTTP_LISTEN` | `tcp://127.0.0.1:18080` | Application listener |
| `QB_METRICS_LISTEN` | `tcp://127.0.0.1:19090` | Separate monitoring listener |
| `QB_REDIS_LATENCY` | `0` | Only `1` enables per-command latency clocks and storage |

- PostgreSQL `queries` counts queue round trips, not API calls; an inline parameterized
  query can prepare and execute separately. `last_query_duration_seconds` is the last
  sent query, not a percentile or average; its initial zero means unobserved.
- Redis `failed` is a subset of `errors`, itself a subset of `commands`. Do not sum them.
  The total and the `commands_by_family_total{family}` breakdown are separate families.
- Redis's histogram has 31 finite inclusive nanosecond bounds and a final `+Inf` bucket.
  It has `_count` but no `_sum`, because no duration sum is stored. Disabling latency
  drops the distribution; lifetime command counters remain intact.
- Stable `connection="demo",core="1"` labels distinguish the database/client source.
  Do not use SQL text, Redis keys, credentials, paths supplied by clients or pointer
  addresses as labels. HTTP uses route patterns; 404s use `unmatched`.
- Registry source callbacks are synchronous owner-core reads. Handles are declared
  after the client members, removed before them, and never transferred between cores.
  A callback copied for an in-progress collection is not retroactively removed by reset.
- `qb_scrape_core_up=0` means a registry did not answer before its deadline. The other
  cores' samples still return. No response is invented for the silent core.
- Core counters include collection traffic; the dashboard is not a performance baseline.
  Redis p95 and HTTP p95 panels are histogram estimates, not exact percentiles.
