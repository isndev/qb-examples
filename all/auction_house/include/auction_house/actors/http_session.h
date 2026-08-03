/**
 * @file actors/http_session.h
 * @brief HttpSession - HTTP session CRTP wrapper.
 *
 * Thin CRTP wrapper that carries the template parameters.
 * All logic lives in the owning AuctionManager.
 */
#pragma once

#include <qbm/http/http.h>

namespace auction_house {
namespace actors {

// Forward declaration
class AuctionManager;

/**
 * @brief HTTP/1.1 session bound to AuctionManager.
 *
 * CRTP Pattern: qb::http::use<Derived>::session<Parent>
 */
class HttpSession : public qb::http::use<HttpSession>::session<AuctionManager> {
public:
    explicit HttpSession(AuctionManager &mgr)
        : session(mgr) {}
};

} // namespace actors
} // namespace auction_house
