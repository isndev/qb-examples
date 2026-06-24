/**
 * @file models/user.h
 * @brief User model - represents a bidder/user.
 */
#pragma once

#include <pgsql/pgsql.h>
#include <string>
#include <qb/json.h>

namespace auction_house {
namespace models {

/**
 * @brief Represents a user/bidder.
 *
 * Maps to the 'users' table in PostgreSQL.
 */
struct User {
    int32_t     id{0};
    std::string username;
    std::string email;
    double      balance{0.0};
    std::string created_at;

    User() = default;

    explicit User(const qb::pg::resultset::row &row) {
        id         = row["id"].as<int32_t>();
        username   = row["username"].as<std::string>();
        email      = row["email"].as<std::string>();
        balance    = row["balance"].as<double>();
        created_at = row["created_at"].as<std::string>();
    }

    [[nodiscard]] qb::json
    to_json() const {
        return qb::json{{"id", id}, {"username", username}, {"email", email}, {"balance", balance}, {"created_at", created_at}};
    }
};

/**
 * @brief Aggregated bidding statistics for a user.
 */
struct UserStats {
    int32_t     user_id{0};
    std::string username;
    int32_t     total_bids{0};
    int32_t     active_auctions{0};
    int32_t     auctions_won{0};
    double      total_spent{0.0};

    [[nodiscard]] qb::json
    to_json() const {
        return qb::json{{"user_id", user_id},           {"username", username},
                        {"total_bids", total_bids},     {"active_auctions", active_auctions},
                        {"auctions_won", auctions_won}, {"total_spent", total_spent}};
    }
};

} // namespace models
} // namespace auction_house
