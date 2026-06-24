/**
 * @file models/bid.h
 * @brief Bid model - represents a bid placed on a lot.
 */
#pragma once

#include <pgsql/pgsql.h>
#include <qb/json.h>
#include <string>

namespace auction_house {
namespace models {

/**
 * @brief Represents a single bid.
 *
 * Maps to the 'bids' table in PostgreSQL.
 * The bidder_username field is populated by JOIN in select_lot_bids.
 */
struct Bid {
    int32_t     id{0};
    int32_t     lot_id{0};
    int32_t     bidder_id{0};
    double      amount{0.0};
    std::string bid_time;
    bool        is_winning{false};
    std::string bidder_username; // Joined from users table

    Bid() = default;

    explicit Bid(const qb::pg::resultset::row &row) {
        id        = row["id"].as<int32_t>();
        lot_id    = row["lot_id"].as<int32_t>();
        bidder_id = row["bidder_id"].as<int32_t>();
        amount    = row["amount"].as<double>();
        bid_time  = row["bid_time"].as<std::string>();

        if (!row["is_winning"].is_null())
            is_winning = row["is_winning"].as<bool>();

        try {
            if (!row["bidder_username"].is_null())
                bidder_username = row["bidder_username"].as<std::string>();
        } catch (...) {
        }
    }

    [[nodiscard]] qb::json
    to_json() const {
        return qb::json{{"id", id},         {"lot_id", lot_id},     {"bidder_id", bidder_id},  {"bidder_username", bidder_username},
                        {"amount", amount}, {"bid_time", bid_time}, {"is_winning", is_winning}};
    }
};

/**
 * @brief Bid history for a lot, with aggregate metadata.
 */
struct BidHistory {
    std::vector<Bid> bids;
    int32_t          lot_id{0};
    double           current_price{0.0};
    int32_t          total_bids{0};

    explicit BidHistory(int32_t lot, const qb::pg::resultset &res)
        : lot_id(lot) {
        for (const auto &row : res)
            bids.emplace_back(row);
        total_bids = static_cast<int32_t>(bids.size());
        if (!bids.empty())
            current_price = bids.front().amount; // Ordered DESC — first = highest
    }

    [[nodiscard]] qb::json
    to_json() const {
        qb::json::array_t arr;
        for (const auto &bid : bids)
            arr.push_back(bid.to_json());
        return qb::json{{"lot_id", lot_id}, {"current_price", current_price}, {"total_bids", total_bids}, {"bids", arr}};
    }
};

/**
 * @brief Response returned to the client after placing a bid.
 */
struct BidResult {
    bool        success{false};
    std::string message;
    int32_t     bid_id{0};
    double      new_price{0.0};
    int32_t     time_left{0};

    [[nodiscard]] qb::json
    to_json() const {
        return qb::json{{"success", success}, {"message", message}, {"bid_id", bid_id}, {"new_price", new_price}, {"time_left", time_left}};
    }
};

} // namespace models
} // namespace auction_house
