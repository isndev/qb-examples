/**
 * @file events.h
 * @brief Inter-actor events for Auction House.
 *
 * NewConnectionEvent: Dispatched by TcpListener to AuctionManager workers.
 */
#pragma once

#include <qb/actor.h>
#include <qb/io.h>

namespace auction_house {
namespace events {

/**
 * @brief Event dispatched by TcpListener to forward new TCP connections.
 *
 * TcpListener accepts connections on Core 0 and round-robin dispatches
 * them to AuctionManager actors on worker cores via this event.
 */
struct NewConnectionEvent : public qb::Event {
    qb::io::tcp::socket socket;
};

} // namespace events
} // namespace auction_house
