# Auction House - Real-Time Bidding System (coroutine-first)

A full-stack auction application built with the QB Framework, demonstrating real-time
bidding with WebSocket, PostgreSQL, and Redis — written **end-to-end with C++20
coroutines**.

## 🏛️ Features

- **Coroutine everything**: `onInit()` `co_await`s its DB/Redis/WS backends before
  activating (discover-before-activate); every route handler `co_await`s the database
  and Redis directly; the bid path updates the lot and records its bid in one
  database statement, so overlapping handlers cannot share a transaction;
  Redis Pub/Sub is a
  `co_await receive()` loop (`qb::redis::tcp::co_consumer`).
- **Real-Time Bidding**: Instant bid updates via WebSocket broadcast
- **Bid eligibility**: Only active lots whose start time has arrived and end time has
  not passed can receive a bid. Responses and broadcasts use the stored price.
- **Multi-Core Architecture**: TcpListener + 3 AuctionManager workers
- **Cache Strategy**: Redis cache-aside with automatic invalidation
- **Pub/Sub Events**: Redis for real-time client notifications
- **Dark Theme UI**: Modern single-page application

## 🏗️ Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                         ACTOR TOPOLOGY                           │
├─────────────────────────────────────────────────────────────────┤
│                                                                  │
│  Core 0    ┌──────────────────┐                                 │
│            │  TcpListener     │ ← Accept loop (0µs latency)   │
│            └────────┬─────────┘                                 │
│                     │ dispatch                                  │
│                     ▼ round-robin                               │
│  Core 1-3  ┌──────────────────┐    ┌─────────────────────┐      │
│            │ AuctionManager   │◄───│ WebSocketHandler    │      │
│            │ - HTTP API       │    │ - WS sessions       │      │
│            │ - PostgreSQL     │    │ - Redis consumer    │      │
│            │ - Redis cache    │    └─────────────────────┘      │
│            └──────────────────┘                                 │
└─────────────────────────────────────────────────────────────────┘
```

## 🚀 Quick Start

### Prerequisites

- PostgreSQL running with an `auction_house` database
- Redis running on localhost:6379
- QB Framework built

### Setup Database

```bash
# Create database and user
psql -U postgres -c "CREATE DATABASE auction_house;"
psql -U postgres -c "CREATE USER auction_user WITH PASSWORD 'auction_pass';"
psql -U postgres -c "GRANT ALL PRIVILEGES ON DATABASE auction_house TO auction_user;"

# Initialize schema (optional - server can auto-initialize)
psql -U auction_user -d auction_house -f resources/init_db.sql
```

### Build and Run

```bash
# From qb-dev root (examples are enabled per-example as they are ported)
cmake --preset dev
cmake --build build/presets/dev --target qb-example-applications-auction-house -j

# Run (defaults: auction_user / auction_pass / auction_house @ localhost:5432)
./build/presets/dev/examples/07-applications/02-auction-house/qb-example-applications-auction-house

# Or override the DB via env
PG_HOST=localhost PG_PORT=5432 PG_USER=auction_user PG_PASS=auction_pass PG_DB=auction_house \
    ./build/presets/dev/examples/07-applications/02-auction-house/qb-example-applications-auction-house
```

The schema is bootstrapped automatically at startup (a `run_sync` coroutine runs the
idempotent `init_db.sql`), so the DB just needs to exist and the user own its schema.

### Testing API Routes

```bash
# Run all API tests
cd scripts
./test_routes.sh

