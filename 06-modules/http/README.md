# QB HTTP Framework Examples

This directory contains a collection of examples demonstrating various features of the `qb::http` module.

## Building the Examples

Build from the **superproject root**, which force-enables `QB_BUILD_EXAMPLES`:

```bash
cmake --preset release
cmake --build --preset release --target qb-example-modules-http-hello-server
```

Three of the fourteen declare `REQUIRES ssl` (`07-auth-jwt`, `11-https`, `12-http2`) and are **not
created at all** in an SSL-off build. `13-http3` is deliberately not gated that way — "nghttp3 is
present" is not something the `REQUIRES` vocabulary (`ssl` / `quic` / `compression`) can express, so
it guards itself with `#ifdef QBM_HTTP_HAS_HTTP3`.

## Running the Examples

Once built, each executable sits in its own example output directory
(`build/presets/<preset>/examples/06-modules/http/`). Run them directly:

```bash
./build/presets/release/examples/06-modules/http/qb-example-modules-http-hello-server
```

The examples that serve files — `qb-example-modules-http-static-files`, `qb-example-modules-http-https`, `qb-example-modules-http-http2` —
load their assets with a plain `resources/...` path. The build stages a copy of `resources/`
(static site, HTTP/2 site, and the self-signed dev TLS certificate under `resources/ssl/`)
next to the executable, and the examples resolve it relative to the executable's own
location (`qb::io::sys::resolve_resource`). So they run from **any** working directory —
double-click, debugger, CI, or a shell anywhere — with no `cd` and no environment setup:

```bash
build/presets/<preset>/examples/06-modules/http/qb-example-modules-http-static-files   # works regardless of the current directory
```

## Example Descriptions

Below is a list of the available examples and the key features they showcase:

### 1. `01-hello-server.cpp`

* **Description**: A minimal HTTP server that responds with "Hello, World!"
* **Features**:
    * Basic `qb::Actor` setup for a server.
    * Inheriting from `qb::http::Server<>`.
    * Simple GET route (`router().get(...)`) using a lambda handler.
    * Sending plain text and JSON responses.
    * Setting response status and content type.
* **Endpoints**:
    * `GET /`: `text/plain` — `"Hello, World!\nWelcome to QB HTTP Framework!"` (`01-hello-server.cpp:49-54`).
    * `GET /hello`: `application/json` — `{"message": "Hello from QB!", ...}` (`01-hello-server.cpp:56-61`).

### 2. `02-routing.cpp`

* **Description**: Demonstrates various routing capabilities of the framework.
* **Features**:
    * Static routes (e.g., `/users`).
    * Parameterized routes (e.g., `/users/:id`).
    * Wildcard routes (e.g., `/files/*path`).
    * Handling query parameters — the search route reads the key `q`, i.e. `/search?q=...`
      (`02-routing.cpp:317`).
    * Basic CRUD operations for a `/users` resource (in-memory).
    * Returning JSON responses.
* **Key Endpoints**:
    * `GET /`: API information.
    * `GET /users`, `POST /users`
    * `GET /users/:id`, `PUT /users/:id`, `DELETE /users/:id`
    * `GET /hello/:name`
    * `GET /search`
    * `GET /files/*path`

### 3. `03-controllers.cpp`

* **Description**: Organizes routes and handlers using the `Controller` pattern.
* **Features**:
    * Defining `qb::http::Controller` subclasses (`UserController`, `ProductController`).
    * Grouping related routes within a controller.
    * Controller-specific middleware (`Controller::use(...)`).
    * Using member functions as route handlers via the unified verb API (
      `this->get(path, this, &MyController::method)`).
    * Mounting controllers onto the router (`router().controller<C>(...)`).
* **Key Endpoints** (the controllers are mounted at `/api/users` and `/api/products` — there is no `/v1` segment,
  `03-controllers.cpp:471-472`):
    * `GET /api/users`, `POST /api/users`, etc. (CRUD for users)
    * `GET /api/products`, `POST /api/products`, etc. (CRUD for products)
    * `GET /`: API information.

### 4. `04-middleware.cpp`

