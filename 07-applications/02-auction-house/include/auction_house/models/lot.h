/**
 * @file models/lot.h
 * @brief Lot model - represents an auction item.
 */
#pragma once

#include <chrono>
#include <qbm/pgsql/pgsql.h>
#include <string>
#include <qb/json.h>

namespace auction_house {
namespace models {

/**
 * @brief Represents an auction lot/item.
 *
 * Maps to the 'lots' table in PostgreSQL.
 */
struct Lot {
    int32_t     id{0};
    std::string title;
    std::string description;
    std::string category{"general"};
    std::string image_url;

    double start_price{0.0};
    double current_price{0.0};
    double reserve_price{0.0};

    int32_t     seller_id{0};
    std::string status{"active"}; // active, ended, cancelled

    int64_t start_time{0}; // Unix timestamp in seconds (EXTRACT EPOCH)
    int64_t end_time{0};   // Unix timestamp in seconds (EXTRACT EPOCH)
    int32_t time_left{0};  // Seconds remaining (calculated at query time)
    int32_t bid_count{0};  // Total bids (from subquery or joined count)

    std::string created_at;
    std::string updated_at;

    Lot() = default;

    /** Construct from a PostgreSQL result row. */
    explicit Lot(const qb::pg::resultset::row &row) {
        id          = row["id"].as<int32_t>();
        title       = row["title"].as<std::string>();
        description = row["description"].as<std::string>();
        category    = row["category"].as<std::string>();

        if (!row["image_url"].is_null())
            image_url = row["image_url"].as<std::string>();

        start_price   = row["start_price"].as<double>();
        current_price = row["current_price"].as<double>();

        if (!row["reserve_price"].is_null())
            reserve_price = row["reserve_price"].as<double>();

        seller_id = row["seller_id"].as<int32_t>();
        status    = row["status"].as<std::string>();

        // Queries use EXTRACT(EPOCH FROM ...)::bigint — clean Unix seconds
        if (!row["start_time"].is_null())
            start_time = row["start_time"].as<int64_t>();

        if (!row["end_time"].is_null())
            end_time = row["end_time"].as<int64_t>();

        // Compute time_left from actual wall clock
        auto now_sec = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        time_left    = static_cast<int32_t>(end_time - now_sec);
        if (time_left < 0)
            time_left = 0;

        created_at = row["created_at"].as<std::string>();
        updated_at = row["updated_at"].as<std::string>();

        // bid_count only present in queries that include the subquery/join
        try {
            if (!row["bid_count"].is_null())
                bid_count = row["bid_count"].as<int32_t>();
        } catch (...) {
        }
    }

    [[nodiscard]] bool
    is_active() const {
        return status == "active" && time_left > 0;
    }

    [[nodiscard]] qb::json
    to_json() const {
        return qb::json{
            {"id", id},
            {"title", title},
            {"description", description},
            {"category", category},
            {"image_url", image_url},
            {"start_price", start_price},
            {"current_price", current_price},
            {"reserve_price", reserve_price},
            {"seller_id", seller_id},
            {"status", status},
            {"start_time", start_time},
            {"end_time", end_time},
            {"time_left", time_left},
            {"bid_count", bid_count},
            {"created_at", created_at},
            {"updated_at", updated_at}
        };
    }
};

/**
 * @brief Paginated list of lots with cache metadata.
 */
struct LotList {
    std::vector<Lot> lots;
    std::size_t      total{0};
    bool             from_cache{false};

    explicit LotList(const qb::pg::resultset &res, bool cached = false)
        : from_cache(cached) {
        for (const auto &row : res)
            lots.emplace_back(row);
        total = lots.size();
    }

    [[nodiscard]] qb::json
    to_json() const {
        qb::json::array_t arr;
        for (const auto &lot : lots)
            arr.push_back(lot.to_json());
        return qb::json{{"lots", arr}, {"total", total}, {"cached", from_cache}};
    }
};

/**
 * @brief Payload broadcast via Redis Pub/Sub when a lot changes.
 *
 * Not a QB actor event — uses std::string like any normal struct.
 */
struct LotEvent {
    std::string action; // bid | ended | started
    int32_t     lot_id{0};
    double      new_price{0.0};
    std::string bidder;
    int32_t     time_left{0};
    int64_t     timestamp{0};

    [[nodiscard]] qb::json
    to_json() const {
        return qb::json{{"type", "lot_update"}, {"action", action},       {"lot_id", lot_id},      {"new_price", new_price},
                        {"bidder", bidder},     {"time_left", time_left}, {"timestamp", timestamp}};
    }
};

} // namespace models
} // namespace auction_house
