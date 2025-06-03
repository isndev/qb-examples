/**
 * @file 02_chat_client.cpp
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

#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include <atomic>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <qb/main.h>
#include <qb/actor.h>
#include <qb/io/async.h>
#include <qb/io/protocol/text.h>
#include <qb/io/system/file.h>
#include <qb/io/transport/file.h>
#include <qb/io/protocol/text.h>
#include <ws/ws.h>
#include <qb/json.h>

// Forward declarations
class WebSocketClientActor;
class CommandLineActor;

// Events for communication between actors
struct UserInputEvent : qb::Event {
    std::string message;
    explicit UserInputEvent(std::string msg) : message(std::move(msg)) {}
};

struct UsernameChangeEvent : qb::Event {
    std::string new_username;
    explicit UsernameChangeEvent(std::string username) : new_username(std::move(username)) {}
};

struct ConnectEvent : qb::Event {
    std::string server_url;
    explicit ConnectEvent(std::string url) : server_url(std::move(url)) {}
};

struct DisconnectEvent : qb::Event {};

struct SetWebSocketActorEvent : qb::Event {
    qb::ActorId websocket_actor_id;
    explicit SetWebSocketActorEvent(qb::ActorId id) : websocket_actor_id(id) {}
};

struct DisplayMessageEvent : qb::Event {
    std::string message;
    std::string type;
    explicit DisplayMessageEvent(std::string msg, std::string msg_type = "info") 
        : message(std::move(msg)), type(std::move(msg_type)) {}
};

// Thread-safe input queue for cross-platform stdin handling
class InputQueue {
private:
    std::queue<std::string> _queue;
    std::mutex _mutex;
    std::condition_variable _cv;
    std::atomic<bool> _shutdown{false};

public:
    void push(const std::string& input) {
        {
            std::lock_guard<std::mutex> lock(_mutex);
            if (!_shutdown) {
                _queue.push(input);
            }
        }
        _cv.notify_one();
    }

    bool try_pop(std::string& input) {
        std::lock_guard<std::mutex> lock(_mutex);
        if (!_queue.empty()) {
            input = _queue.front();
            _queue.pop();
            return true;
        }
        return false;
    }

    void shutdown() {
        {
            std::lock_guard<std::mutex> lock(_mutex);
            _shutdown = true;
        }
        _cv.notify_all();
    }

    bool is_shutdown() const {
        return _shutdown.load();
    }
};

// Global input queue - shared between input thread and actor
static InputQueue g_input_queue;

/**
 * @brief WebSocket client actor for chat communication
 * 
 * This actor handles:
 * - WebSocket connection to the chat server
 * - Sending/receiving chat messages
 * - Protocol event handling (connect, disconnect, errors)
 * - Message parsing and formatting
 */
class WebSocketClientActor : public qb::Actor, public qb::io::use<WebSocketClientActor>::tcp::client<> {
private:
    std::string _username = "CLIUser";
    std::string _server_url;
    qb::ActorId _cmdline_actor_id;
    bool _connected = false;
    bool _user_announced = false;
    std::string _ws_key;

public:
    using Protocol    = qb::http::protocol_view<WebSocketClientActor>;
    using WS_Protocol = qb::http::ws::protocol<WebSocketClientActor>;

    explicit WebSocketClientActor(qb::ActorId cmdline_id) 
        : _cmdline_actor_id(cmdline_id), _ws_key(qb::http::ws::generateKey()) {}

    bool onInit() override {
        std::cout << "[CLIENT] WebSocket client actor started" << std::endl;
        
        // Register events
        registerEvent<UserInputEvent>(*this);
        registerEvent<UsernameChangeEvent>(*this);
        registerEvent<ConnectEvent>(*this);
        registerEvent<DisconnectEvent>(*this);
        
        // Inform CommandLine actor of our ID
        if (_cmdline_actor_id.is_valid()) {
            push<SetWebSocketActorEvent>(_cmdline_actor_id, this->id());
        }
        
        return true;
    }

    // Handle user input from command line
    void on(const UserInputEvent& event) {
        if (!_connected) {
            push<DisplayMessageEvent>(_cmdline_actor_id, "Not connected to server!", "error");
            return;
        }

        send_chat_message(event.message);
    }

    // Handle username changes
    void on(const UsernameChangeEvent& event) {
        std::string old_username = _username;
        _username = event.new_username;
        
        push<DisplayMessageEvent>(_cmdline_actor_id, 
            "Username changed from '" + old_username + "' to '" + _username + "'", "info");
        
        if (_connected && _user_announced) {
            send_username_update();
        }
    }

