/**
 * @file examples/05-services/04-shutdown-and-drain/session.h
 * @brief The per-connection session — and the reason it is such a small file.
 *
 * @details
 * A SESSION IS NOT AN ACTOR, and that is the whole architecture of a qb service in one sentence.
 * It is a qb-io object that frames bytes and belongs to exactly one actor: the one holding the
 * `io_handler`. So a session never spawns a coroutine, never owns state another core can see, and
 * never decides anything about shutdown. It hands what it decoded to its owner and stops there.
 *
 * That is also why the DRAIN is the owner's problem rather than the session's. The pool knows how
 * many units of work it has in flight (`active_coroutine_count()`) and whether each session's
 * output buffer has reached the socket (`has_pending_write()`). A session, on its own, knows
 * neither — it cannot even see its siblings.
 */

#pragma once

#include <qb/io/async.h>
#include <qb/io/protocol/text.h>
#include "events.h"

namespace drain_demo {

class SessionPool;

/// One connection. Newline-framed text in, newline-framed text out.
class WorkSession : public qb::io::use<WorkSession>::tcp::client<SessionPool> {
public:
    using Protocol = qb::protocol::text::command<WorkSession>;

    explicit WorkSession(IOServer &pool)
        : client(pool) {}

    /// Defined in main.cpp, next to the pool: it pushes to its owner, and the owner's type is
    /// necessarily incomplete here.
    void on(Protocol::message &&msg);
};

} // namespace drain_demo
