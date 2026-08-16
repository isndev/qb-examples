# QB Module (QBM) Examples

This directory serves as an index for examples related to various QB Modules (QBM). QB Modules extend the core QB
Framework with specialized functionalities, often providing clients or utilities for interacting with external services
or implementing specific protocols.

Each subdirectory listed below contains a set of examples for a particular QB Module, along with its own detailed
`README.md` explaining how to build and run those examples.

## Available QB Module Examples

### 1. HTTP Module (`./http/`) — 15 programs

* **Focus**: HTTP/1.1, HTTP/2 and HTTP/3 servers plus the persistent client.
* **Content**: `01-hello-server`, `02-routing`, `03-controllers`, `04-middleware`, `05-rest-api-json`,
  `06-validation`, `07-auth-jwt`, `08-static-files`, `09-coroutine-handlers`, `10-client`, `11-https`,
  `12-http2`, `13-http3` (one dual-stack server on TCP **and** QUIC), `14-streaming-and-cookies`
  (chunked framing, SSE, cookies and the `CookieJar`, forms and multipart, HTTP dates and
  conditional `304`s) and `15-http2-and-http3-clients` — the CLIENT side of 12 and 13, which had no
  demonstrator at all: `qb::http2::Client` and `qb::http3::Client` beside `qb::http1::Client`, with
  the same four-request batch run over all three and the elapsed times printed, because
  multiplexing is the only thing that differs and it is a number rather than a claim.
* **Gated**: `07`, `11`, `12` and `15` declare `REQUIRES ssl`; `13` guards itself with
  `#ifdef QBM_HTTP_HAS_HTTP3` because "nghttp3 is present" is not a condition the `REQUIRES`
  vocabulary can express, and `15`'s HTTP/3 third does the same.
* [**View HTTP Module Examples &raquo;**](./http/README.md)

### 2. WebSocket Examples (`./ws/`) — 4 programs

* **Focus**: Advanced WebSocket implementations with separated server architectures and real-time communication.
  WebSocket is not a standalone module — it ships inside qbm-http as `qb::http::ws` (header `<qbm/http/ws.h>`), so these
  examples depend on `qbm-http`.
* **Content**: `01-chat-server` and `02-chat-client` are the callback form — a complete chat system demonstrating
  transport extraction (`extractSession`), actor-based responsibility separation, a message-dispatch map, and both a
  web-browser UI and a command-line client. `03-coro-session` and `04-coro-client` are the **coroutine** form of the
  same two halves: `coro_session<Self, Server>` / `coro_client`, where the position in the conversation is the
  coroutine's own control flow instead of a bag of member flags.
* **Gated**: all four declare `REQUIRES ssl` — `ws/ws.h` `#error`s without OpenSSL, because the handshake needs
  SHA-1 and base64.
* [**View WebSocket Examples &raquo;**](./ws/README.md)

### 3. PostgreSQL Module (`./pgsql/`) — 10 programs

* **Focus**: Asynchronous interaction with PostgreSQL databases.
* **Content**: `01-connect-and-query`, `02-parameters` (prepared statements with explicit parameter OIDs),
  `03-transactions` (manual BEGIN/COMMIT, `with_transaction`, and **savepoints**), `04-types` (the OID map end to
  end), `05-errors`, `06-typed-rows` (a row as a `std::tuple`, `one<>()`), `07-listen-notify` (PostgreSQL as an
  event bus, with a `notify_co_consumer` you `co_await`), `08-tls-and-limits` (STARTTLS on the same port),
  `09-callbacks-and-await` — the whole NON-coroutine half of the client, which had zero call sites:
  the fluent `execute`/`then`/`success`/`error` chain, the three `discard_*` no-ops, `prepare_file()`
  and the `Transaction::await()` drain — and `10-streaming-results` (`query_stream()`, a server-side
  cursor that reads a result set larger than memory).
* **Needs a live server**: every one of them. `dev/agent/run-examples.py` reports a SKIP, never a pass, when
  PostgreSQL does not answer.
* [**View PostgreSQL Module Examples &raquo;**](./pgsql/README.md)

### 4. Redis Module (`./redis/`) — 14 programs

* **Focus**: Utilizing Redis for caching, messaging, and data storage within QB actor systems.
* **Content**: `01-connect`, `02-data-types` (string, hash, list, set — the merge target of the two retired
  pre-3.0 programs), `03-coroutines-and-pipelining`, `04-pubsub`, `05-transactions` (`MULTI`/`EXEC`/`DISCARD`/
  `WATCH`), `06-streams` (a consumer group **and** a plain cursor `XREAD`), `07-scripting` (`EVAL`, `EVALSHA`,
  functions), `08-sorted-sets-and-ttl`, `09-reliability` (`RetryPolicy`, `connect_with_retry`, auto-reconnect)
  `10-cache-actor` (the client as an actor member), `11-callbacks-and-consumers` — the CALLBACK half
  of the client, which had zero call sites: every command is generated twice and only the `co_await`
  overload was ever shown, so this one adds the handler-first form, `await()`, `tcp::pipeline` and
  `tcp::cb_consumer` — `12-cardinality-and-bitmaps` (HyperLogLog and bitmaps, the two ways to answer
  "how many distinct?"), `13-geospatial` (`GEOADD`/`GEOSEARCH` and the lossy `GEOPOS` round trip) and
  `14-acl-and-topology` (`ACL DRYRUN` for testable least-privilege rules, plus the one `CLUSTER`
  question a standalone server can answer).
* **Needs a live server**: every one of them, same SKIP rule as pgsql.
* [**View Redis Module Examples &raquo;**](./redis/README.md)

---

Navigate to the respective module's README for detailed instructions and example descriptions.

[**&laquo; Back to Main Examples Overview**](../README.md) 