    // Handle connection requests
    void on(const ConnectEvent& event) {
        if (_connected) {
            push<DisplayMessageEvent>(_cmdline_actor_id, "Already connected!", "warning");
            return;
        }

        _server_url = event.server_url;
        push<DisplayMessageEvent>(_cmdline_actor_id, "Connecting to " + _server_url + "...", "info");
        
        qb::io::uri uri(_server_url);
        
        // Extract host and port from URI
        std::string host = std::string(uri.host());
        uint16_t port = uri.port().empty() ? 80 : std::stoi(std::string(uri.port()));
        
        push<DisplayMessageEvent>(_cmdline_actor_id, "Connecting to " + host + ":" + std::to_string(port), "info");
        
        // Connect to the server
        if (transport().connect(uri) == qb::io::SocketStatus::Done) {
            start();
            send_websocket_handshake(uri);
        } else {
            push<DisplayMessageEvent>(_cmdline_actor_id, "Failed to connect to server", "error");
        }
    }

    // Handle disconnect requests
    void on(const DisconnectEvent& event) {
        if (_connected) {
            disconnect();
        } else {
            push<DisplayMessageEvent>(_cmdline_actor_id, "Not connected!", "warning");
        }
    }

    // Handle HTTP response to WebSocket handshake
    void on(Protocol::response &&response) {
        push<DisplayMessageEvent>(_cmdline_actor_id, 
            "Received HTTP response: " + std::to_string(response.status()), "info");

        if (!this->switch_protocol<WS_Protocol>(*this, response, _ws_key)) {
            push<DisplayMessageEvent>(_cmdline_actor_id, "Failed to switch to WebSocket protocol", "error");
            disconnect();
        } else {
            _connected = true;
            push<DisplayMessageEvent>(_cmdline_actor_id, "✓ Connected to WebSocket server!", "success");
            
            // Send user joined notification
            send_user_joined();
        }
    }

    // Handle incoming WebSocket messages
    void on(WS_Protocol::message &&event) {
        try {
            std::string message_data(event.data, event.size);
            auto message_json = qb::json::parse(message_data);
            
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
                push<DisplayMessageEvent>(_cmdline_actor_id, 
                    "[UNKNOWN] " + message_data, "warning");
            }
        } catch (const std::exception& e) {
            push<DisplayMessageEvent>(_cmdline_actor_id, 
                "[PARSE ERROR] " + std::string(event.data, event.size), "error");
        }
    }

    // Handle ping frames from the server
    void on(WS_Protocol::ping &&event) {
        // Ping/pong is handled automatically by the WebSocket implementation
    }

    // Handle pong frames from the server
    void on(WS_Protocol::pong &&event) {
        // Automatic pong handling
    }

    // Handle close frames from the server
    void on(WS_Protocol::close &&event) {
        std::string close_reason(event.data, event.size);
        push<DisplayMessageEvent>(_cmdline_actor_id, 
            "Connection closed: " + close_reason, "warning");
        _connected = false;
        _user_announced = false;
    }

    // Handle TCP disconnection
    void on(qb::io::async::event::disconnected &&) {
        push<DisplayMessageEvent>(_cmdline_actor_id, "✗ Disconnected from server", "warning");
        _connected = false;
        _user_announced = false;
    }