* **Description**: Showcases the use of middleware for request processing.
* **Features**:
    * Global middleware (e.g., request logger, timing).
    * Group-specific middleware (`RouteGroup::use(...)`).
    * Simulated authentication middleware (checking for a token).
    * Simulated rate-limiting middleware.
    * Middleware modifying request/response or short-circuiting.
    * Passing data between middleware via `Context::set/get`.
* **Key Endpoints**:
    * `GET /`: Basic response.
    * `GET /public`: Publicly accessible.
    * `GET /api/profile`, `GET /api/data`: the protected group is mounted at **`/api`**
      (`04-middleware.cpp:136`). The gate is a request **header**, not a context value:
      `Authorization: Bearer secret-token-123` (`:129-162`). Missing/malformed header → 401; wrong token → 403. On
      success the middleware *writes* `user_id` / `user_name` into the context (`:155-156`) for the handlers to read.
    * `GET /limited/`: Demonstrates rate limiting concept — the group is mounted at `/limited` (`:169`).
    * Reaching a protected route: `curl -H 'Authorization: Bearer secret-token-123' http://localhost:8080/api/profile`

### 5. `05-rest-api-json.cpp`

* **Description**: A more complete REST API example using JSON for a "Book" resource.
* **Features**:
    * Full CRUD operations for books.
    * JSON request body parsing and response serialization.
    * Using `qb::json` (nlohmann::json).
    * Standard middleware stack, installed in this order (`05-rest-api-json.cpp:116-163`): CORS, Compression,
      SecurityHeaders, Logging, RateLimit. There is no timing middleware in this example.
    * Custom error handling middleware (`ErrorHandlingMiddleware`).
    * Search/filter functionality.
* **Key Endpoints**:
    * `GET /api/v1/books`, `POST /api/v1/books`
    * `GET /api/v1/books/:id`, `PUT /api/v1/books/:id`, `DELETE /api/v1/books/:id`, `PATCH /api/v1/books/:id`
    * `GET /api/v1/books/search`
    * `GET /api/v1/stats`
    * `GET /health`

### 6. `06-validation.cpp`

* **Description**: The `qb::http::validation` namespace. **Rewritten**: the previous version was
  1008 lines, included five `validation/` headers, and contained the string `validation::`
  **zero** times — every check was a hand-written `if (!json.contains(...))` and two comments
  described a middleware that was never installed. It is the defect
  `dev/agent/check-example-headers.py` exists to make impossible.
* **Features**:
    * `SchemaValidator` — one JSON schema against one `qb::json` value.
    * `ParameterValidator` + `ParameterRuleSet` — named, TYPED parameters with defaults, stacked
      rules and an optional strict mode that rejects anything undeclared.
    * `Sanitizer` + `PredefinedSanitizers` — in-place rewrites (`trim`, `escape_html`,
      `normalize_whitespace`), with the `a.b`, `a[i]` and `a[*]` path grammar.
    * `RequestValidator` — body schema, query, header and path params, plus per-field sanitizers,
      in one call against a whole `qb::http::Request`.
    * `qb::http::validation_middleware<Session>(...)` handed to `router().use(...)`.
    * `Result` / `Error` — the out-parameter and the `{field_path, rule_violated, message,
      offending_value}` shape you read failures out of.
* **How it is checked**: a self-check runs BEFORE the server binds and prints a gated verdict for
  each of the four pieces, so the run is red if any of them stops behaving as documented. The
  server then serves the same validator as middleware, for a human with `curl`.
* **Two things it measures that are easy to get wrong**: there are no rule factory functions (it
  is `std::make_shared<MinimumRule>(18)`), and a validator carrying `for_path_param` **fails**
  when no `PathParameters` is supplied — it is not silently skipped.
* **Key Endpoints**:
    * `POST /api/users` (body schema + query `page` + header `X-Api-Version`, with sanitizers)
    * `GET /api/users/:id` (path parameter, typed and range-checked)

### 7. `07-auth-jwt.cpp`

