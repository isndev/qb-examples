<!-- GENERATED FILE — DO NOT EDIT. -->
<!-- Regenerate with: python3 dev/agent/gen-capability-index.py --write -->
<!-- Source of truth: the @demonstrates blocks in examples/. `verify.sh` fails on any -->
<!-- byte of drift, so editing this file by hand is a red build, not a shortcut.     -->

# Example capability index

Which shipped example demonstrates a given qb / qbm capability, derived from the
`@demonstrates` blocks in `examples/`.

Every claim below has already been checked by `dev/agent/check-example-headers.py`:
the name occurs in that file's **code**, with comments and string literals blanked first.
So this is not a list of promises — it is a list of promises that were verified.

Paths are relative to `examples/`. There are deliberately no CMake target names here:
the build derives those from the path and writes the authoritative mapping to
`<build>/examples/example-roster.txt`, and a name typed in a second place is a name that
can disagree.

**99 programs, 7 tiers, 820 distinct capabilities, 1614 claims.**

## 1. By capability

### `(unqualified members)`

| capability | demonstrated by |
| --- | --- |
| `Header` | `02-io/06-framing-toolbox.cpp` |
| `QB_LOG_CRIT` | `02-io/11-logging-and-metrics.cpp` |
| `QB_LOG_DEBUG` | `02-io/11-logging-and-metrics.cpp` |
| `QB_LOG_INFO` | `02-io/11-logging-and-metrics.cpp` |
| `QB_LOG_VERB` | `02-io/11-logging-and-metrics.cpp` |
| `QB_LOG_WARN` | `02-io/11-logging-and-metrics.cpp` |
| `acl_cat` | `06-modules/redis/14-acl-and-topology.cpp` |
| `acl_deluser` | `06-modules/redis/14-acl-and-topology.cpp` |
| `acl_dryrun` | `06-modules/redis/14-acl-and-topology.cpp` |
| `acl_genpass` | `06-modules/redis/14-acl-and-topology.cpp` |
| `acl_getuser` | `06-modules/redis/14-acl-and-topology.cpp` |
| `acl_list` | `06-modules/redis/14-acl-and-topology.cpp` |
| `acl_setuser` | `06-modules/redis/14-acl-and-topology.cpp` |
| `acl_users` | `06-modules/redis/14-acl-and-topology.cpp` |
| `acl_whoami` | `06-modules/redis/14-acl-and-topology.cpp` |
| `active_coroutine_count` | `05-services/04-shutdown-and-drain/main.cpp` |
| `active_count` | `03-coroutines/07-structured-concurrency.cpp` |
| `add` | `04-patterns/03-worker-pool.cpp`, `04-patterns/08-batching-and-idempotency.cpp` |
| `addActor` | `05-services/04-shutdown-and-drain/main.cpp` |
| `addActor<T>` | `01-actors/01-hello-actor.cpp`, `01-actors/02-messaging.cpp`, `01-actors/03-event-payloads.cpp`, `01-actors/04-cores-and-placement.cpp`, `01-actors/05-lifecycle.cpp`, `01-actors/06-doing-things-later.cpp`, `01-actors/07-service-actor.cpp`, `01-actors/08-child-actors.cpp`, `01-actors/09-state-machine.cpp`, `01-actors/10-signals-and-shutdown.cpp`, `01-actors/11-hot-path.cpp`, `01-actors/12-lockfree-bridge.cpp`, `03-coroutines/02-actor-coroutines.cpp`, `03-coroutines/04-ask-request-response.cpp`, `04-patterns/01-pubsub.cpp`, `04-patterns/03-worker-pool.cpp`, `04-patterns/07-saga.cpp`, `04-patterns/09-discovery.cpp`, `05-services/01-tcp-chat/client/main.cpp`, `05-services/01-tcp-chat/server/main.cpp`, `05-services/02-pubsub-broker/client/main.cpp`, `05-services/02-pubsub-broker/server/main.cpp`, `05-services/03-file-pipeline/main.cpp`, `06-modules/http/01-hello-server.cpp`, `07-applications/01-taskmanager/src/main.cpp`, `07-applications/02-auction-house/src/main.cpp`, `07-applications/03-market-data-hub/src/main.cpp` |
| `addRefActor<Database>` | `03-coroutines/03-awaiting-oninit.cpp` |
| `addRefActor<Member>` | `01-actors/08-child-actors.cpp` |
| `addRefActor<T>` | `04-patterns/02-supervisor.cpp` |
| `addRefHandle<Member>` | `01-actors/08-child-actors.cpp` |
| `add_chunk` | `06-modules/http/14-streaming-and-cookies.cpp` |
| `add_cookie` | `06-modules/http/14-streaming-and-cookies.cpp` |
| `add_final_chunk` | `06-modules/http/14-streaming-and-cookies.cpp` |
| `add_subprotocol` | `06-modules/ws/03-coro-session.cpp` |
| `all` | `03-coroutines/11-async-streams.cpp` |
| `all<int, std::string>` | `06-modules/pgsql/06-typed-rows.cpp` |
| `allocated_push<Blob>` | `01-actors/11-hot-path.cpp` |
| `alpn` | `02-io/07-tls.cpp` |
| `any` | `03-coroutines/11-async-streams.cpp` |
| `append` | `06-modules/redis/02-data-types.cpp` |
| `application_name` | `06-modules/pgsql/08-tls-and-limits.cpp` |
| `arrive_and_wait` | `03-coroutines/12-sync-primitives.cpp` |
| `as<qb::http::Form>` | `06-modules/http/14-streaming-and-cookies.cpp` |
| `as<qb::http::Multipart>` | `06-modules/http/14-streaming-and-cookies.cpp` |
| `as<std::tuple<int, std::string, std::optional<std::string>, double>>` | `06-modules/pgsql/06-typed-rows.cpp` |
| `attempts` | `03-coroutines/13-retry-and-single-flight.cpp` |
| `available_permits` | `03-coroutines/12-sync-primitives.cpp` |
| `await` | `06-modules/pgsql/09-callbacks-and-await.cpp`, `06-modules/redis/11-callbacks-and-consumers.cpp` |
| `backpressure` | `03-coroutines/11-async-streams.cpp` |
| `begin` | `06-modules/pgsql/03-transactions.cpp`, `06-modules/pgsql/07-listen-notify.cpp`, `06-modules/pgsql/08-tls-and-limits.cpp`, `06-modules/pgsql/10-streaming-results.cpp` |
| `bitcount` | `06-modules/redis/12-cardinality-and-bitmaps.cpp` |
| `bitfield` | `06-modules/redis/12-cardinality-and-bitmaps.cpp` |
| `bitop` | `06-modules/redis/12-cardinality-and-bitmaps.cpp` |
| `bitpos` | `06-modules/redis/12-cardinality-and-bitmaps.cpp` |
| `blpop` | `06-modules/redis/02-data-types.cpp` |
| `boundary` | `06-modules/http/14-streaming-and-cookies.cpp` |
| `broadcast<E>` | `01-actors/04-cores-and-placement.cpp` |
| `broadcast<KillEvent>` | `01-actors/09-state-machine.cpp` |
| `broadcast<qb::KillEvent>` | `01-actors/03-event-payloads.cpp`, `01-actors/05-lifecycle.cpp`, `01-actors/06-doing-things-later.cpp`, `01-actors/07-service-actor.cpp`, `01-actors/12-lockfree-bridge.cpp`, `03-coroutines/02-actor-coroutines.cpp`, `05-services/03-file-pipeline/main.cpp` |
| `brpop` | `06-modules/redis/09-reliability.cpp` |
| `buffer` | `03-coroutines/11-async-streams.cpp` |
| `build_event<Tick>` | `01-actors/11-hot-path.cpp` |
| `builder()` | `05-services/01-tcp-chat/server/main.cpp`, `05-services/02-pubsub-broker/server/main.cpp` |
| `bytes_written` | `02-io/09-graceful-drain.cpp` |
| `cancel_all` | `03-coroutines/07-structured-concurrency.cpp` |
| `cancel_token` | `03-coroutines/07-structured-concurrency.cpp` |
| `capacity` | `02-io/11-logging-and-metrics.cpp` |
| `chain` | `03-coroutines/11-async-streams.cpp` |
| `child` | `04-patterns/02-supervisor.cpp` |
| `child_count` | `04-patterns/02-supervisor.cpp` |
| `child_token` | `03-coroutines/06-cancellation.cpp` |
| `client_id` | `06-modules/redis/09-reliability.cpp` |
| `client_kill` | `06-modules/redis/09-reliability.cpp` |
| `close` | `02-io/02-files.cpp` |
| `close_after_deliver` | `02-io/09-graceful-drain.cpp` |
| `close_async` | `06-modules/ws/03-coro-session.cpp`, `06-modules/ws/04-coro-client.cpp` |
| `cluster_info` | `06-modules/redis/14-acl-and-topology.cpp` |
| `cluster_keyslot` | `06-modules/redis/14-acl-and-topology.cpp` |
| `cluster_myid` | `06-modules/redis/14-acl-and-topology.cpp` |
| `cluster_shards` | `06-modules/redis/14-acl-and-topology.cpp` |
| `collect` | `03-coroutines/11-async-streams.cpp` |
| `commit` | `06-modules/pgsql/03-transactions.cpp`, `06-modules/pgsql/07-listen-notify.cpp`, `06-modules/pgsql/10-streaming-results.cpp` |
| `compile` | `06-modules/http/01-hello-server.cpp`, `06-modules/http/02-routing.cpp`, `06-modules/http/03-controllers.cpp`, `06-modules/http/04-middleware.cpp`, `06-modules/http/05-rest-api-json.cpp`, `06-modules/http/06-validation.cpp`, `06-modules/http/07-auth-jwt.cpp`, `06-modules/http/08-static-files.cpp`, `06-modules/http/09-coroutine-handlers.cpp`, `06-modules/http/10-client.cpp`, `06-modules/http/11-https.cpp`, `06-modules/http/12-http2.cpp`, `06-modules/http/13-http3.cpp`, `06-modules/http/14-streaming-and-cookies.cpp`, `06-modules/http/15-http2-and-http3-clients.cpp`, `06-modules/ws/01-chat-server.cpp` |
| `compress` | `06-modules/http/14-streaming-and-cookies.cpp` |
| `connect` | `06-modules/http/15-http2-and-http3-clients.cpp`, `06-modules/pgsql/08-tls-and-limits.cpp`, `06-modules/redis/11-callbacks-and-consumers.cpp`, `06-modules/ws/03-coro-session.cpp`, `06-modules/ws/04-coro-client.cpp` |
| `connect_with_retry` | `06-modules/redis/09-reliability.cpp` |
| `context()` | `03-coroutines/03-awaiting-oninit.cpp`, `03-coroutines/06-cancellation.cpp`, `04-patterns/08-batching-and-idempotency.cpp` |
| `controller<T>` | `06-modules/http/03-controllers.cpp` |
| `cookie` | `06-modules/http/14-streaming-and-cookies.cpp` |
| `cookie_value` | `06-modules/http/14-streaming-and-cookies.cpp` |
| `core` | `07-applications/03-market-data-hub/src/main.cpp` |
| `count` | `03-coroutines/11-async-streams.cpp` |
| `count_down` | `03-coroutines/12-sync-primitives.cpp` |
| `create_part` | `06-modules/http/14-streaming-and-cookies.cpp` |
| `ctx.cancellable` | `03-coroutines/06-cancellation.cpp` |
| `ctx.cancellation_point` | `03-coroutines/06-cancellation.cpp` |
| `ctx.cancelled` | `03-coroutines/06-cancellation.cpp` |
| `ctx.id` | `04-patterns/09-discovery.cpp` |
| `ctx.push<Drained>` | `01-actors/10-signals-and-shutdown.cpp` |
| `ctx.push<Step>` | `01-actors/08-child-actors.cpp` |
| `ctx.push_to` | `05-services/04-shutdown-and-drain/main.cpp` |
| `ctx.push_to<Compare>` | `04-patterns/09-discovery.cpp` |
| `ctx.push_to<Quote>` | `03-coroutines/04-ask-request-response.cpp`, `04-patterns/04-scatter-gather.cpp` |
| `ctx.sleep` | `01-actors/02-messaging.cpp`, `01-actors/04-cores-and-placement.cpp`, `01-actors/05-lifecycle.cpp`, `01-actors/06-doing-things-later.cpp`, `01-actors/08-child-actors.cpp`, `01-actors/09-state-machine.cpp`, `01-actors/10-signals-and-shutdown.cpp`, `03-coroutines/03-awaiting-oninit.cpp`, `03-coroutines/04-ask-request-response.cpp`, `03-coroutines/06-cancellation.cpp`, `04-patterns/04-scatter-gather.cpp`, `04-patterns/05-resilience.cpp`, `05-services/03-file-pipeline/main.cpp`, `05-services/04-shutdown-and-drain/main.cpp`, `06-modules/redis/04-pubsub.cpp`, `06-modules/redis/06-streams.cpp`, `06-modules/redis/10-cache-actor.cpp` |
| `ctx.time` | `03-coroutines/02-actor-coroutines.cpp`, `03-coroutines/04-ask-request-response.cpp`, `04-patterns/04-scatter-gather.cpp`, `04-patterns/05-resilience.cpp`, `04-patterns/06-streaming.cpp` |
| `ctx.token` | `03-coroutines/06-cancellation.cpp` |
| `ctx.until_cancelled` | `03-coroutines/06-cancellation.cpp` |
| `current_count` | `03-coroutines/12-sync-primitives.cpp` |
| `current_state` | `02-io/12-quic.cpp` |
| `debounce` | `03-coroutines/11-async-streams.cpp` |
| `del` | `06-modules/http/02-routing.cpp`, `06-modules/http/03-controllers.cpp`, `06-modules/redis/02-data-types.cpp`, `06-modules/redis/06-streams.cpp`, `06-modules/redis/07-scripting.cpp`, `06-modules/redis/08-sorted-sets-and-ttl.cpp`, `06-modules/redis/09-reliability.cpp`, `06-modules/redis/11-callbacks-and-consumers.cpp`, `06-modules/redis/12-cardinality-and-bitmaps.cpp`, `06-modules/redis/13-geospatial.cpp`, `06-modules/redis/14-acl-and-topology.cpp` |
| `disable_auto_reconnect` | `06-modules/redis/09-reliability.cpp` |
| `discard` | `06-modules/redis/05-transactions.cpp` |
| `disconnect` | `06-modules/http/15-http2-and-http3-clients.cpp` |
| `drain_to` | `03-coroutines/11-async-streams.cpp` |
| `empty` | `03-coroutines/11-async-streams.cpp`, `04-patterns/03-worker-pool.cpp` |
| `enable_auto_reconnect` | `06-modules/redis/09-reliability.cpp` |
| `engine.core` | `05-services/01-tcp-chat/server/main.cpp`, `05-services/02-pubsub-broker/server/main.cpp`, `07-applications/01-taskmanager/src/main.cpp`, `07-applications/02-auction-house/src/main.cpp` |
| `engine.hasError` | `05-services/01-tcp-chat/client/main.cpp`, `05-services/01-tcp-chat/server/main.cpp`, `05-services/02-pubsub-broker/client/main.cpp`, `05-services/02-pubsub-broker/server/main.cpp`, `07-applications/01-taskmanager/src/main.cpp`, `07-applications/02-auction-house/src/main.cpp` |
| `engine.join` | `05-services/01-tcp-chat/client/main.cpp`, `05-services/01-tcp-chat/server/main.cpp`, `05-services/02-pubsub-broker/client/main.cpp`, `05-services/02-pubsub-broker/server/main.cpp`, `07-applications/01-taskmanager/src/main.cpp`, `07-applications/02-auction-house/src/main.cpp` |
| `engine.start` | `05-services/01-tcp-chat/client/main.cpp`, `05-services/01-tcp-chat/server/main.cpp`, `05-services/02-pubsub-broker/client/main.cpp`, `05-services/02-pubsub-broker/server/main.cpp`, `07-applications/01-taskmanager/src/main.cpp`, `07-applications/02-auction-house/src/main.cpp` |
| `engine.stop` | `05-services/02-pubsub-broker/server/main.cpp` |
| `enqueue` | `07-applications/03-market-data-hub/src/main.cpp` |
| `error` | `06-modules/pgsql/09-callbacks-and-await.cpp`, `06-modules/pgsql/10-streaming-results.cpp`, `06-modules/redis/05-transactions.cpp`, `06-modules/redis/07-scripting.cpp`, `06-modules/redis/09-reliability.cpp`, `06-modules/redis/11-callbacks-and-consumers.cpp`, `06-modules/redis/12-cardinality-and-bitmaps.cpp`, `06-modules/redis/13-geospatial.cpp`, `06-modules/redis/14-acl-and-topology.cpp` |
| `eval<bool>` | `06-modules/redis/07-scripting.cpp` |
| `eval<long long>` | `06-modules/redis/07-scripting.cpp` |
| `eval<std::string>` | `06-modules/redis/07-scripting.cpp` |
| `evalRo<std::string>` | `06-modules/redis/07-scripting.cpp` |
| `evalsha<bool>` | `06-modules/redis/07-scripting.cpp` |
| `exec<std::string>` | `06-modules/redis/05-transactions.cpp` |
| `execute` | `06-modules/pgsql/03-transactions.cpp`, `06-modules/pgsql/06-typed-rows.cpp`, `06-modules/pgsql/07-listen-notify.cpp`, `06-modules/pgsql/08-tls-and-limits.cpp`, `06-modules/pgsql/09-callbacks-and-await.cpp`, `06-modules/pgsql/10-streaming-results.cpp` |
| `exists` | `06-modules/redis/12-cardinality-and-bitmaps.cpp`, `06-modules/redis/13-geospatial.cpp`, `06-modules/redis/14-acl-and-topology.cpp` |
| `expire` | `06-modules/redis/06-streams.cpp`, `06-modules/redis/08-sorted-sets-and-ttl.cpp` |
| `extractSession` | `02-io/09-graceful-drain.cpp` |
| `fcall<std::string>` | `06-modules/redis/07-scripting.cpp` |
| `fcallRo<std::string>` | `06-modules/redis/07-scripting.cpp` |
| `filter` | `03-coroutines/11-async-streams.cpp` |
| `find` | `03-coroutines/11-async-streams.cpp` |
| `first` | `03-coroutines/11-async-streams.cpp` |
| `flush` | `04-patterns/08-batching-and-idempotency.cpp`, `06-modules/redis/11-callbacks-and-consumers.cpp` |
| `for_each` | `03-coroutines/11-async-streams.cpp` |
| `for_key` | `04-patterns/03-worker-pool.cpp` |
| `from_channel` | `03-coroutines/11-async-streams.cpp` |
| `from_vector` | `03-coroutines/11-async-streams.cpp` |
| `full` | `02-io/11-logging-and-metrics.cpp` |
| `function_delete` | `06-modules/redis/07-scripting.cpp` |
| `function_list` | `06-modules/redis/07-scripting.cpp` |
| `function_load` | `06-modules/redis/07-scripting.cpp` |
| `geoadd` | `06-modules/redis/13-geospatial.cpp` |
| `geodist` | `06-modules/redis/13-geospatial.cpp` |
| `geohash` | `06-modules/redis/13-geospatial.cpp` |
| `geopos` | `06-modules/redis/13-geospatial.cpp` |
| `georadius` | `06-modules/redis/13-geospatial.cpp` |
| `georadiusbymember` | `06-modules/redis/13-geospatial.cpp` |
| `geosearch` | `06-modules/redis/13-geospatial.cpp` |
| `get` | `06-modules/http/01-hello-server.cpp`, `06-modules/http/02-routing.cpp`, `06-modules/http/03-controllers.cpp`, `06-modules/http/04-middleware.cpp`, `06-modules/http/07-auth-jwt.cpp`, `06-modules/http/08-static-files.cpp`, `06-modules/http/09-coroutine-handlers.cpp`, `06-modules/http/11-https.cpp`, `06-modules/http/12-http2.cpp`, `06-modules/redis/02-data-types.cpp`, `06-modules/redis/07-scripting.cpp`, `06-modules/redis/11-callbacks-and-consumers.cpp`, `06-modules/ws/01-chat-server.cpp` |
| `get()` | `01-actors/08-child-actors.cpp`, `03-coroutines/03-awaiting-oninit.cpp` |
| `getIndex` | `01-actors/03-event-payloads.cpp`, `01-actors/07-service-actor.cpp`, `01-actors/11-hot-path.cpp`, `01-actors/12-lockfree-bridge.cpp`, `03-coroutines/04-ask-request-response.cpp` |
| `getIndex()` | `01-actors/04-cores-and-placement.cpp`, `04-patterns/01-pubsub.cpp` |
| `getMessageSize` | `02-io/05-custom-protocol.cpp` |
| `getPipe` | `01-actors/11-hot-path.cpp` |
| `getService` | `04-patterns/01-pubsub.cpp` |
| `getService<ConfigService>` | `01-actors/07-service-actor.cpp` |
| `getServiceId<ConfigTag>` | `01-actors/07-service-actor.cpp` |
| `getSource()` | `01-actors/02-messaging.cpp`, `01-actors/05-lifecycle.cpp`, `01-actors/06-doing-things-later.cpp`, `01-actors/09-state-machine.cpp` |
| `getTimeout` | `02-io/08-timeouts-and-watchers.cpp` |
| `get_alpn_selected_protocol` | `02-io/07-tls.cpp` |
| `get_base_uri` | `06-modules/http/15-http2-and-http3-clients.cpp` |
| `get_negotiated_cipher_suite` | `02-io/07-tls.cpp` |
| `get_negotiated_tls_version` | `02-io/07-tls.cpp` |
| `get_peer_certificate_details` | `02-io/07-tls.cpp` |
| `get_stats` | `06-modules/http/10-client.cpp`, `06-modules/http/15-http2-and-http3-clients.cpp` |
| `get_timeout` | `06-modules/pgsql/08-tls-and-limits.cpp` |
| `getbit` | `06-modules/redis/12-cardinality-and-bitmaps.cpp` |
| `group` | `06-modules/http/04-middleware.cpp`, `06-modules/http/05-rest-api-json.cpp`, `06-modules/http/07-auth-jwt.cpp`, `06-modules/http/08-static-files.cpp`, `06-modules/http/12-http2.cpp` |
| `hasError` | `01-actors/10-signals-and-shutdown.cpp`, `05-services/04-shutdown-and-drain/main.cpp`, `07-applications/03-market-data-hub/src/main.cpp` |
| `has_active_coroutines` | `05-services/04-shutdown-and-drain/main.cpp` |
| `has_next` | `03-coroutines/10-generators.cpp` |
| `has_pending_write` | `02-io/09-graceful-drain.cpp`, `05-services/04-shutdown-and-drain/main.cpp` |
| `hdel` | `06-modules/redis/02-data-types.cpp` |
| `hexists` | `06-modules/redis/02-data-types.cpp` |
| `hget` | `06-modules/redis/02-data-types.cpp` |
| `hgetall` | `06-modules/redis/02-data-types.cpp` |
| `hincrby` | `06-modules/redis/02-data-types.cpp` |
| `hkeys` | `06-modules/redis/02-data-types.cpp` |
| `hlen` | `06-modules/redis/02-data-types.cpp` |
| `hset` | `06-modules/redis/02-data-types.cpp` |
| `http_only` | `06-modules/http/14-streaming-and-cookies.cpp` |
| `hvals` | `06-modules/redis/02-data-types.cpp` |
| `id()` | `01-actors/04-cores-and-placement.cpp`, `01-actors/08-child-actors.cpp`, `01-actors/09-state-machine.cpp`, `03-coroutines/03-awaiting-oninit.cpp`, `05-services/03-file-pipeline/main.cpp` |
| `idList()` | `05-services/01-tcp-chat/server/main.cpp`, `05-services/02-pubsub-broker/server/main.cpp` |
| `in_transaction` | `06-modules/pgsql/10-streaming-results.cpp` |
| `incr` | `06-modules/redis/08-sorted-sets-and-ttl.cpp`, `06-modules/redis/11-callbacks-and-consumers.cpp` |
| `incrby` | `06-modules/redis/02-data-types.cpp` |
| `index` | `04-patterns/09-discovery.cpp` |
| `info` | `06-modules/redis/09-reliability.cpp` |
| `is<Worker>` | `04-patterns/09-discovery.cpp` |
| `is_active` | `03-coroutines/03-awaiting-oninit.cpp` |
| `is_actor_alive` | `01-actors/08-child-actors.cpp`, `03-coroutines/03-awaiting-oninit.cpp`, `04-patterns/09-discovery.cpp` |
| `is_alive` | `03-coroutines/03-awaiting-oninit.cpp` |
| `is_closed` | `03-coroutines/09-channels.cpp` |
| `is_connected` | `06-modules/http/10-client.cpp`, `06-modules/http/15-http2-and-http3-clients.cpp`, `06-modules/pgsql/07-listen-notify.cpp`, `06-modules/redis/09-reliability.cpp` |
| `is_in_multi` | `06-modules/redis/05-transactions.cpp` |
| `is_locked` | `03-coroutines/12-sync-primitives.cpp` |
| `is_null` | `06-modules/pgsql/06-typed-rows.cpp` |
| `is_ready` | `03-coroutines/12-sync-primitives.cpp`, `03-coroutines/13-retry-and-single-flight.cpp` |
| `is_valid` | `04-patterns/09-discovery.cpp` |
| `join` | `07-applications/03-market-data-hub/src/main.cpp` |
| `join_all` | `03-coroutines/07-structured-concurrency.cpp` |
| `join_all_for` | `03-coroutines/07-structured-concurrency.cpp` |
| `join_any` | `03-coroutines/07-structured-concurrency.cpp` |
| `json` | `06-modules/pgsql/06-typed-rows.cpp` |
| `kill` | `05-services/04-shutdown-and-drain/main.cpp` |
| `kill()` | `01-actors/01-hello-actor.cpp`, `01-actors/02-messaging.cpp`, `01-actors/04-cores-and-placement.cpp`, `01-actors/05-lifecycle.cpp`, `01-actors/06-doing-things-later.cpp`, `01-actors/08-child-actors.cpp`, `01-actors/09-state-machine.cpp`, `01-actors/10-signals-and-shutdown.cpp`, `01-actors/11-hot-path.cpp`, `03-coroutines/02-actor-coroutines.cpp`, `04-patterns/01-pubsub.cpp` |
| `lindex` | `06-modules/redis/02-data-types.cpp` |
| `listen` | `06-modules/http/01-hello-server.cpp`, `06-modules/http/02-routing.cpp`, `06-modules/http/04-middleware.cpp`, `06-modules/http/06-validation.cpp`, `06-modules/http/08-static-files.cpp`, `06-modules/http/11-https.cpp`, `06-modules/http/12-http2.cpp`, `06-modules/http/13-http3.cpp`, `06-modules/http/15-http2-and-http3-clients.cpp`, `06-modules/pgsql/07-listen-notify.cpp`, `06-modules/ws/01-chat-server.cpp` |
| `listen_v4` | `06-modules/http/10-client.cpp`, `06-modules/http/14-streaming-and-cookies.cpp`, `06-modules/http/15-http2-and-http3-clients.cpp` |
| `llen` | `06-modules/redis/02-data-types.cpp` |
| `local_endpoint` | `02-io/06-framing-toolbox.cpp`, `02-io/12-quic.cpp`, `03-coroutines/14-foreign-awaitables.cpp`, `05-services/04-shutdown-and-drain/main.cpp`, `06-modules/ws/03-coro-session.cpp` |
| `lock` | `01-actors/12-lockfree-bridge.cpp` |
| `locked` | `01-actors/12-lockfree-bridge.cpp` |
| `lpop` | `06-modules/redis/02-data-types.cpp` |
| `lpush` | `06-modules/redis/02-data-types.cpp`, `06-modules/redis/11-callbacks-and-consumers.cpp` |
| `lrange` | `06-modules/redis/02-data-types.cpp`, `06-modules/redis/11-callbacks-and-consumers.cpp` |
| `lset` | `06-modules/redis/02-data-types.cpp` |
| `ltrim` | `06-modules/redis/02-data-types.cpp` |
| `map` | `03-coroutines/11-async-streams.cpp` |
| `max_age` | `06-modules/http/14-streaming-and-cookies.cpp` |
| `memory_usage` | `06-modules/redis/12-cardinality-and-bitmaps.cpp` |
| `messages_processed` | `02-io/09-graceful-drain.cpp` |
| `mget` | `06-modules/redis/02-data-types.cpp` |
| `min_version` | `02-io/07-tls.cpp` |
| `mset` | `06-modules/redis/02-data-types.cpp` |
| `multi` | `06-modules/redis/05-transactions.cpp` |
| `native_handle` | `03-coroutines/14-foreign-awaitables.cpp` |
| `negotiated_subprotocol` | `06-modules/ws/03-coro-session.cpp`, `06-modules/ws/04-coro-client.cpp` |
| `next` | `03-coroutines/10-generators.cpp`, `04-patterns/03-worker-pool.cpp` |
| `next_frame` | `06-modules/ws/03-coro-session.cpp`, `06-modules/ws/04-coro-client.cpp` |
| `not_ok` | `02-io/06-framing-toolbox.cpp` |
| `notify` | `06-modules/pgsql/07-listen-notify.cpp` |
| `notify_channel_capacity` | `06-modules/pgsql/07-listen-notify.cpp` |
| `ok` | `06-modules/pgsql/10-streaming-results.cpp`, `06-modules/redis/02-data-types.cpp`, `06-modules/redis/05-transactions.cpp`, `06-modules/redis/07-scripting.cpp`, `06-modules/redis/08-sorted-sets-and-ttl.cpp`, `06-modules/redis/09-reliability.cpp`, `06-modules/redis/11-callbacks-and-consumers.cpp`, `06-modules/redis/12-cardinality-and-bitmaps.cpp`, `06-modules/redis/13-geospatial.cpp`, `06-modules/redis/14-acl-and-topology.cpp` |
| `onMessage` | `02-io/05-custom-protocol.cpp`, `02-io/06-framing-toolbox.cpp` |
| `on_compensate` | `04-patterns/07-saga.cpp` |
| `on_disconnected` | `06-modules/redis/11-callbacks-and-consumers.cpp` |
| `on_error` | `06-modules/redis/11-callbacks-and-consumers.cpp` |
| `on_escalate` | `04-patterns/02-supervisor.cpp` |
| `on_message` | `06-modules/redis/11-callbacks-and-consumers.cpp` |
| `on_notify` | `06-modules/pgsql/07-listen-notify.cpp` |
| `on_notify_dropped` | `06-modules/pgsql/07-listen-notify.cpp` |
| `one<int, std::string>` | `06-modules/pgsql/06-typed-rows.cpp` |
| `open` | `02-io/02-files.cpp` |
| `open_bidirectional_stream` | `02-io/12-quic.cpp` |
| `parts` | `06-modules/http/14-streaming-and-cookies.cpp` |
| `path` | `06-modules/http/14-streaming-and-cookies.cpp` |
| `pending` | `04-patterns/07-saga.cpp`, `04-patterns/08-batching-and-idempotency.cpp` |
| `pending_reply_count` | `06-modules/redis/11-callbacks-and-consumers.cpp` |
| `persist` | `06-modules/redis/08-sorted-sets-and-ttl.cpp` |
| `pfadd` | `06-modules/redis/12-cardinality-and-bitmaps.cpp` |
| `pfcount` | `06-modules/redis/12-cardinality-and-bitmaps.cpp` |
| `pfmerge` | `06-modules/redis/12-cardinality-and-bitmaps.cpp` |
| `post` | `06-modules/http/02-routing.cpp`, `06-modules/http/03-controllers.cpp`, `06-modules/http/07-auth-jwt.cpp` |
| `prepare` | `06-modules/pgsql/09-callbacks-and-await.cpp` |
| `prepare_file` | `06-modules/pgsql/09-callbacks-and-await.cpp` |
| `prune_completed` | `03-coroutines/07-structured-concurrency.cpp` |
| `publish` | `02-io/09-graceful-drain.cpp`, `04-patterns/01-pubsub.cpp`, `06-modules/redis/11-callbacks-and-consumers.cpp` |
| `push<E>` | `01-actors/01-hello-actor.cpp`, `01-actors/02-messaging.cpp`, `01-actors/03-event-payloads.cpp`, `01-actors/04-cores-and-placement.cpp`, `01-actors/05-lifecycle.cpp`, `01-actors/06-doing-things-later.cpp`, `01-actors/07-service-actor.cpp`, `01-actors/08-child-actors.cpp`, `01-actors/09-state-machine.cpp`, `01-actors/10-signals-and-shutdown.cpp`, `01-actors/12-lockfree-bridge.cpp`, `04-patterns/01-pubsub.cpp`, `04-patterns/02-supervisor.cpp`, `04-patterns/03-worker-pool.cpp`, `04-patterns/06-streaming.cpp`, `04-patterns/08-batching-and-idempotency.cpp` |
| `push<Ping>` | `03-coroutines/03-awaiting-oninit.cpp` |
| `push<Tick>` | `01-actors/11-hot-path.cpp` |
| `push_back` | `02-io/11-logging-and-metrics.cpp` |
| `push_request` | `06-modules/http/10-client.cpp`, `06-modules/http/15-http2-and-http3-clients.cpp` |
| `push_requests` | `06-modules/http/10-client.cpp`, `06-modules/http/15-http2-and-http3-clients.cpp` |
| `put` | `06-modules/http/02-routing.cpp`, `06-modules/http/03-controllers.cpp` |
| `query` | `06-modules/pgsql/03-transactions.cpp`, `06-modules/pgsql/06-typed-rows.cpp`, `06-modules/pgsql/10-streaming-results.cpp` |
| `query_stream` | `06-modules/pgsql/10-streaming-results.cpp` |
| `raw` | `06-modules/redis/05-transactions.cpp`, `06-modules/redis/09-reliability.cpp`, `06-modules/redis/13-geospatial.cpp` |
| `read` | `02-io/02-files.cpp` |
| `ready()` | `01-actors/08-child-actors.cpp` |
| `ready_async` | `03-coroutines/03-awaiting-oninit.cpp` |
| `receive` | `06-modules/pgsql/07-listen-notify.cpp`, `06-modules/ws/03-coro-session.cpp`, `06-modules/ws/04-coro-client.cpp` |
| `recv_for` | `03-coroutines/09-channels.cpp` |
| `reduce` | `03-coroutines/11-async-streams.cpp` |
| `registerCallback` | `01-actors/01-hello-actor.cpp`, `01-actors/03-event-payloads.cpp`, `01-actors/12-lockfree-bridge.cpp`, `05-services/04-shutdown-and-drain/main.cpp` |
| `registerEvent<E>` | `01-actors/01-hello-actor.cpp`, `01-actors/02-messaging.cpp`, `01-actors/03-event-payloads.cpp`, `01-actors/04-cores-and-placement.cpp`, `01-actors/05-lifecycle.cpp`, `01-actors/06-doing-things-later.cpp`, `01-actors/07-service-actor.cpp`, `01-actors/08-child-actors.cpp`, `01-actors/09-state-machine.cpp`, `01-actors/10-signals-and-shutdown.cpp`, `01-actors/11-hot-path.cpp`, `01-actors/12-lockfree-bridge.cpp`, `03-coroutines/02-actor-coroutines.cpp`, `03-coroutines/03-awaiting-oninit.cpp`, `03-coroutines/04-ask-request-response.cpp`, `03-coroutines/06-cancellation.cpp`, `04-patterns/01-pubsub.cpp`, `04-patterns/02-supervisor.cpp`, `04-patterns/03-worker-pool.cpp`, `04-patterns/04-scatter-gather.cpp`, `04-patterns/06-streaming.cpp`, `04-patterns/07-saga.cpp`, `04-patterns/08-batching-and-idempotency.cpp`, `04-patterns/09-discovery.cpp`, `05-services/03-file-pipeline/main.cpp`, `05-services/04-shutdown-and-drain/main.cpp`, `06-modules/redis/04-pubsub.cpp`, `06-modules/redis/06-streams.cpp`, `06-modules/redis/10-cache-actor.cpp`, `06-modules/ws/02-chat-client.cpp` |
| `registerSession` | `05-services/04-shutdown-and-drain/main.cpp` |
| `release_savepoint` | `06-modules/pgsql/03-transactions.cpp` |
| `remove` | `04-patterns/03-worker-pool.cpp` |
| `resolve_ask` | `03-coroutines/04-ask-request-response.cpp`, `04-patterns/04-scatter-gather.cpp`, `04-patterns/05-resilience.cpp`, `04-patterns/06-streaming.cpp`, `04-patterns/07-saga.cpp`, `04-patterns/08-batching-and-idempotency.cpp` |
| `resolve_require` | `04-patterns/09-discovery.cpp` |
| `restarts` | `04-patterns/02-supervisor.cpp` |
| `result` | `06-modules/redis/02-data-types.cpp`, `06-modules/redis/05-transactions.cpp`, `06-modules/redis/07-scripting.cpp`, `06-modules/redis/08-sorted-sets-and-ttl.cpp`, `06-modules/redis/09-reliability.cpp`, `06-modules/redis/11-callbacks-and-consumers.cpp`, `06-modules/redis/12-cardinality-and-bitmaps.cpp`, `06-modules/redis/13-geospatial.cpp`, `06-modules/redis/14-acl-and-topology.cpp` |
| `rethrow_if_error` | `03-coroutines/07-structured-concurrency.cpp` |
| `rethrow_last` | `03-coroutines/13-retry-and-single-flight.cpp` |
| `rollback` | `06-modules/pgsql/03-transactions.cpp`, `06-modules/pgsql/07-listen-notify.cpp`, `06-modules/pgsql/08-tls-and-limits.cpp` |
| `rollback_savepoint` | `06-modules/pgsql/03-transactions.cpp` |
| `router` | `06-modules/http/06-validation.cpp`, `06-modules/http/10-client.cpp`, `06-modules/http/14-streaming-and-cookies.cpp`, `06-modules/http/15-http2-and-http3-clients.cpp` |
| `router()` | `06-modules/http/01-hello-server.cpp`, `06-modules/http/02-routing.cpp`, `06-modules/http/03-controllers.cpp`, `06-modules/http/04-middleware.cpp`, `06-modules/http/05-rest-api-json.cpp`, `06-modules/http/07-auth-jwt.cpp`, `06-modules/http/08-static-files.cpp`, `06-modules/http/09-coroutine-handlers.cpp`, `06-modules/http/11-https.cpp`, `06-modules/http/12-http2.cpp`, `06-modules/http/13-http3.cpp`, `06-modules/ws/01-chat-server.cpp` |
| `rows<int, std::string, std::optional<std::string>, double>` | `06-modules/pgsql/06-typed-rows.cpp` |
| `rpop` | `06-modules/redis/02-data-types.cpp` |
| `rpush` | `06-modules/redis/02-data-types.cpp`, `06-modules/redis/09-reliability.cpp` |
| `sadd` | `06-modules/redis/02-data-types.cpp`, `06-modules/redis/12-cardinality-and-bitmaps.cpp` |
| `same_site` | `06-modules/http/14-streaming-and-cookies.cpp` |
| `savepoint` | `06-modules/pgsql/03-transactions.cpp` |
| `scan` | `06-modules/redis/08-sorted-sets-and-ttl.cpp` |
| `scard` | `06-modules/redis/02-data-types.cpp`, `06-modules/redis/12-cardinality-and-bitmaps.cpp` |
| `scoped_acquire` | `03-coroutines/12-sync-primitives.cpp` |
| `scoped_lock` | `03-coroutines/12-sync-primitives.cpp` |
| `scoped_read_lock` | `03-coroutines/12-sync-primitives.cpp` |
| `scoped_write_lock` | `03-coroutines/12-sync-primitives.cpp` |
| `script_exists` | `06-modules/redis/07-scripting.cpp` |
| `script_load` | `06-modules/redis/07-scripting.cpp` |
| `sdiff` | `06-modules/redis/02-data-types.cpp` |
| `send<Tick>` | `01-actors/11-hot-path.cpp` |
| `send_datagram` | `02-io/12-quic.cpp` |
| `send_for` | `03-coroutines/09-channels.cpp` |
| `send_stream_data` | `02-io/12-quic.cpp` |
| `session_cache` | `02-io/07-tls.cpp` |
| `session_count` | `05-services/04-shutdown-and-drain/main.cpp` |
| `sessions` | `05-services/04-shutdown-and-drain/main.cpp` |
| `set` | `06-modules/redis/02-data-types.cpp`, `06-modules/redis/07-scripting.cpp`, `06-modules/redis/08-sorted-sets-and-ttl.cpp`, `06-modules/redis/11-callbacks-and-consumers.cpp` |
| `setAffinity` | `01-actors/11-hot-path.cpp` |
| `setDestination` | `02-io/04-udp.cpp` |
| `setLatency` | `01-actors/11-hot-path.cpp`, `07-applications/01-taskmanager/src/main.cpp`, `07-applications/02-auction-house/src/main.cpp`, `07-applications/03-market-data-hub/src/main.cpp` |
| `setTimeout` | `02-io/01-event-loop.cpp`, `02-io/08-timeouts-and-watchers.cpp`, `02-io/11-logging-and-metrics.cpp` |
| `set_auto_reconnect` | `06-modules/http/10-client.cpp` |
| `set_connect_timeout` | `06-modules/http/15-http2-and-http3-clients.cpp` |
| `set_error_task_chain` | `06-modules/http/05-rest-api-json.cpp` |
| `set_handshake_hook` | `06-modules/ws/03-coro-session.cpp` |
| `set_insecure` | `02-io/07-tls.cpp` |
| `set_pending_cap` | `06-modules/ws/03-coro-session.cpp`, `06-modules/ws/04-coro-client.cpp` |
| `set_request_timeout` | `06-modules/http/10-client.cpp`, `06-modules/http/15-http2-and-http3-clients.cpp` |
| `set_settings` | `02-io/12-quic.cpp` |
| `set_timeout` | `06-modules/pgsql/03-transactions.cpp`, `06-modules/pgsql/08-tls-and-limits.cpp` |
| `set_verify_peer` | `06-modules/http/15-http2-and-http3-clients.cpp` |
| `setbit` | `06-modules/redis/12-cardinality-and-bitmaps.cpp` |
| `setex` | `06-modules/redis/08-sorted-sets-and-ttl.cpp` |
| `shiftSize` | `02-io/06-framing-toolbox.cpp` |
| `single` | `03-coroutines/11-async-streams.cpp` |
| `sinter` | `06-modules/redis/02-data-types.cpp` |
| `sismember` | `06-modules/redis/02-data-types.cpp` |
| `size` | `02-io/11-logging-and-metrics.cpp`, `04-patterns/03-worker-pool.cpp`, `06-modules/pgsql/06-typed-rows.cpp` |
| `skip` | `03-coroutines/11-async-streams.cpp` |
| `smembers` | `06-modules/redis/02-data-types.cpp` |
| `spawn` | `01-actors/02-messaging.cpp`, `01-actors/04-cores-and-placement.cpp`, `01-actors/05-lifecycle.cpp`, `01-actors/06-doing-things-later.cpp`, `01-actors/08-child-actors.cpp`, `01-actors/09-state-machine.cpp`, `01-actors/10-signals-and-shutdown.cpp`, `03-coroutines/02-actor-coroutines.cpp`, `03-coroutines/03-awaiting-oninit.cpp`, `03-coroutines/04-ask-request-response.cpp`, `03-coroutines/06-cancellation.cpp`, `04-patterns/04-scatter-gather.cpp`, `04-patterns/05-resilience.cpp`, `04-patterns/06-streaming.cpp`, `04-patterns/07-saga.cpp`, `04-patterns/08-batching-and-idempotency.cpp`, `04-patterns/09-discovery.cpp`, `05-services/03-file-pipeline/main.cpp`, `05-services/04-shutdown-and-drain/main.cpp`, `06-modules/redis/04-pubsub.cpp`, `06-modules/redis/06-streams.cpp`, `06-modules/redis/10-cache-actor.cpp` |
| `spawn_cancellable` | `03-coroutines/07-structured-concurrency.cpp` |
| `spawn_child` | `04-patterns/02-supervisor.cpp` |
| `srem` | `06-modules/redis/02-data-types.cpp` |
| `start` | `07-applications/03-market-data-hub/src/main.cpp` |
| `stop()` | `04-patterns/02-supervisor.cpp` |
| `strlen` | `06-modules/redis/02-data-types.cpp`, `06-modules/redis/12-cardinality-and-bitmaps.cpp` |
| `subscribe` | `04-patterns/01-pubsub.cpp`, `06-modules/redis/11-callbacks-and-consumers.cpp` |
| `subscriber_count` | `04-patterns/01-pubsub.cpp` |
| `success` | `06-modules/pgsql/09-callbacks-and-await.cpp` |
| `sunion` | `06-modules/redis/02-data-types.cpp` |
| `supervisor()` | `04-patterns/02-supervisor.cpp` |
| `switch_protocol` | `02-io/09-graceful-drain.cpp` |
| `switch_protocol<T>` | `02-io/05-custom-protocol.cpp` |
| `take` | `03-coroutines/11-async-streams.cpp` |
| `text` | `06-modules/pgsql/06-typed-rows.cpp` |
| `then` | `06-modules/pgsql/09-callbacks-and-await.cpp` |
| `throttle` | `03-coroutines/11-async-streams.cpp` |
| `to` | `01-actors/11-hot-path.cpp` |
| `to_header` | `06-modules/http/14-streaming-and-cookies.cpp` |
| `total_count` | `03-coroutines/07-structured-concurrency.cpp` |
| `total_permits` | `03-coroutines/12-sync-primitives.cpp` |
| `tracked_slot_count` | `04-patterns/01-pubsub.cpp` |
| `transport().bind_v4` | `02-io/04-udp.cpp` |
| `transport().connect` | `06-modules/ws/02-chat-client.cpp` |
| `transport().connect_v4` | `02-io/03-tcp.cpp`, `02-io/05-custom-protocol.cpp` |
| `transport().init()` | `02-io/04-udp.cpp` |
| `transport().listen_v4` | `02-io/03-tcp.cpp`, `02-io/05-custom-protocol.cpp`, `02-io/06-framing-toolbox.cpp`, `05-services/04-shutdown-and-drain/main.cpp` |
| `trust` | `02-io/07-tls.cpp` |
| `try_acquire` | `03-coroutines/12-sync-primitives.cpp` |
| `try_lock` | `03-coroutines/12-sync-primitives.cpp` |
| `try_recv` | `03-coroutines/09-channels.cpp` |
| `try_send` | `03-coroutines/09-channels.cpp` |
| `trylock` | `01-actors/12-lockfree-bridge.cpp` |
| `trylock_for` | `01-actors/12-lockfree-bridge.cpp` |
| `ttl` | `06-modules/redis/08-sorted-sets-and-ttl.cpp` |
| `type` | `06-modules/redis/13-geospatial.cpp` |
| `uncompress` | `06-modules/http/14-streaming-and-cookies.cpp` |
| `unlisten` | `06-modules/pgsql/07-listen-notify.cpp` |
| `unlisten_all` | `06-modules/pgsql/07-listen-notify.cpp` |
| `unlock` | `01-actors/12-lockfree-bridge.cpp` |
| `unregisterCallback` | `01-actors/03-event-payloads.cpp`, `01-actors/12-lockfree-bridge.cpp`, `05-services/04-shutdown-and-drain/main.cpp` |
| `unsubscribe` | `04-patterns/01-pubsub.cpp`, `06-modules/redis/11-callbacks-and-consumers.cpp` |
| `unwatch` | `06-modules/redis/05-transactions.cpp` |
| `updateTimeout` | `02-io/08-timeouts-and-watchers.cpp` |
| `use` | `06-modules/http/03-controllers.cpp`, `06-modules/http/04-middleware.cpp`, `06-modules/http/05-rest-api-json.cpp`, `06-modules/http/06-validation.cpp`, `06-modules/http/07-auth-jwt.cpp`, `06-modules/http/08-static-files.cpp`, `06-modules/http/11-https.cpp`, `06-modules/http/12-http2.cpp`, `06-modules/http/13-http3.cpp`, `06-modules/ws/01-chat-server.cpp` |
| `usedCoreSet` | `01-actors/07-service-actor.cpp`, `07-applications/03-market-data-hub/src/main.cpp` |
| `valid` | `03-coroutines/13-retry-and-single-flight.cpp` |
| `valid()` | `01-actors/08-child-actors.cpp` |
| `verify` | `02-io/07-tls.cpp` |
| `view` | `06-modules/pgsql/06-typed-rows.cpp` |
| `waiters_count` | `03-coroutines/12-sync-primitives.cpp` |
| `watch` | `06-modules/redis/05-transactions.cpp` |
| `with_connect_timeout` | `06-modules/redis/09-reliability.cpp` |
| `with_initial_delay` | `06-modules/redis/09-reliability.cpp` |
| `with_jitter` | `06-modules/redis/09-reliability.cpp` |
| `with_max_attempts` | `06-modules/redis/09-reliability.cpp` |
| `with_multiplier` | `06-modules/redis/09-reliability.cpp` |
| `with_on_retry` | `06-modules/redis/09-reliability.cpp` |
| `workers` | `04-patterns/03-worker-pool.cpp` |
| `write` | `02-io/02-files.cpp` |
| `xack` | `06-modules/redis/06-streams.cpp` |
| `xadd` | `06-modules/redis/06-streams.cpp` |
| `xgroup_create` | `06-modules/redis/06-streams.cpp` |
| `xlen` | `06-modules/redis/06-streams.cpp` |
| `xpending` | `06-modules/redis/06-streams.cpp` |
| `xread` | `06-modules/redis/06-streams.cpp` |
| `xreadgroup` | `06-modules/redis/06-streams.cpp` |
| `xtrim` | `06-modules/redis/06-streams.cpp` |
| `yield_answer` | `04-patterns/06-streaming.cpp` |
| `zadd` | `06-modules/redis/08-sorted-sets-and-ttl.cpp` |
| `zcard` | `06-modules/redis/08-sorted-sets-and-ttl.cpp`, `06-modules/redis/13-geospatial.cpp` |
| `zincrby` | `06-modules/redis/08-sorted-sets-and-ttl.cpp` |
| `zrangebyscore` | `06-modules/redis/08-sorted-sets-and-ttl.cpp` |
| `zrem` | `06-modules/redis/08-sorted-sets-and-ttl.cpp` |
| `zremrangebyscore` | `06-modules/redis/08-sorted-sets-and-ttl.cpp` |
| `zrevrange` | `06-modules/redis/08-sorted-sets-and-ttl.cpp` |
| `zrevrank` | `06-modules/redis/08-sorted-sets-and-ttl.cpp` |
| `zscore` | `06-modules/redis/08-sorted-sets-and-ttl.cpp`, `06-modules/redis/13-geospatial.cpp` |

