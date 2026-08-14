/**
 * QB Task Manager – Kanban Frontend
 *
 * Architecture:
 *  - Single source of truth: `tasks[]`
 *  - Optimistic updates with server reconciliation
 *  - Event delegation (no inline onclick) – fixes all attribute-escaping bugs
 *  - HTML5 Drag & Drop for column transitions
 *  - WebSocket with exponential-backoff reconnect
 */

'use strict';

const API = '';
const WS_URL = `ws://${location.host}/ws`;

// ── State ──────────────────────────────────────────────────────────────────────
let tasks = [];
let selectedStatus = 'pending';
let draggingId = null;
let loadPending = false;
let ws = null;
let wsDelay = 1000;
let wsTimer = null;

// ── DOM refs ───────────────────────────────────────────────────────────────────
const $ = id => document.getElementById(id);
const eventsLog = $('events-log');
const wsBadge = $('ws-badge');
const cacheBadge = $('cache-badge');
const totalCount = $('total-count');
const drawer = $('drawer');
const overlay = $('drawer-overlay');
const taskForm = $('task-form');
const titleInput = $('task-title');
const descInput = $('task-desc');
const toastContainer = $('toast-container');
const board = $('kanban-board');

// ── Bootstrap ──────────────────────────────────────────────────────────────────
async function init() {
    bindUI();
    showLoadingSkeletons();
    await loadTasks();
    connectWS();
}

// ── UI bindings ────────────────────────────────────────────────────────────────
function bindUI() {
    // Drawer open / close
    $('new-task-btn').addEventListener('click', openDrawer);
    $('btn-close-drawer').addEventListener('click', closeDrawer);
    overlay.addEventListener('click', closeDrawer);
    document.addEventListener('keydown', e => {
        if (e.key === 'Escape') closeDrawer();
    });

    // Form submission
    taskForm.addEventListener('submit', handleCreate);

    // Status chip picker inside drawer
    document.querySelectorAll('.status-chip').forEach(chip => {
        chip.addEventListener('click', () => {
            document.querySelectorAll('.status-chip').forEach(c => c.classList.remove('selected'));
            chip.classList.add('selected');
            selectedStatus = chip.dataset.value;
        });
    });

    // Event delegation for task cards (delete & advance buttons)
    board.addEventListener('click', handleCardAction);

    // Drag & drop on drop zones
    document.querySelectorAll('.drop-zone').forEach(zone => {
        zone.addEventListener('dragover', e => {
            e.preventDefault();
            zone.classList.add('drag-over');
        });
        zone.addEventListener('dragleave', e => {
            if (!zone.contains(e.relatedTarget)) zone.classList.remove('drag-over');
        });
        zone.addEventListener('drop', handleDrop);
    });

    // Clear events
    $('clear-events-btn').addEventListener('click', () => {
        eventsLog.innerHTML = '';
    });
}

// ── Drawer ─────────────────────────────────────────────────────────────────────
function openDrawer() {
    drawer.classList.add('open');
    overlay.classList.add('open');
    titleInput.focus();
}

function closeDrawer() {
    drawer.classList.remove('open');
    overlay.classList.remove('open');
    taskForm.reset();
    // Reset status picker
    document.querySelectorAll('.status-chip').forEach(c => c.classList.remove('selected'));
    document.querySelector('.status-chip[data-value="pending"]').classList.add('selected');
    selectedStatus = 'pending';
}

// ── REST helpers ───────────────────────────────────────────────────────────────
async function loadTasks() {
    if (loadPending) return;
    loadPending = true;
    try {
        const res = await fetch(`${API}/tasks`);
        if (!res.ok) throw new Error(`HTTP ${res.status}`);
        const data = await res.json();
        updateCacheBadge(res.headers.get('X-Cache'));
        tasks = data.tasks || [];
        renderBoard();
    } catch (err) {
        logEvent('Failed to load tasks', 'error', '⚠️');
    } finally {
        loadPending = false;
    }
}

async function handleCreate(e) {
    e.preventDefault();
    const title = titleInput.value.trim();
    const description = descInput.value.trim();
    const status = selectedStatus;
    if (!title) return;

    const btn = $('btn-submit');
    btn.disabled = true;
    btn.textContent = 'Creating…';

    try {
        const res = await fetch(`${API}/tasks`, {
            method: 'POST',
            headers: {'Content-Type': 'application/json'},
            body: JSON.stringify({title, description, status})
        });

        if (res.ok) {
            closeDrawer();
            logEvent(`Created: "${title}"`, 'created', '✨');
            showToast(`Task created: "${title}"`, 'success');
            setTimeout(loadTasks, 350);
        } else {
            const body = await res.json().catch(() => ({}));
            showToast('Create failed: ' + (body.error || res.status), 'error');
        }
    } catch {
        showToast('Network error', 'error');
    } finally {
        btn.disabled = false;
        btn.textContent = 'Create Task';
    }
}

