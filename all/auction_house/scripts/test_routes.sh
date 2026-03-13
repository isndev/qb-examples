#!/usr/bin/env bash
# Test script for Auction House API
# Usage: ./test_routes.sh [host:port]

BASE="${1:-http://localhost:8080}"
SEP="────────────────────────────────────────────"
CURL="curl -s --max-time 5 --no-location"

pass=0
fail=0

check() {
    local label="$1"
    local expected_code="$2"
    local actual_code="$3"
    local body="$4"
    local extra="$5"

    if [ "$actual_code" = "$expected_code" ]; then
        echo "  ✓  [$actual_code] $label${extra:+  ($extra)}"
        ((pass++))
    else
        echo "  ✗  [$actual_code != $expected_code] $label"
        [ -n "$body" ] && echo "     body: $body"
        ((fail++))
    fi
}

echo ""
echo "╔══════════════════════════════════════════════════════════╗"
echo "║       AUCTION HOUSE - API Route Testing                  ║"
echo "╚══════════════════════════════════════════════════════════╝"
echo ""
echo "Target: $BASE"
echo "$SEP"

# ── GET /health ──────────────────────────────────────────────────────────────
echo ""
echo "▶ GET /health"
r=$($CURL -i "$BASE/health")
code=$(echo "$r" | grep -m1 "^HTTP" | awk '{print $2}')
body=$(echo "$r" | tail -1)
check "/health" "200" "$code" "$body"
echo "     Response: $body"

# ── GET / (redirect) ─────────────────────────────────────────────────────────
echo ""
echo "▶ GET / → redirect to static"
r=$($CURL -i "$BASE/")
code=$(echo "$r" | grep -m1 "^HTTP" | awk '{print $2}')
location=$(echo "$r" | grep -i "^Location:" | tr -d '\r')
check "/ redirect" "302" "$code" ""
echo "     Location: $location"

# ── GET /static/index.html ───────────────────────────────────────────────────
echo ""
echo "▶ GET /static/index.html"
r=$($CURL -i "$BASE/static/index.html")
code=$(echo "$r" | grep -m1 "^HTTP" | awk '{print $2}')
check "/static/index.html" "200" "$code" ""
echo "     Content-Type: $(echo "$r" | grep -i "^Content-Type:" | awk '{print $2}' | tr -d '\r')"

# ── GET /api/users ───────────────────────────────────────────────────────────
echo ""
echo "▶ GET /api/users  [list all users]"
r=$($CURL -i "$BASE/api/users")
code=$(echo "$r" | grep -m1 "^HTTP" | awk '{print $2}')
body=$(echo "$r" | tail -1)
check "/api/users" "200" "$code" "$body"
user_count=$(echo "$body" | python3 -c 'import sys,json; d=json.load(sys.stdin); print(d.get("total",0))' 2>/dev/null || echo "0")
echo "     Users: $user_count"

# ── GET /api/lots ───────────────────────────────────────────────────────────
echo ""
echo "▶ GET /api/lots  [list all active lots]"
r=$($CURL -i "$BASE/api/lots")
code=$(echo "$r" | grep -m1 "^HTTP" | awk '{print $2}')
body=$(echo "$r" | tail -1)
cache=$(echo "$r" | grep -i "^X-Cache:" | awk '{print $2}' | tr -d '\r')
check "/api/lots" "200" "$code" "$body" "X-Cache: $cache"
lot_count=$(echo "$body" | python3 -c 'import sys,json; print(len(json.load(sys.stdin).get("lots",[])))' 2>/dev/null || echo "0")
echo "     Active lots: $lot_count"

