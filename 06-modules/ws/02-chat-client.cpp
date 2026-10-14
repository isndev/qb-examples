/**
 * @file examples/06-modules/ws/02-chat-client.cpp
 * @tier 06-modules
 * @teaches The client half of the handshake, by hand: generateKey, the upgrade request, then the
 *          same frame protocol the server speaks — which is what ws::coro_client will replace.
 * @demonstrates qb::http::ws::protocol<S>, qb::http::ws::generateKey, qb::http::ws::MessageText,
 *               qb::http::ws::MessageClose, qb::http::ws::CloseStatus::Normal,
 *               qb::http::WebSocketRequest, qb::Event, registerEvent<E>,
 *               qb::io::async::tcp::connect, qb::ScopedCoroContext, ctx.offload
 * @prerequisites 06-modules/ws/01-chat-server
 * @expect "[CLIENT] WebSocket client actor started"
 * @expect "=== QB WebSocket Chat Client ==="
 * @brief Command Line WebSocket Chat Client
 *
 * This example demonstrates:
 * - WebSocket client connecting to a chat server
 * - Command line interface for user interaction
 * - Dual actor architecture (CLI + WebSocket)
 * - Real-time message exchange with the server
 *
 * @author qb - C++ Actor Framework
 * @copyright Copyright (c) 2011-2025 qb - isndev (cpp.actor)
 * Licensed under the Apache License, Version 2.0 (http://www.apache.org/licenses/LICENSE-2.0)
 * @ingroup Examples
 */

#include <chrono>
#include <cstdint>
#include <exception>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include "../../common/CancelableLineSource.h"
#include <qb/main.h>
#include <qb/actor.h>
#include <qb/string.h>
#include <qb/system/parse.h>
#include <qb/io/async.h>
#include <qb/io/async/coroutine.h>
#include <qb/io/tcp/socket.h>
#include <qbm/http/ws.h>
#include <qb/json.h>
#include <qb/io.h>

// Forward declarations
class WebSocketClientActor;
class CommandLineActor;

// Events for communication between actors.
//
// NOTE ON EVENT PAYLOADS: the engine relocates an event with `memcpy` and never runs the source
// destructor, so a payload member may hold no pointer into itself. On libstdc++ a SHORT
// std::string holds exactly that -- `_M_p` addresses its own inline buffer -- so after the
// relocation it still points at the old storage. libc++ recomputes the pointer from `this`, which
// is why the defect is invisible on macOS and corrupts on Linux. Every event here really is
// relocated: the command-line actor runs on core 0 and the WebSocket actor on core 1, and they
// push to each other in both directions. (Relocation is not cross-core-only in general either --
// pipe growth, compaction, `reply()` and `forward()` relocate same-core events too.)
//
// Bounded payloads use `qb::string<N>`; unbounded ones are boxed behind a `std::shared_ptr` so
// the pointer is relocated and the characters stay put on the heap.
struct UserInputEvent : qb::Event {
    std::shared_ptr<std::string> message; // a chat line has no bound
    explicit UserInputEvent(std::string msg)
        : message(std::make_shared<std::string>(std::move(msg))) {}
};

struct UsernameChangeEvent : qb::Event {
    qb::string<64> new_username;
    explicit UsernameChangeEvent(std::string_view username)
        : new_username(username) {}
};

struct ConnectEvent : qb::Event {
    qb::string<256> server_url;
    explicit ConnectEvent(std::string_view url)
        : server_url(url) {}
};

struct DisconnectEvent : qb::Event {};
struct ConnectDeadlineEvent : qb::Event {
    std::uint64_t generation;
    explicit ConnectDeadlineEvent(std::uint64_t attempt)
        : generation(attempt) {}
};
struct CloseDeadlineEvent : qb::Event {};
struct FinalizeShutdownEvent : qb::Event {};

/**
 * @brief Self-addressed wake-ups for `CommandLineActor`'s two delays.
 *
 * Both replace a `qb::io::async::callback([this]{ if (is_alive()) ... }, d)`. That overload
 * heap-allocates a `Timeout` owned by the event loop, not by the actor: nothing cancels it when
 * the actor is killed, so it fires against a destroyed object — and the `is_alive()` guard is
 * not a guard at all, because CALLING it is already the read of freed memory. `spawn(...)` +
 * `co_await ctx.sleep(d)` puts the wait inside the actor's cancellation scope instead, so a
 * killed actor stops receiving ticks and the polling loop ends by itself.
 */
