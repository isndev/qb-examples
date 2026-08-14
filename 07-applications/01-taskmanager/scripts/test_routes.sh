#!/usr/bin/env bash
# Test script for taskmanager_ultimate routes
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
    local cache="$5"

    if [ "$actual_code" = "$expected_code" ]; then
        echo "  ✓  [$actual_code] $label${cache:+  (X-Cache: $cache)}"
        ((pass++))
    else
        echo "  ✗  [$actual_code != $expected_code] $label"
        echo "     body: $body"
        ((fail++))
    fi
}

echo ""
echo "Target: $BASE"
echo "$SEP"

# ── GET /health ──────────────────────────────────────────────────────────────
echo "GET /health"
r=$($CURL -i "$BASE/health")
code=$(echo "$r" | grep -m1 "^HTTP" | awk '{print $2}')
body=$(echo "$r" | tail -1)
check "/health" "200" "$code" "$body"
echo "     $body"
echo ""

# ── GET / (redirect) ─────────────────────────────────────────────────────────
echo "GET / → redirect"
r=$($CURL -i "$BASE/")
code=$(echo "$r" | grep -m1 "^HTTP" | awk '{print $2}')
location=$(echo "$r" | grep -i "^Location:" | tr -d '\r')
echo "     $location"
check "/ redirect" "302" "$code" ""
echo ""
# Note: if 301 is used instead of 302, update expected above

# ── GET /static/index.html ───────────────────────────────────────────────────
echo "GET /static/index.html"
r=$($CURL -i "$BASE/static/index.html")
code=$(echo "$r" | grep -m1 "^HTTP" | awk '{print $2}')
check "/static/index.html" "200" "$code" ""
echo ""

# ── GET /tasks (first hit → MISS) ────────────────────────────────────────────
echo "GET /tasks  [1st call → expect X-Cache: MISS]"
r=$($CURL -i "$BASE/tasks")
code=$(echo "$r" | grep -m1 "^HTTP" | awk '{print $2}')
body=$(echo "$r" | tail -1)
cache=$(echo "$r" | grep -i "^X-Cache:" | awk '{print $2}' | tr -d '\r')
check "/tasks (1st)" "200" "$code" "$body" "$cache"
echo "     tasks count: $(echo "$body" | python3 -c 'import sys,json; d=json.load(sys.stdin); print(len(d.get("tasks",[])), "tasks, total=", d.get("total","?"))' 2>/dev/null || echo "parse error: $body")"
echo ""

# ── GET /tasks (second hit → HIT) ────────────────────────────────────────────
echo "GET /tasks  [2nd call → expect X-Cache: HIT]"
r=$($CURL -i "$BASE/tasks")
code=$(echo "$r" | grep -m1 "^HTTP" | awk '{print $2}')
body=$(echo "$r" | tail -1)
cache=$(echo "$r" | grep -i "^X-Cache:" | awk '{print $2}' | tr -d '\r')
check "/tasks (2nd)" "200" "$code" "$body" "$cache"
echo "     tasks count: $(echo "$body" | python3 -c 'import sys,json; d=json.load(sys.stdin); print(len(d.get("tasks",[])), "tasks, total=", d.get("total","?"))' 2>/dev/null || echo "parse error: $body")"
echo ""

# ── POST /tasks ───────────────────────────────────────────────────────────────
echo "POST /tasks  [create]"
r=$($CURL -i -X POST "$BASE/tasks" \
    -H "Content-Type: application/json" \
    -d '{"title":"Test Task","description":"From test script","status":"pending"}')
code=$(echo "$r" | grep -m1 "^HTTP" | awk '{print $2}')
body=$(echo "$r" | tail -1)
check "POST /tasks" "201" "$code" "$body"
TASK_ID=$(echo "$body" | python3 -c 'import sys,json; print(json.load(sys.stdin).get("id",""))' 2>/dev/null)
echo "     created id: ${TASK_ID:-PARSE_FAILED   body=$body}"
echo ""