* **Description**: Implements JWT-based authentication and role-based authorization.
* **Features**:
    * `qb::http::auth::Manager` for token generation and verification.
    * `qb::http::AuthMiddleware` for protecting routes.
    * Login (`/auth/login`) and registration (`/auth/register`) endpoints.
    * Storing user data in `Context` after successful authentication.
    * Role-based access control (e.g., admin-only routes) via nested groups (`07-auth-jwt.cpp:226`, `:237`, `:256`).
    * Token refresh mechanism (conceptual).
    * Secure password handling (conceptual, uses plain text for demo simplicity).
* **Key Endpoints** (the authenticated group is `/api` — there is no `/v1` segment, `07-auth-jwt.cpp:226`):
    * `POST /auth/login`, `POST /auth/register`
    * `GET /api/profile`, `PUT /api/profile` (requires auth — `:223-225`)
    * `POST /api/auth/logout`, `POST /api/auth/refresh` (requires auth — `:265`, `:268`)
    * `GET /api/admin/users`, `PUT /api/admin/users/:username/status` (requires admin role — `:242-244`)
    * `GET /api/manager/reports` (requires manager or admin role — `:262`)

### 8. `08-static-files.cpp`

* **Description**: Demonstrates serving static files and handling file uploads.
* **Features**:
    * `qb::http::StaticFilesMiddleware` for serving files from a directory (`./resources/static`).
    * Serving uploaded files from a separate directory (`./uploads`).
    * Directory browsing (`/browse`).
    * File upload API (`POST /api/upload`) handling `multipart/form-data`.
    * API for listing, retrieving metadata, and deleting files.
    * MIME type detection, ETag, Last-Modified headers.
    * Interaction with static HTML/JS/CSS frontend (`index.html`, `upload.html`, etc. in `resources/static`).
* **Key Endpoints**:
    * `GET /static/*path`: Serves files from `resources/static`.
    * `GET /uploads/*path`: Serves files from `uploads` directory (created by example).
    * `GET /browse`, `GET /browse/*path`: Directory listing for uploads.
    * `GET /api/files`, `GET /api/files/:filename`, `DELETE /api/files/:filename`
    * `POST /api/upload`
    * `PUT /api/files/:filename/metadata`

### 9. `09-coroutine-handlers.cpp`

> This section absorbed the pre-3.0 `06_async_handlers.cpp`, which has been retired. Its subject
> was response TIMING and it reported 0 ms for a 1.2 s handler, because `next()` returns at the
> first suspension — the timing middleware in `04-middleware.cpp` measures it correctly, and the
> coroutine-handler material is here.

* **Description**: The coroutine routing API — a route handler may simply return `qb::io::async::task<void>` and
  `co_await` its asynchronous work. The router auto-detects a coroutine handler; no wrapper and no callbacks.
* **Features**:
    * Coroutine lambda handler awaiting a timer: `co_await qb::io::async::sleep(...)` (`09-coroutine-handlers.cpp:59-64`).
    * Coroutine HTTP **client** inside a handler: `co_await qb::http::GET(...)` to relay an upstream response
      (`:61-67`, header `<qbm/http/coro.h>`).
    * Two upstreams fetched **in parallel** with `co_await qb::io::async::when_all(...)`, then combined (`:70-77`).
    * A coroutine bound to a **member function** through the unified verb API,
      `router().get("/member", this, &CoroutineServer::handle_member)` (`:80`, `:94-99`).
    * Typed path parameters via `ctx->path_param_or<int>("ms", 100)` (`:51`), and the `ctx->json(...)` /
      `ctx->text(...)` response shorthands.
* **Key Endpoints** (all on `http://localhost:8080`, `:83`):
    * `GET /delay/:ms`: sleeps `ms` milliseconds, then replies `{"slept_ms": …, "handler": "coroutine"}`.
    * `GET /hello`: plain synchronous handler; it is the upstream target the two routes below call.
    * `GET /proxy`: fetches `/hello` from itself and relays status + body.
    * `GET /aggregate`: fetches `/hello` and `/delay/50` concurrently, reports both status codes.
    * `GET /member`: member-function coroutine handler.

### 10. `10-client.cpp`

