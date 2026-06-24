/**
 * @file actors/http_session.h
 * @brief HttpSession – one HTTP/1.1 session per accepted connection.
 *
 * ## QB convention
 * Each session type is a thin CRTP wrapper whose only job is to carry the
 * correct template parameters.  All application logic lives in the owning
 * io_handler (TaskManager).  The session is created by TaskManager via
 * `registerSession()` and is automatically destroyed once the response has
 * been fully transmitted (EOS event) or the 60-second keep-alive timer fires.
 *
 * ## Dependency note
 * HttpSession's base class (`qb::http::use<HttpSession>::session<TaskManager>`)
 * stores a `TaskManager&`.  A forward declaration is sufficient here; the
 * complete TaskManager type is required only in translation units that
 * instantiate the base-class methods (i.e. task_manager.cpp).
 */
#pragma once

#include <http/http.h>

namespace taskmanager {
namespace actors {

// Forward-declare the owning handler so this header stays lean.
class TaskManager;

/**
 * @brief HTTP/1.1 session bound to a TaskManager handler.
 *
 * Inherits the full qb-http session machinery (protocol parsing, timeout,
 * response transmission, EOS/disconnect events).  The constructor simply
 * forwards the TaskManager reference to the base class.
 */
class HttpSession : public qb::http::use<HttpSession>::session<TaskManager> {
public:
    explicit HttpSession(TaskManager &mgr)
        : session(mgr) {}
};

} // namespace actors
} // namespace taskmanager