# Test against different host/port
./test_routes.sh http://localhost:9090
```

From the qb-dev root, with the application stopped and PostgreSQL/Redis running:

```bash
python3 examples/07-applications/02-auction-house/scripts/check_bid_atomicity.py --build-dir build/presets/dev
python3 examples/07-applications/02-auction-house/scripts/check_bid_reply_loss.py --build-dir build/presets/dev
```

Tests cover:

- Health check
- Static file serving
- Lots API (list, get, bids)
- Bids API (place bid)
- Users API (info, stats)
- WebSocket upgrade
- 404 error handling

`check_bid_atomicity.py` starts the built application, opens persistent sessions
on the same and different workers, then holds a temporary PostgreSQL row lock
until both bid requests are logged by HTTP middleware and one update is waiting
on the lock. It reads the database to assert one accepted response, one rejected
response, one bid row and the right price. The gate proves both requests reached
the server while a bid statement was blocked; it cannot inspect whether the second
handler has already entered the PostgreSQL client's internal queue. The script
also verifies rollback after a failed insert and a valid follow-up bid.
It checks cancelled and future-start lots, plus the rounded price returned for a
fractional-cent offer. It deletes its own temporary lots before exiting.
It also checks the required UUID, exact response replay after another bid and
lot expiry, and rejection of a changed payload under the same UUID.

`check_bid_reply_loss.py` forwards PostgreSQL traffic through a local protocol
proxy. It cuts the bound bid before PostgreSQL receives it or withholds the reply
through `ReadyForQuery` after the statement has committed. A third case blocks
the reconciliation connection. Each case checks the database state and retries
twice with the same UUID, once on the original worker and once after restart.
It primes the old-price Redis cache before each fault and verifies the recovered
price after confirmation, including the 503-then-201 case.

### Access

Open browser: http://localhost:8080

## 📡 API Endpoints

### Lots

- `GET /api/lots` - List active auctions
- `GET /api/lots/:id` - Get lot details
- `GET /api/lots/:id/bids` - Get bid history
- `POST /api/lots/:id/bids` - Place a bid. JSON must include `bidder_id`, `amount`, and a
  client-generated UUID `request_id` (for example, `{"bidder_id":2,"amount":5500,"request_id":"550e8400-e29b-41d4-a716-446655440000"}`).
  Generate the UUID once per intended bid and reuse it after a timeout or 503. A missing or
  malformed key returns 400. Repeating the same key and bid returns the original 201 body,
  even after the lot price changes or closes. Reusing it for a different lot, bidder, or
  amount returns 409. A 503 means the database outcome could not be confirmed; retry with
  the same key. The browser retains an unresolved key in local storage. Bid rows and their
  keys are retained with the lot across restarts; the startup cleanup removes only expired
  lots without bids. Explicitly deleting a lot cascades to its bids and ends that replay window.
  The idempotent schema upgrade leaves historical bids with null keys; requests made before
  this contract cannot be reconciled retroactively.

### Users

- `GET /api/users/:id` - Get user info
- `GET /api/users/:id/stats` - Get user stats

### WebSocket

- `GET /ws` - WebSocket upgrade for real-time updates

## 🔌 WebSocket Messages

### Server → Client

```json
{"type": "connected", "message": "Welcome to Auction House!"}
{"type": "lot_update", "action": "bid", "lot_id": 1, "new_price": 5500, "bidder": "alice"}
```

### Client → Server

```json
{"type": "ping"}
{"type": "subscribe_lot", "lot_id": 1}
```

## 📁 Project Structure

```
02-auction-house/
├── CMakeLists.txt
├── README.md
├── resources/
│   ├── init_db.sql           # Database schema (auto-executed via execute_file())
│   └── static/
├── scripts/
│   ├── test_routes.sh        # API smoke checks
│   ├── check_bid_atomicity.py # overlapping bids and database state
│   ├── check_bid_reply_loss.py # before/after commit connection loss
│   └── measure_bid_path.py  # live accepted-bid latency comparison
├── include/auction_house/
│   ├── events.h              # NewConnectionEvent
│   ├── models/
│   │   ├── lot.h             # Lot, LotList, LotEvent
│   │   ├── bid.h             # Bid, BidHistory
│   │   └── user.h            # User, UserStats
│   └── actors/
│       ├── tcp_listener.h    # TCP acceptor
│       ├── http_session.h    # HTTP CRTP wrapper
│       ├── ws_session.h        # WebSocket CRTP wrapper
│       ├── websocket_handler.h # WS pool + Redis consumer
│       └── auction_manager.h   # Main actor
├── src/
│   ├── main.cpp              # Engine setup
│   └── actors/
│       ├── auction_manager.cpp   # HTTP handlers + DB + execute_file()
│       └── websocket_handler.cpp # WS upgrade + broadcast
└── resources/static/
    ├── index.html            # SPA
    ├── style.css             # Dark theme
    └── app.js                # Frontend JS
```

## 🎯 Key Patterns Demonstrated

1. **Coroutine `onInit`**: `co_await` DB + Redis + WS before activating (discover-before-activate)
2. **Coroutine handlers**: `task<void>(ctx)` lambdas passed directly to the router, `co_await`ing the database and Redis
3. **Atomic, replayable bid statement**: an active, started lot update guarded by end time and price feeds the bid insert;
   no transaction spans coroutine suspension. The bid row stores a unique request UUID and the
   accepted response's time-left value. A lost PostgreSQL reply is reconciled by UUID on a fresh
   connection, and a failed reconciliation returns 503 instead of claiming a price conflict.
4. **Coroutine Pub/Sub**: `qb::redis::tcp::co_consumer` retained by the receive loop; after a
   committed message resumes, the actor's cancellation token guards WebSocket broadcast.
5. **Pre-engine bootstrap**: `qb::io::async::run_sync` runs the idempotent `init_db.sql` via coroutine `execute_file()`
6. **Actor Topology**: TcpListener on dedicated core, workers distributed
7. **CRTP Sessions** + **Socket Transfer** (HTTP → WebSocket upgrade via `extractSession()`)
8. **Cache-Aside** with invalidation, and **disconnection handling** (forward to base `disconnected()`)

## 📊 Performance

- **Bid database work**: an accepted new bid still uses one prepared statement and one round trip;
  a repeat adds one indexed lookup by UUID, and a failed reply opens a fresh connection for
  reconciliation. The UUID index and nullable response snapshot add storage per new bid; old
  rows remain valid after the idempotent schema migration. Measure latency on the target host
  before assigning a throughput budget.
- **Scaling**: AuctionManager workers can run on separate cores. Measure latency and connection capacity on the target host before setting a limit.

## 🛠️ Tech Stack

- **Backend**: QB Framework (C++20 coroutines)
- **HTTP**: qbm-http
- **WebSocket**: qbm-http (qb::http::ws)
- **Database**: PostgreSQL (qbm-pgsql)
- **Cache**: Redis (qbm-redis)
- **Frontend**: Vanilla JS, CSS Grid

## 📜 License

Same as QB Framework