### `Protocol`

| capability | demonstrated by |
| --- | --- |
| `Protocol::end` | `02-io/03-tcp.cpp` |

### `ev`

| capability | demonstrated by |
| --- | --- |
| `ev::stat` | `02-io/01-event-loop.cpp` |

### `qb`

| capability | demonstrated by |
| --- | --- |
| `qb::Actor` | `01-actors/01-hello-actor.cpp`, `01-actors/09-state-machine.cpp`, `01-actors/11-hot-path.cpp`, `01-actors/12-lockfree-bridge.cpp`, `05-services/03-file-pipeline/main.cpp`, `06-modules/http/01-hello-server.cpp`, `06-modules/http/06-validation.cpp` |
| `qb::ActorHandle<Database>` | `03-coroutines/03-awaiting-oninit.cpp` |
| `qb::ActorHandle<Member>` | `01-actors/08-child-actors.cpp` |
| `qb::ActorId` | `01-actors/01-hello-actor.cpp`, `01-actors/03-event-payloads.cpp`, `01-actors/04-cores-and-placement.cpp`, `01-actors/07-service-actor.cpp`, `03-coroutines/04-ask-request-response.cpp`, `04-patterns/09-discovery.cpp`, `05-services/01-tcp-chat/client/main.cpp`, `05-services/02-pubsub-broker/client/main.cpp`, `05-services/03-file-pipeline/main.cpp`, `05-services/04-shutdown-and-drain/main.cpp`, `07-applications/01-taskmanager/src/main.cpp`, `07-applications/02-auction-house/src/main.cpp`, `07-applications/03-market-data-hub/src/main.cpp` |
| `qb::BroadcastId` | `06-modules/redis/06-streams.cpp` |
| `qb::CPU` | `01-actors/11-hot-path.cpp` |
| `qb::ChildDown` | `04-patterns/02-supervisor.cpp` |
| `qb::CircuitBreaker` | `04-patterns/05-resilience.cpp` |
| `qb::CoreIdSet` | `01-actors/11-hot-path.cpp` |
| `qb::Event` | `01-actors/01-hello-actor.cpp`, `01-actors/03-event-payloads.cpp`, `01-actors/12-lockfree-bridge.cpp`, `05-services/03-file-pipeline/main.cpp`, `06-modules/ws/02-chat-client.cpp` |
| `qb::EventQOS0` | `01-actors/11-hot-path.cpp` |
| `qb::FillEvent<int>` | `01-actors/03-event-payloads.cpp` |
| `qb::ICallback` | `01-actors/01-hello-actor.cpp`, `01-actors/03-event-payloads.cpp`, `01-actors/12-lockfree-bridge.cpp`, `05-services/04-shutdown-and-drain/main.cpp` |
| `qb::KillEvent` | `01-actors/03-event-payloads.cpp`, `01-actors/05-lifecycle.cpp`, `01-actors/06-doing-things-later.cpp`, `01-actors/07-service-actor.cpp`, `01-actors/09-state-machine.cpp`, `01-actors/11-hot-path.cpp`, `01-actors/12-lockfree-bridge.cpp`, `03-coroutines/02-actor-coroutines.cpp`, `03-coroutines/06-cancellation.cpp`, `04-patterns/02-supervisor.cpp`, `05-services/03-file-pipeline/main.cpp`, `06-modules/redis/04-pubsub.cpp`, `06-modules/redis/06-streams.cpp`, `06-modules/redis/10-cache-actor.cpp` |
| `qb::LoopEvent` | `01-actors/01-hello-actor.cpp`, `01-actors/03-event-payloads.cpp`, `01-actors/12-lockfree-bridge.cpp`, `05-services/04-shutdown-and-drain/main.cpp` |
| `qb::Main` | `01-actors/01-hello-actor.cpp`, `01-actors/02-messaging.cpp`, `01-actors/03-event-payloads.cpp`, `01-actors/04-cores-and-placement.cpp`, `01-actors/05-lifecycle.cpp`, `01-actors/06-doing-things-later.cpp`, `01-actors/07-service-actor.cpp`, `01-actors/08-child-actors.cpp`, `01-actors/09-state-machine.cpp`, `01-actors/10-signals-and-shutdown.cpp`, `01-actors/11-hot-path.cpp`, `01-actors/12-lockfree-bridge.cpp`, `03-coroutines/02-actor-coroutines.cpp`, `03-coroutines/03-awaiting-oninit.cpp`, `03-coroutines/04-ask-request-response.cpp`, `03-coroutines/06-cancellation.cpp`, `04-patterns/01-pubsub.cpp`, `04-patterns/03-worker-pool.cpp`, `04-patterns/04-scatter-gather.cpp`, `04-patterns/06-streaming.cpp`, `04-patterns/07-saga.cpp`, `04-patterns/08-batching-and-idempotency.cpp`, `04-patterns/09-discovery.cpp`, `05-services/01-tcp-chat/client/main.cpp`, `05-services/01-tcp-chat/server/main.cpp`, `05-services/02-pubsub-broker/client/main.cpp`, `05-services/02-pubsub-broker/server/main.cpp`, `05-services/03-file-pipeline/main.cpp`, `06-modules/http/01-hello-server.cpp`, `06-modules/http/06-validation.cpp`, `07-applications/01-taskmanager/src/main.cpp`, `07-applications/02-auction-house/src/main.cpp`, `07-applications/03-market-data-hub/src/main.cpp` |
| `qb::Pipe` | `01-actors/11-hot-path.cpp` |
| `qb::PubSub<PriceTick>` | `04-patterns/01-pubsub.cpp` |
| `qb::Request<int>` | `03-coroutines/04-ask-request-response.cpp`, `04-patterns/04-scatter-gather.cpp`, `04-patterns/07-saga.cpp`, `04-patterns/08-batching-and-idempotency.cpp` |
| `qb::RequireEvent` | `04-patterns/09-discovery.cpp` |
| `qb::SagaScope` | `04-patterns/07-saga.cpp` |
| `qb::ScopedCoroContext` | `01-actors/02-messaging.cpp`, `01-actors/04-cores-and-placement.cpp`, `01-actors/05-lifecycle.cpp`, `01-actors/06-doing-things-later.cpp`, `01-actors/08-child-actors.cpp`, `01-actors/09-state-machine.cpp`, `01-actors/10-signals-and-shutdown.cpp`, `03-coroutines/02-actor-coroutines.cpp`, `03-coroutines/03-awaiting-oninit.cpp`, `03-coroutines/04-ask-request-response.cpp`, `03-coroutines/06-cancellation.cpp`, `04-patterns/04-scatter-gather.cpp`, `04-patterns/07-saga.cpp`, `05-services/03-file-pipeline/main.cpp`, `05-services/04-shutdown-and-drain/main.cpp`, `06-modules/redis/04-pubsub.cpp`, `06-modules/redis/06-streams.cpp`, `06-modules/redis/10-cache-actor.cpp` |
| `qb::Service` | `01-actors/07-service-actor.cpp` |
| `qb::ServiceActor<ConfigTag>` | `01-actors/07-service-actor.cpp` |
| `qb::SignalEvent` | `01-actors/10-signals-and-shutdown.cpp`, `05-services/04-shutdown-and-drain/main.cpp` |
| `qb::StreamRequest` | `04-patterns/06-streaming.cpp` |
| `qb::SupervisedActor` | `04-patterns/02-supervisor.cpp` |
| `qb::Supervisor` | `04-patterns/02-supervisor.cpp` |
| `qb::WithData<int>` | `01-actors/03-event-payloads.cpp` |
| `qb::WorkerPool` | `04-patterns/03-worker-pool.cpp` |
| `qb::answer` | `03-coroutines/04-ask-request-response.cpp`, `04-patterns/04-scatter-gather.cpp`, `04-patterns/05-resilience.cpp`, `04-patterns/07-saga.cpp` |
| `qb::answer_idempotent` | `04-patterns/08-batching-and-idempotency.cpp` |
| `qb::ask` | `03-coroutines/04-ask-request-response.cpp`, `04-patterns/04-scatter-gather.cpp`, `04-patterns/07-saga.cpp`, `04-patterns/08-batching-and-idempotency.cpp` |
| `qb::ask_all` | `04-patterns/04-scatter-gather.cpp` |
| `qb::ask_any` | `04-patterns/04-scatter-gather.cpp` |
| `qb::ask_by` | `04-patterns/04-scatter-gather.cpp` |
| `qb::ask_guarded` | `04-patterns/05-resilience.cpp` |
| `qb::ask_quorum` | `04-patterns/04-scatter-gather.cpp` |
| `qb::ask_retry` | `04-patterns/05-resilience.cpp`, `04-patterns/08-batching-and-idempotency.cpp` |
| `qb::ask_stream` | `04-patterns/06-streaming.cpp` |
| `qb::batcher<int>` | `04-patterns/08-batching-and-idempotency.cpp` |
| `qb::bulkhead` | `04-patterns/05-resilience.cpp` |
| `qb::circuit_open_error` | `04-patterns/05-resilience.cpp` |
| `qb::custom_message` | `02-io/05-custom-protocol.cpp` |
| `qb::deadline` | `04-patterns/04-scatter-gather.cpp` |
| `qb::deadline_in` | `04-patterns/04-scatter-gather.cpp` |
| `qb::dedup_map<std::uint64_t, int>` | `04-patterns/08-batching-and-idempotency.cpp` |
| `qb::duration` | `02-io/01-event-loop.cpp`, `02-io/08-timeouts-and-watchers.cpp`, `05-services/03-file-pipeline/main.cpp` |
| `qb::end_stream` | `04-patterns/06-streaming.cpp` |
| `qb::generate_random_uuid` | `02-io/10-crypto-and-compression.cpp` |
| `qb::json` | `02-io/06-framing-toolbox.cpp`, `06-modules/http/02-routing.cpp`, `06-modules/http/05-rest-api-json.cpp`, `06-modules/redis/14-acl-and-topology.cpp` |
| `qb::mono_now` | `02-io/08-timeouts-and-watchers.cpp` |
| `qb::no_default_events` | `01-actors/11-hot-path.cpp` |
| `qb::ping` | `04-patterns/09-discovery.cpp` |
| `qb::rate_limiter` | `04-patterns/05-resilience.cpp` |
| `qb::remaining` | `04-patterns/04-scatter-gather.cpp` |
| `qb::require<Worker>` | `04-patterns/09-discovery.cpp` |
| `qb::restart_strategy` | `04-patterns/02-supervisor.cpp` |
| `qb::retry_policy` | `04-patterns/05-resilience.cpp`, `04-patterns/08-batching-and-idempotency.cpp` |
| `qb::ring_buffer` | `02-io/11-logging-and-metrics.cpp` |
| `qb::run_saga` | `04-patterns/07-saga.cpp` |
| `qb::stream<Tail>` | `04-patterns/06-streaming.cpp` |
| `qb::stream_overflow_error` | `04-patterns/06-streaming.cpp` |
| `qb::string<32>` | `01-actors/03-event-payloads.cpp` |
| `qb::string<8>` | `03-coroutines/04-ask-request-response.cpp`, `04-patterns/01-pubsub.cpp` |
| `qb::to_number<T>` | `02-io/05-custom-protocol.cpp` |
| `qb::tsc_ticks` | `01-actors/11-hot-path.cpp`, `02-io/11-logging-and-metrics.cpp` |
| `qb::uuid` | `02-io/10-crypto-and-compression.cpp` |

