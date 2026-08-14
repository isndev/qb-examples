// QB HTTP/3 demo front-end. Every call hits the same dual-stack server that served this page;
// once the browser upgrades via Alt-Svc, they ride HTTP/3 over QUIC.

const $ = (id) => document.getElementById(id);

// ---- safe JSON syntax highlighting (builds text/span nodes, never innerHTML) ----
function highlightInto(pre, value) {
    const str = typeof value === 'string' ? value : JSON.stringify(value, null, 2);
    pre.replaceChildren();
    const re = /("(?:\\.|[^"\\])*"\s*:)|("(?:\\.|[^"\\])*")|\b(true|false|null)\b|(-?\d+(?:\.\d+)?(?:[eE][+-]?\d+)?)/g;
    let last = 0, m;
    while ((m = re.exec(str)) !== null) {
        if (m.index > last) pre.appendChild(document.createTextNode(str.slice(last, m.index)));
        const span = document.createElement('span');
        span.textContent = m[0];
        span.className = m[1] ? 'j-key' : m[2] ? 'j-str' : m[3] ? 'j-bool' : 'j-num';
        pre.appendChild(span);
        last = re.lastIndex;
    }
    if (last < str.length) pre.appendChild(document.createTextNode(str.slice(last)));
}

function setStatus(el, code) {
    el.className = 'st ' + (code >= 200 && code < 300 ? 'ok' : code >= 400 && code < 500 ? 'warn' : 'err');
    el.textContent = code;
}

async function call(method, path, body) {
    const t0 = performance.now();
    const opts = { method, headers: { 'Accept': 'application/json' } };
    if (body !== undefined) { opts.headers['Content-Type'] = 'application/json'; opts.body = body; }
    const res = await fetch(path, opts);
    const text = await res.text();
    let data; try { data = JSON.parse(text); } catch { data = text; }
    return { status: res.status, data, ms: performance.now() - t0 };
}

// nextHopProtocol is the browser's own view of the negotiated ALPN ("h3", "h2", "http/1.1").
function detectProtocol() {
    try {
        const entries = performance.getEntriesByType('resource').filter((e) => e.nextHopProtocol);
        if (entries.length) return entries[entries.length - 1].nextHopProtocol;
        const nav = performance.getEntriesByType('navigation')[0];
        if (nav && nav.nextHopProtocol) return nav.nextHopProtocol;
    } catch { /* ignore */ }
    return '';
}

function refreshProtocol() {
    const p = detectProtocol();
    const badge = $('proto-badge'), text = $('proto-text');
    badge.dataset.proto = (p === 'h3' || p === 'h2') ? p : '';
    text.textContent = p ? p.toUpperCase() : 'unknown';
    $('proto-chip').textContent = 'protocol: ' + (p || 'unknown');
}

async function loadFeatures() {
    const ul = $('features');
    try {
        const { data } = await call('GET', '/api/h3-features');
        ul.replaceChildren();
        (data.features || []).forEach((f) => {
            const li = document.createElement('li');
            li.textContent = f;
            ul.appendChild(li);
        });
    } catch (e) {
        const li = document.createElement('li');
        li.textContent = 'failed to load: ' + e;
        ul.replaceChildren(li);
    }
}

function wirePlayground() {
    document.querySelectorAll('.btn[data-get]').forEach((btn) => {
        btn.addEventListener('click', async () => {
            const path = btn.getAttribute('data-get');
            const head = $('pg-head'), out = $('pg-out'), st = $('pg-status');
            st.className = 'st pending'; st.textContent = '…';
            head.querySelector('.chip')?.remove(); head.querySelector('.time')?.remove();
            try {
                const { status, data, ms } = await call('GET', path);
                const chip = document.createElement('span'); chip.className = 'chip'; chip.textContent = 'GET ' + path;
                const time = document.createElement('span'); time.className = 'time'; time.textContent = ms.toFixed(1) + ' ms';
                head.append(chip, time);
                setStatus(st, status);
                highlightInto(out, data);
                $('rtt-chip').textContent = 'last call: ' + ms.toFixed(1) + ' ms';
                refreshProtocol();
            } catch (e) {
                st.className = 'st err'; st.textContent = 'ERR';
                highlightInto(out, String(e));
            }
        });
    });
}

function wireEcho() {
    $('echo-btn').addEventListener('click', async () => {
        const st = $('echo-status'), out = $('echo-out');
        st.className = 'st pending'; st.textContent = '…';
        try {
            const { status, data } = await call('POST', '/api/echo', $('echo-body').value);
            setStatus(st, status);
            highlightInto(out, data);
        } catch (e) {
            st.className = 'st err'; st.textContent = 'ERR';
            highlightInto(out, String(e));
        }
    });
}

function wireBurst() {
    $('burst-btn').addEventListener('click', async () => {
        const n = Math.max(1, Math.min(120, parseInt($('burst').value, 10) || 24));
        const lanes = $('lanes'), meta = $('lanes-meta');
        lanes.replaceChildren();
        const cells = [];
        for (let i = 0; i < n; i++) {
            const lane = document.createElement('div'); lane.className = 'lane';
            const fill = document.createElement('div'); fill.className = 'fill';
            const label = document.createElement('span'); label.textContent = '·';
            lane.append(fill, label);
            lanes.appendChild(lane);
            cells.push({ lane, label });
        }
        meta.textContent = 'firing ' + n + ' concurrent requests…';
        const t0 = performance.now();
        let ok = 0;
        const ids = new Set();
        await Promise.all(cells.map(async (c, i) => {
            try {
                const { status, data } = await call('GET', '/api/stream-demo/' + n);
                if (status === 200) ok++;
                const sid = data && data.stream_id !== undefined ? data.stream_id : '?';
                ids.add(sid);
                c.label.textContent = sid;
            } catch { c.label.textContent = '×'; }
            c.lane.classList.add('done');
        }));
        const ms = (performance.now() - t0).toFixed(1);
        meta.textContent = ok + '/' + n + ' OK in ' + ms + ' ms · ' + ids.size + ' distinct stream ids';
        refreshProtocol();
    });
}

async function pollConnection() {
    try {
        await call('GET', '/api/connection-info');
        $('conn-badge').classList.add('live');
        $('conn-text').textContent = 'connected';
    } catch {
        $('conn-badge').classList.remove('live');
        $('conn-text').textContent = 'offline';
    }
    refreshProtocol();
}

window.addEventListener('DOMContentLoaded', () => {
    wirePlayground();
    wireEcho();
    wireBurst();
    loadFeatures();
    pollConnection();
    setInterval(pollConnection, 5000);
    setTimeout(refreshProtocol, 800); // give the first h3 upgrade a moment
});