struct InputPollTick : qb::Event {}; ///< every 50 ms: drain bounded console input
struct ShutdownTick : qb::Event {};  ///< 500 ms after /quit: broadcast the kill

struct SetWebSocketActorEvent : qb::Event {
    qb::ActorId websocket_actor_id;
    explicit SetWebSocketActorEvent(qb::ActorId id)
        : websocket_actor_id(id) {}
};

struct DisplayMessageEvent : qb::Event {
    std::shared_ptr<std::string> message; // display text has no bound
    qb::string<16>               type;
    explicit DisplayMessageEvent(std::string msg, std::string_view msg_type = "info")
        : message(std::make_shared<std::string>(std::move(msg)))
        , type(msg_type) {}
};

/**
 * @brief WebSocket client actor for chat communication
 *
 * This actor handles:
 * - WebSocket connection to the chat server
 * - Sending/receiving chat messages
 * - Protocol event handling (connect, disconnect, errors)
 * - Message parsing and formatting
 */
class WebSocketClientActor
    : public qb::Actor
    , public qb::io::use<WebSocketClientActor>::tcp::client<> {
private:
    std::string          _username = "CLIUser";
    std::string          _server_url;
    qb::ActorId          _cmdline_actor_id;
    bool                 _connected          = false;
    bool                 _connecting         = false;
    bool                 _closing            = false; // Peer Close queued; wait for dispose before reusing I/O.
    bool                 _close_sent         = false;
    bool                 _shutdown_requested = false;
    bool                 _user_announced     = false;
    std::string          _ws_key;
    std::shared_ptr<int> _connect_attempt;
    std::uint64_t        _connect_generation = 0;

public:
    using Protocol    = qb::http::protocol<WebSocketClientActor>;
    using WS_Protocol = qb::http::ws::protocol<WebSocketClientActor>;

    explicit WebSocketClientActor(qb::ActorId cmdline_id)
        : _cmdline_actor_id(cmdline_id)
        , _ws_key(qb::http::ws::generateKey()) {}

    qb::io::async::task<bool>
    onInit() override {
        // Shutdown wiring. Event dispatch is by SUBSCRIPTION, not by vtable: qb::Actor's
        // constructor already subscribed its own default handlers for these two, and
        // re-registering here is what replaces them with OURS. Without these two lines
        // the handlers below compile, are never called, and their cleanup is lost.
        registerEvent<qb::KillEvent>(*this);
        registerEvent<qb::SignalEvent>(*this);

        qb::io::cout() << "[CLIENT] WebSocket client actor started" << std::endl;

        // Register events
        registerEvent<UserInputEvent>(*this);
        registerEvent<UsernameChangeEvent>(*this);
        registerEvent<ConnectEvent>(*this);
        registerEvent<DisconnectEvent>(*this);
        registerEvent<ConnectDeadlineEvent>(*this);
        registerEvent<CloseDeadlineEvent>(*this);
        registerEvent<FinalizeShutdownEvent>(*this);

        // Inform CommandLine actor of our ID
        if (_cmdline_actor_id.is_valid()) {
            push<SetWebSocketActorEvent>(_cmdline_actor_id, this->id());
        }

        co_return true;
    }

    // Handle user input from command line
    void
    on(const UserInputEvent &event) {
        if (!_connected) {
            push<DisplayMessageEvent>(_cmdline_actor_id, "Not connected to server!", "error");
            return;
        }

        send_chat_message(*event.message);
    }

    // Handle username changes
    void
    on(const UsernameChangeEvent &event) {
        std::string old_username = _username;
        _username                = event.new_username.c_str();

        push<DisplayMessageEvent>(_cmdline_actor_id, "Username changed from '" + old_username + "' to '" + _username + "'", "info");

        if (_connected && _user_announced) {
            send_username_update();
        }
    }

    // Handle connection requests
    void
    on(const ConnectEvent &event) {
        if (_shutdown_requested)
            return;
        if (_closing) {
            push<DisplayMessageEvent>(_cmdline_actor_id, "Close in progress; retry /connect after closure", "warning");
            return;
        }
        if (_connected || _connecting) {
            push<DisplayMessageEvent>(_cmdline_actor_id, "Already connected or connecting!", "warning");
            return;
        }

        _server_url = event.server_url.c_str();
        push<DisplayMessageEvent>(_cmdline_actor_id, "Connecting to " + _server_url + "...", "info");

        qb::io::uri uri(_server_url);

        // Extract host and port from URI
        std::string host = std::string(uri.host());
        uint16_t    port = uri.port().empty() ? 80 : qb::to_number<uint16_t>(uri.port()).value_or(80);

        push<DisplayMessageEvent>(_cmdline_actor_id, "Connecting to " + host + ":" + std::to_string(port), "info");

        // A standalone client's dispose stops its watcher, but retains the old socket,
        // buffers and protocol. Start each handshake with a fresh HTTP parser and key.
        reset_for_reconnect();
        reset_io_state();
        _ws_key = qb::http::ws::generateKey();
        this->switch_protocol<Protocol>(*this);
        _connecting                    = true;
        _close_sent                    = false;
        _connect_attempt               = std::make_shared<int>(0);
        const std::uint64_t generation = ++_connect_generation;

        // The URI form of async::tcp::connect still resolves DNS on this core.
        // Resolve on the offload pool, then use its endpoint-list connector with
        // a finite TCP deadline. The callback's weak token makes a cancelled or
        // destroyed actor inert without keeping it alive.
        const std::weak_ptr<int> attempt = _connect_attempt;
        const std::string        url     = _server_url;
        const int                family  = uri.af();
        try {
            // The TCP connector's deadline starts only after DNS. This actor-owned
            // timer bounds the whole intent even if the offload worker never returns.
            spawn([generation](qb::ScopedCoroContext ctx) -> qb::io::async::task<void> {
                co_await ctx.sleep(std::chrono::seconds(5));
                ctx.template push<ConnectDeadlineEvent>(generation);
            });
            spawn([this, attempt, url, family, host, port](qb::ScopedCoroContext ctx) -> qb::io::async::task<void> {
                try {
                    auto complete = [this, attempt, url](qb::io::tcp::socket &&socket) {
                        const auto live = attempt.lock();
                        if (!live)
                            return;
                        _connect_attempt.reset();
                        if (!socket.is_open()) {
                            _connecting = false;
                            push<DisplayMessageEvent>(_cmdline_actor_id, "Failed to connect to server", "error");
                            return;
                        }
                        try {
                            transport() = std::move(socket);
                            start();
                            send_websocket_handshake(qb::io::uri(url));
                        } catch (const std::exception &) {
                            _connecting = false;
                            disconnect_now();
                            push<DisplayMessageEvent>(_cmdline_actor_id, "Could not start WebSocket handshake", "error");
                        }
                    };

                    if (family == AF_UNIX) {
                        if (ctx.cancelled() || !attempt.lock())
                            co_return;
                        qb::io::async::tcp::connect<qb::io::tcp::socket>(qb::io::uri(url), std::move(complete), std::chrono::seconds(5));
                        co_return;
                    }
                    auto endpoints = co_await ctx.offload(
                        [](int af, std::string name, uint16_t number) { return qb::io::tcp::resolve_endpoints(af, name, number); }, family,
                        host, port);
                    if (ctx.cancelled() || !attempt.lock())
                        co_return;
                    if (endpoints.empty()) {
                        _connect_attempt.reset();
                        _connecting = false;
                        push<DisplayMessageEvent>(_cmdline_actor_id, "No address found for server", "error");
                        co_return;
                    }
                    qb::io::async::tcp::connect<qb::io::tcp::socket>(std::move(endpoints), host, std::move(complete), std::chrono::seconds(5));
                } catch (const qb::io::async::cancelled_error &) {
                    co_return;
                } catch (const std::exception &e) {
                    if (ctx.cancelled() || !attempt.lock())
                        co_return;
                    _connect_attempt.reset();
                    _connecting = false;
                    push<DisplayMessageEvent>(_cmdline_actor_id, "Connection setup failed: " + std::string(e.what()), "error");
                }
            });
        } catch (const std::exception &e) {
            _connect_attempt.reset();
            _connecting = false;
            push<DisplayMessageEvent>(_cmdline_actor_id, "Connection setup failed: " + std::string(e.what()), "error");
        }
    }

    // Handle disconnect requests
    void
    on(const DisconnectEvent &event) {
        // Invalidate any unresolved attempt even if a prior socket still awaits dispose.
        _connect_attempt.reset();
        if (_connecting && !transport().is_open()) {
            _connecting = false;
            push<DisplayMessageEvent>(_cmdline_actor_id, "Connection attempt cancelled", "info");
            return;
        }
        if (_closing) {
            push<DisplayMessageEvent>(_cmdline_actor_id, "Close in progress", "warning");
            return;
        }
        if (_connected || _connecting) {
            // This is an actor event, outside the I/O dispatch: finish dispose now so
            // a queued /connect cannot restart the old watcher before teardown.
            disconnect_now();
        } else {
            push<DisplayMessageEvent>(_cmdline_actor_id, "Not connected!", "warning");
        }
    }

    void
    on(const ConnectDeadlineEvent &event) {
        // A delayed event from an earlier attempt must not cancel a retry.
        if (!_connecting || event.generation != _connect_generation)
            return;
        _connect_attempt.reset();
        _connecting = false;
        if (transport().is_open())
            disconnect_now();
        push<DisplayMessageEvent>(_cmdline_actor_id, "Connection timed out", "error");
    }

    // Handle HTTP response to WebSocket handshake
    void
    on(Protocol::response &&response) {
        push<DisplayMessageEvent>(_cmdline_actor_id, "Received HTTP response: " + std::to_string(response.status()), "info");

        if (!this->switch_protocol<WS_Protocol>(*this, response, _ws_key)) {
            push<DisplayMessageEvent>(_cmdline_actor_id, "Failed to switch to WebSocket protocol", "error");
            disconnect();
        } else {
            _connecting = false;
            _connected  = true;
            push<DisplayMessageEvent>(_cmdline_actor_id, "✓ Connected to WebSocket server!", "success");

            // Send user joined notification
            send_user_joined();
        }
    }

    // Handle incoming WebSocket messages
    void
    on(WS_Protocol::message &&event) {
        try {
            std::string message_data(event.data, event.size);
            auto        message_json = qb::json::parse(message_data);

            std::string type = message_json.value("type", "");

            if (type == "message") {
                handle_chat_message(message_json);
            } else if (type == "user_joined") {
                handle_user_joined(message_json);
            } else if (type == "user_left") {
                handle_user_left(message_json);
            } else if (type == "username_changed") {
                handle_username_changed(message_json);
            } else if (type == "system") {
                handle_system_message(message_json);
            } else if (type == "error") {
                handle_error_message(message_json);
            } else {
                push<DisplayMessageEvent>(_cmdline_actor_id, "[UNKNOWN] " + message_data, "warning");
            }
        } catch (const std::exception &) {
            push<DisplayMessageEvent>(_cmdline_actor_id, "[PARSE ERROR] " + std::string(event.data, event.size), "error");
        }
    }

    // Handle ping frames from the server
    void
    on(WS_Protocol::ping &&event) {
        // Ping/pong is handled automatically by the WebSocket implementation
    }

    // Handle pong frames from the server
    void
    on(WS_Protocol::pong &&event) {
        // Automatic pong handling
    }

    // Handle close frames from the server
    void
    on(WS_Protocol::close &&event) {
        _closing = true;
        std::string close_reason(event.data, event.size);
        push<DisplayMessageEvent>(_cmdline_actor_id, "Connection closed: " + close_reason, "warning");
        // Declaring this handler suppresses the protocol's default Close echo.
        // The parser marked this frame for client-side masking. An initiated
        // close already sent one frame; do not send a second on its echo.
        if (!_close_sent) {
            *this << event.ws;
            _close_sent = true;
        }
        _connected      = false;
        _user_announced = false;
        // The protocol marks itself not-ok after this handler and disposes only
        // after its queued Close bytes have drained; dispose wakes shutdown.
    }

    // Handle TCP disconnection
    void
    on(qb::io::async::event::disconnected &&) {
        push<DisplayMessageEvent>(_cmdline_actor_id, "✗ Disconnected from server", "warning");
        _connecting     = false;
        _connected      = false;
        _user_announced = false;
    }

    void
    on(qb::io::async::event::dispose &&) {
        // The watcher is stopped; close the socket before allowing reconnect.
        transport().close();
        _closing    = false;
        _close_sent = false;
        if (_shutdown_requested)
            push<FinalizeShutdownEvent>(id());
    }

    void
    on(const CloseDeadlineEvent &) {
        if (!_shutdown_requested)
            return;
        // Peer Close or output drain may stall. This actor event runs outside the
        // I/O dispatch, so synchronous teardown is safe before actor destruction.
        disconnect_now();
        qb::Actor::kill();
    }

    void
    on(const FinalizeShutdownEvent &) {
        if (_shutdown_requested)
            qb::Actor::kill();
    }

private:
    void
    send_websocket_handshake(const qb::io::uri &uri) {
        qb::http::WebSocketRequest request(_ws_key);
        request.uri() = uri;
        uint16_t port = uri.port().empty() ? 80 : qb::to_number<uint16_t>(uri.port()).value_or(80);
        request.headers()["Host"].emplace_back(std::string(uri.host()) + ":" + std::to_string(port));

        push<DisplayMessageEvent>(_cmdline_actor_id, "Sending WebSocket handshake...", "info");
        *this << request;
    }

    void
    send_chat_message(const std::string &message) {
        qb::json message_obj = {
            {"type", "message"},
            {"username", _username},
            {"message", message},
            {"timestamp", std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count()}
        };

        qb::http::ws::MessageText ws_message;
        ws_message.masked = true;
        ws_message << message_obj.dump();
        *this << ws_message;
    }

    void
    send_user_joined() {
        if (_user_announced)
            return;

        qb::json message_obj = {
            {"type", "user_joined"},
            {"username", _username},
            {"timestamp", std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count()}
        };

        qb::http::ws::MessageText ws_message;
        ws_message.masked = true;
        ws_message << message_obj.dump();
        *this << ws_message;

        _user_announced = true;
        push<DisplayMessageEvent>(_cmdline_actor_id, "Announced presence to chat", "info");
    }

    void
    send_username_update() {
        qb::json message_obj = {
            {"type", "username_update"},
            {"username", _username},
            {"timestamp", std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count()}
        };

        qb::http::ws::MessageText ws_message;
        ws_message.masked = true;
        ws_message << message_obj.dump();
        *this << ws_message;
    }

    void
    handle_chat_message(const qb::json &msg) {
        std::string username = msg.value("username", "Unknown");
        std::string message  = msg.value("message", "");

        push<DisplayMessageEvent>(_cmdline_actor_id, "<" + username + "> " + message, "chat");
    }

    void
    handle_user_joined(const qb::json &msg) {
        std::string username = msg.value("username", "Unknown");
        push<DisplayMessageEvent>(_cmdline_actor_id, "★ " + username + " joined the chat", "system");
    }

    void
    handle_user_left(const qb::json &msg) {
        std::string username = msg.value("username", "Unknown");
        push<DisplayMessageEvent>(_cmdline_actor_id, "★ " + username + " left the chat", "system");
    }

    void
    handle_username_changed(const qb::json &msg) {
        // Try different possible field names
        std::string old_username = msg.value("old_username", msg.value("oldUsername", "Unknown"));
        std::string new_username = msg.value("new_username", msg.value("newUsername", msg.value("username", "Unknown")));
        push<DisplayMessageEvent>(_cmdline_actor_id, "★ " + old_username + " is now known as " + new_username, "system");
    }

    void
    handle_system_message(const qb::json &msg) {
        std::string message = msg.value("message", "");
        push<DisplayMessageEvent>(_cmdline_actor_id, "[SYSTEM] " + message, "system");
    }

    void
    handle_error_message(const qb::json &msg) {
        std::string message = msg.value("message", "Unknown error");
        push<DisplayMessageEvent>(_cmdline_actor_id, "[ERROR] " + message, "error");
    }

public:
    // Ctrl+C / SIGTERM. qb::Main::start() installs both, so every actor receives a
    // qb::SignalEvent; routing it into the KillEvent below keeps ONE shutdown path.
    // Both handlers must be PUBLIC — the router's dispatch trampoline calls
    // `handler.on(event)` from outside the class (qb/system/event/router.h).
    void
    on(const qb::SignalEvent &event) noexcept {
        qb::io::cout() << "Signal " << event.signum << " received." << std::endl;
        push<qb::KillEvent>(id());
    }

    void
    on(const qb::KillEvent &event) noexcept {
        if (_shutdown_requested)
            return;
        _shutdown_requested = true;
        _connect_attempt.reset();
        if (_connected || _closing) {
            if (_connected && !_close_sent) {
                try {
                    qb::http::ws::MessageClose close_msg(qb::http::ws::CloseStatus::Normal, "Client shutting down");
                    close_msg.masked = true;
                    *this << close_msg;
                    _close_sent = true;
                    _closing    = true;
                } catch (const std::exception &) {
                    disconnect_now();
                    qb::Actor::kill();
                    return;
                }
            }
            // Wait for the peer's Close or a transport drop. If neither arrives,
            // force teardown after a bounded grace period instead of hanging join().
            try {
                spawn([](qb::ScopedCoroContext ctx) -> qb::io::async::task<void> {
                    co_await ctx.sleep(std::chrono::seconds(2));
                    ctx.template push<CloseDeadlineEvent>();
                });
            } catch (const std::exception &) {
                disconnect_now();
                qb::Actor::kill();
            }
            return;
        }

        if (_connecting || transport().is_open())
            disconnect_now();
        qb::Actor::kill();
    }
};