# ── GET /tasks (after create → cache MISS, invalidated) ──────────────────────
echo "GET /tasks  [after POST → cache should be MISS (invalidated)]"
r=$($CURL -i "$BASE/tasks")
code=$(echo "$r" | grep -m1 "^HTTP" | awk '{print $2}')
body=$(echo "$r" | tail -1)
cache=$(echo "$r" | grep -i "^X-Cache:" | awk '{print $2}' | tr -d '\r')
check "/tasks (after POST)" "200" "$code" "$body" "$cache"
echo "     tasks count: $(echo "$body" | python3 -c 'import sys,json; d=json.load(sys.stdin); print(len(d.get("tasks",[])), "tasks, total=", d.get("total","?"))' 2>/dev/null || echo "parse error: $body")"
echo ""

if [ -n "$TASK_ID" ]; then
    # ── GET /tasks/:id ───────────────────────────────────────────────────────
    echo "GET /tasks/$TASK_ID"
    r=$($CURL -i "$BASE/tasks/$TASK_ID")
    code=$(echo "$r" | grep -m1 "^HTTP" | awk '{print $2}')
    body=$(echo "$r" | tail -1)
    check "GET /tasks/:id" "200" "$code" "$body"
    echo "     $body"
    echo ""

    # ── PUT /tasks/:id ───────────────────────────────────────────────────────
    echo "PUT /tasks/$TASK_ID  [update status → in_progress]"
    r=$($CURL -i -X PUT "$BASE/tasks/$TASK_ID" \
        -H "Content-Type: application/json" \
        -d '{"title":"Test Task","description":"Updated","status":"in_progress"}')
    code=$(echo "$r" | grep -m1 "^HTTP" | awk '{print $2}')
    body=$(echo "$r" | tail -1)
    check "PUT /tasks/:id" "200" "$code" "$body"
    echo "     $body"
    echo ""

    # ── GET /tasks/:id  verify update ────────────────────────────────────────
    echo "GET /tasks/$TASK_ID  [verify update]"
    r=$($CURL -i "$BASE/tasks/$TASK_ID")
    code=$(echo "$r" | grep -m1 "^HTTP" | awk '{print $2}')
    body=$(echo "$r" | tail -1)
    check "GET /tasks/:id (after PUT)" "200" "$code" "$body"
    echo "     $body"
    echo ""

    # ── DELETE /tasks/:id ────────────────────────────────────────────────────
    echo "DELETE /tasks/$TASK_ID"
    r=$($CURL -i -X DELETE "$BASE/tasks/$TASK_ID")
    code=$(echo "$r" | grep -m1 "^HTTP" | awk '{print $2}')
    body=$(echo "$r" | tail -1)
    check "DELETE /tasks/:id" "200" "$code" "$body"
    echo "     $body"
    echo ""

    # ── GET /tasks/:id  after delete ─────────────────────────────────────────
    echo "GET /tasks/$TASK_ID  [after delete → expect 404]"
    r=$($CURL -i "$BASE/tasks/$TASK_ID")
    code=$(echo "$r" | grep -m1 "^HTTP" | awk '{print $2}')
    body=$(echo "$r" | tail -1)
    check "GET /tasks/:id (404)" "404" "$code" "$body"
    echo ""
fi

# ── 404 unknown route ─────────────────────────────────────────────────────────
echo "GET /nonexistent  [expect 404]"
r=$($CURL -i "$BASE/nonexistent")
code=$(echo "$r" | grep -m1 "^HTTP" | awk '{print $2}')
check "/nonexistent" "404" "$code" ""
echo ""

# ── WebSocket upgrade ─────────────────────────────────────────────────────────
echo "GET /ws  [WebSocket upgrade → expect 101]"
# curl with max-time 2: gets the 101 then times out (normal)
r=$($CURL --max-time 2 -i "$BASE/ws" \
    -H "Upgrade: websocket" \
    -H "Connection: Upgrade" \
    -H "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==" \
    -H "Sec-WebSocket-Version: 13" 2>&1)
code=$(echo "$r" | grep -m1 "^HTTP" | awk '{print $2}')
echo "     $(echo "$r" | grep -E "^HTTP|^Upgrade:|^Connection:|^Sec-WebSocket" | tr '\n' ' ')"
check "/ws upgrade" "101" "$code" ""
echo ""

# ── Summary ───────────────────────────────────────────────────────────────────
echo "$SEP"
total=$((pass + fail))
echo "  Results: $pass/$total passed  |  $fail failed"
echo ""