* **Description**: The **persistent** HTTP/1.1 client, measured against an upstream this program
  hosts itself. **Rewritten**: it used to make three one-shot calls to the public `httpbin.org`,
  which meant it could not run offline and — measured — printed "HTTP Client demo completed!" and
  exited 0 on a run in which all three requests came back **503**. Meanwhile `make_client` had
  zero occurrences anywhere in the corpus.
* **Features**:
    * `qb::http1::make_client` / `qb::http1::Client`, the coroutine `connect()` and its
      `ConnectResult`, and the callback form beside it.
    * `push_request` (coroutine and callback), `push_requests` for a batch whose results are
      indexed by the ORIGINAL request position.
    * `set_request_timeout`, `set_auto_reconnect`, `is_connected`, `get_stats`.
    * A `qb::http::use<T>::server<Session>` upstream on the SAME event loop, so the connection
      counts it prints are the server's own observation.
* **The number it exists for**: three sequential requests on the persistent client cost **one**
  connection; the same three through the one-shot `qb::http::GET` cost **three**.
* **Interaction**: none. It binds `127.0.0.1:18081` in-process and needs no network.

### 11. `11-https.cpp`

* **Description**: Sets up an HTTPS server and an HTTP server that redirects to HTTPS.
* **Features**:
    * `qb::http::ssl::Server<>` for HTTPS.
    * Uses the **bundled** self-signed dev certificate — it does not generate one. `_cert_file` /`_key_file` are
      hard-coded to `resources/ssl/cert.pem` and `resources/ssl/key.pem` (`11-https.cpp:48-49`), resolved next
      to the executable with `qb::io::sys::resolve_resource` (`:90-91`). If either file is missing the example prints
      `SSL certificate not found (...)` and refuses to start (`:92-98`, `:146-149`) — there is no `openssl`
      invocation and no `system()` call anywhere in this directory.
    * Configuring the server with certificate and private key files (`listen(uri, cert, key)`, `:156`).
    * Setting up a separate HTTP server on port 8080 that issues 301 redirects to the HTTPS server on port 8443.
    * Security-related headers (HSTS, CSP - conceptual).
* **Servers**:
    * HTTPS server on `https://localhost:8443`
    * HTTP redirect server on `http://localhost:8080`

### 12. `12-http2.cpp`

* **Description**: An HTTP/2 server demonstrating various HTTP/2 features using a static frontend.
* **Features**:
    * Built on the CRTP server form: `class Http2StaticServer : public qb::Actor, public
      qb::http2::use<Http2StaticServer>::server<Http2StaticSession>` (`12-http2.cpp:90-92`).
    * Serves static files from `./resources/http2` to an interactive demo page (`index.html`).
    * ALPN for protocol negotiation (HTTP/2 over TLS).
    * Endpoints to simulate/demonstrate:
        * Request Multiplexing (`/api/multiplexing-demo`)
        * Stream Prioritization (`/api/stream-priority/:level`)
        * Server Push concept (`/api/server-push-demo` - backend provides info, frontend simulates)
        * Performance characteristics (`/api/performance/:iterations`)
    * Uses self-signed certificates (similar to `11-https.cpp`).
* **Server**:
    * HTTP/2 server on `https://localhost:8443`
* **Static Resources**: `examples/06-modules/http/resources/http2/` contains the frontend HTML, JS, CSS for the demo.

### 13. `13-http3.cpp`

* **Description**: A browser-testable **HTTP/3** demo, served as a **dual stack** (HTTP/2 over
  TCP + HTTP/3 over QUIC on the same port) so a browser can actually reach it. Same routing
  engine, static-files middleware, and interactive frontend as the HTTP/2 example.
* **Why dual-stack**: browsers never speak HTTP/3 first — they connect over TCP (HTTP/2 or
  HTTP/1.1), read an `Alt-Svc: h3=...` header, and only then upgrade to HTTP/3. A pure-h3
  (UDP-only) server therefore can't even be *loaded* by a browser. This example uses
  `qb::http::dual_stack_server`: HTTP/2 (TCP/TLS, ALPN `h2` + `http/1.1`) and HTTP/3 (QUIC/UDP,
  ALPN `h3`) with one set of routes mirrored onto both, and the h2 responses advertise
  `Alt-Svc` — exactly how HTTP/3 is deployed in production.