/**
 * @brief Command line interface actor for user interaction
 *
 * This actor handles:
 * - Non-blocking user input from the cancellable source owned by main
 * - Command parsing (/connect, /username, /quit, etc.)
 * - One automatic connection, after the WebSocket actor announces its ID, when given a URL
 * - Message display with color coding
 * - Help and status information
 */
class CommandLineActor : public qb::Actor {
private:
    qb::ActorId                    _websocket_actor_id;
    qb_example::input::LineSource &_input;
    bool                           _running          = true;
    std::string                    _current_username = "CLIUser";
    std::string                    _initial_url;

public:
    CommandLineActor(qb::ActorId &websocket_id, qb_example::input::LineSource &input, std::string initial_url)
        : _websocket_actor_id(websocket_id)
        , _input(input)
        , _initial_url(std::move(initial_url)) {}

    void
    set_websocket_actor_id(qb::ActorId websocket_id) {
        _websocket_actor_id = websocket_id;
    }

    qb::io::async::task<bool>
    onInit() override {
        // Shutdown wiring. Event dispatch is by SUBSCRIPTION, not by vtable: qb::Actor's
        // constructor already subscribed its own default handlers for these two, and
        // re-registering here is what replaces them with OURS. Without these two lines
        // the handlers below compile, are never called, and their cleanup is lost.
        registerEvent<qb::KillEvent>(*this);
        registerEvent<qb::SignalEvent>(*this);

        qb::io::cout() << "=== QB WebSocket Chat Client ===\n";
        qb::io::cout() << "Type /help for available commands\n";
        qb::io::cout() << "Type /connect ws://localhost:8080/ws to connect to server\n\n";

        // Register events
        registerEvent<DisplayMessageEvent>(*this);
        registerEvent<SetWebSocketActorEvent>(*this);
        registerEvent<InputPollTick>(*this);
        registerEvent<ShutdownTick>(*this);

        // Start periodic checking for input with a small delay
        schedule_input_check();

        co_return true;
    }