### `qb::Actor`

| capability | demonstrated by |
| --- | --- |
| `qb::Actor::EventBuilder` | `01-actors/11-hot-path.cpp` |

### `qb::CPU`

| capability | demonstrated by |
| --- | --- |
| `qb::CPU::Architecture` | `02-io/11-logging-and-metrics.cpp` |
| `qb::CPU::ClockSpeed` | `02-io/11-logging-and-metrics.cpp` |
| `qb::CPU::HyperThreading` | `02-io/11-logging-and-metrics.cpp` |
| `qb::CPU::LogicalCores` | `02-io/11-logging-and-metrics.cpp` |
| `qb::CPU::PhysicalCores` | `02-io/11-logging-and-metrics.cpp` |
| `qb::CPU::ThreadPinningSupported` | `02-io/11-logging-and-metrics.cpp` |

### `qb::Main`

| capability | demonstrated by |
| --- | --- |
| `qb::Main::ignoreSignal` | `01-actors/10-signals-and-shutdown.cpp`, `05-services/04-shutdown-and-drain/main.cpp` |
| `qb::Main::registerSignal` | `01-actors/10-signals-and-shutdown.cpp`, `07-applications/01-taskmanager/src/main.cpp`, `07-applications/02-auction-house/src/main.cpp` |
| `qb::Main::stop` | `01-actors/10-signals-and-shutdown.cpp`, `05-services/04-shutdown-and-drain/main.cpp` |
| `qb::Main::unregisterSignal` | `01-actors/10-signals-and-shutdown.cpp` |