* **Features**:
    * `qb::http::dual_stack_server` owned by a `qb::Actor`; middleware installed on both routers.
    * **Interactive demo frontend** from `resources/http3/` (`index.html` + `static_files_middleware`
      for `/static/*`). The page live-fetches the API, shows the **negotiated protocol** (via
      `performance.nextHopProtocol`), and fires a concurrent burst to illustrate independent streams.
    * JSON API: `/api/h3-features`, `/api/transport-info`, `/api/connection-info`,
      `/api/no-hol-blocking`, `/api/stream-demo/:count`, `POST /api/echo`.
    * Graceful shutdown (HTTP/3 GOAWAY + HTTP/2 close) on teardown; self-signed dev cert under
      `resources/ssl/`.
* **Server**: `https://127.0.0.1:8444` — HTTP/2 on TCP **and** HTTP/3 on UDP.
* **Browser testing** (no special flags):
    1. Open **`https://127.0.0.1:8444/`** and accept the self-signed-certificate warning once.
    2. The page loads over HTTP/2, then upgrades to HTTP/3 via `Alt-Svc`.
    3. DevTools → Network → the *Protocol* column flips to `h3` on the API calls (Chrome may need
       one reload). The pill at the top of the page shows the negotiated protocol live.
    * Use `127.0.0.1`, not `localhost` (the server binds IPv4; `localhost` may resolve to IPv6 `::1`).
    * CLI check of each stack: `curl -k --http2 https://127.0.0.1:8444/api/h3-features` (TCP/h2),
      and an HTTP/3-capable curl for the QUIC side.
* **Requires**: qbm-http built with HTTP/3 (SSL + QUIC + nghttp3, i.e. `QBM_HTTP_HAS_HTTP3`);
  otherwise the example prints how to enable it and exits.
* **Static Resources**: `examples/06-modules/http/resources/http3/` — the frontend HTML/CSS/JS.

---

See the individual `.cpp` files for detailed code and comments.
The `resources` directory contains static assets used by some examples.

---

### 14. Streaming and Cookies (`14-streaming-and-cookies.cpp`) — **new**

* **Purpose**: the message-level surface the other thirteen never touch. Every other HTTP example here
  is about ROUTING or about the CONNECTION; these six subjects are about what is in the bytes, and each
  of them had zero demonstrators.
* **API**: `qb::http::Chunk` + `Body::add_chunk`/`add_final_chunk`; `qb::http::Cookie` /`CookieJar` /
  `SameSite` / `parse_set_cookie` / `Response::add_cookie` / `Request::cookie_value`;
  `qb::http::Form` (a MULTI-map: `get()` returns a vector, `get_first()` is the common case);
  `qb::http::Multipart` BUILT with `create_part()`; `Body::compress`/`uncompress`;
  `qb::http::date::format_http_date` / `parse_http_date` driving a conditional GET to a 304.
* **The two halves of chunked, which are easy to conflate**: setting `Transfer-Encoding: chunked` makes
  the SERIALISER emit the chunked wire form for whatever body you assigned — neither side's `body()`
  ever holds chunk headers. `Chunk` + `add_chunk` is the other half: it appends the chunk-encoded BYTES
  yourself. Doing both encodes twice.
* **One measured trap, and it is the reason the body below is assigned PLAIN**: the one-shot verbs
  (`1.1/http.h:726-731`) and `qb::http1::Client` (`1.1/client.cpp:122`) COMPRESS the body themselves
  when `Content-Encoding` is set. Calling `compress()` as well encodes twice — measured, 9000 bytes →
  108 gzip → 109 on the wire, and the peer's single `uncompress()` hands back 108 bytes of gzip that
  look like a corrupt payload. There is no diagnostic.
* **Run**: `./build/presets/release/examples/06-modules/http/qb-example-modules-http-streaming-and-cookies`