    void
    on(const DisplayMessageEvent &event) {
        display_message(*event.message, event.type.c_str());
    }

    void
    on(const SetWebSocketActorEvent &event) {
        _websocket_actor_id = event.websocket_actor_id;
        if (!_websocket_actor_id.is_valid()) {
            return;
        }
        display_message("WebSocket actor connected to CommandLine interface", "info");
        if (_running && !_initial_url.empty()) {
            // The actor ID arrives after both actors start. Consume the argument once,
            // through the same validation and event path as an interactive /connect.
            std::string url = std::move(_initial_url);
            _initial_url.clear();
            request_connect(url);
        }
    }

    /**
     * @brief One turn of the input poll, 50 ms after the previous one
     *
     * A handler only ever runs on a live actor, so there is nothing to guard against here — the
     * `if (is_alive())` that used to wrap this body was itself the use-after-free it pretended
     * to prevent.
     */
    void
    on(const InputPollTick &) {
        poll_input_once();
    }

    void
    on(const ShutdownTick &) {
        // Also covers EOF before SetWebSocketActorEvent delivered the peer ID.
        // A second KillEvent cannot interrupt an in-progress Close handshake.
        broadcast<qb::KillEvent>();
    }

private:
    void
    schedule_input_check() {
        // The 50 ms wait lives in a coroutine this actor's cancellation scope owns, so killing
        // the actor ends the polling loop instead of leaving a timer aimed at freed memory.
        // The body touches no actor state: `ctx.push` addresses the actor by id.
        spawn([](qb::ScopedCoroContext ctx) -> qb::io::async::task<void> {
            co_await ctx.sleep(std::chrono::milliseconds(50));
            ctx.template push<InputPollTick>();
        });
    }

