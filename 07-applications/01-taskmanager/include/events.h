/**
 * @file events.h
 * @brief Inter-actor QB events for the Task Manager.
 *
 * Only one event is needed at the system boundary: the socket transfer
 * from TcpListener → TaskManager.  All other communication happens
 * in-process through direct HTTP context callbacks (no actor ping-pong).
 */
#pragma once

#include <qb/actor.h>
#include <qb/io/async.h>

namespace taskmanager {

/**
 * @brief Transfers an accepted TCP socket from TcpListener to a TaskManager.
 *
 * TcpListener pushes this event to the TaskManager chosen by round-robin.
 * TaskManager calls `registerSession(std::move(evt.socket))` to own the fd.
 */
struct NewConnectionEvent : public qb::Event {
    qb::io::tcp::socket socket;
    NewConnectionEvent() = default;
};

} // namespace taskmanager
