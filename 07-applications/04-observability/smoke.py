#!/usr/bin/env python3
"""Check the running two-core example, optionally through Prometheus and Grafana too."""
import argparse
import base64
import concurrent.futures
import json
import re
import time
import urllib.error
import urllib.parse
import urllib.request


def fetch(url, credentials=None):
    request = urllib.request.Request(url)
    if credentials:
        encoded = base64.b64encode(credentials.encode()).decode()
        request.add_header('Authorization', 'Basic ' + encoded)
    try:
        with urllib.request.urlopen(request, timeout=10) as response:
            return response.status, response.headers, response.read().decode()
    except urllib.error.HTTPError as error:
        return error.code, error.headers, error.read().decode()


def counter(text, name):
    matches = re.findall(r'^' + re.escape(name) + r'(?:\{[^\n]*\})? (\d+)$', text, re.M)
    assert matches, f'missing counter {name}'
    return sum(map(int, matches))


def check_snapshot(text):
    for core in (0, 1):
        assert f'qb_scrape_core_up{{core="{core}"}} 1\n' in text
    assert counter(text, 'qb_metrics_source_failures_total') == 0
    assert 'connection="demo",core="1"' in text
    assert 'qb_redis_command_duration_seconds_sum' not in text


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--app', default='http://127.0.0.1:18080')
    parser.add_argument('--metrics', default='http://127.0.0.1:19090')
    parser.add_argument('--prometheus')
    parser.add_argument('--grafana')
    parser.add_argument('--grafana-auth', default='admin:qb-local-demo')
    parser.add_argument('--latency', action='store_true')
    args = parser.parse_args()
    # The owner must wait for the application's three ready lines before invoking this check.
    status, headers, before = fetch(args.metrics + '/metrics')
    assert status == 200 and headers.get_content_type() == 'text/plain'
    assert 'version=0.0.4' in headers['Content-Type']
    check_snapshot(before)
    for _ in range(3):
        status, _, body = fetch(args.app + '/work')
        assert status == 200 and json.loads(body) == {'pgsql': True, 'redis': True}, body
    assert fetch(args.app + '/absent')[0] == 404
    assert fetch(args.app + '/metrics')[0] == 404, 'metrics leaked onto application listener'
    _, _, after = fetch(args.metrics + '/metrics')
    check_snapshot(after)
    assert counter(after, 'qb_pgsql_queries_total') >= counter(before, 'qb_pgsql_queries_total') + 3
    assert counter(after, 'qb_redis_commands_total') >= counter(before, 'qb_redis_commands_total') + 3
    assert counter(after, 'qb_http_requests_total') >= 5
    has_latency = 'qb_redis_command_duration_seconds_bucket' in after
    assert has_latency == args.latency, 'latency must be explicitly enabled by the application'
    if args.latency:
        assert counter(after, 'qb_redis_command_duration_seconds_count') >= 3
    with concurrent.futures.ThreadPoolExecutor(max_workers=4) as workers:
        for status, _, text in workers.map(lambda _: fetch(args.metrics + '/metrics'), range(8)):
            assert status == 200
            check_snapshot(text)
    print('PASS HTTP, PG query, Redis PING, both registries, concurrent scrapes, latency opt-in')
    if args.prometheus:
        query = urllib.parse.urlencode({'query': 'qb_scrape_core_up{job="qb-observability"}'})
        deadline = time.monotonic() + 40
        while True:
            status, _, text = fetch(args.prometheus + '/api/v1/query?' + query)
            data = json.loads(text)
            samples = data.get('data', {}).get('result', [])
            if status == 200 and len(samples) == 2 and all(s['value'][1] == '1' for s in samples):
                break
            assert time.monotonic() < deadline, text
            time.sleep(1)
        print('PASS Prometheus ingested both core series')
    if args.grafana:
        status, _, text = fetch(args.grafana + '/api/dashboards/uid/qb-observability', args.grafana_auth)
        dashboard = json.loads(text)
        assert status == 200 and len(dashboard['dashboard']['panels']) == 12, text
        query = urllib.parse.urlencode({'query': 'qb_scrape_core_up{job="qb-observability"}'})
        status, _, text = fetch(args.grafana + '/api/datasources/proxy/uid/qb-prometheus/api/v1/query?' + query, args.grafana_auth)
        samples = json.loads(text).get('data', {}).get('result', [])
        assert status == 200 and len(samples) == 2 and all(s['value'][1] == '1' for s in samples), text
        print('PASS Grafana dashboard and provisioned datasource query')


if __name__ == '__main__':
    main()
