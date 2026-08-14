/**
 * QB Auction House — Frontend
 *
 * Integrates directly with the QB C++ backend:
 *   GET  /api/lots               → list active lots (X-Cache header)
 *   GET  /api/lots/:id           → single lot
 *   GET  /api/lots/:id/bids      → bid history
 *   POST /api/lots/:id/bids      → place bid  (201 or 409)
 *   GET  /api/users              → list users
 *   GET  /api/users/:id/stats    → user stats
 *   GET  /ws                     → WebSocket (lot_update broadcasts)
 */

// ─── Constants ────────────────────────────────────────────────────────────────

const WS_RECONNECT_MS = 2500;
const MAX_RECONNECTS = 8;
const HEARTBEAT_MS = 25000;
const TOAST_DURATION = 4500;
const MAX_LOG_ENTRIES = 80;

// ─── State ────────────────────────────────────────────────────────────────────

let lots = new Map();   // id → lot object
let users = [];          // array of user objects
let currentUser = null;        // { id, username, balance, email }

let activeCategory = 'all';
let activeSort = 'ending';

let selectedLotId = null;    // lot open in drawer
let countdownTimer = null;    // setInterval handle for drawer countdown

let ws = null;
let wsReconnects = 0;
let heartbeatTimer = null;
let reconnectTimer = null;

// ─── DOM refs ─────────────────────────────────────────────────────────────────

const $ = id => document.getElementById(id);

const els = {
    wsBadge: $('ws-badge'),
    cacheBadge: $('cache-badge'),
    totalCount: $('total-count'),
    userSelect: $('user-select'),
    userBalance: $('user-balance'),
    lotsGrid: $('lots-grid'),
    eventsLog: $('events-log'),
    drawerOverlay: $('drawer-overlay'),
    drawer: $('drawer'),
    drawerCategory: $('drawer-category'),
    drawerTitle: $('drawer-title'),
    drawerDesc: $('drawer-description'),
    drawerCountdown: $('drawer-countdown'),
    drawerStartPrice: $('drawer-start-price'),
    drawerBidsCount: $('drawer-bids-count'),
    drawerPrice: $('drawer-price'),
    bidFormBlock: $('bid-form-block'),
    bidEndedNotice: $('bid-ended-notice'),
    bidAmount: $('bid-amount'),
    placeBidBtn: $('place-bid-btn'),
    bidResult: $('bid-result'),
    bidList: $('bid-list'),
    bidHistoryTotal: $('bid-history-total'),
    toastContainer: $('toast-container'),
};

// ─── Bootstrap ────────────────────────────────────────────────────────────────

async function init() {
    setupEventListeners();
    await Promise.all([loadUsers(), loadLots()]);
    connectWebSocket();
}

// ─── API helpers ─────────────────────────────────────────────────────────────

async function apiFetch(url, options = {}) {
    const res = await fetch(url, {
        headers: {'Content-Type': 'application/json'},
        ...options,
    });
    const body = await res.json().catch(() => null);
    return {ok: res.ok, status: res.status, data: body, headers: res.headers};
}

// ─── Load Users ───────────────────────────────────────────────────────────────

async function loadUsers() {
    const {ok, data} = await apiFetch('/api/users');
    if (!ok || !data?.users?.length) return;

    users = data.users;

    // Populate select
    els.userSelect.innerHTML = users.map(u =>
        `<option value="${u.id}">${u.username}</option>`
    ).join('');

    selectUser(users[0]);
}

function selectUser(user) {
    currentUser = user;
    els.userBalance.textContent = `$${Number(user.balance).toLocaleString()}`;
}

// ─── Load Lots ────────────────────────────────────────────────────────────────

async function loadLots() {
    const {ok, data, headers} = await apiFetch('/api/lots');
    if (!ok || !data?.lots) return;

    // Cache badge
    const cacheHeader = headers.get('X-Cache') || '';
    showCacheBadge(cacheHeader);

    // Update state
    lots.clear();
    for (const lot of data.lots) {
        lots.set(lot.id, lot);
    }

    renderGrid();
}