    void
    poll_input_once() {
        if (!_running)
            return;

        // Bound work per tick so a large pasted input cannot starve socket events.
        qb_example::input::Event next;
        for (std::size_t drained = 0; drained < 16 && _input.try_pop(next); ++drained) {
            if (next.kind != qb_example::input::Event::Kind::line) {
                if (next.kind == qb_example::input::Event::Kind::error) {
                    display_message("Console input failed; closing chat client", "error");
                }
                begin_shutdown();
                return;
            }
            if (!next.text.empty()) {
                handle_input(next.text);
                if (!_running) {
                    return;
                }
            }
        }

        // Schedule next check in 50ms
        schedule_input_check();
    }

    void
    handle_input(const std::string &input) {
        // Handle commands
        if (input[0] == '/') {
            handle_command(input);
        } else {
            // Regular chat message
            if (_websocket_actor_id.is_valid()) {
                // Display only the server echo after it accepts the message.
                push<UserInputEvent>(_websocket_actor_id, input);
            } else {
                display_message("WebSocket connection not initialized. Please wait...", "error");
            }
        }
    }

    void
    handle_command(const std::string &command) {
        if (command == "/help") {
            show_help();
        } else if (command == "/quit" || command == "/exit") {
            begin_shutdown();
        } else if (command.substr(0, 9) == "/connect ") {
            request_connect(std::string_view(command).substr(9));
        } else if (command.substr(0, 10) == "/username ") {
            std::string new_username = command.substr(10);
            if (!new_username.empty() && new_username.length() <= 50 && _websocket_actor_id.is_valid()) {
                _current_username = new_username;
                push<UsernameChangeEvent>(_websocket_actor_id, new_username);
            } else if (!_websocket_actor_id.is_valid()) {
                display_message("WebSocket connection not initialized. Please wait...", "error");
            } else {
                display_message("Usage: /username <new_name> (1-50 characters)", "error");
            }
        } else if (command == "/disconnect") {
            if (_websocket_actor_id.is_valid()) {
                push<DisconnectEvent>(_websocket_actor_id);
            } else {
                display_message("WebSocket connection not initialized", "error");
            }
        } else if (command == "/status") {
            display_message("Current username: " + _current_username, "info");
        } else {
            display_message("Unknown command: " + command + " (type /help for help)", "error");
        }
    }