private:
    void send_websocket_handshake(const qb::io::uri& uri) {
        qb::http::WebSocketRequest request(_ws_key);
        request.uri() = uri;
        uint16_t port = uri.port().empty() ? 80 : std::stoi(std::string(uri.port()));
        request.headers()["Host"].emplace_back(std::string(uri.host()) + ":" + std::to_string(port));
        
        push<DisplayMessageEvent>(_cmdline_actor_id, "Sending WebSocket handshake...", "info");
        *this << request;
    }

    void send_chat_message(const std::string& message) {
        qb::json message_obj = {
            {"type", "message"},
            {"username", _username},
            {"message", message},
            {"timestamp", std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count()}
        };
        
        qb::http::ws::MessageText ws_message;
        ws_message.masked = true;
        ws_message << message_obj.dump();
        *this << ws_message;
    }

    void send_user_joined() {
        if (_user_announced) return;
        
        qb::json message_obj = {
            {"type", "user_joined"},
            {"username", _username},
            {"timestamp", std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count()}
        };
        
        qb::http::ws::MessageText ws_message;
        ws_message.masked = true;
        ws_message << message_obj.dump();
        *this << ws_message;
        
        _user_announced = true;
        push<DisplayMessageEvent>(_cmdline_actor_id, "Announced presence to chat", "info");
    }

    void send_username_update() {
        qb::json message_obj = {
            {"type", "username_update"},
            {"username", _username},
            {"timestamp", std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count()}
        };
        
        qb::http::ws::MessageText ws_message;
        ws_message.masked = true;
        ws_message << message_obj.dump();
        *this << ws_message;
    }

    void handle_chat_message(const qb::json& msg) {
        std::string username = msg.value("username", "Unknown");
        std::string message = msg.value("message", "");
        
        push<DisplayMessageEvent>(_cmdline_actor_id, 
            "<" + username + "> " + message, "chat");
    }

    void handle_user_joined(const qb::json& msg) {
        std::string username = msg.value("username", "Unknown");
        push<DisplayMessageEvent>(_cmdline_actor_id, 
            "★ " + username + " joined the chat", "system");
    }

    void handle_user_left(const qb::json& msg) {
        std::string username = msg.value("username", "Unknown");
        push<DisplayMessageEvent>(_cmdline_actor_id, 
            "★ " + username + " left the chat", "system");
    }

    void handle_username_changed(const qb::json& msg) {
        // Try different possible field names
        std::string old_username = msg.value("old_username", 
                                    msg.value("oldUsername", "Unknown"));
        std::string new_username = msg.value("new_username", 
                                    msg.value("newUsername", 
                                    msg.value("username", "Unknown")));
        push<DisplayMessageEvent>(_cmdline_actor_id, 
            "★ " + old_username + " is now known as " + new_username, "system");
    }

    void handle_system_message(const qb::json& msg) {
        std::string message = msg.value("message", "");
        push<DisplayMessageEvent>(_cmdline_actor_id, 
            "[SYSTEM] " + message, "system");
    }

    void handle_error_message(const qb::json& msg) {
        std::string message = msg.value("message", "Unknown error");
        push<DisplayMessageEvent>(_cmdline_actor_id, 
            "[ERROR] " + message, "error");
    }

    void on(const qb::KillEvent& event) noexcept {
        if (_connected) {
            // Send close frame before disconnecting
            qb::http::ws::MessageClose close_msg(qb::http::ws::CloseStatus::Normal, "Client shutting down");
            *this << close_msg;
        }
        
        disconnect();
        qb::Actor::kill();
    }
};

/**
 * @brief Command line interface actor for user interaction
 * 
 * This actor handles:
 * - Non-blocking user input via polling thread-safe queue
 * - Command parsing (/connect, /username, /quit, etc.)
 * - Message display with color coding
 * - Help and status information
 */
class CommandLineActor : public qb::Actor {
private:
    qb::ActorId _websocket_actor_id;
    std::thread _input_thread;
    std::atomic<bool> _running{true};
    std::string _current_username = "CLIUser";

public:
    explicit CommandLineActor(qb::ActorId& websocket_id) 
        : _websocket_actor_id(websocket_id) {}

    // Destructor to ensure thread is properly cleaned up
    ~CommandLineActor() {
        cleanup_thread();
    }

    void set_websocket_actor_id(qb::ActorId websocket_id) {
        _websocket_actor_id = websocket_id;
    }

    bool onInit() override {
        std::cout << "=== QB WebSocket Chat Client ===\n";
        std::cout << "Type /help for available commands\n";
        std::cout << "Type /connect ws://localhost:8080/ws to connect to server\n\n";
        
        // Register events
        registerEvent<DisplayMessageEvent>(*this);
        registerEvent<SetWebSocketActorEvent>(*this);
        
        // Start input handling thread
        _input_thread = std::thread([this]() {
            std::string line;
            while (_running && std::getline(std::cin, line)) {
                if (!line.empty()) {
                    g_input_queue.push(line);
                }
            }
            g_input_queue.shutdown();
        });
        
        // Start periodic checking for input with a small delay
        schedule_input_check();
        
        return true;
    }

    void on(const DisplayMessageEvent& event) {
        display_message(event.message, event.type);
    }

    void on(const SetWebSocketActorEvent& event) {
        _websocket_actor_id = event.websocket_actor_id;
        display_message("WebSocket actor connected to CommandLine interface", "info");
    }

private:
    void schedule_input_check() {
        if (!is_alive() || !_running) return;
        
        // Check for input non-blockingly
        std::string input;
        if (g_input_queue.try_pop(input)) {
            handle_input(input);
        }
        
        // Schedule next check in 50ms
        qb::io::async::callback([this]() {
            if (is_alive()) {
                schedule_input_check();
            }
        }, 0.05);
    }