// ─── Grid Rendering ───────────────────────────────────────────────────────────

function getFilteredSortedLots() {
    let arr = Array.from(lots.values());

    if (activeCategory !== 'all') {
        arr = arr.filter(l => l.category === activeCategory);
    }

    switch (activeSort) {
        case 'ending':
            arr.sort((a, b) => a.end_time - b.end_time);
            break;
        case 'price-asc':
            arr.sort((a, b) => a.current_price - b.current_price);
            break;
        case 'price-desc':
            arr.sort((a, b) => b.current_price - a.current_price);
            break;
    }

    return arr;
}

function renderGrid() {
    const arr = getFilteredSortedLots();

    els.totalCount.textContent = `${lots.size} lot${lots.size !== 1 ? 's' : ''}`;

    if (!arr.length) {
        els.lotsGrid.innerHTML = '<div class="loading-hint">No lots in this category.</div>';
        return;
    }

    els.lotsGrid.innerHTML = arr.map(lot => lotCardHTML(lot)).join('');

    // Click handlers
    els.lotsGrid.querySelectorAll('.lot-card').forEach(card => {
        card.addEventListener('click', () => {
            if (card.classList.contains('ended')) return;
            const id = parseInt(card.dataset.id);
            openDrawer(id);
        });
    });
}

function lotCardHTML(lot) {
    const secondsLeft = computeTimeLeft(lot);
    const isEnded = secondsLeft <= 0;
    const isUrgent = secondsLeft > 0 && secondsLeft <= 300;
    const isSelected = lot.id === selectedLotId;
    const timeStr = isEnded ? 'Ended' : formatTimeShort(secondsLeft);

    return `
        <div class="lot-card ${isSelected ? 'selected' : ''} ${isUrgent ? 'ending-soon' : ''} ${isEnded ? 'ended' : ''}"
             data-id="${lot.id}">
            <div class="lot-card-top">
                <span class="lot-cat-badge">${esc(lot.category)}</span>
                <span class="lot-time ${isUrgent ? 'urgent' : ''} ${isEnded ? 'ended' : ''}">${timeStr}</span>
            </div>
            <h3>${esc(lot.title)}</h3>
            <p class="lot-desc">${esc(lot.description)}</p>
            <div class="lot-card-footer">
                <span class="lot-price">$${Number(lot.current_price).toLocaleString()}</span>
                <span class="lot-bids">${lot.bid_count || 0} bid${lot.bid_count !== 1 ? 's' : ''}</span>
            </div>
        </div>
    `;
}

// ─── Drawer ───────────────────────────────────────────────────────────────────

function openDrawer(lotId) {
    const lot = lots.get(lotId);
    if (!lot) return;

    selectedLotId = lotId;

    // Mark selected card
    els.lotsGrid.querySelectorAll('.lot-card').forEach(c => {
        c.classList.toggle('selected', parseInt(c.dataset.id) === lotId);
    });

    const secondsLeft = computeTimeLeft(lot);
    const isEnded = secondsLeft <= 0;

    // Fill header
    els.drawerCategory.textContent = lot.category.toUpperCase();
    els.drawerTitle.textContent = lot.title;
    els.drawerDesc.textContent = lot.description;

    // Fill meta
    els.drawerStartPrice.textContent = `$${Number(lot.start_price).toLocaleString()}`;
    els.drawerBidsCount.textContent = `${lot.bid_count || 0} bids`;
    els.drawerPrice.textContent = `$${Number(lot.current_price).toLocaleString()}`;

    // Show/hide bid form
    if (isEnded) {
        els.bidFormBlock.classList.add('hidden');
        els.bidEndedNotice.classList.remove('hidden');
    } else {
        els.bidFormBlock.classList.remove('hidden');
        els.bidEndedNotice.classList.add('hidden');
        els.placeBidBtn.disabled = false;

        // Pre-fill with minimum bid
        const minBid = Math.ceil(lot.current_price * 1.05 / 100) * 100;
        els.bidAmount.value = minBid;
    }

    hideBidResult();

    // Start countdown
    startCountdown(lot);

    // Load bid history
    loadBidHistory(lotId);

    // Subscribe to lot via WS
    wsSend({type: 'subscribe_lot', lot_id: lotId});

    // Open drawer
    els.drawerOverlay.classList.add('open');
    els.drawer.classList.add('open');
}

