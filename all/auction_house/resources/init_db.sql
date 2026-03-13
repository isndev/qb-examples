-- Auction House Database Schema
-- Idempotent — safe to run on every server startup.

-- ── Schema ──────────────────────────────────────────────────────────────────

CREATE TABLE IF NOT EXISTS users (
    id SERIAL PRIMARY KEY,
    username VARCHAR(64) UNIQUE NOT NULL,
    email VARCHAR(128) UNIQUE NOT NULL,
    balance DECIMAL(12, 2) DEFAULT 10000.00,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE IF NOT EXISTS lots (
    id SERIAL PRIMARY KEY,
    title VARCHAR(256) NOT NULL,
    description TEXT,
    category VARCHAR(64) DEFAULT 'general',
    image_url VARCHAR(512),
    start_price DECIMAL(12, 2) NOT NULL,
    current_price DECIMAL(12, 2) NOT NULL,
    reserve_price DECIMAL(12, 2),
    seller_id INTEGER REFERENCES users(id),
    status VARCHAR(32) DEFAULT 'active',  -- active, ended, cancelled
    start_time TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    end_time TIMESTAMP NOT NULL,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE IF NOT EXISTS bids (
    id SERIAL PRIMARY KEY,
    lot_id INTEGER NOT NULL REFERENCES lots(id) ON DELETE CASCADE,
    bidder_id INTEGER NOT NULL REFERENCES users(id),
    amount DECIMAL(12, 2) NOT NULL,
    bid_time TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    is_winning BOOLEAN DEFAULT false
);

CREATE TABLE IF NOT EXISTS auction_results (
    id SERIAL PRIMARY KEY,
    lot_id INTEGER UNIQUE NOT NULL REFERENCES lots(id),
    winner_id INTEGER REFERENCES users(id),
    final_price DECIMAL(12, 2) NOT NULL,
    total_bids INTEGER DEFAULT 0,
    ended_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

-- Indexes
CREATE INDEX IF NOT EXISTS idx_lots_status   ON lots(status);
CREATE INDEX IF NOT EXISTS idx_lots_end_time ON lots(end_time);
CREATE INDEX IF NOT EXISTS idx_bids_lot_id   ON bids(lot_id);
CREATE INDEX IF NOT EXISTS idx_bids_bidder   ON bids(bidder_id);
CREATE INDEX IF NOT EXISTS idx_bids_time     ON bids(bid_time DESC);

-- ── Idempotent seed data ────────────────────────────────────────────────────
-- Remove all expired + ended lots to keep the demo clean across restarts.
-- Cascades to bids (ON DELETE CASCADE).
DELETE FROM lots WHERE end_time < NOW();

-- Users — never changes
INSERT INTO users (username, email, balance) VALUES
    ('alice',   'alice@example.com',   50000.00),
    ('bob',     'bob@example.com',     30000.00),
    ('charlie', 'charlie@example.com', 25000.00),
    ('david',   'david@example.com',   40000.00)
ON CONFLICT DO NOTHING;

-- Lots: insert only when title does not already exist and is still active.
-- Long durations so the demo stays fresh for many hours.
INSERT INTO lots (title, description, category, start_price, current_price, seller_id, end_time)
SELECT v.title, v.description, v.category, v.start_price, v.current_price,
       u.id AS seller_id, v.end_time
FROM (VALUES
    ('Vintage Rolex Submariner 1965',
     'Beautiful vintage Rolex Submariner from 1965. Original dial and bezel, recently serviced. Box and papers included.',
     'watches',   5000.00::numeric,   5000.00::numeric, 'alice',   NOW() + INTERVAL '4 hours'),
    ('1973 Porsche 911 Targa',
     'Classic 911 Targa in Guards Red. Matching-numbers engine. Full restoration completed 2020. Only 3 owners.',
     'automotive', 45000.00::numeric, 45000.00::numeric, 'bob',    NOW() + INTERVAL '6 hours'),
    ('Original Picasso Lithograph 1954',
     'Signed and numbered lithograph, one of only 150 impressions. Certificate of authenticity from Sotheby''s.',
     'art',       15000.00::numeric, 15000.00::numeric, 'charlie', NOW() + INTERVAL '2 hours'),
    ('Harry Potter First Edition 1997',
     'First edition, first printing — Harry Potter and the Philosopher''s Stone. Fine condition. Bloomsbury 1997.',
     'books',     25000.00::numeric, 25000.00::numeric, 'david',   NOW() + INTERVAL '8 hours'),
    ('1959 Gibson Les Paul Standard',
     'Original 1959 sunburst. All-original PAF pickups and hardware. One owner from new, played by a legendary blues guitarist.',
     'music',    120000.00::numeric,120000.00::numeric, 'alice',   NOW() + INTERVAL '12 hours')
) AS v(title, description, category, start_price, current_price, seller_username, end_time)
JOIN users u ON u.username = v.seller_username
WHERE NOT EXISTS (
    SELECT 1 FROM lots l
    WHERE l.title = v.title AND l.end_time > NOW() AND l.status = 'active'
);