### `qb::VirtualCore`

| capability | demonstrated by |
| --- | --- |
| `qb::VirtualCore::activation_deadline_ns` | `03-coroutines/03-awaiting-oninit.cpp` |

### `qb::allocator`

| capability | demonstrated by |
| --- | --- |
| `qb::allocator::pipe<char>::put<T>` | `02-io/05-custom-protocol.cpp` |

### `qb::compression::builtin`

| capability | demonstrated by |
| --- | --- |
| `qb::compression::builtin::supported` | `02-io/10-crypto-and-compression.cpp` |

### `qb::compression::builtin::algorithm`

| capability | demonstrated by |
| --- | --- |
| `qb::compression::builtin::algorithm::supported` | `02-io/10-crypto-and-compression.cpp` |

### `qb::crypto`

| capability | demonstrated by |
| --- | --- |
| `qb::crypto::DigestAlgorithm` | `02-io/10-crypto-and-compression.cpp` |
| `qb::crypto::SymmetricAlgorithm` | `02-io/10-crypto-and-compression.cpp` |
| `qb::crypto::constant_time_compare` | `02-io/10-crypto-and-compression.cpp` |
| `qb::crypto::decrypt` | `02-io/10-crypto-and-compression.cpp` |
| `qb::crypto::encrypt` | `02-io/10-crypto-and-compression.cpp` |
| `qb::crypto::generate_iv` | `02-io/10-crypto-and-compression.cpp` |
| `qb::crypto::generate_key` | `02-io/10-crypto-and-compression.cpp` |
| `qb::crypto::hash` | `02-io/10-crypto-and-compression.cpp` |
| `qb::crypto::hash_password` | `02-io/10-crypto-and-compression.cpp` |
| `qb::crypto::hmac` | `02-io/10-crypto-and-compression.cpp` |
| `qb::crypto::secure_random_fill` | `02-io/10-crypto-and-compression.cpp` |
| `qb::crypto::sha256` | `02-io/10-crypto-and-compression.cpp` |
| `qb::crypto::to_hex_string` | `02-io/10-crypto-and-compression.cpp` |
| `qb::crypto::verify_password` | `02-io/10-crypto-and-compression.cpp` |

### `qb::crypto::base64`

| capability | demonstrated by |
| --- | --- |
| `qb::crypto::base64::decode` | `02-io/10-crypto-and-compression.cpp` |
| `qb::crypto::base64::encode` | `02-io/10-crypto-and-compression.cpp` |

### `qb::deflate`

| capability | demonstrated by |
| --- | --- |
| `qb::deflate::compress` | `02-io/10-crypto-and-compression.cpp` |

### `qb::duration`

| capability | demonstrated by |
| --- | --- |
| `qb::duration::zero` | `07-applications/01-taskmanager/src/main.cpp`, `07-applications/02-auction-house/src/main.cpp`, `07-applications/03-market-data-hub/src/main.cpp` |

### `qb::gzip`

| capability | demonstrated by |
| --- | --- |
| `qb::gzip::compress` | `02-io/10-crypto-and-compression.cpp` |
| `qb::gzip::uncompress` | `02-io/10-crypto-and-compression.cpp` |

### `qb::http`

| capability | demonstrated by |
| --- | --- |
| `qb::http::AuthMiddleware<S>` | `06-modules/http/07-auth-jwt.cpp` |
| `qb::http::Chunk` | `06-modules/http/14-streaming-and-cookies.cpp` |
| `qb::http::CompressionMiddleware<S>` | `06-modules/http/05-rest-api-json.cpp`, `06-modules/http/08-static-files.cpp` |
| `qb::http::Context<S>` | `06-modules/http/02-routing.cpp`, `06-modules/http/03-controllers.cpp`, `06-modules/http/04-middleware.cpp`, `06-modules/http/09-coroutine-handlers.cpp` |
| `qb::http::Controller<S>` | `06-modules/http/03-controllers.cpp` |
| `qb::http::Cookie` | `06-modules/http/14-streaming-and-cookies.cpp` |
| `qb::http::CookieJar` | `06-modules/http/14-streaming-and-cookies.cpp` |
| `qb::http::CorsMiddleware<S>` | `06-modules/http/05-rest-api-json.cpp`, `06-modules/http/08-static-files.cpp`, `06-modules/http/11-https.cpp`, `06-modules/http/12-http2.cpp`, `06-modules/http/13-http3.cpp`, `06-modules/ws/01-chat-server.cpp` |
| `qb::http::DefaultSession` | `06-modules/http/02-routing.cpp`, `06-modules/http/03-controllers.cpp`, `06-modules/http/04-middleware.cpp`, `06-modules/http/06-validation.cpp` |
| `qb::http::Form` | `06-modules/http/14-streaming-and-cookies.cpp` |
| `qb::http::GET` | `06-modules/http/09-coroutine-handlers.cpp`, `06-modules/http/10-client.cpp`, `06-modules/http/14-streaming-and-cookies.cpp`, `06-modules/http/15-http2-and-http3-clients.cpp` |
| `qb::http::LoggingMiddleware<S>` | `06-modules/http/05-rest-api-json.cpp`, `06-modules/http/08-static-files.cpp`, `06-modules/http/11-https.cpp`, `06-modules/http/12-http2.cpp`, `06-modules/http/13-http3.cpp`, `06-modules/ws/01-chat-server.cpp` |
| `qb::http::MiddlewareTask<S>` | `06-modules/http/05-rest-api-json.cpp` |
| `qb::http::Multipart` | `06-modules/http/14-streaming-and-cookies.cpp` |
| `qb::http::POST` | `06-modules/http/14-streaming-and-cookies.cpp` |
| `qb::http::RateLimitMiddleware<S>` | `06-modules/http/05-rest-api-json.cpp` |
| `qb::http::Request` | `06-modules/http/09-coroutine-handlers.cpp`, `06-modules/http/10-client.cpp`, `06-modules/http/15-http2-and-http3-clients.cpp`, `06-modules/ws/01-chat-server.cpp` |
| `qb::http::Response` | `06-modules/ws/01-chat-server.cpp` |
| `qb::http::SameSite` | `06-modules/http/14-streaming-and-cookies.cpp` |
| `qb::http::SecurityHeadersMiddleware<S>` | `06-modules/http/05-rest-api-json.cpp`, `06-modules/http/08-static-files.cpp`, `06-modules/http/11-https.cpp` |
| `qb::http::Server<>` | `06-modules/http/01-hello-server.cpp`, `06-modules/http/02-routing.cpp`, `06-modules/http/03-controllers.cpp`, `06-modules/http/06-validation.cpp`, `06-modules/http/09-coroutine-handlers.cpp`, `06-modules/http/11-https.cpp` |
| `qb::http::StaticFilesMiddleware<S>` | `06-modules/ws/01-chat-server.cpp` |
| `qb::http::StaticFilesOptions` | `06-modules/http/08-static-files.cpp`, `06-modules/http/12-http2.cpp` |
| `qb::http::WebSocketRequest` | `06-modules/ws/02-chat-client.cpp` |
| `qb::http::dual_stack_server<>` | `06-modules/http/13-http3.cpp` |
| `qb::http::make_dual_stack_server<>` | `06-modules/http/13-http3.cpp` |
| `qb::http::method` | `06-modules/http/10-client.cpp`, `06-modules/http/15-http2-and-http3-clients.cpp` |
| `qb::http::parse_set_cookie` | `06-modules/http/14-streaming-and-cookies.cpp` |
| `qb::http::static_files_middleware<S>` | `06-modules/http/08-static-files.cpp`, `06-modules/http/12-http2.cpp`, `06-modules/http/13-http3.cpp` |
| `qb::http::status` | `06-modules/http/10-client.cpp`, `06-modules/http/15-http2-and-http3-clients.cpp` |
| `qb::http::use<AssetServer>::server<AssetSession>` | `06-modules/http/14-streaming-and-cookies.cpp` |
| `qb::http::use<EchoServer>::server<EchoSession>` | `06-modules/http/10-client.cpp` |
| `qb::http::use<PlainServer>::server<PlainSession>` | `06-modules/http/15-http2-and-http3-clients.cpp` |
| `qb::http::use<T>` | `06-modules/ws/01-chat-server.cpp` |
| `qb::http::validation_middleware` | `06-modules/http/06-validation.cpp` |

### `qb::http1`

| capability | demonstrated by |
| --- | --- |
| `qb::http1::Client` | `06-modules/http/10-client.cpp`, `06-modules/http/15-http2-and-http3-clients.cpp` |
| `qb::http1::make_client` | `06-modules/http/10-client.cpp`, `06-modules/http/15-http2-and-http3-clients.cpp` |