async function deleteTask(id) {
    const task = tasks.find(t => t.id === id);
    if (!task) return;

    // Animate card out, then optimistically remove from state
    const card = document.querySelector(`.task-card[data-id="${id}"]`);
    if (card) {
        card.classList.add('removing');
        await sleep(260);
    }
    tasks = tasks.filter(t => t.id !== id);
    renderBoard();

    try {
        const res = await fetch(`${API}/tasks/${id}`, {method: 'DELETE'});
        if (res.ok) {
            logEvent(`Deleted: "${task.title}"`, 'deleted', '🗑️');
            showToast(`Deleted: "${task.title}"`, 'success');
            // Reconcile after WS broadcast
            setTimeout(loadTasks, 350);
        } else {
            // Revert on failure
            tasks.push(task);
            renderBoard();
            showToast('Delete failed: ' + res.status, 'error');
        }
    } catch {
        tasks.push(task);
        renderBoard();
        showToast('Network error', 'error');
    }
}

async function updateStatus(id, newStatus) {
    const task = tasks.find(t => t.id === id);
    if (!task || task.status === newStatus) return;

    const prev = task.status;
    task.status = newStatus;   // Optimistic update
    renderBoard();

    try {
        const res = await fetch(`${API}/tasks/${id}`, {
            method: 'PUT',
            headers: {'Content-Type': 'application/json'},
            body: JSON.stringify({status: newStatus})
        });

        if (res.ok) {
            logEvent(`"${task.title}" → ${statusLabel(newStatus)}`, 'updated', '↗️');
            if (newStatus === 'completed') showToast(`✅ "${task.title}" completed!`, 'success');
            setTimeout(loadTasks, 350);
        } else {
            task.status = prev;  // Revert
            renderBoard();
            showToast('Update failed: ' + res.status, 'error');
        }
    } catch {
        task.status = prev;
        renderBoard();
        showToast('Network error', 'error');
    }
}

// ── Drag & Drop ────────────────────────────────────────────────────────────────
function handleDragStart(e, id) {
    draggingId = id;
    e.currentTarget.classList.add('dragging');
    e.dataTransfer.effectAllowed = 'move';
    e.dataTransfer.setData('text/plain', String(id));
}

function handleDragEnd(e) {
    e.currentTarget.classList.remove('dragging');
    document.querySelectorAll('.drop-zone').forEach(z => z.classList.remove('drag-over'));
    draggingId = null;
}

function handleDrop(e) {
    e.preventDefault();
    const zone = e.currentTarget;
    const newStatus = zone.dataset.status;
    const id = parseInt(e.dataTransfer.getData('text/plain'), 10);
    zone.classList.remove('drag-over');
    if (id && newStatus) updateStatus(id, newStatus);
}

// ── Card action delegation (delete / advance) ──────────────────────────────────
function handleCardAction(e) {
    // Advance-status button
    const advBtn = e.target.closest('.btn-advance');
    if (advBtn) {
        const id = parseInt(advBtn.dataset.id, 10);
        const task = tasks.find(t => t.id === id);
        if (task) {
            const order = ['pending', 'in_progress', 'completed'];
            const next = order[(order.indexOf(task.status) + 1) % order.length];
            updateStatus(id, next);
        }
        return;
    }

    // Delete button
    const delBtn = e.target.closest('.btn-delete');
    if (delBtn) {
        deleteTask(parseInt(delBtn.dataset.id, 10));
    }
}

// ── Rendering ──────────────────────────────────────────────────────────────────
const STATUSES = ['pending', 'in_progress', 'completed'];

function renderBoard() {
    STATUSES.forEach(status => {
        const col = tasks.filter(t => t.status === status);
        const zone = $(`zone-${status}`);
        const badge = $(`count-${status}`);

        badge.textContent = col.length;

        if (col.length === 0) {
            const hints = {
                pending: 'No pending tasks',
                in_progress: 'Drag tasks here to start',
                completed: '🎉 Completed tasks appear here'
            };
            zone.innerHTML = `<div class="empty-hint">${hints[status]}</div>`;
            return;
        }

        zone.innerHTML = col.map(renderCard).join('');

        // Attach drag event listeners after DOM insertion
        zone.querySelectorAll('.task-card[draggable]').forEach(card => {
            const id = parseInt(card.dataset.id, 10);
            card.addEventListener('dragstart', e => handleDragStart(e, id));
            card.addEventListener('dragend', handleDragEnd);
        });
    });

    // Header task count
    totalCount.textContent = `${tasks.length} task${tasks.length !== 1 ? 's' : ''}`;
}