    void handle_input(const std::string& input) {
        // Handle commands
        if (input[0] == '/') {
            handle_command(input);
        } else {
            // Regular chat message
            if (_websocket_actor_id.is_valid()) {
                push<UserInputEvent>(_websocket_actor_id, input);
                display_message("<" + _current_username + "> " + input, "own");
            } else {
                display_message("WebSocket connection not initialized. Please wait...", "error");
            }
        }
    }

    void handle_command(const std::string& command) {
        if (command == "/help") {
            show_help();
        } else if (command == "/quit" || command == "/exit") {
            _running = false;  // Stop the input thread
            display_message("Shutting down chat client...", "info");
            
            // Send disconnect to WebSocket first
            if (_websocket_actor_id.is_valid()) {
                push<DisconnectEvent>(_websocket_actor_id);
            }
            
            // Broadcast KillEvent to all actors to shutdown gracefully
            qb::io::async::callback([this]() {
                if (is_alive()) {
                    broadcast<qb::KillEvent>();
                }
            }, 0.5);  // Small delay to let disconnect finish
        } else if (command.substr(0, 9) == "/connect ") {
            std::string url = command.substr(9);
            if (!url.empty() && _websocket_actor_id.is_valid()) {
                push<ConnectEvent>(_websocket_actor_id, url);
            } else if (!_websocket_actor_id.is_valid()) {
                display_message("WebSocket connection not initialized. Please wait...", "error");
            } else {
                display_message("Usage: /connect <websocket_url>", "error");
            }
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

    void show_help() {
        std::cout << "\n=== Available Commands ===\n";
        std::cout << "/connect <url>    - Connect to WebSocket server (e.g., ws://localhost:8080/ws)\n";
        std::cout << "/disconnect       - Disconnect from server\n";
        std::cout << "/username <name>  - Change your username\n";
        std::cout << "/status          - Show current status\n";
        std::cout << "/help            - Show this help message\n";
        std::cout << "/quit or /exit   - Exit the chat client\n";
        std::cout << "\nType any other text to send a chat message\n\n";
    }

    void display_message(const std::string& message, const std::string& type) {
        if (type == "error") {
            std::cout << "\033[31m" << message << "\033[0m\n"; // Red
        } else if (type == "success") {
            std::cout << "\033[32m" << message << "\033[0m\n"; // Green
        } else if (type == "warning") {
            std::cout << "\033[33m" << message << "\033[0m\n"; // Yellow
        } else if (type == "info") {
            std::cout << "\033[36m" << message << "\033[0m\n"; // Cyan
        } else if (type == "system") {
            std::cout << "\033[35m" << message << "\033[0m\n"; // Magenta
        } else if (type == "chat") {
            std::cout << "\033[37m" << message << "\033[0m\n"; // White
        } else if (type == "own") {
            std::cout << "\033[32m" << message << "\033[0m\n"; // Green for own messages
        } else if (type == "join") {
            std::cout << "\033[32m" << message << "\033[0m\n"; // Green for joins
        } else if (type == "leave") {
            std::cout << "\033[31m" << message << "\033[0m\n"; // Red for leaves
        } else if (type == "rename") {
            std::cout << "\033[33m" << message << "\033[0m\n"; // Yellow for renames
        } else {
            std::cout << message << "\n"; // Default
        }
    }

    void cleanup_thread() {
        _running = false;
        g_input_queue.shutdown();
        
        // Make sure thread is properly joined
        if (_input_thread.joinable()) {
            // Give the thread a moment to finish reading current line
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            _input_thread.join();
        }
    }

    void on(const qb::KillEvent& event) noexcept {
        cleanup_thread();
        this->kill();
    }
};

int main(int argc, char* argv[]) {
    qb::Main engine;
    
    std::cout << "QB WebSocket Chat Client Configuration:\n";
    
    // Create actors - CommandLine first since WebSocket needs its ID
    auto websocket_actor_id = qb::ActorId{};
    auto cmdline_actor_id = engine.addActor<CommandLineActor>(0, websocket_actor_id);
    websocket_actor_id = engine.addActor<WebSocketClientActor>(1, cmdline_actor_id);
    
    std::cout << "WebSocket Actor ID: " << websocket_actor_id.index() << "\n";
    std::cout << "CommandLine Actor ID: " << cmdline_actor_id.index() << "\n\n";
    
    // Handle command line arguments
    if (argc > 1) {
        std::string server_url = argv[1];
        std::cout << "Auto-connecting to: " << server_url << "\n";
        std::cout << "After startup, type: /connect " << server_url << " to connect\n\n";
    }
    
    // Start the engine
    engine.start();
    engine.join();
    
    if (engine.hasError()) {
        std::cerr << "Engine error occurred" << std::endl;
        return 1;
    }
    
    return 0;
} 