### `qb::http2`

| capability | demonstrated by |
| --- | --- |
| `qb::http2::Client` | `06-modules/http/15-http2-and-http3-clients.cpp` |
| `qb::http2::DefaultSession` | `06-modules/http/13-http3.cpp` |
| `qb::http2::make_client` | `06-modules/http/15-http2-and-http3-clients.cpp` |
| `qb::http2::make_server` | `06-modules/http/15-http2-and-http3-clients.cpp` |
| `qb::http2::use<T>` | `06-modules/http/12-http2.cpp` |

### `qb::http3`

| capability | demonstrated by |
| --- | --- |
| `qb::http3::DefaultSession` | `06-modules/http/13-http3.cpp` |

### `qb::http::AsyncTaskResult`

| capability | demonstrated by |
| --- | --- |
| `qb::http::AsyncTaskResult::COMPLETE` | `06-modules/http/01-hello-server.cpp` |

### `qb::http::Method`

| capability | demonstrated by |
| --- | --- |
| `qb::http::Method::OPTIONS` | `06-modules/http/04-middleware.cpp` |

### `qb::http::Status`

| capability | demonstrated by |
| --- | --- |
| `qb::http::Status::CREATED` | `06-modules/http/02-routing.cpp` |
| `qb::http::Status::FORBIDDEN` | `06-modules/http/04-middleware.cpp`, `06-modules/http/07-auth-jwt.cpp` |
| `qb::http::Status::NOT_FOUND` | `06-modules/http/02-routing.cpp` |
| `qb::http::Status::NO_CONTENT` | `06-modules/http/02-routing.cpp` |
| `qb::http::Status::OK` | `06-modules/http/01-hello-server.cpp` |
| `qb::http::Status::TOO_MANY_REQUESTS` | `06-modules/http/04-middleware.cpp` |
| `qb::http::Status::UNAUTHORIZED` | `06-modules/http/04-middleware.cpp`, `06-modules/http/07-auth-jwt.cpp` |

### `qb::http::async`

| capability | demonstrated by |
| --- | --- |
| `qb::http::async::Reply` | `06-modules/http/09-coroutine-handlers.cpp` |

### `qb::http::auth`

| capability | demonstrated by |
| --- | --- |
| `qb::http::auth::Manager` | `06-modules/http/07-auth-jwt.cpp` |
| `qb::http::auth::Options` | `06-modules/http/07-auth-jwt.cpp` |
| `qb::http::auth::User` | `06-modules/http/07-auth-jwt.cpp` |

### `qb::http::date`

| capability | demonstrated by |
| --- | --- |
| `qb::http::date::format_http_date` | `06-modules/http/14-streaming-and-cookies.cpp` |
| `qb::http::date::parse_http_date` | `06-modules/http/14-streaming-and-cookies.cpp` |

### `qb::http::ssl`

| capability | demonstrated by |
| --- | --- |
| `qb::http::ssl::DefaultSecureSession` | `06-modules/http/11-https.cpp` |
| `qb::http::ssl::Server<>` | `06-modules/http/11-https.cpp` |

### `qb::http::status`

| capability | demonstrated by |
| --- | --- |
| `qb::http::status::NOT_MODIFIED` | `06-modules/http/14-streaming-and-cookies.cpp` |

### `qb::http::validation`

| capability | demonstrated by |
| --- | --- |
| `qb::http::validation::DataType` | `06-modules/http/06-validation.cpp` |
| `qb::http::validation::EnumRule` | `06-modules/http/06-validation.cpp` |
| `qb::http::validation::Error` | `06-modules/http/06-validation.cpp` |
| `qb::http::validation::MinLengthRule` | `06-modules/http/06-validation.cpp` |
| `qb::http::validation::MinimumRule` | `06-modules/http/06-validation.cpp` |
| `qb::http::validation::ParameterRuleSet` | `06-modules/http/06-validation.cpp` |
| `qb::http::validation::ParameterValidator` | `06-modules/http/06-validation.cpp` |
| `qb::http::validation::PatternRule` | `06-modules/http/06-validation.cpp` |
| `qb::http::validation::PredefinedSanitizers` | `06-modules/http/06-validation.cpp` |
| `qb::http::validation::RequestValidator` | `06-modules/http/06-validation.cpp` |
| `qb::http::validation::Result` | `06-modules/http/06-validation.cpp` |
| `qb::http::validation::Sanitizer` | `06-modules/http/06-validation.cpp` |
| `qb::http::validation::SchemaValidator` | `06-modules/http/06-validation.cpp` |

### `qb::http::ws`

| capability | demonstrated by |
| --- | --- |
| `qb::http::ws::CloseResult` | `06-modules/ws/04-coro-client.cpp` |
| `qb::http::ws::CloseStatus` | `06-modules/ws/03-coro-session.cpp`, `06-modules/ws/04-coro-client.cpp` |
| `qb::http::ws::ConnectResult` | `06-modules/ws/04-coro-client.cpp` |
| `qb::http::ws::IncomingFrame` | `06-modules/ws/03-coro-session.cpp`, `06-modules/ws/04-coro-client.cpp` |
| `qb::http::ws::MessageBinary` | `06-modules/ws/03-coro-session.cpp` |
| `qb::http::ws::MessageClose` | `06-modules/ws/02-chat-client.cpp` |
| `qb::http::ws::MessagePing` | `06-modules/ws/04-coro-client.cpp` |
| `qb::http::ws::MessageText` | `06-modules/ws/01-chat-server.cpp`, `06-modules/ws/02-chat-client.cpp`, `06-modules/ws/03-coro-session.cpp`, `06-modules/ws/04-coro-client.cpp` |
| `qb::http::ws::coro_client` | `06-modules/ws/03-coro-session.cpp`, `06-modules/ws/04-coro-client.cpp` |
| `qb::http::ws::coro_client_secure` | `06-modules/ws/04-coro-client.cpp` |
| `qb::http::ws::coro_session<EchoSession, EchoServer>` | `06-modules/ws/03-coro-session.cpp`, `06-modules/ws/04-coro-client.cpp` |
| `qb::http::ws::generateKey` | `06-modules/ws/02-chat-client.cpp` |
| `qb::http::ws::protocol<S>` | `06-modules/ws/01-chat-server.cpp`, `06-modules/ws/02-chat-client.cpp` |

### `qb::http::ws::CloseStatus`

| capability | demonstrated by |
| --- | --- |
| `qb::http::ws::CloseStatus::Normal` | `06-modules/ws/02-chat-client.cpp` |

### `qb::io`

| capability | demonstrated by |
| --- | --- |
| `qb::io::cerr` | `02-io/01-event-loop.cpp`, `02-io/02-files.cpp`, `05-services/01-tcp-chat/client/main.cpp`, `05-services/01-tcp-chat/server/main.cpp`, `05-services/02-pubsub-broker/client/main.cpp`, `05-services/02-pubsub-broker/server/main.cpp`, `05-services/03-file-pipeline/main.cpp`, `07-applications/01-taskmanager/src/main.cpp`, `07-applications/02-auction-house/src/main.cpp`, `07-applications/03-market-data-hub/src/main.cpp` |
| `qb::io::cout` | `01-actors/01-hello-actor.cpp`, `02-io/01-event-loop.cpp`, `02-io/02-files.cpp`, `02-io/08-timeouts-and-watchers.cpp`, `02-io/09-graceful-drain.cpp`, `02-io/10-crypto-and-compression.cpp`, `02-io/11-logging-and-metrics.cpp`, `02-io/12-quic.cpp`, `05-services/01-tcp-chat/client/main.cpp`, `05-services/01-tcp-chat/server/main.cpp`, `05-services/02-pubsub-broker/client/main.cpp`, `05-services/02-pubsub-broker/server/main.cpp`, `05-services/03-file-pipeline/main.cpp`, `05-services/04-shutdown-and-drain/main.cpp`, `07-applications/01-taskmanager/src/main.cpp`, `07-applications/02-auction-house/src/main.cpp`, `07-applications/03-market-data-hub/src/main.cpp` |
| `qb::io::endpoint` | `02-io/04-udp.cpp` |
| `qb::io::uri` | `02-io/07-tls.cpp`, `05-services/01-tcp-chat/client/main.cpp`, `05-services/01-tcp-chat/server/main.cpp`, `05-services/02-pubsub-broker/client/main.cpp`, `05-services/02-pubsub-broker/server/main.cpp`, `06-modules/http/12-http2.cpp`, `07-applications/01-taskmanager/src/main.cpp`, `07-applications/02-auction-house/src/main.cpp` |
| `qb::io::use<EchoServer>::tcp::server<EchoSession>` | `06-modules/ws/03-coro-session.cpp`, `06-modules/ws/04-coro-client.cpp` |
| `qb::io::use<T>` | `06-modules/ws/01-chat-server.cpp` |
| `qb::io::use<T>::file` | `02-io/08-timeouts-and-watchers.cpp` |
| `qb::io::use<T>::quic::session` | `02-io/12-quic.cpp` |
| `qb::io::use<T>::tcp::acceptor` | `05-services/04-shutdown-and-drain/main.cpp` |
| `qb::io::use<T>::tcp::client<S>` | `02-io/03-tcp.cpp`, `02-io/05-custom-protocol.cpp`, `02-io/06-framing-toolbox.cpp`, `02-io/09-graceful-drain.cpp` |
| `qb::io::use<T>::tcp::io_handler<S>` | `05-services/04-shutdown-and-drain/main.cpp` |
| `qb::io::use<T>::tcp::server<S>` | `02-io/03-tcp.cpp`, `02-io/05-custom-protocol.cpp`, `02-io/06-framing-toolbox.cpp`, `02-io/09-graceful-drain.cpp` |
| `qb::io::use<T>::tcp::ssl::client<S>` | `02-io/07-tls.cpp` |
| `qb::io::use<T>::tcp::ssl::server<S>` | `02-io/07-tls.cpp` |
| `qb::io::use<T>::udp::client` | `02-io/04-udp.cpp` |
| `qb::io::use<T>::udp::server` | `02-io/04-udp.cpp` |

### `qb::io::SocketStatus`

| capability | demonstrated by |
| --- | --- |
| `qb::io::SocketStatus::Done` | `02-io/03-tcp.cpp`, `06-modules/ws/02-chat-client.cpp` |

### `qb::io::async`