function closeDrawer() {
    els.drawerOverlay.classList.remove('open');
    els.drawer.classList.remove('open');
    selectedLotId = null;
    stopCountdown();
    els.lotsGrid.querySelectorAll('.lot-card.selected').forEach(c => c.classList.remove('selected'));
}

// ─── Countdown ────────────────────────────────────────────────────────────────

function computeTimeLeft(lot) {
    // end_time from backend is a Unix timestamp in seconds (qb::Timestamp::count())
    return Math.max(0, Math.floor(lot.end_time - Date.now() / 1000));
}

function startCountdown(lot) {
    stopCountdown();
    updateCountdownDisplay(lot);
    countdownTimer = setInterval(() => {
        const current = lots.get(lot.id);
        if (!current) {
            stopCountdown();
            return;
        }
        updateCountdownDisplay(current);
    }, 1000);
}

function stopCountdown() {
    if (countdownTimer) {
        clearInterval(countdownTimer);
        countdownTimer = null;
    }
}

function updateCountdownDisplay(lot) {
    const secs = computeTimeLeft(lot);

    els.drawerCountdown.textContent = formatCountdown(secs);
    els.drawerCountdown.classList.toggle('ending', secs > 0 && secs <= 60);
    els.drawerCountdown.classList.toggle('ended', secs <= 0);

    if (secs <= 0 && !els.bidEndedNotice.classList.contains('hidden') === false) {
        els.bidFormBlock.classList.add('hidden');
        els.bidEndedNotice.classList.remove('hidden');
    }
}

function formatCountdown(secs) {
    if (secs <= 0) return 'ENDED';
    const h = Math.floor(secs / 3600);
    const m = Math.floor((secs % 3600) / 60);
    const s = secs % 60;
    if (h > 0) return `${pad(h)}:${pad(m)}:${pad(s)}`;
    return `${pad(m)}:${pad(s)}`;
}

function formatTimeShort(secs) {
    if (secs <= 0) return 'Ended';
    if (secs < 60) return `${secs}s`;
    if (secs < 3600) return `${Math.floor(secs / 60)}m`;
    return `${Math.floor(secs / 3600)}h ${Math.floor((secs % 3600) / 60)}m`;
}

function pad(n) {
    return String(n).padStart(2, '0');
}

// ─── Bid History ─────────────────────────────────────────────────────────────

async function loadBidHistory(lotId) {
    els.bidList.innerHTML = '<div class="bid-empty">Loading…</div>';

    const {ok, data} = await apiFetch(`/api/lots/${lotId}/bids`);
    if (!ok || selectedLotId !== lotId) return;

    const bids = data?.bids || [];
    els.bidHistoryTotal.textContent = `${data?.total_bids || bids.length} total`;

    // Update lot bid count in state
    const lot = lots.get(lotId);
    if (lot) lot.bid_count = data?.total_bids || bids.length;

    renderBidList(bids);

    // Also update current price from authoritative source
    if (data?.current_price !== undefined) {
        updateLotPrice(lotId, data.current_price, false);
    }
}

function renderBidList(bids) {
    if (!bids.length) {
        els.bidList.innerHTML = '<div class="bid-empty">No bids yet — be the first!</div>';
        return;
    }
    els.bidList.innerHTML = bids.map(b => bidRowHTML(b, false)).join('');
}

function prependBid(bid) {
    // Remove empty state
    const empty = els.bidList.querySelector('.bid-empty');
    if (empty) empty.remove();

    const row = document.createElement('div');
    row.innerHTML = bidRowHTML(bid, true);
    const el = row.firstElementChild;
    els.bidList.prepend(el);

    // Remove .new-bid highlight after animation
    setTimeout(() => el.classList.remove('new-bid'), 2500);
}