    void
    request_connect(std::string_view url) {
        if (url.empty()) {
            display_message("Usage: /connect <websocket_url>", "error");
        } else if (url.size() > qb::string<256>{}.capacity() || url.find('\0') != std::string_view::npos) {
            display_message("WebSocket URL must be at most 256 bytes and contain no NUL", "error");
        } else if (!_websocket_actor_id.is_valid()) {
            display_message("WebSocket connection not initialized. Please wait...", "error");
        } else {
            push<ConnectEvent>(_websocket_actor_id, url);
        }
    }

    void
    show_help() {
        qb::io::cout() << "\n=== Available Commands ===\n";
        qb::io::cout() << "/connect <url>    - Connect to WebSocket server (URL up to 256 bytes)\n";
        qb::io::cout() << "/disconnect       - Disconnect from server\n";
        qb::io::cout() << "/username <name>  - Change your username\n";
        qb::io::cout() << "/status          - Show current status\n";
        qb::io::cout() << "/help            - Show this help message\n";
        qb::io::cout() << "/quit or /exit   - Exit the chat client\n";
        qb::io::cout() << "\nType any other text to send a chat message\n\n";
    }

    void
    display_message(const std::string &message, const std::string &type) {
        if (type == "error") {
            qb::io::cout() << "\033[31m" << message << "\033[0m\n"; // Red
        } else if (type == "success") {
            qb::io::cout() << "\033[32m" << message << "\033[0m\n"; // Green
        } else if (type == "warning") {
            qb::io::cout() << "\033[33m" << message << "\033[0m\n"; // Yellow
        } else if (type == "info") {
            qb::io::cout() << "\033[36m" << message << "\033[0m\n"; // Cyan
        } else if (type == "system") {
            qb::io::cout() << "\033[35m" << message << "\033[0m\n"; // Magenta
        } else if (type == "chat") {
            qb::io::cout() << "\033[37m" << message << "\033[0m\n"; // White
        } else if (type == "join") {
            qb::io::cout() << "\033[32m" << message << "\033[0m\n"; // Green for joins
        } else if (type == "leave") {
            qb::io::cout() << "\033[31m" << message << "\033[0m\n"; // Red for leaves
        } else if (type == "rename") {
            qb::io::cout() << "\033[33m" << message << "\033[0m\n"; // Yellow for renames
        } else {
            qb::io::cout() << message << "\n"; // Default
        }
        // qb::io::cout flushes this complete line under its output lock.
    }