| capability | demonstrated by |
| --- | --- |
| `qb::io::async::AProtocol<T>` | `02-io/05-custom-protocol.cpp` |
| `qb::io::async::aggressive_retry_policy` | `03-coroutines/13-retry-and-single-flight.cpp` |
| `qb::io::async::async_awaiter<int>` | `03-coroutines/14-foreign-awaitables.cpp` |
| `qb::io::async::async_event` | `03-coroutines/12-sync-primitives.cpp` |
| `qb::io::async::async_generator<int>` | `03-coroutines/10-generators.cpp` |
| `qb::io::async::async_latch` | `03-coroutines/12-sync-primitives.cpp` |
| `qb::io::async::async_mutex` | `03-coroutines/12-sync-primitives.cpp` |
| `qb::io::async::async_rw_lock` | `03-coroutines/12-sync-primitives.cpp` |
| `qb::io::async::async_stream<int>` | `03-coroutines/11-async-streams.cpp` |
| `qb::io::async::backoff_strategy` | `03-coroutines/13-retry-and-single-flight.cpp` |
| `qb::io::async::barrier` | `03-coroutines/12-sync-primitives.cpp` |
| `qb::io::async::callback` | `02-io/06-framing-toolbox.cpp`, `02-io/08-timeouts-and-watchers.cpp`, `02-io/09-graceful-drain.cpp`, `03-coroutines/14-foreign-awaitables.cpp`, `06-modules/redis/04-pubsub.cpp`, `06-modules/redis/10-cache-actor.cpp` |
| `qb::io::async::cancellable_sleep` | `03-coroutines/06-cancellation.cpp` |
| `qb::io::async::cancellation_token` | `03-coroutines/06-cancellation.cpp`, `03-coroutines/08-bounded-fan-out.cpp` |
| `qb::io::async::cancelled_error` | `03-coroutines/06-cancellation.cpp` |
| `qb::io::async::cancelling_scope` | `03-coroutines/07-structured-concurrency.cpp` |
| `qb::io::async::capture_result` | `03-coroutines/08-bounded-fan-out.cpp` |
| `qb::io::async::channel<int>` | `03-coroutines/09-channels.cpp`, `03-coroutines/11-async-streams.cpp` |
| `qb::io::async::channel<std::string>` | `03-coroutines/09-channels.cpp` |
| `qb::io::async::channel_closed` | `03-coroutines/09-channels.cpp` |
| `qb::io::async::collect` | `03-coroutines/09-channels.cpp` |
| `qb::io::async::collect_to_vector` | `03-coroutines/10-generators.cpp` |
| `qb::io::async::concat` | `03-coroutines/10-generators.cpp` |
| `qb::io::async::coro_scheduler` | `06-modules/http/15-http2-and-http3-clients.cpp`, `06-modules/pgsql/01-connect-and-query.cpp`, `06-modules/pgsql/03-transactions.cpp`, `06-modules/pgsql/06-typed-rows.cpp`, `06-modules/pgsql/07-listen-notify.cpp`, `06-modules/pgsql/08-tls-and-limits.cpp`, `06-modules/pgsql/10-streaming-results.cpp`, `06-modules/redis/01-connect.cpp`, `06-modules/redis/02-data-types.cpp`, `06-modules/redis/03-coroutines-and-pipelining.cpp`, `06-modules/redis/05-transactions.cpp`, `06-modules/redis/07-scripting.cpp`, `06-modules/redis/08-sorted-sets-and-ttl.cpp`, `06-modules/redis/09-reliability.cpp`, `06-modules/redis/12-cardinality-and-bitmaps.cpp`, `06-modules/redis/13-geospatial.cpp`, `06-modules/redis/14-acl-and-topology.cpp`, `06-modules/ws/03-coro-session.cpp`, `06-modules/ws/04-coro-client.cpp` |
| `qb::io::async::coro_with_timeout` | `03-coroutines/05-combinators.cpp` |
| `qb::io::async::coroutine_scope` | `03-coroutines/07-structured-concurrency.cpp`, `03-coroutines/08-bounded-fan-out.cpp`, `03-coroutines/09-channels.cpp`, `03-coroutines/12-sync-primitives.cpp`, `03-coroutines/13-retry-and-single-flight.cpp`, `03-coroutines/14-foreign-awaitables.cpp` |
| `qb::io::async::defer` | `02-io/09-graceful-drain.cpp` |
| `qb::io::async::detaching_scope` | `03-coroutines/07-structured-concurrency.cpp` |
| `qb::io::async::directory_watcher` | `02-io/08-timeouts-and-watchers.cpp` |
| `qb::io::async::filter` | `03-coroutines/09-channels.cpp` |
| `qb::io::async::filter_to_vector` | `03-coroutines/10-generators.cpp` |
| `qb::io::async::for_each` | `03-coroutines/10-generators.cpp` |
| `qb::io::async::from_range` | `03-coroutines/10-generators.cpp` |
| `qb::io::async::generator<int>` | `03-coroutines/10-generators.cpp` |
| `qb::io::async::idempotent_policy` | `03-coroutines/13-retry-and-single-flight.cpp` |
| `qb::io::async::init` | `02-io/01-event-loop.cpp`, `02-io/03-tcp.cpp`, `02-io/04-udp.cpp`, `02-io/06-framing-toolbox.cpp`, `02-io/07-tls.cpp`, `02-io/08-timeouts-and-watchers.cpp`, `02-io/09-graceful-drain.cpp`, `02-io/11-logging-and-metrics.cpp`, `02-io/12-quic.cpp`, `06-modules/http/10-client.cpp`, `06-modules/http/14-streaming-and-cookies.cpp`, `06-modules/http/15-http2-and-http3-clients.cpp`, `06-modules/pgsql/01-connect-and-query.cpp`, `06-modules/pgsql/03-transactions.cpp`, `06-modules/pgsql/06-typed-rows.cpp`, `06-modules/pgsql/07-listen-notify.cpp`, `06-modules/pgsql/08-tls-and-limits.cpp`, `06-modules/pgsql/09-callbacks-and-await.cpp`, `06-modules/pgsql/10-streaming-results.cpp`, `06-modules/redis/01-connect.cpp`, `06-modules/redis/02-data-types.cpp`, `06-modules/redis/03-coroutines-and-pipelining.cpp`, `06-modules/redis/05-transactions.cpp`, `06-modules/redis/07-scripting.cpp`, `06-modules/redis/08-sorted-sets-and-ttl.cpp`, `06-modules/redis/09-reliability.cpp`, `06-modules/redis/11-callbacks-and-consumers.cpp`, `06-modules/redis/12-cardinality-and-bitmaps.cpp`, `06-modules/redis/13-geospatial.cpp`, `06-modules/redis/14-acl-and-topology.cpp`, `06-modules/ws/03-coro-session.cpp`, `06-modules/ws/04-coro-client.cpp`, `07-applications/02-auction-house/src/main.cpp` |
| `qb::io::async::interval` | `03-coroutines/11-async-streams.cpp` |
| `qb::io::async::iota` | `03-coroutines/10-generators.cpp` |
| `qb::io::async::joining_scope` | `03-coroutines/07-structured-concurrency.cpp` |
| `qb::io::async::make_channel` | `03-coroutines/09-channels.cpp` |
| `qb::io::async::make_pipeline` | `03-coroutines/09-channels.cpp` |
| `qb::io::async::make_retryable` | `03-coroutines/13-retry-and-single-flight.cpp` |
| `qb::io::async::make_shared_task` | `03-coroutines/13-retry-and-single-flight.cpp` |
| `qb::io::async::map_to_vector` | `03-coroutines/10-generators.cpp` |
| `qb::io::async::merge_streams` | `03-coroutines/11-async-streams.cpp` |
| `qb::io::async::parallel` | `03-coroutines/08-bounded-fan-out.cpp` |
| `qb::io::async::parallel_map` | `03-coroutines/08-bounded-fan-out.cpp` |
| `qb::io::async::race` | `03-coroutines/05-combinators.cpp` |
| `qb::io::async::range` | `03-coroutines/10-generators.cpp` |
| `qb::io::async::range_stream` | `03-coroutines/11-async-streams.cpp` |
| `qb::io::async::reduce` | `03-coroutines/10-generators.cpp` |
| `qb::io::async::repeat_n` | `03-coroutines/10-generators.cpp` |
| `qb::io::async::repeat_value` | `03-coroutines/11-async-streams.cpp` |
| `qb::io::async::repeat_while` | `03-coroutines/08-bounded-fan-out.cpp` |
| `qb::io::async::retry` | `03-coroutines/13-retry-and-single-flight.cpp` |
| `qb::io::async::retry_exhausted` | `03-coroutines/13-retry-and-single-flight.cpp` |
| `qb::io::async::retry_policy` | `03-coroutines/13-retry-and-single-flight.cpp` |
| `qb::io::async::run` | `02-io/01-event-loop.cpp`, `02-io/03-tcp.cpp`, `02-io/04-udp.cpp` |
| `qb::io::async::run_for` | `02-io/12-quic.cpp` |
| `qb::io::async::run_sync` | `03-coroutines/01-first-coroutine.cpp`, `03-coroutines/05-combinators.cpp`, `03-coroutines/07-structured-concurrency.cpp`, `03-coroutines/08-bounded-fan-out.cpp`, `03-coroutines/09-channels.cpp`, `03-coroutines/10-generators.cpp`, `03-coroutines/11-async-streams.cpp`, `03-coroutines/12-sync-primitives.cpp`, `03-coroutines/13-retry-and-single-flight.cpp`, `03-coroutines/14-foreign-awaitables.cpp`, `06-modules/pgsql/09-callbacks-and-await.cpp`, `07-applications/02-auction-house/src/main.cpp` |
| `qb::io::async::run_until` | `02-io/06-framing-toolbox.cpp`, `02-io/07-tls.cpp`, `02-io/08-timeouts-and-watchers.cpp`, `02-io/09-graceful-drain.cpp`, `02-io/11-logging-and-metrics.cpp`, `06-modules/http/10-client.cpp`, `06-modules/http/14-streaming-and-cookies.cpp`, `06-modules/http/15-http2-and-http3-clients.cpp`, `06-modules/pgsql/01-connect-and-query.cpp`, `06-modules/pgsql/02-parameters.cpp`, `06-modules/pgsql/03-transactions.cpp`, `06-modules/pgsql/04-types.cpp`, `06-modules/pgsql/06-typed-rows.cpp`, `06-modules/pgsql/07-listen-notify.cpp`, `06-modules/pgsql/08-tls-and-limits.cpp`, `06-modules/pgsql/10-streaming-results.cpp`, `06-modules/redis/01-connect.cpp`, `06-modules/redis/02-data-types.cpp`, `06-modules/redis/03-coroutines-and-pipelining.cpp`, `06-modules/redis/05-transactions.cpp`, `06-modules/redis/07-scripting.cpp`, `06-modules/redis/08-sorted-sets-and-ttl.cpp`, `06-modules/redis/09-reliability.cpp`, `06-modules/redis/11-callbacks-and-consumers.cpp`, `06-modules/redis/12-cardinality-and-bitmaps.cpp`, `06-modules/redis/13-geospatial.cpp`, `06-modules/redis/14-acl-and-topology.cpp`, `06-modules/ws/03-coro-session.cpp`, `06-modules/ws/04-coro-client.cpp` |
| `qb::io::async::scoped_callback` | `02-io/08-timeouts-and-watchers.cpp` |
| `qb::io::async::select` | `03-coroutines/09-channels.cpp` |
| `qb::io::async::select_result` | `03-coroutines/09-channels.cpp` |
| `qb::io::async::semaphore` | `03-coroutines/12-sync-primitives.cpp` |
| `qb::io::async::shared_task<int>` | `03-coroutines/13-retry-and-single-flight.cpp` |
| `qb::io::async::skip` | `03-coroutines/10-generators.cpp` |
| `qb::io::async::sleep` | `03-coroutines/01-first-coroutine.cpp`, `03-coroutines/02-actor-coroutines.cpp`, `03-coroutines/05-combinators.cpp`, `03-coroutines/06-cancellation.cpp`, `03-coroutines/07-structured-concurrency.cpp`, `03-coroutines/08-bounded-fan-out.cpp`, `03-coroutines/09-channels.cpp`, `03-coroutines/10-generators.cpp`, `03-coroutines/12-sync-primitives.cpp`, `03-coroutines/13-retry-and-single-flight.cpp`, `03-coroutines/14-foreign-awaitables.cpp`, `06-modules/http/09-coroutine-handlers.cpp`, `06-modules/http/15-http2-and-http3-clients.cpp`, `06-modules/pgsql/07-listen-notify.cpp`, `06-modules/redis/09-reliability.cpp`, `06-modules/ws/04-coro-client.cpp` |
| `qb::io::async::take` | `03-coroutines/10-generators.cpp` |
| `qb::io::async::task<bool>` | `01-actors/01-hello-actor.cpp`, `01-actors/02-messaging.cpp`, `03-coroutines/02-actor-coroutines.cpp`, `03-coroutines/03-awaiting-oninit.cpp`, `03-coroutines/06-cancellation.cpp`, `06-modules/http/01-hello-server.cpp`, `06-modules/redis/06-streams.cpp`, `06-modules/redis/09-reliability.cpp` |
| `qb::io::async::task<int>` | `03-coroutines/01-first-coroutine.cpp`, `03-coroutines/05-combinators.cpp`, `03-coroutines/06-cancellation.cpp`, `03-coroutines/07-structured-concurrency.cpp`, `03-coroutines/08-bounded-fan-out.cpp`, `03-coroutines/13-retry-and-single-flight.cpp`, `03-coroutines/14-foreign-awaitables.cpp` |
| `qb::io::async::task<std::string>` | `03-coroutines/08-bounded-fan-out.cpp` |
| `qb::io::async::task<std::uint64_t>` | `06-modules/pgsql/10-streaming-results.cpp` |
| `qb::io::async::task<void>` | `01-actors/06-doing-things-later.cpp`, `03-coroutines/01-first-coroutine.cpp`, `03-coroutines/02-actor-coroutines.cpp`, `03-coroutines/05-combinators.cpp`, `03-coroutines/06-cancellation.cpp`, `03-coroutines/07-structured-concurrency.cpp`, `03-coroutines/08-bounded-fan-out.cpp`, `03-coroutines/09-channels.cpp`, `03-coroutines/10-generators.cpp`, `03-coroutines/11-async-streams.cpp`, `03-coroutines/12-sync-primitives.cpp`, `03-coroutines/14-foreign-awaitables.cpp`, `06-modules/http/09-coroutine-handlers.cpp`, `06-modules/http/10-client.cpp`, `06-modules/http/14-streaming-and-cookies.cpp`, `06-modules/http/15-http2-and-http3-clients.cpp`, `06-modules/pgsql/01-connect-and-query.cpp`, `06-modules/pgsql/02-parameters.cpp`, `06-modules/pgsql/03-transactions.cpp`, `06-modules/pgsql/04-types.cpp`, `06-modules/pgsql/05-errors.cpp`, `06-modules/pgsql/06-typed-rows.cpp`, `06-modules/pgsql/07-listen-notify.cpp`, `06-modules/pgsql/08-tls-and-limits.cpp`, `06-modules/pgsql/10-streaming-results.cpp`, `06-modules/redis/01-connect.cpp`, `06-modules/redis/02-data-types.cpp`, `06-modules/redis/03-coroutines-and-pipelining.cpp`, `06-modules/redis/04-pubsub.cpp`, `06-modules/redis/05-transactions.cpp`, `06-modules/redis/06-streams.cpp`, `06-modules/redis/07-scripting.cpp`, `06-modules/redis/08-sorted-sets-and-ttl.cpp`, `06-modules/redis/09-reliability.cpp`, `06-modules/redis/10-cache-actor.cpp`, `06-modules/redis/12-cardinality-and-bitmaps.cpp`, `06-modules/redis/13-geospatial.cpp`, `06-modules/redis/14-acl-and-topology.cpp`, `06-modules/ws/03-coro-session.cpp`, `06-modules/ws/04-coro-client.cpp`, `07-applications/02-auction-house/src/main.cpp` |
| `qb::io::async::timeout_error` | `03-coroutines/04-ask-request-response.cpp`, `03-coroutines/05-combinators.cpp`, `04-patterns/04-scatter-gather.cpp`, `04-patterns/05-resilience.cpp`, `04-patterns/06-streaming.cpp` |
| `qb::io::async::timer` | `03-coroutines/11-async-streams.cpp` |
| `qb::io::async::transform` | `03-coroutines/09-channels.cpp` |
| `qb::io::async::transient_network_policy` | `03-coroutines/13-retry-and-single-flight.cpp` |
| `qb::io::async::wait_for_io` | `03-coroutines/14-foreign-awaitables.cpp` |
| `qb::io::async::wait_readable` | `03-coroutines/14-foreign-awaitables.cpp` |
| `qb::io::async::wait_writable` | `03-coroutines/14-foreign-awaitables.cpp` |
| `qb::io::async::when_all` | `03-coroutines/01-first-coroutine.cpp`, `03-coroutines/05-combinators.cpp`, `04-patterns/05-resilience.cpp`, `06-modules/http/09-coroutine-handlers.cpp`, `06-modules/pgsql/10-streaming-results.cpp`, `06-modules/redis/03-coroutines-and-pipelining.cpp` |
| `qb::io::async::when_any` | `03-coroutines/05-combinators.cpp` |
| `qb::io::async::when_any_result` | `03-coroutines/05-combinators.cpp` |
| `qb::io::async::with_deadline` | `03-coroutines/05-combinators.cpp` |
| `qb::io::async::with_lock` | `03-coroutines/12-sync-primitives.cpp` |
| `qb::io::async::with_retry` | `03-coroutines/13-retry-and-single-flight.cpp` |
| `qb::io::async::with_retry_until` | `03-coroutines/13-retry-and-single-flight.cpp` |
| `qb::io::async::with_scope` | `03-coroutines/07-structured-concurrency.cpp` |
| `qb::io::async::with_semaphore` | `03-coroutines/12-sync-primitives.cpp` |
| `qb::io::async::with_timeout` | `02-io/08-timeouts-and-watchers.cpp`, `02-io/11-logging-and-metrics.cpp` |
| `qb::io::async::with_timeout<T>` | `02-io/01-event-loop.cpp` |
| `qb::io::async::zip` | `03-coroutines/11-async-streams.cpp` |

### `qb::io::async::coroutine_scope`

| capability | demonstrated by |
| --- | --- |
| `qb::io::async::coroutine_scope::cleanup_policy` | `03-coroutines/07-structured-concurrency.cpp` |

### `qb::io::async::event`

| capability | demonstrated by |
| --- | --- |
| `qb::io::async::event::disconnected` | `02-io/05-custom-protocol.cpp`, `02-io/06-framing-toolbox.cpp`, `02-io/09-graceful-drain.cpp` |
| `qb::io::async::event::dispose` | `02-io/09-graceful-drain.cpp` |
| `qb::io::async::event::eos` | `02-io/09-graceful-drain.cpp` |
| `qb::io::async::event::extracted` | `02-io/09-graceful-drain.cpp` |
| `qb::io::async::event::file` | `02-io/08-timeouts-and-watchers.cpp` |
| `qb::io::async::event::input_drained` | `02-io/09-graceful-drain.cpp` |
| `qb::io::async::event::pending_read` | `02-io/09-graceful-drain.cpp` |
| `qb::io::async::event::pending_write` | `02-io/09-graceful-drain.cpp` |
| `qb::io::async::event::timer` | `02-io/01-event-loop.cpp`, `02-io/08-timeouts-and-watchers.cpp`, `02-io/11-logging-and-metrics.cpp` |

### `qb::io::async::listener`

| capability | demonstrated by |
| --- | --- |
| `qb::io::async::listener::current.loop()` | `02-io/01-event-loop.cpp` |

### `qb::io::async::quic`

| capability | demonstrated by |
| --- | --- |
| `qb::io::async::quic::connector` | `02-io/12-quic.cpp` |
| `qb::io::async::quic::server` | `02-io/12-quic.cpp` |

### `qb::io::async::quic::event`

| capability | demonstrated by |
| --- | --- |
| `qb::io::async::quic::event::connected` | `02-io/12-quic.cpp` |
| `qb::io::async::quic::event::connection_closed` | `02-io/12-quic.cpp` |
| `qb::io::async::quic::event::datagram` | `02-io/12-quic.cpp` |
| `qb::io::async::quic::event::stream_data` | `02-io/12-quic.cpp` |
| `qb::io::async::quic::event::stream_started` | `02-io/12-quic.cpp` |

### `qb::io::async::tcp`

| capability | demonstrated by |
| --- | --- |
| `qb::io::async::tcp::connect` | `02-io/07-tls.cpp` |

### `qb::io::log`

| capability | demonstrated by |
| --- | --- |
| `qb::io::log::Level` | `02-io/11-logging-and-metrics.cpp` |
| `qb::io::log::init` | `02-io/11-logging-and-metrics.cpp` |
| `qb::io::log::setLevel` | `02-io/11-logging-and-metrics.cpp` |

### `qb::io::quic`

| capability | demonstrated by |
| --- | --- |
| `qb::io::quic::settings` | `02-io/12-quic.cpp` |
| `qb::io::quic::tls_config` | `02-io/12-quic.cpp` |

### `qb::io::ssl`

| capability | demonstrated by |
| --- | --- |
| `qb::io::ssl::Certificate` | `02-io/07-tls.cpp` |
| `qb::io::ssl::Context` | `02-io/07-tls.cpp` |
| `qb::io::ssl::TlsVersion` | `02-io/07-tls.cpp` |
| `qb::io::ssl::VerifyMode` | `02-io/07-tls.cpp` |

### `qb::io::ssl::Context`

| capability | demonstrated by |
| --- | --- |
| `qb::io::ssl::Context::client` | `02-io/07-tls.cpp` |
| `qb::io::ssl::Context::server` | `02-io/07-tls.cpp` |

### `qb::io::sys`

| capability | demonstrated by |
| --- | --- |
| `qb::io::sys::file` | `02-io/01-event-loop.cpp`, `02-io/02-files.cpp`, `02-io/08-timeouts-and-watchers.cpp` |

### `qb::io::tcp`

| capability | demonstrated by |
| --- | --- |
| `qb::io::tcp::socket` | `02-io/06-framing-toolbox.cpp` |

### `qb::io::tcp::ssl`

| capability | demonstrated by |
| --- | --- |
| `qb::io::tcp::ssl::socket` | `02-io/07-tls.cpp` |

### `qb::io::udp`

| capability | demonstrated by |
| --- | --- |
| `qb::io::udp::socket` | `03-coroutines/14-foreign-awaitables.cpp` |

### `qb::jwt`

| capability | demonstrated by |
| --- | --- |
| `qb::jwt::Algorithm` | `02-io/10-crypto-and-compression.cpp` |
| `qb::jwt::CreateOptions` | `02-io/10-crypto-and-compression.cpp` |
| `qb::jwt::ValidationError` | `02-io/10-crypto-and-compression.cpp` |
| `qb::jwt::VerifyOptions` | `02-io/10-crypto-and-compression.cpp` |
| `qb::jwt::create` | `02-io/10-crypto-and-compression.cpp` |
| `qb::jwt::verify` | `02-io/10-crypto-and-compression.cpp` |

### `qb::lockfree`

| capability | demonstrated by |
| --- | --- |
| `qb::lockfree::SpinLock` | `01-actors/12-lockfree-bridge.cpp` |

### `qb::lockfree::mpsc`

| capability | demonstrated by |
| --- | --- |
| `qb::lockfree::mpsc::ringbuffer<Job, RING_SLOTS, PRODUCERS>` | `01-actors/12-lockfree-bridge.cpp` |
| `qb::lockfree::mpsc::ringbuffer<Job, TINY_SLOTS, 1>` | `01-actors/12-lockfree-bridge.cpp` |

### `qb::lockfree::spsc`

| capability | demonstrated by |
| --- | --- |
| `qb::lockfree::spsc::ringbuffer<Sample, 1024>` | `01-actors/03-event-payloads.cpp` |
| `qb::lockfree::spsc::ringbuffer<Tick, 4096>` | `07-applications/03-market-data-hub/src/main.cpp` |

### `qb::pg`

| capability | demonstrated by |
| --- | --- |
| `qb::pg::Reply<void>` | `06-modules/pgsql/10-streaming-results.cpp` |
| `qb::pg::await` | `06-modules/pgsql/09-callbacks-and-await.cpp` |
| `qb::pg::connection_options` | `06-modules/pgsql/08-tls-and-limits.cpp` |
| `qb::pg::discard_error` | `06-modules/pgsql/09-callbacks-and-await.cpp` |
| `qb::pg::discard_prepare` | `06-modules/pgsql/09-callbacks-and-await.cpp` |
| `qb::pg::discard_query` | `06-modules/pgsql/09-callbacks-and-await.cpp` |
| `qb::pg::isolation_level` | `06-modules/pgsql/03-transactions.cpp` |
| `qb::pg::notification` | `06-modules/pgsql/07-listen-notify.cpp` |
| `qb::pg::params` | `06-modules/pgsql/02-parameters.cpp`, `06-modules/pgsql/04-types.cpp`, `06-modules/pgsql/05-errors.cpp`, `06-modules/pgsql/09-callbacks-and-await.cpp` |
| `qb::pg::results` | `06-modules/pgsql/06-typed-rows.cpp`, `06-modules/pgsql/10-streaming-results.cpp` |
| `qb::pg::ssl_verify_mode` | `06-modules/pgsql/08-tls-and-limits.cpp` |
| `qb::pg::transaction_abort` | `06-modules/pgsql/03-transactions.cpp` |
| `qb::pg::transaction_mode` | `06-modules/pgsql/03-transactions.cpp` |
| `qb::pg::type_oid_sequence` | `06-modules/pgsql/02-parameters.cpp`, `06-modules/pgsql/09-callbacks-and-await.cpp` |
| `qb::pg::with_transaction` | `06-modules/pgsql/03-transactions.cpp` |