function bidRowHTML(bid, isNew) {
    const username = bid.bidder_username || bid.bidder || 'Anonymous';
    const initial = username.charAt(0).toUpperCase();
    const timeStr = bid.bid_time ? formatBidTime(bid.bid_time) : 'just now';
    return `
        <div class="bid-row${isNew ? ' new-bid' : ''}">
            <div class="bid-row-left">
                <div class="bid-avatar">${esc(initial)}</div>
                <div>
                    <div class="bid-user">${esc(username)}</div>
                    <div class="bid-time">${timeStr}</div>
                </div>
            </div>
            <div class="bid-amount">$${Number(bid.amount).toLocaleString()}</div>
        </div>
    `;
}

function formatBidTime(timeStr) {
    try {
        const d = new Date(timeStr);
        const diff = Math.floor((Date.now() - d.getTime()) / 1000);
        if (diff < 5) return 'just now';
        if (diff < 60) return `${diff}s ago`;
        if (diff < 3600) return `${Math.floor(diff / 60)}m ago`;
        return d.toLocaleTimeString([], {hour: '2-digit', minute: '2-digit'});
    } catch {
        return timeStr;
    }
}

// ─── Place Bid ────────────────────────────────────────────────────────────────

async function placeBid() {
    if (!selectedLotId || !currentUser) return;

    const amount = parseFloat(els.bidAmount.value);
    const lot = lots.get(selectedLotId);

    if (!amount || amount <= 0) {
        showBidResult('Enter a valid amount.', 'error');
        return;
    }
    if (lot && amount <= lot.current_price) {
        showBidResult(`Must be higher than $${Number(lot.current_price).toLocaleString()}.`, 'error');
        return;
    }

    els.placeBidBtn.disabled = true;
    els.placeBidBtn.textContent = 'Placing…';

    const {ok, status, data} = await apiFetch(`/api/lots/${selectedLotId}/bids`, {
        method: 'POST',
        body: JSON.stringify({amount, bidder_id: currentUser.id}),
    });

    els.placeBidBtn.disabled = false;
    els.placeBidBtn.textContent = 'Place Bid';

    if (ok && data?.success) {
        // 201 Created
        showBidResult(`✓ Bid of $${amount.toLocaleString()} placed!`, 'success');
        toast(`Bid placed: $${amount.toLocaleString()}`, 'success');

        // Update local state immediately
        updateLotPrice(selectedLotId, data.new_price, true);

        // Add to bid history immediately
        prependBid({
            bidder_username: currentUser.username,
            amount: data.new_price,
            bid_time: new Date().toISOString(),
        });

        // Auto-suggest next minimum bid
        const nextMin = Math.ceil(data.new_price * 1.05 / 100) * 100;
        els.bidAmount.value = nextMin;

    } else {
        // 409 Conflict or other error
        const errMsg = data?.error || `Error ${status}`;
        showBidResult(errMsg, 'error');
        if (status === 409) {
            toast('Auction may have ended — try refreshing.', 'warn');
            loadLots(); // Refresh lots
        }
    }
}

function updateLotPrice(lotId, newPrice, broadcast) {
    const lot = lots.get(lotId);
    if (!lot) return;
    lot.current_price = newPrice;

    // Update card on grid
    const card = els.lotsGrid.querySelector(`[data-id="${lotId}"]`);
    if (card) {
        const priceEl = card.querySelector('.lot-price');
        if (priceEl) {
            priceEl.textContent = `$${Number(newPrice).toLocaleString()}`;
            // Flash highlight
            priceEl.style.color = '#34d399';
            setTimeout(() => {
                priceEl.style.color = '';
            }, 1500);
        }
    }

    // Update drawer if open
    if (selectedLotId === lotId) {
        els.drawerPrice.textContent = `$${Number(newPrice).toLocaleString()}`;
        // Flash
        els.drawerPrice.style.animation = 'none';
        els.drawerPrice.offsetHeight;
        els.drawerPrice.style.animation = '';
    }
}