    void
    begin_shutdown() {
        if (!_running) {
            return;
        }
        _running = false;
        display_message("Shutting down chat client...", "info");

        // Ask the WebSocket actor to send its masked Close and finish its own
        // bounded handshake. DisconnectEvent would tear the socket down first.
        if (_websocket_actor_id.is_valid()) {
            push<qb::KillEvent>(_websocket_actor_id);
        }
        spawn([](qb::ScopedCoroContext ctx) -> qb::io::async::task<void> {
            co_await ctx.sleep(std::chrono::milliseconds(500));
            ctx.template push<ShutdownTick>();
        });
    }

public:
    // Ctrl+C / SIGTERM. qb::Main::start() installs both, so every actor receives a
    // qb::SignalEvent; routing it into the KillEvent below keeps ONE shutdown path.
    // Both handlers must be PUBLIC — the router's dispatch trampoline calls
    // `handler.on(event)` from outside the class (qb/system/event/router.h).
    void
    on(const qb::SignalEvent &event) noexcept {
        qb::io::cout() << "Signal " << event.signum << " received." << std::endl;
        push<qb::KillEvent>(id());
    }

    void
    on(const qb::KillEvent &event) noexcept {
        this->kill();
    }
};

int
main(int argc, char *argv[]) {
    if (auto helper = qb_example::input::maybe_run_helper(argc, argv)) {
        return *helper;
    }

    // The source outlives the engine; only main stops and joins its reader.
    qb_example::input::LineSource input;
    qb::Main                      engine;

    qb::io::cout() << "QB WebSocket Chat Client Configuration:\n";

    // Create actors - CommandLine first since WebSocket needs its ID
    auto        websocket_actor_id = qb::ActorId{};
    std::string initial_url        = argc > 1 ? argv[1] : "";
    auto        cmdline_actor_id = engine.addActor<CommandLineActor>(0, std::ref(websocket_actor_id), std::ref(input), std::move(initial_url));
    websocket_actor_id           = engine.addActor<WebSocketClientActor>(1, cmdline_actor_id);

    qb::io::cout() << "WebSocket Actor ID: " << websocket_actor_id.index() << "\n";
    qb::io::cout() << "CommandLine Actor ID: " << cmdline_actor_id.index() << "\n\n";

    // Handle command line arguments
    if (argc > 1) {
        qb::io::cout() << "Auto-connect requested: " << argv[1] << "\n\n";
    }

    // Start the engine
    engine.start();
    engine.join();
    input.stop();

    if (engine.hasError()) {
        qb::io::cerr() << "Engine error occurred" << std::endl;
        return 1;
    }

    return 0;
}