### `qb::pg::detail`

| capability | demonstrated by |
| --- | --- |
| `qb::pg::detail::numeric` | `06-modules/pgsql/04-types.cpp` |

### `qb::pg::error`

| capability | demonstrated by |
| --- | --- |
| `qb::pg::error::db_error` | `06-modules/pgsql/05-errors.cpp`, `06-modules/pgsql/09-callbacks-and-await.cpp` |
| `qb::pg::error::field_type_mismatch` | `06-modules/pgsql/05-errors.cpp` |
| `qb::pg::error::value_is_null` | `06-modules/pgsql/05-errors.cpp` |

### `qb::pg::oid`

| capability | demonstrated by |
| --- | --- |
| `qb::pg::oid::boolean` | `06-modules/pgsql/04-types.cpp` |
| `qb::pg::oid::bpchar` | `06-modules/pgsql/04-types.cpp` |
| `qb::pg::oid::bytea` | `06-modules/pgsql/04-types.cpp` |
| `qb::pg::oid::date` | `06-modules/pgsql/04-types.cpp` |
| `qb::pg::oid::float4` | `06-modules/pgsql/04-types.cpp` |
| `qb::pg::oid::float8` | `06-modules/pgsql/04-types.cpp`, `06-modules/pgsql/09-callbacks-and-await.cpp` |
| `qb::pg::oid::int2` | `06-modules/pgsql/04-types.cpp` |
| `qb::pg::oid::int4` | `06-modules/pgsql/02-parameters.cpp`, `06-modules/pgsql/04-types.cpp`, `06-modules/pgsql/05-errors.cpp`, `06-modules/pgsql/09-callbacks-and-await.cpp` |
| `qb::pg::oid::text` | `06-modules/pgsql/02-parameters.cpp`, `06-modules/pgsql/05-errors.cpp` |

### `qb::pg::sqlstate`

| capability | demonstrated by |
| --- | --- |
| `qb::pg::sqlstate::query_canceled` | `06-modules/pgsql/03-transactions.cpp`, `06-modules/pgsql/08-tls-and-limits.cpp` |

### `qb::pg::tcp`

| capability | demonstrated by |
| --- | --- |
| `qb::pg::tcp::database` | `06-modules/pgsql/01-connect-and-query.cpp`, `06-modules/pgsql/02-parameters.cpp`, `06-modules/pgsql/03-transactions.cpp`, `06-modules/pgsql/04-types.cpp`, `06-modules/pgsql/05-errors.cpp`, `06-modules/pgsql/06-typed-rows.cpp`, `06-modules/pgsql/07-listen-notify.cpp`, `06-modules/pgsql/08-tls-and-limits.cpp`, `06-modules/pgsql/09-callbacks-and-await.cpp`, `06-modules/pgsql/10-streaming-results.cpp`, `07-applications/02-auction-house/src/main.cpp` |
| `qb::pg::tcp::notify_co_consumer` | `06-modules/pgsql/07-listen-notify.cpp` |

### `qb::pg::tcp::ssl`

| capability | demonstrated by |
| --- | --- |
| `qb::pg::tcp::ssl::database` | `06-modules/pgsql/08-tls-and-limits.cpp` |

### `qb::protocol`

| capability | demonstrated by |
| --- | --- |
| `qb::protocol::json` | `02-io/06-framing-toolbox.cpp` |
| `qb::protocol::json_packed` | `02-io/06-framing-toolbox.cpp` |

### `qb::protocol::base`

| capability | demonstrated by |
| --- | --- |
| `qb::protocol::base::byte_terminated` | `02-io/06-framing-toolbox.cpp` |
| `qb::protocol::base::bytes_terminated` | `02-io/06-framing-toolbox.cpp` |
| `qb::protocol::base::size_as_header` | `02-io/06-framing-toolbox.cpp` |

### `qb::protocol::text`

| capability | demonstrated by |
| --- | --- |
| `qb::protocol::text::binary16` | `02-io/09-graceful-drain.cpp` |
| `qb::protocol::text::binary8` | `02-io/06-framing-toolbox.cpp` |
| `qb::protocol::text::command` | `02-io/07-tls.cpp`, `02-io/08-timeouts-and-watchers.cpp`, `02-io/09-graceful-drain.cpp` |
| `qb::protocol::text::command<T>` | `02-io/03-tcp.cpp`, `02-io/04-udp.cpp` |

### `qb::redis`

| capability | demonstrated by |
| --- | --- |
| `qb::redis::BoundedInterval<double>` | `06-modules/redis/08-sorted-sets-and-ttl.cpp` |
| `qb::redis::GeoUnit` | `06-modules/redis/13-geospatial.cpp` |
| `qb::redis::LeftBoundedInterval<double>` | `06-modules/redis/08-sorted-sets-and-ttl.cpp` |
| `qb::redis::LimitOptions` | `06-modules/redis/08-sorted-sets-and-ttl.cpp` |
| `qb::redis::Reply<T>` | `06-modules/redis/02-data-types.cpp`, `06-modules/redis/03-coroutines-and-pipelining.cpp`, `06-modules/redis/05-transactions.cpp`, `06-modules/redis/07-scripting.cpp`, `06-modules/redis/08-sorted-sets-and-ttl.cpp`, `06-modules/redis/09-reliability.cpp`, `06-modules/redis/11-callbacks-and-consumers.cpp`, `06-modules/redis/12-cardinality-and-bitmaps.cpp`, `06-modules/redis/13-geospatial.cpp` |
| `qb::redis::Reply<std::string>` | `06-modules/redis/14-acl-and-topology.cpp` |
| `qb::redis::RetryPolicy` | `06-modules/redis/09-reliability.cpp` |
| `qb::redis::geo_pos` | `06-modules/redis/13-geospatial.cpp` |
| `qb::redis::message` | `06-modules/redis/11-callbacks-and-consumers.cpp` |
| `qb::redis::scan<>` | `06-modules/redis/08-sorted-sets-and-ttl.cpp` |
| `qb::redis::score_member` | `06-modules/redis/08-sorted-sets-and-ttl.cpp` |

### `qb::redis::tcp`

| capability | demonstrated by |
| --- | --- |
| `qb::redis::tcp::cb_consumer` | `06-modules/redis/11-callbacks-and-consumers.cpp` |
| `qb::redis::tcp::client` | `06-modules/redis/01-connect.cpp`, `06-modules/redis/02-data-types.cpp`, `06-modules/redis/03-coroutines-and-pipelining.cpp`, `06-modules/redis/04-pubsub.cpp`, `06-modules/redis/05-transactions.cpp`, `06-modules/redis/06-streams.cpp`, `06-modules/redis/07-scripting.cpp`, `06-modules/redis/08-sorted-sets-and-ttl.cpp`, `06-modules/redis/09-reliability.cpp`, `06-modules/redis/10-cache-actor.cpp`, `06-modules/redis/11-callbacks-and-consumers.cpp`, `06-modules/redis/12-cardinality-and-bitmaps.cpp`, `06-modules/redis/13-geospatial.cpp`, `06-modules/redis/14-acl-and-topology.cpp` |
| `qb::redis::tcp::co_consumer` | `06-modules/redis/04-pubsub.cpp` |
| `qb::redis::tcp::pipeline` | `06-modules/redis/11-callbacks-and-consumers.cpp` |

## 2. By example

### `01-actors`

- **`01-actors/01-hello-actor.cpp`** — 13 capabilities
  - The smallest complete qb program: an actor that subscribes to one event and one that sends it on every turn of the core's loop, plus the engine that runs both and the two ways an actor ends.
- **`01-actors/02-messaging.cpp`** — 10 capabilities
  - Request and response between actors: who sent it, how the answer gets back, and why the pause in the middle is a coroutine sleep and never a blocked handler.
- **`01-actors/03-event-payloads.cpp`** — 17 capabilities
  - The one rule about event payloads you cannot discover by testing on a Mac: an event is RELOCATED with memcpy and its source destructor is never run, so no member may STORE a pointer into itself. Which shapes are safe, which are not, why the compiler cannot tell you, what the debug guard does — and how a foreign thread feeds an actor system without a mutex.
- **`01-actors/04-cores-and-placement.cpp`** — 12 capabilities
  - Where an actor runs and how you address it once it is there: placing actors on several cores, reading the core back out of an id, and the system-wide broadcast.
- **`01-actors/05-lifecycle.cpp`** — 11 capabilities
  - How an actor ends: a KillEvent handler that runs only because it was registered, a two-phase drain that finishes the work in flight first, and the difference between "stop taking work" and "terminate".
- **`01-actors/06-doing-things-later.cpp`** — 12 capabilities
  - The four ways to run work later and which one is right when — and that inside an actor the answer is a coroutine sleep that the framework cancels when the actor dies, not a blocked handler and not a per-turn callback.
- **`01-actors/07-service-actor.cpp`** — 13 capabilities
  - The framework's standard bootstrap object, which had zero demonstrators before this file: one singleton per core per tag, reachable from any actor on that core with a TYPED pointer and no id plumbing at all — plus the id of its peer on any OTHER core, computed rather than looked up.
- **`01-actors/08-child-actors.cpp`** — 17 capabilities
  - Actor trees: creating a child at runtime from inside another actor, holding a TYPED handle to it instead of a bare id, keeping a registry of children bounded as they die — and the property the word "child" gets wrong, which is that the parent does not own it and killing the parent leaves it running.
- **`01-actors/09-state-machine.cpp`** — 13 capabilities
  - An actor is a natural home for a finite state machine: one thread owns the state, a transition table owns the rules, and every timed step is a message to self rather than a wait.
- **`01-actors/10-signals-and-shutdown.cpp`** — 15 capabilities
  - The first thing anyone needs in order to ship a server: SIGINT and SIGTERM arriving as ordinary events, an extra signal asked for by name and handled WITHOUT dying, draining in-flight work before the process leaves — and the exit-code contract, including the measured reason `hasError()` alone is not a health check.
- **`01-actors/11-hot-path.cpp`** — 22 capabilities
  - The knobs qb is sold on and the corpus never showed: `send` versus `push`, a variable-length event written straight into the pipe with `getPipe` + `allocated_push`, an actor that opts out of the five default event registrations, the latency/affinity settings — each one measured with `tsc_ticks` rather than asserted.
- **`01-actors/12-lockfree-bridge.cpp`** — 21 capabilities
  - The other half of the foreign-thread boundary: 03-event-payloads bridged ONE outside thread with an spsc ring; this bridges MANY with qb::lockfree::mpsc::ringbuffer, shows why its three enqueue overloads are not interchangeable (one of them takes a lock and two do not), why the drain must be BOUNDED per loop turn, and what qb::lockfree::SpinLock is — the primitive underneath all of it, and the one you will most often be wrong to use.

### `02-io`

- **`02-io/01-event-loop.cpp`** — 11 capabilities
  - The qb-io event loop with no actor and no engine behind it: initialise it, run it, and let a timer, a synchronous file and a filesystem watcher share one thread.
- **`02-io/02-files.cpp`** — 7 capabilities
  - qb's synchronous file handle, and where it ends: open/read/write/close through qb::io::sys::file, with mmap, stat and std::filesystem left to the platform because qb wraps none of them.
- **`02-io/03-tcp.cpp`** — 9 capabilities
  - A TCP server, its per-connection session and a client, all as CRTP roles over one loop, framed by the shipped newline protocol instead of a hand-written parser.
- **`02-io/04-udp.cpp`** — 9 capabilities
  - The datagram shape of the same CRTP roles: bind instead of listen, an explicit destination endpoint per send, and the same shipped text protocol carried over UDP.
- **`02-io/05-custom-protocol.cpp`** — 12 capabilities
  - Writing a wire protocol qb does not ship: framing in getMessageSize, dispatch in onMessage, serialisation as a pipe<char>::put specialisation, and switch_protocol to change it mid-stream.
- **`02-io/06-framing-toolbox.cpp`** — 20 capabilities
  - Most wire formats need NO parser. Six framings — a delimiter byte, a delimiter sequence, an 8-bit length prefix, a 32-bit length prefix, JSON and MessagePack — each expressed in a handful of lines by reusing a shipped archetype, and all six decoded out of ONE write per connection, because TCP has no message boundaries and the archetype is what supplies them.
- **`02-io/07-tls.cpp`** — 24 capabilities
  - TLS on its own terms — a `qb::io::ssl::Context` built fluently, a secure server and a secure client over ONE event loop, ALPN negotiated, the peer certificate inspected, and the beat that matters most: a client that does not trust the certificate FAILS to connect, because verification is on by default and switching it off is a decision with a name.
- **`02-io/08-timeouts-and-watchers.cpp`** — 17 capabilities
  - What `with_timeout` actually is — an INACTIVITY watchdog, not a periodic timer — proved by running the two re-arming calls side by side and counting how often each fires; a one-shot timer you can still cancel; a `file_watcher` that tails a growing file and frames its lines for you; and a `directory_watcher` whose real limit is the lesson: it polls, and it tells you THAT something changed, never WHAT.
- **`02-io/09-graceful-drain.cpp`** — 23 capabilities
  - The shutdown and backpressure vocabulary, which had ZERO coverage in the pre-3.0 corpus: `pending_read` / `input_drained` on the way in, `pending_write` / `eos` on the way out, `close_after_deliver()` for "answer, THEN hang up", `disconnected` and `dispose` in the order they really run, `extracted` for handing a live socket to another owner — and `async::defer`, the one primitive that lets a handler replace the object it is running on.
- **`02-io/10-crypto-and-compression.cpp`** — 30 capabilities
  - The security and payload primitives qb-io already ships, on their own terms: digests and HMAC, an AEAD that REFUSES a tampered ciphertext, Argon2 password hashing, a constant-time comparison, a signed token, and a compressor whose uncompress takes an OUTPUT BOUND — because a decompression bomb is an attack, not a corner case.
- **`02-io/11-logging-and-metrics.cpp`** — 26 capabilities
  - The two production surfaces a qb-io service needs and that nothing in the corpus used: the asynchronous logger behind QB_LOG_* — which every qb binary has ALREADY started before main() runs — and a fixed-capacity rolling window of measurements taken with the raw CPU counter, so a hot loop can report its own latency without allocating.
- **`02-io/12-quic.cpp`** — 19 capabilities
  - QUIC as a qb-io transport in its own right, not as something HTTP/3 happens to sit on: one `endpoint` type for both roles, ALPN chosen during the handshake rather than by the port, independent streams over one connection, unreliable datagrams alongside them, and the refusal that matters — a peer whose ALPN does not match never connects.

### `03-coroutines`

- **`03-coroutines/01-first-coroutine.cpp`** — 5 capabilities
  - A coroutine with no actor and no engine underneath it: what task<T> is, what drives it, how several of them run at once — and the by-value parameter rule, which is the one mistake here that produces live undefined behaviour rather than a compile error.
- **`03-coroutines/02-actor-coroutines.cpp`** — 12 capabilities
  - A coroutine spawned from inside an actor: capture by value before the first suspend, talk back only through the context, and let the framework cancel the frame when the actor dies — the actor may be destroyed while the coroutine is parked.
- **`03-coroutines/03-awaiting-oninit.cpp`** — 17 capabilities
  - 3.0's headline behavioural change — `onInit()` is a coroutine, so setup that WAITS is written straight down the page. What the engine does with an actor whose init is still in flight (it stashes its mail instead of dropping it), how to wait for one without guessing a sleep, and the deadline that fails a stalled init rather than wedging a core forever.
- **`03-coroutines/04-ask-request-response.cpp`** — 16 capabilities
  - The one-to-one exchange: `co_await qb::ask(...)` replaces a correlation map, a reply handler and a timeout timer with one line — what the correlation id is, the single line you cannot leave out, and the two ways to get it wrong that produce a timeout and no other symptom whatsoever.
- **`03-coroutines/05-combinators.cpp`** — 11 capabilities
  - The five ways to combine awaitables — wait for ALL of them, wait for the FIRST, and bound one with a timeout or a deadline — plus the one property that decides which you should reach for: what happens to the branches that LOSE.
- **`03-coroutines/06-cancellation.cpp`** — 20 capabilities
  - Why an actor may be killed while its coroutines are parked and nothing leaks: the per-actor cancellation scope, the four awaits that respect it, the destructors that still run — and the await that does NOT respect it, which is the whole reason `ctx.sleep` exists next to `qb::io::async::sleep`.
- **`03-coroutines/07-structured-concurrency.cpp`** — 20 capabilities
  - A scope OWNS the coroutines you spawn into it: it is where you join them, and its destructor is the one place that decides what happens to the ones still running. The three named policies differ only there — and only one of them does anything a worker can feel, which this program measures instead of asserting.
- **`03-coroutines/08-bounded-fan-out.cpp`** — 11 capabilities
  - How many pieces of work are allowed to be in flight AT ONCE — all of them (`parallel`), exactly K of them (`parallel_map`), or one (`repeat_while`). The middle one is the answer almost every real system needs, and this program measures the ceiling rather than trusting it.
- **`03-coroutines/09-channels.cpp`** — 19 capabilities
  - Handing values from one coroutine to another through a queue whose CAPACITY is the backpressure policy, closing it as the one shutdown protocol, and waiting on several of them at once with `select`.
- **`03-coroutines/10-generators.cpp`** — 19 capabilities
  - Producing a sequence one value at a time instead of returning a container: `generator<T>` when the production is synchronous, `async_generator<T>` when it has to await. Laziness is the point, so this program counts what the source actually produced rather than trusting that it stopped when the consumer did.
- **`03-coroutines/11-async-streams.cpp`** — 32 capabilities
  - Composing a sequence instead of looping over it: `async_stream<T>` is a chain of transformations that produces NOTHING until a terminal pulls on it, and the only sequence library here that treats TIME as a source and as a transform.
- **`03-coroutines/12-sync-primitives.cpp`** — 26 capabilities
  - That single-threaded does NOT mean synchronisation-free: every `co_await` is a place where another coroutine runs, so an invariant that spans one is exactly as broken as an invariant that spans a thread switch. The program produces a real lost update first, then fixes it, then covers the five other primitives that exist for the same reason.
