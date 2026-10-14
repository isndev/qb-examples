#!/usr/bin/env bash
# Fail-closed smoke test for the TaskManager HTTP API.
# Usage: ./test_routes.sh [http://host:port]

set -uo pipefail

BASE="${1:-http://localhost:8080}"
SEP="────────────────────────────────────────────"
CURL=(curl -sS --max-time 5 --no-location)
MIN_CHECKS=31

pass=0
fail=0
response=""
code=""
body=""

record() {
    local ok="$1"
    local label="$2"
    local detail="${3:-}"
    if [ "$ok" -eq 1 ]; then
        echo "  ✓  $label"
        pass=$((pass + 1))
    else
        echo "  ✗  $label${detail:+ — $detail}"
        fail=$((fail + 1))
    fi
}

request() {
    local allow_ws_timeout=0
    if [ "${1:-}" = "--allow-ws-timeout" ]; then
        allow_ws_timeout=1
        shift
    fi
    local rc=0
    response=$("${CURL[@]}" -i "$@" 2>&1) || rc=$?
    code=$(printf '%s\n' "$response" | awk '/^HTTP\// { value=$2 } END { print value }')
    body=$(printf '%s\n' "$response" | awk 'found { print } /^\r?$/ { found=1 }')
    if [ "$rc" -ne 0 ] && ! { [ "$allow_ws_timeout" -eq 1 ] && [ "$rc" -eq 28 ] && [ "$code" = "101" ]; }; then
        record 0 "transport $*" "curl exit $rc"
    fi
}

header() {
    local name="$1"
    printf '%s\n' "$response" | awk -v wanted="$name" '
        index(tolower($0), tolower(wanted) ":") == 1 {
            sub("^[^:]*:[[:space:]]*", ""); sub("\\r$", ""); value=$0
        }
        END { print value }
    '
}

expect_code() {
    local label="$1"
    local expected="$2"
    if [ "$code" = "$expected" ]; then
        record 1 "$label [$code]"
    else
        record 0 "$label" "HTTP ${code:-<none>}, expected $expected; body=$body"
    fi
}

expect_header() {
    local label="$1"
    local name="$2"
    local expected="$3"
    local actual
    actual=$(header "$name")
    if [ "$actual" = "$expected" ]; then
        record 1 "$label [$name: $actual]"
    else
        record 0 "$label" "$name=${actual:-<missing>}, expected $expected"
    fi
}

expect_json() {
    local label="$1"
    local predicate="$2"
    local rc=0
    printf '%s' "$body" | python3 -c '
import json, sys
document = json.load(sys.stdin)
scope = {"__builtins__": {"type": type, "int": int, "list": list, "any": any}, "d": document}
if not eval(sys.argv[1], scope, {}):
    raise SystemExit(1)
' "$predicate" >/dev/null 2>&1 || rc=$?
    if [ "$rc" -eq 0 ]; then
        record 1 "$label"
    else
        record 0 "$label" "JSON assertion failed; body=$body"
    fi
}

echo ""
echo "Target: $BASE"
echo "$SEP"

request "$BASE/health"
expect_code "GET /health" 200
expect_json "/health readiness JSON" 'd.get("status") == "ok" and d.get("db") is True and d.get("redis") is True'

request "$BASE/"
expect_code "GET / redirect" 302
expect_header "GET / redirect target" Location "/static/index.html"

request "$BASE/static/index.html"
expect_code "GET /static/index.html" 200

request "$BASE/tasks"
expect_code "GET /tasks first" 200
expect_header "GET /tasks first cache" X-Cache MISS
expect_json "GET /tasks first JSON" 'type(d.get("tasks")) is list and type(d.get("total")) is int'

request "$BASE/tasks"
expect_code "GET /tasks second" 200
expect_header "GET /tasks second cache" X-Cache HIT
expect_json "GET /tasks second JSON" 'type(d.get("tasks")) is list and type(d.get("total")) is int'

request -X POST "$BASE/tasks" -H "Content-Type: application/json" \
    -d '{"title":"Test Task","description":"original","status":"pending"}'
expect_code "POST /tasks" 201
expect_json "POST /tasks response" 'type(d.get("id")) is int and d.get("id") > 0 and d.get("status") == "created"'
TASK_ID=$(printf '%s' "$body" | python3 -c 'import json,sys; value=json.load(sys.stdin).get("id"); print(value if type(value) is int and value > 0 else "")' 2>/dev/null || true)
if [ -z "$TASK_ID" ]; then
    record 0 "POST /tasks produced a usable ID" "body=$body"
    TASK_ID=0
else
    record 1 "POST /tasks produced ID $TASK_ID"
fi

request "$BASE/tasks"
expect_code "GET /tasks after POST" 200
expect_header "GET /tasks after POST cache invalidation" X-Cache MISS
expect_json "GET /tasks contains created task" 'any(t.get("id") == int("'$TASK_ID'") and t.get("description") == "original" for t in d.get("tasks", []))'

request "$BASE/tasks/$TASK_ID"
expect_code "GET /tasks/:id" 200
expect_json "GET /tasks/:id initial fields" 'd.get("id") == int("'$TASK_ID'") and d.get("description") == "original" and d.get("status") == "pending"'

# Omitted description preserves the existing text.
request -X PUT "$BASE/tasks/$TASK_ID" -H "Content-Type: application/json" \
    -d '{"status":"in_progress"}'
expect_code "PUT /tasks/:id without description" 200
expect_json "PUT /tasks/:id response" 'd.get("id") == int("'$TASK_ID'") and d.get("status") == "updated"'

request "$BASE/tasks/$TASK_ID"
expect_code "GET after omitted description" 200
expect_json "omitted description is preserved" 'd.get("description") == "original" and d.get("status") == "in_progress"'

# A present empty description is data and clears the column.
request -X PUT "$BASE/tasks/$TASK_ID" -H "Content-Type: application/json" \
    -d '{"description":""}'
expect_code "PUT /tasks/:id empty description" 200

request "$BASE/tasks/$TASK_ID"
expect_code "GET after empty description" 200
expect_json "empty description is stored" 'd.get("description") == "" and d.get("status") == "in_progress"'

request -X DELETE "$BASE/tasks/$TASK_ID"
expect_code "DELETE /tasks/:id" 200
expect_json "DELETE /tasks/:id response" 'd.get("id") == int("'$TASK_ID'") and d.get("status") == "deleted"'

request "$BASE/tasks/$TASK_ID"
expect_code "GET deleted task" 404

request "$BASE/nonexistent"
expect_code "GET /nonexistent" 404

# curl times out after receiving 101 because the upgraded connection remains open.
request --allow-ws-timeout --max-time 2 "$BASE/ws" \
    -H "Upgrade: websocket" \
    -H "Connection: Upgrade" \
    -H "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==" \
    -H "Sec-WebSocket-Version: 13"
expect_code "GET /ws upgrade" 101

echo "$SEP"
total=$((pass + fail))
echo "  Results: $pass/$total passed  |  $fail failed"

if [ "$total" -lt "$MIN_CHECKS" ]; then
    echo "  ✗  only $total checks ran; floor is $MIN_CHECKS"
    exit 1
fi
if [ "$fail" -ne 0 ]; then
    exit 1
fi
exit 0