function renderCard(task) {
    const order = ['pending', 'in_progress', 'completed'];
    const nextSt = order[(order.indexOf(task.status) + 1) % order.length];
    const nextLbl = statusLabel(nextSt);
    const advIcon = task.status === 'completed' ? '↩' : '→';
    const date = formatDate(task.created_at);

    // Use data-* attributes + event delegation: no inline handlers, no escaping issues
    return `
<div class="task-card ${escAttr(task.status)}" data-id="${task.id}" draggable="true">
    <div class="task-title">${escHtml(task.title)}</div>
    ${task.description ? `<div class="task-desc">${escHtml(task.description)}</div>` : ''}
    <div class="task-footer">
        <span class="task-date">${date}</span>
        <div class="task-actions">
            <button class="btn-card advance btn-advance" data-id="${task.id}" title="Move to ${nextLbl}">${advIcon}</button>
            <button class="btn-card delete  btn-delete"  data-id="${task.id}" title="Delete task">✕</button>
        </div>
    </div>
</div>`.trim();
}

function showLoadingSkeletons() {
    const skeleton = `
        <div class="loading-card">
            <div class="loading-line"></div>
            <div class="loading-line short"></div>
            <div class="loading-line xshort"></div>
        </div>`;
    STATUSES.forEach(s => {
        const zone = $(`zone-${s}`);
        if (zone) zone.innerHTML = skeleton.repeat(2);
    });
}

// ── WebSocket ──────────────────────────────────────────────────────────────────
function connectWS() {
    if (ws && ws.readyState < 2) return;
    clearTimeout(wsTimer);

    ws = new WebSocket(WS_URL);

    ws.onopen = () => {
        wsDelay = 1000;
        setWsConnected(true);
        logEvent('WebSocket connected', 'connected', '🔌');
    };

    ws.onmessage = e => {
        try {
            const msg = JSON.parse(e.data);
            if (msg.type === 'ack') return;
            if (msg.action) {
                const label = msg.title ? `"${escHtml(msg.title)}"` : `#${msg.task_id}`;
                const icons = {created: '✨', updated: '↗️', deleted: '🗑️'};
                logEvent(`${msg.action} ${label}`, msg.action, icons[msg.action] ?? '📡');
                loadTasks();
            }
        } catch { /* malformed frame */
        }
    };

    ws.onclose = e => {
        setWsConnected(false);
        if (e.code !== 1000 && e.code !== 1001) {
            logEvent(`Reconnecting in ${wsDelay / 1000}s…`, 'disconnected', '🔄');
            wsTimer = setTimeout(() => {
                wsDelay = Math.min(wsDelay * 2, 30_000);
                connectWS();
            }, wsDelay);
        } else {
            logEvent('WebSocket disconnected', 'disconnected', '🔌');
        }
    };

    ws.onerror = () => { /* onclose fires next */
    };
}

// ── UI helpers ─────────────────────────────────────────────────────────────────
function setWsConnected(ok) {
    wsBadge.classList.toggle('connected', ok);
    wsBadge.querySelector('.ws-label').textContent = ok ? 'LIVE' : 'WS';
}

function updateCacheBadge(header) {
    if (!header) {
        cacheBadge.className = 'cache-badge hidden';
        return;
    }
    const hit = header === 'HIT';
    cacheBadge.textContent = `CACHE ${header}`;
    cacheBadge.className = `cache-badge ${hit ? 'hit' : 'miss'}`;
}

function logEvent(message, type = 'info', icon = '📡') {
    const el = document.createElement('div');
    el.className = `event-item ${type}`;
    el.innerHTML = `
        <span class="event-icon">${icon}</span>
        <div>
            <div class="event-msg">${message}</div>
            <div class="event-time">${new Date().toLocaleTimeString()}</div>
        </div>`;
    eventsLog.insertBefore(el, eventsLog.firstChild);
    while (eventsLog.children.length > 60) eventsLog.removeChild(eventsLog.lastChild);
}

function showToast(message, type = 'success') {
    const t = document.createElement('div');
    t.className = `toast ${type}`;
    t.textContent = message;
    toastContainer.appendChild(t);
    setTimeout(() => t.remove(), 3000);
}

// ── Pure helpers ───────────────────────────────────────────────────────────────
function statusLabel(s) {
    return {pending: 'Pending', in_progress: 'In Progress', completed: 'Completed'}[s] ?? s;
}

function formatDate(str) {
    if (!str) return '';
    try {
        const d = new Date(str.replace(' ', 'T'));
        const diff = (Date.now() - d.getTime()) / 1000;
        if (diff < 60) return 'just now';
        if (diff < 3600) return `${Math.floor(diff / 60)}m ago`;
        if (diff < 86400) return `${Math.floor(diff / 3600)}h ago`;
        return d.toLocaleDateString();
    } catch {
        return '';
    }
}

/** Safe HTML escaping via DOM (no regex tricks). */
function escHtml(text) {
    if (text == null) return '';
    const d = document.createElement('div');
    d.textContent = String(text);
    return d.innerHTML;
}

/** Safe CSS class-name fragment (only alphanumeric + underscore). */
function escAttr(text) {
    return text ? String(text).replace(/[^a-zA-Z0-9_]/g, '_') : '';
}

function sleep(ms) {
    return new Promise(r => setTimeout(r, ms));
}

// ── Start ──────────────────────────────────────────────────────────────────────
init();