- **`03-coroutines/13-retry-and-single-flight.cpp`** — 20 capabilities
  - One flaky operation and several callers who all want it: `with_retry` decides when to try again and when to give up, and `shared_task` makes five callers share ONE attempt instead of each starting their own. Together they are the difference between a retry that heals a blip and a retry that becomes the outage.
- **`03-coroutines/14-foreign-awaitables.cpp`** — 13 capabilities
  - Awaiting something qb does not own: a raw socket handle, via `wait_readable`/`wait_writable`/`wait_for_io`, and a callback-based library, via `async_awaiter<T>`. These two are the escape hatch that keeps a foreign API from forcing a blocking call onto the event loop.

### `04-patterns`

- **`04-patterns/01-pubsub.cpp`** — 14 capabilities
  - A publish/subscribe bus you do not write: `qb::PubSub<Topic>` is a per-core ServiceActor, so a publisher reaches every subscriber on its own core with no registry, no ids plumbed through constructors and no cleanup code — and a subscriber killed without unsubscribing is inert rather than a leak.
- **`04-patterns/02-supervisor.cpp`** — 15 capabilities
  - Let something else restart your actors. `qb::Supervisor` owns a fixed set of child slots, restarts them by a declared `restart_strategy` when one terminates, ignores a stale report from an already-replaced child, and escalates instead of restarting forever once a restart-intensity cap is exceeded.
- **`04-patterns/03-worker-pool.cpp`** — 12 capabilities
  - The two routing decisions a pool of workers ever has to make, and the one line each costs: `next()` when any worker will do, `for_key(k)` when the same key must keep reaching the same worker — plus the caveat that makes the second one honest.
- **`04-patterns/04-scatter-gather.cpp`** — 19 capabilities
  - Request/response between actors as ONE line — `co_await qb::ask(...)` — the four fan-out shapes built on it (every reply, the first reply, the first k replies, and a bounded fan-out with at most N outstanding), and the one budget that bounds a whole CHAIN of asks instead of resetting at every hop.
- **`04-patterns/05-resilience.cpp`** — 14 capabilities
  - The four things you do to a call that might fail: retry it with backoff, stop calling a dependency that is down, slow yourself to a rate it can take, and cap how many calls are in flight at once — each a policy object rather than a piece of hand-written bookkeeping.
- **`04-patterns/06-streaming.cpp`** — 13 capabilities
  - One request, MANY replies. `qb::ask_stream` gives the asker a `qb::stream<E>` it drains with `while (auto chunk = co_await s.next())`, while the responder pushes with `yield_answer` and finishes with `end_stream` — plus the two ways a stream ends badly, and why a bounded buffer that throws beats an unbounded one that grows.
- **`04-patterns/07-saga.cpp`** — 13 capabilities
  - How to undo a multi-step operation that has no transaction to roll back. Each step registers its own compensation as soon as it succeeds; if a later step fails, `qb::run_saga` runs the registered compensations in REVERSE order and then re-throws — so exactly the steps that happened are the steps that get undone.
- **`04-patterns/08-batching-and-idempotency.cpp`** — 16 capabilities
  - Two patterns that belong together because retrying is what makes both necessary: `qb::batcher<T>` coalesces many small items into one costly write (on a count OR a time trigger, whichever comes first), and `qb::dedup_map` + `qb::answer_idempotent` make a responder run its side effect at most once per key, however many times the same request arrives.
- **`04-patterns/09-discovery.cpp`** — 15 capabilities
  - How one actor finds another it was never handed: `co_await qb::require<T>(ctx, w)` discovers every live actor of a type across every core, `co_await qb::ping(ctx, id)` asks one of them whether it is still there — and `is_actor_alive(id)` answers a DIFFERENT, core-local question that will lie to you about a remote actor.

### `05-services`

- **`05-services/01-tcp-chat/client/main.cpp`** — 9 capabilities
  - The other half of a two-binary project: console input on one core, the socket on another, so a blocking read never holds up the network — and an exit code that reports what the engine actually did.
- **`05-services/01-tcp-chat/server/main.cpp`** — 11 capabilities
  - How a real server is laid out across cores: the acceptor on one, a pool of session actors on another, the room's state on a third — and one builder() chain that names the pool so the acceptor can round-robin over it.
- **`05-services/02-pubsub-broker/client/main.cpp`** — 9 capabilities
  - A publish/subscribe client: the same input-actor / network-actor split as the chat client, driving subscribe, unsubscribe and publish over one connection.
- **`05-services/02-pubsub-broker/server/main.cpp`** — 12 capabilities
  - Topic fan-out without copying the payload: one message body behind a shared_ptr and N events carrying views into it — the corpus's payload model, in the same acceptor / session-pool / logic-actor layout as 01-tcp-chat.
- **`05-services/03-file-pipeline/main.cpp`** — 15 capabilities
  - Getting blocking work off the event loop: a manager that owns the queue, a pool of worker actors spread over the cores that do the file I/O, and a client that drives the whole run and then shuts it down.
- **`05-services/04-shutdown-and-drain/main.cpp`** — 27 capabilities
  - The full shutdown story of a real server, end to end: SIGTERM arrives as an event, the acceptor STOPS ACCEPTING, the work already taken is DRAINED to completion, every output buffer is FLUSHED to its socket, and only then does the process leave — with an exit code that means something, including on the path where the port could not be bound.

### `06-modules`

- **`06-modules/http/01-hello-server.cpp`** — 11 capabilities
  - The actor seam, stated once for the whole module: qb::http::Server<> inherited alongside qb::Actor, two routes, and the compile() that turns them into a radix tree.
- **`06-modules/http/02-routing.cpp`** — 14 capabilities
  - The router itself: literal paths, :params, wildcards and every verb, resolved by one radix tree that compile() builds once.
- **`06-modules/http/03-controllers.cpp`** — 12 capabilities
  - A controller groups routes with the state they share, so a handler is a member function rather than a lambda closing over the server.
- **`06-modules/http/04-middleware.cpp`** — 12 capabilities
  - Middleware as a chain: each hop either calls next() or answers, and a route group scopes the chain to one prefix instead of the whole server.
- **`06-modules/http/05-rest-api-json.cpp`** — 12 capabilities
  - A JSON REST service assembled from SHIPPED middleware — CORS, compression, security headers, logging, rate limit and an error chain — rather than hand-written equivalents.
- **`06-modules/http/06-validation.cpp`** — 22 capabilities
  - The `qb::http::validation` namespace, which this file previously included five headers of and used zero times: a JSON-schema validator, typed query/path/header parameter rules, a sanitizer that runs BEFORE validation, the error shape you read them out of, and the middleware that wires all of it in front of a router.
- **`06-modules/http/07-auth-jwt.cpp`** — 12 capabilities
  - JWT authentication end to end: an auth::Manager that signs and verifies, the middleware that puts an auth::User in the context, and the routes that read it back out.
- **`06-modules/http/08-static-files.cpp`** — 12 capabilities
  - Serving a directory: the shipped static-files middleware, compression and security headers over it, and an upload endpoint beside it.
- **`06-modules/http/09-coroutine-handlers.cpp`** — 11 capabilities
  - A route handler that is a coroutine: await a sleep, await an outbound request, await two at once with when_all, and return the reply — no callback, no continuation, no state bag.
- **`06-modules/http/10-client.cpp`** — 19 capabilities
  - The PERSISTENT HTTP/1.1 client — one connection reused across many requests, a batch issued in one call, the callback form beside the coroutine one, and the stats it keeps — measured against a server this program hosts itself, on the same event loop, so the connection count is a number rather than a claim.
- **`06-modules/http/11-https.cpp`** — 11 capabilities
  - TLS is a one-type switch: qb::http::ssl::Server<> instead of qb::http::Server<>, the same router and the same middleware over qb::http::ssl::DefaultSecureSession.
- **`06-modules/http/12-http2.cpp`** — 12 capabilities
  - HTTP/2 over TLS+ALPN with the same router and the same middleware: the session and server types change, nothing above them does.
- **`06-modules/http/13-http3.cpp`** — 11 capabilities
  - One server on two transports: HTTP/2 over TCP and HTTP/3 over QUIC behind a single dual-stack object, with Alt-Svc advertising the upgrade.
- **`06-modules/http/14-streaming-and-cookies.cpp`** — 36 capabilities
  - The parts of an HTTP message the other thirteen programs never touch: chunked framing and the `Chunk` builder, cookies with their attributes and a `CookieJar`, a url-encoded `Form`, a multipart body you BUILD rather than parse, `Body::compress`, and the HTTP date helpers that make a conditional GET answer 304.
- **`06-modules/http/15-http2-and-http3-clients.cpp`** — 29 capabilities
  - The two clients this corpus documented and never used: qb::http2::Client and qb::http3::Client. The API is the SAME as http1::Client — make_client, connect, push_request, push_requests, get_stats — so the file is really about the one thing that differs, MULTIPLEXING, measured here against an HTTP/1.1 client doing identical work on the same event loop; plus the rules both clients enforce that http1 does not.
- **`06-modules/pgsql/01-connect-and-query.cpp`** — 5 capabilities
  - The pgsql seam: a database as an ordinary object, connected with co_await, queried with co_await, and a result set you iterate by name.
- **`06-modules/pgsql/02-parameters.cpp`** — 7 capabilities
  - Getting off string concatenation: prepare a statement once with its parameter OIDs, then execute it with qb::pg::params — including a NULL through std::optional.
- **`06-modules/pgsql/03-transactions.cpp`** — 19 capabilities
  - Everything the word "transaction" covers in this client: the manual BEGIN/COMMIT you start with, `with_transaction` which deletes the branch you keep forgetting, SAVEPOINTs for partial rollback, an isolation/read-only mode, and a per-transaction statement timeout — with the one rule that makes all of them work.
- **`06-modules/pgsql/04-types.cpp`** — 13 capabilities
  - The type map, end to end: every common PostgreSQL OID written from and read back into its C++ counterpart, civil time and numeric included.
- **`06-modules/pgsql/05-errors.cpp`** — 8 capabilities
  - What failure looks like on each layer: a syntax error, a constraint violation, a NULL read as a value and a field read as the wrong type — each with the exception that names it.
- **`06-modules/pgsql/06-typed-rows.cpp`** — 17 capabilities
  - Reading a result set without writing a loop: a row as a std::tuple, `one<>()` for the single-row query, `all<>()` versus the lazy `rows<>()` view, `field::text()` for a read with no copy, and `resultset::json()` instead of a hand-written to_json().
- **`06-modules/pgsql/07-listen-notify.cpp`** — 21 capabilities
  - PostgreSQL as an event bus: LISTEN/NOTIFY, a `notify_co_consumer` you `co_await`, a TRIGGER that publishes on every INSERT — and the four rules that decide whether that is a good idea (transactional delivery, no durability, an 8000-byte payload, and a subscription that does not survive a reconnect).
- **`06-modules/pgsql/08-tls-and-limits.cpp`** — 16 capabilities
  - Encrypting a PostgreSQL connection — which is a STARTTLS negotiation on the same port, not a second port — what the default verification level really promises, and the two limits you set per connection and per transaction: connect_timeout and statement_timeout.
- **`06-modules/pgsql/09-callbacks-and-await.cpp`** — 19 capabilities
  - qbm-pgsql from code that is NOT a coroutine — which is the half of this client the other eight programs never show. The fluent chain (execute/then/success/error), the three `discard_*` handlers that exist so a callback is never simply omitted, `prepare_file()` for SQL that lives in a .sql file, and `await()`, the one blocking drain that turns a queue of callbacks back into straight-line code.
- **`06-modules/pgsql/10-streaming-results.cpp`** — 17 capabilities
  - The result set that does not fit: `query()` buffers every row before you see the first one, `query_stream()` walks a server-side CURSOR and hands you one batch at a time. What the row handed to your callback actually IS (a view, valid only during the call), what happens to the cursor when your callback throws, and the rule that decides whether two overlapping streams on one connection work or destroy each other.
- **`06-modules/redis/01-connect.cpp`** — 5 capabilities
  - The redis seam: a client as an ordinary object, connected with co_await, and the string commands with the Reply<T> you check before you read.
- **`06-modules/redis/02-data-types.cpp`** — 43 capabilities
  - The four everyday Redis structures — string, hash, list, set — and the one thing that tells them apart in this client: the C++ TYPE each command's Reply comes back as. An unordered_set is not a stylistic choice, it is what a Redis set IS.
- **`06-modules/redis/03-coroutines-and-pipelining.cpp`** — 7 capabilities
  - Awaiting Redis, and what pipelining is: three commands issued inside one when_all leave together and come back together, where three sequential co_awaits pay three round trips.
- **`06-modules/redis/04-pubsub.cpp`** — 9 capabilities
  - Redis Pub/Sub as two actors: a publisher on one connection, and a subscriber whose whole loop is `while (auto msg = co_await receive())` on a second one.
- **`06-modules/redis/05-transactions.cpp`** — 16 capabilities
  - Redis transactions as this client actually implements them: MULTI queues, EXEC runs the batch in one atomic step, DISCARD throws it away, and WATCH makes the whole thing conditional on nobody else having touched a key. Plus the trap that decides whether your MULTI block works: what a QUEUED reply looks like to a TYPED client.
- **`06-modules/redis/06-streams.cpp`** — 19 capabilities
  - Redis Streams as a work queue and as a log: producers XADD; a consumer group SPLITS the entries between its competing consumers while a SECOND group gets its own independent copy; XACK empties the pending list; a plain XREAD needs no group at all; XTRIM bounds the stream.
- **`06-modules/redis/07-scripting.cpp`** — 24 capabilities
  - Running your logic INSIDE Redis, in the three forms the server offers: an anonymous EVAL, a cached script called by SHA, and a named 7.0 Function. Includes the trap that decides whether an EVALSHA deployment survives a restart — NOSCRIPT.
- **`06-modules/redis/08-sorted-sets-and-ttl.cpp`** — 30 capabilities
  - The sorted set as the structure that keeps the ORDER for you — a leaderboard and a sliding-window rate limiter — plus expiry (EXPIRE/TTL/PERSIST and which writes clear a TTL) and the cursor SCAN you must reach for instead of KEYS.
- **`06-modules/redis/09-reliability.cpp`** — 29 capabilities
  - What every other Redis example assumes away: that the server can be unreachable, can drop your connection, and can make you wait. Bounded connect retry, auto-reconnect, what happens to a command that was IN FLIGHT when the link died, blocking commands that park a coroutine without blocking the loop, INFO as a health probe — and TLS.
- **`06-modules/redis/10-cache-actor.cpp`** — 8 capabilities
  - Redis inside an actor rather than beside one: the client is a member, the connect happens in a coroutine onInit, and every command is awaited from a handler that never blocks.
- **`06-modules/redis/11-callbacks-and-consumers.cpp`** — 26 capabilities
  - The half of qbm-redis the other ten programs never touch: the CALLBACK surface. Every command has a second overload whose FIRST argument is the handler, `await()` is the drain that replaces `co_await`, `tcp::pipeline` batches without a single coroutine, and `tcp::cb_consumer` is Pub/Sub for code that has no coroutine to park.
- **`06-modules/redis/12-cardinality-and-bitmaps.cpp`** — 24 capabilities
  - Two families for the same question — "how many DISTINCT things?" — and the trade between them. HyperLogLog answers it in a fixed 12 KB for any cardinality, approximately, and MERGES without double counting; a bitmap answers it exactly at one bit per id, and supports server-side set algebra (AND/OR/XOR/NOT) you would otherwise write a job for.
- **`06-modules/redis/13-geospatial.cpp`** — 24 capabilities
  - "What is near here?" answered by the server: GEOADD builds an index that is really a SORTED SET keyed on a 52-bit geohash, GEODIST and GEOPOS read it back (lossily — that is measured, not glossed), GEOSEARCH is the modern query and GEORADIUS the one it replaced, and the typed Reply's shape is what decides which query options you can actually use.
- **`06-modules/redis/14-acl-and-topology.cpp`** — 25 capabilities
  - The two questions you should ask a Redis you did not configure yourself, and the two command families that answer them: ACL — who am I, what may I run, and would THIS user be allowed to run THAT (which `ACL DRYRUN` answers without you becoming them) — and CLUSTER, which tells you whether the single-node assumptions in your code still hold.
- **`06-modules/ws/01-chat-server.cpp`** — 14 capabilities
  - A WebSocket server and an HTTP server side by side: the HTTP one serves the page, the WebSocket one owns the sockets that the page upgrades onto.
- **`06-modules/ws/02-chat-client.cpp`** — 10 capabilities
  - The client half of the handshake, by hand: generateKey, the upgrade request, then the same frame protocol the server speaks — which is what ws::coro_client will replace.
- **`06-modules/ws/03-coro-session.cpp`** — 20 capabilities
  - A WebSocket session written as ONE coroutine instead of a bag of callbacks: `coro_session<Self, Server>`, `while (auto f = co_await next_frame())`, a handshake hook that negotiates a subprotocol or refuses the upgrade, and a graceful close.
- **`06-modules/ws/04-coro-client.cpp`** — 21 capabilities
  - The WebSocket client as a coroutine: `coro_client`, `co_await connect/receive/ close_async`, the tagged frame you must branch on, the ONE-awaiter rule, the buffer that catches frames nobody is waiting for, and a reconnect on the same object.

### `07-applications`

- **`07-applications/01-taskmanager/src/main.cpp`** — 13 capabilities
  - The whole stack in one program: an acceptor core at zero latency dispatching to a pool of workers that each own an HTTP router, a WebSocket pool, a PostgreSQL connection and a Redis cache — and a main() that reports what the engine did.
- **`07-applications/02-auction-house/src/main.cpp`** — 17 capabilities
  - Work that has to happen BEFORE the engine exists: the schema is created by a coroutine driven with run_sync on the main thread, and only then do the actors start — because an actor that finds no table has nowhere to put the failure.
- **`07-applications/03-market-data-hub/src/main.cpp`** — 14 capabilities
  - A whole application with NO HTTP and NO SQL: a foreign feed thread bridged by a lock-free ring, sticky fan-out with WorkerPool, per-shard state, batched publication, a binary protocol on qb-io, `send<>` on the hot path, `setLatency(0)` — and an end-to-end latency DISTRIBUTION rather than a headline number.