# Fetch first active lot id dynamically for subsequent tests
FIRST_LOT_ID=$(echo "$body" | python3 -c '
import sys,json
lots = json.load(sys.stdin).get("lots",[])
print(lots[0]["id"] if lots else 0)
' 2>/dev/null || echo "0")

# ── GET /api/lots/:id ────────────────────────────────────────────────────────
echo ""
echo "▶ GET /api/lots/$FIRST_LOT_ID  [get first active lot]"
r=$($CURL -i "$BASE/api/lots/$FIRST_LOT_ID")
code=$(echo "$r" | grep -m1 "^HTTP" | awk '{print $2}')
body=$(echo "$r" | tail -1)
check "/api/lots/$FIRST_LOT_ID" "200" "$code" "$body"
lot_title=$(echo "$body" | python3 -c 'import sys,json; print(json.load(sys.stdin).get("title","?"))' 2>/dev/null || echo "?")
echo "     Lot title: $lot_title"

# ── GET /api/lots/:id/bids ───────────────────────────────────────────────────
echo ""
echo "▶ GET /api/lots/$FIRST_LOT_ID/bids  [get bid history]"
r=$($CURL -i "$BASE/api/lots/$FIRST_LOT_ID/bids")
code=$(echo "$r" | grep -m1 "^HTTP" | awk '{print $2}')
body=$(echo "$r" | tail -1)
check "/api/lots/$FIRST_LOT_ID/bids" "200" "$code" "$body"
bid_count=$(echo "$body" | python3 -c 'import sys,json; print(json.load(sys.stdin).get("total_bids",0))' 2>/dev/null || echo "0")
echo "     Bid count: $bid_count"

# ── POST /api/lots/:id/bids ──────────────────────────────────────────────────
echo ""
echo "▶ POST /api/lots/$FIRST_LOT_ID/bids  [place a bid - 201 expected]"
# Get current price to bid above it
current_price=$(echo "$body" | python3 -c 'import sys,json; print(json.load(sys.stdin).get("current_price",0))' 2>/dev/null || echo "0")
bid_amount=$(python3 -c "print(int(float('$current_price') * 1.1) + 100)" 2>/dev/null || echo "1000")

r=$($CURL -i -X POST "$BASE/api/lots/$FIRST_LOT_ID/bids" \
    -H "Content-Type: application/json" \
    -d "{\"bidder_id\":2,\"amount\":$bid_amount}")
code=$(echo "$r" | grep -m1 "^HTTP" | awk '{print $2}')
body=$(echo "$r" | tail -1)
if [ "$code" = "201" ] || [ "$code" = "409" ]; then
    check "POST /api/lots/$FIRST_LOT_ID/bids" "$code" "$code" "$body"
else
    check "POST /api/lots/$FIRST_LOT_ID/bids (201 or 409)" "201" "$code" "$body"
fi
bid_id=$(echo "$body" | python3 -c "import sys,json; print(json.load(sys.stdin).get('bid_id','N/A'))" 2>/dev/null || echo "N/A")
echo "     bid_id=$bid_id  amount=\$$bid_amount  status=$code"

# ── GET /api/users/:id ──────────────────────────────────────────────────────
echo ""
echo "▶ GET /api/users/1  [get user info]"
r=$($CURL -i "$BASE/api/users/1")
code=$(echo "$r" | grep -m1 "^HTTP" | awk '{print $2}')
body=$(echo "$r" | tail -1)
check "/api/users/1" "200" "$code" "$body"
username=$(echo "$body" | python3 -c 'import sys,json; print(json.load(sys.stdin).get("username","?"))' 2>/dev/null || echo "?")
echo "     Username: $username"

# ── GET /api/users/:id/stats ─────────────────────────────────────────────────
echo ""
echo "▶ GET /api/users/2/stats  [get user stats]"
r=$($CURL -i "$BASE/api/users/2/stats")
code=$(echo "$r" | grep -m1 "^HTTP" | awk '{print $2}')
body=$(echo "$r" | tail -1)
check "/api/users/2/stats" "200" "$code" "$body"
stats=$(echo "$body" | python3 -c "import sys,json; d=json.load(sys.stdin); print('bids={}, active={}, won={}'.format(d.get('total_bids',0), d.get('active_auctions',0), d.get('auctions_won',0)))" 2>/dev/null || echo "parse error")
echo "     Stats: $stats"

# ── GET /api/lots (second call = cache HIT) ──────────────────────────────────
echo ""
echo "▶ GET /api/lots  [second call → cache HIT]"
r=$($CURL -i "$BASE/api/lots")
code=$(echo "$r" | grep -m1 "^HTTP" | awk '{print $2}')
body=$(echo "$r" | tail -1)
cache=$(echo "$r" | grep -i "^X-Cache:" | awk '{print $2}' | tr -d '\r')
check "/api/lots (cached)" "200" "$code" "$body" "X-Cache: $cache"

# ── 404 unknown route ─────────────────────────────────────────────────────────
echo ""
echo "▶ GET /api/nonexistent  [expect 404]"
r=$($CURL -i "$BASE/api/nonexistent")
code=$(echo "$r" | grep -m1 "^HTTP" | awk '{print $2}')
check "/api/nonexistent" "404" "$code" ""

# ── GET /api/lots/99999 ──────────────────────────────────────────────────────
echo ""
echo "▶ GET /api/lots/99999  [non-existent lot → expect 404]"
r=$($CURL -i "$BASE/api/lots/99999")
code=$(echo "$r" | grep -m1 "^HTTP" | awk '{print $2}')
check "/api/lots/99999" "404" "$code" ""

# ── WebSocket upgrade ─────────────────────────────────────────────────────────
echo ""
echo "▶ GET /ws  [WebSocket upgrade → expect 101]"
r=$($CURL --max-time 2 -i "$BASE/ws" \
    -H "Upgrade: websocket" \
    -H "Connection: Upgrade" \
    -H "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==" \
    -H "Sec-WebSocket-Version: 13" 2>&1)
code=$(echo "$r" | grep -m1 "^HTTP" | awk '{print $2}')
echo "     $(echo "$r" | grep -E "^HTTP|^Upgrade:|^Connection:" | tr '\n' '  ')"
check "/ws upgrade" "101" "$code" ""

# ── Summary ───────────────────────────────────────────────────────────────────
echo ""
echo "$SEP"
echo ""
total=$((pass + fail))
if [ $fail -eq 0 ]; then
    echo "  ✅ All $total tests passed"
else
    echo "  ⚠ Results: $pass passed | $fail failed"
fi
echo ""
echo "$SEP"
echo ""