function showBidResult(msg, type) {
    els.bidResult.textContent = msg;
    els.bidResult.className = `bid-result ${type}`;
    els.bidResult.classList.remove('hidden');
    setTimeout(() => els.bidResult.classList.add('hidden'), 6000);
}

function hideBidResult() {
    els.bidResult.classList.add('hidden');
}

// ─── WebSocket ────────────────────────────────────────────────────────────────

function connectWebSocket() {
    const url = `ws://${location.host}/ws`;

    ws = new WebSocket(url);

    ws.onopen = () => {
        wsReconnects = 0;
        setWsStatus(true);
        addEvent('🔗', 'WebSocket connected', '');
        startHeartbeat();

        // Re-subscribe to open lot
        if (selectedLotId) wsSend({type: 'subscribe_lot', lot_id: selectedLotId});
    };

    ws.onmessage = (e) => {
        try {
            handleWsMessage(JSON.parse(e.data));
        } catch { /* ignore malformed */
        }
    };

    ws.onclose = () => {
        setWsStatus(false);
        stopHeartbeat();
        scheduleReconnect();
    };

    ws.onerror = () => { /* onclose handles it */
    };
}

function handleWsMessage(data) {
    switch (data.type) {
        case 'lot_update':
            onLotUpdate(data);
            break;

        case 'subscribed':
            // Server confirmed subscription — no UI action needed
            break;

        case 'pong':
            // Heartbeat ack
            break;
    }
}

function onLotUpdate(data) {
    const lot = lots.get(data.lot_id);
    if (!lot) return;

    const oldPrice = lot.current_price;
    const newPrice = data.new_price;

    // Update state
    lot.current_price = newPrice;
    if (data.time_left !== undefined) lot.time_left = data.time_left;

    // Sync bid count from WS if provided
    if (typeof data.bid_count === 'number') lot.bid_count = data.bid_count;

    // Update grid card
    updateLotPrice(data.lot_id, newPrice, false);

    // Update bid count on card
    const card = els.lotsGrid.querySelector(`[data-id="${data.lot_id}"]`);
    if (card) {
        const bidsEl = card.querySelector('.lot-bids');
        if (bidsEl && lot.bid_count !== undefined) {
            bidsEl.textContent = `${lot.bid_count} bid${lot.bid_count !== 1 ? 's' : ''}`;
        }
    }

    // Live activity
    const bidder = data.bidder || 'Someone';
    addEvent('💰', `New bid on "${truncate(lot.title, 28)}"`,
        `${bidder} → $${Number(newPrice).toLocaleString()}`);

    // Notification for open drawer
    if (selectedLotId === data.lot_id) {
        toast(`${bidder} bid $${Number(newPrice).toLocaleString()}`, 'info');

        // Add to bid history
        prependBid({
            bidder_username: bidder,
            amount: newPrice,
            bid_time: new Date().toISOString(),
        });

        // Update bid count in drawer
        lot.bid_count = (lot.bid_count || 0) + 1;
        els.drawerBidsCount.textContent = `${lot.bid_count} bids`;
        els.bidHistoryTotal.textContent = `${lot.bid_count} total`;
    }

    // Mark lot as ending-soon if < 5 min
    if (data.time_left !== undefined && data.time_left <= 300 && data.time_left > 0) {
        if (card && !card.classList.contains('ending-soon')) {
            card.classList.add('ending-soon');
            addEvent('⚠️', `Ending soon: "${truncate(lot.title, 28)}"`, `${formatTimeShort(data.time_left)} left`);
        }
    }
}

function wsSend(obj) {
    if (ws?.readyState === WebSocket.OPEN) {
        ws.send(JSON.stringify(obj));
    }
}

function setWsStatus(connected) {
    els.wsBadge.classList.toggle('disconnected', !connected);
    els.wsBadge.querySelector('.ws-label').textContent = connected ? 'WS' : 'WS';
}

function startHeartbeat() {
    heartbeatTimer = setInterval(() => wsSend({type: 'ping'}), HEARTBEAT_MS);
}

function stopHeartbeat() {
    clearInterval(heartbeatTimer);
    heartbeatTimer = null;
}

function scheduleReconnect() {
    if (wsReconnects >= MAX_RECONNECTS) {
        toast('WebSocket connection lost. Refresh to reconnect.', 'error');
        return;
    }
    wsReconnects++;
    const delay = Math.min(WS_RECONNECT_MS * wsReconnects, 30000);
    reconnectTimer = setTimeout(connectWebSocket, delay);
}

// ─── Live Events Sidebar ─────────────────────────────────────────────────────

function addEvent(icon, title, detail) {
    const now = new Date();
    const timeStr = now.toLocaleTimeString([], {hour: '2-digit', minute: '2-digit', second: '2-digit'});

    const item = document.createElement('div');
    item.className = 'event-item';
    item.innerHTML = `
        <div class="event-top">
            <span class="event-icon">${icon}</span>
            <span class="event-title">${esc(title)}</span>
            <span class="event-time">${timeStr}</span>
        </div>
        ${detail ? `<div class="event-detail">${esc(detail)}</div>` : ''}
    `;

    els.eventsLog.prepend(item);

    // Trim to max entries
    while (els.eventsLog.children.length > MAX_LOG_ENTRIES) {
        els.eventsLog.lastChild.remove();
    }
}

// ─── Cache badge ──────────────────────────────────────────────────────────────

function showCacheBadge(cacheValue) {
    const v = cacheValue.trim().toUpperCase();
    if (!v) {
        els.cacheBadge.classList.add('hidden');
        return;
    }

    els.cacheBadge.textContent = v === 'HIT' ? '⚡ CACHE HIT' : '🔄 CACHE MISS';
    els.cacheBadge.className = `cache-badge ${v === 'HIT' ? 'hit' : 'miss'}`;
    els.cacheBadge.classList.remove('hidden');

    // Log cache event
    addEvent(v === 'HIT' ? '⚡' : '🔄', `Cache ${v}`, '/api/lots');
}

// ─── Toast notifications ──────────────────────────────────────────────────────

function toast(msg, type = 'success') {
    const el = document.createElement('div');
    el.className = `toast ${type}`;
    el.textContent = msg;
    els.toastContainer.prepend(el);

    setTimeout(() => {
        el.classList.add('out');
        el.addEventListener('animationend', () => el.remove());
    }, TOAST_DURATION);
}

// ─── Event listeners ─────────────────────────────────────────────────────────

function setupEventListeners() {
    // Category tabs
    document.getElementById('category-tabs').addEventListener('click', e => {
        const btn = e.target.closest('.cat-tab');
        if (!btn) return;
        document.querySelectorAll('.cat-tab').forEach(b => b.classList.remove('active'));
        btn.classList.add('active');
        activeCategory = btn.dataset.cat;
        renderGrid();
    });

    // Sort
    $('sort-select').addEventListener('change', e => {
        activeSort = e.target.value;
        renderGrid();
    });

    // User select
    els.userSelect.addEventListener('change', () => {
        const id = parseInt(els.userSelect.value);
        const user = users.find(u => u.id === id);
        if (user) selectUser(user);
    });

    // Drawer close
    $('btn-close-drawer').addEventListener('click', closeDrawer);
    els.drawerOverlay.addEventListener('click', closeDrawer);

    // Place bid
    els.placeBidBtn.addEventListener('click', placeBid);
    els.bidAmount.addEventListener('keydown', e => {
        if (e.key === 'Enter') placeBid();
    });

    // Clear events
    $('clear-events-btn').addEventListener('click', () => {
        els.eventsLog.innerHTML = '';
    });
}

// ─── Utility helpers ─────────────────────────────────────────────────────────

function esc(str) {
    if (!str) return '';
    const d = document.createElement('div');
    d.textContent = str;
    return d.innerHTML;
}

function truncate(str, n) {
    return str.length > n ? str.slice(0, n) + '…' : str;
}

// ─── Start ────────────────────────────────────────────────────────────────────

init();
