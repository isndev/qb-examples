/**
 * @file 01_chat_server.cpp
 * @brief WebSocket Chat Server with Static File Serving
 *
 * This example demonstrates:
 * - Serving static chat interface files (HTML, CSS, JS)
 * - WebSocket endpoint for real-time chat communication
 * - Combined HTTP static files + WebSocket server
 * - Modern chat UI with real-time messaging
 *
 * @author qb - C++ Actor Framework
 * @copyright Copyright (c) 2011-2025 qb - isndev (cpp.actor)
 * Licensed under the Apache License, Version 2.0 (http://www.apache.org/licenses/LICENSE-2.0)
 * @ingroup Examples
 */

#include <iostream>
#include <filesystem>
#include <qb/main.h>
#include <http/http.h>
#include <ws/ws.h>
#include <http/middleware/static_files.h>
#include <http/middleware/cors.h>
#include <http/middleware/logging.h>

class ChatServer;
class ChatSession : public qb::http::use<ChatSession>::session<ChatServer> {
public:
    using ws_protocol = qb::http::ws::protocol<ChatSession>;

    // Message type constants for better maintainability
    static constexpr const char* MSG_TYPE_MESSAGE = "message";
    static constexpr const char* MSG_TYPE_USER_JOINED = "user_joined";
    static constexpr const char* MSG_TYPE_USER_LEFT = "user_left";
    static constexpr const char* MSG_TYPE_USERNAME_UPDATE = "username_update";
    static constexpr const char* MSG_TYPE_USERNAME_CHANGED = "username_changed";
    static constexpr const char* MSG_TYPE_SYSTEM = "system";
    static constexpr const char* MSG_TYPE_ERROR = "error";

    ChatSession(ChatServer &server) : session(server) {}

    void on(ws_protocol::message &&event) {
        try {
            // Parse JSON message from client
            auto        message_json = qb::json::parse(event.ws.data().view());
            std::string type = message_json.value("type", "");
            
            if (type == MSG_TYPE_MESSAGE) {
                handle_chat_message(message_json);
            } else if (type == MSG_TYPE_USER_JOINED) {
                handle_user_joined(message_json);
            } else if (type == MSG_TYPE_USERNAME_UPDATE) {
                handle_username_update(message_json);
            } else {
                std::cout << "Unknown message type: " << type << std::endl;
                send_error("Unknown message type: " + type);
            }
        } catch (const std::exception& e) {
            std::cout << "Error parsing WebSocket message: " << e.what() << std::endl;
            std::cout << "Raw message data: " << event.data << std::endl;
            send_error("Invalid message format");
        }
    }

    void switch_to_ws(qb::http::Context<ChatSession> *ctx) {
        _ws = this->template switch_protocol<ChatSession::ws_protocol>(*this, ctx->request(), ctx->response());
        if (ctx->response().status() != qb::http::Status::SWITCHING_PROTOCOLS) {
            std::cerr << "Failed to switch to WebSocket protocol" << std::endl;
            ctx->response().status() = qb::http::Status::INTERNAL_SERVER_ERROR;
        } else {
            keep_alive(true);
            setTimeout(0); // Disable timeout for WebSocket sessions
        }
        ctx->complete();
    }

    bool is_ws() const {
        return _ws != nullptr;
    }

    const std::string& get_username() const {
        return _username;
    }

private:
    ws_protocol *_ws = nullptr;
    std::string _username = "Anonymous";
    bool _user_announced = false;

    // Helper method to create JSON messages with common fields
    qb::json create_message(const std::string& type, uint64_t timestamp = 0) {
        return qb::json{
            {"type", type},
            {"timestamp", timestamp == 0 ? qb::Timestamp::now().count() : timestamp}
        };
    }

    void handle_chat_message(const qb::json& message_json) {
        std::string username = message_json.value("username", "Anonymous");
        std::string message = message_json.value("message", "");
        uint64_t timestamp = message_json.value("timestamp", 0ULL);
        
        if (message.empty()) {
            send_error("Empty message");
            return;
        }
        
        // Validate message length (prevent spam/DoS)
        if (message.length() > 1000) {
            send_error("Message too long (max 1000 characters)");
            return;
        }
        
        // Update our username if it changed
        _username = username;
        
        std::cout << "[CHAT] " << username << ": " << message << std::endl;
        
        // Create response message to broadcast
        auto response = create_message(MSG_TYPE_MESSAGE, timestamp);
        response["username"] = username;
        response["message"] = message;
        
        // Broadcast to all WebSocket sessions
        broadcast_to_websockets(response.dump());
    }
    
    void handle_user_joined(const qb::json& message_json) {
        std::string username = message_json.value("username", "Anonymous");
        _username = username;
        
        if (!_user_announced) {
            _user_announced = true;
            std::cout << "[JOIN] " << username << " joined the chat" << std::endl;
            
            // Notify other users (except this one)
            auto response = create_message(MSG_TYPE_USER_JOINED, message_json.value("timestamp", 0ULL));
            response["username"] = username;
            
            broadcast_to_websockets(response.dump(), this); // Exclude this session
            
            // Send welcome message to this user
            auto welcome = create_message(MSG_TYPE_SYSTEM);
            welcome["message"] = "Welcome to QB WebSocket Chat, " + username + "!";
            
            send_ws_message(welcome.dump());
        }
    }
    
    void handle_username_update(const qb::json& message_json) {
        std::string new_username = message_json.value("username", "Anonymous");
        std::string old_username = _username;
        
        // Validate username (basic sanitization)
        if (new_username.empty() || new_username.length() > 50) {
            send_error("Invalid username (1-50 characters required)");
            return;
        }
        
        if (new_username != old_username && _user_announced) {
            _username = new_username;
            
            std::cout << "[USERNAME] " << old_username << " is now " << new_username << std::endl;
            
            // Notify all users about username change
            auto response = create_message(MSG_TYPE_USERNAME_CHANGED, message_json.value("timestamp", 0ULL));
            response["old_username"] = old_username;
            response["new_username"] = new_username;
            
            broadcast_to_websockets(response.dump());
        } else {
            _username = new_username;
        }
    }
    
    void send_ws_message(const std::string& message) {
        if (_ws) {
            qb::http::ws::MessageText ws_message;
            ws_message.masked = false;
            ws_message << message;
            *this << ws_message;
        }
    }
    
    void send_error(const std::string& error_message) {
        auto error_response = create_message(MSG_TYPE_ERROR);
        error_response["message"] = error_message;
        
        send_ws_message(error_response.dump());
    }
    
    void broadcast_to_websockets(const std::string& message, ChatSession* exclude = nullptr);
    
public:
    // Called when session is destroyed (user disconnects)
    ~ChatSession();
};

class ChatServer
 : public qb::Actor
 , public qb::http::use<ChatServer>::template server<ChatSession> {
private:
    std::string _static_root;
    uint16_t _port;
    size_t _connected_users = 0;
    
public:
    explicit ChatServer(const std::string& static_root = "./resources/chat", uint16_t port = 8080) 
        : _static_root(static_root), _port(port) {}

    bool onInit() override {
        std::cout << "Starting WebSocket Chat Server..." << std::endl;
        
        // Validate static root exists
        if (!std::filesystem::exists(_static_root)) {
            std::cerr << "Static root directory does not exist: " << _static_root << std::endl;
            std::cerr << "Creating directory..." << std::endl;
            std::filesystem::create_directories(_static_root);
        }
        
        // Setup middleware and routes
        setup_middleware();
        setup_routes();
        
        // Compile the router
        router().compile();
        
        // Start listening
        const std::string bind_address = "tcp://0.0.0.0:" + std::to_string(_port);
        if (!listen({bind_address})) {
            std::cerr << "Failed to start server on port " << _port << std::endl;
            std::cerr << "Port may already be in use. Try a different port." << std::endl;
            return false;
        }
        start();
        
        std::cout << "WebSocket Chat Server running on http://localhost:" << _port << std::endl;
        std::cout << "Chat interface: http://localhost:" << _port << "/" << std::endl;
        std::cout << "WebSocket endpoint: ws://localhost:" << _port << "/ws" << std::endl;
        std::cout << "Static files served from: " << _static_root << std::endl;
        
        print_usage_info();
        
        return true;
    }

    // Handle new session connections
    void on(ChatSession& session) {
        ++_connected_users;
        std::cout << "[SERVER] New connection established. Total users: " << _connected_users << std::endl;
    }

    // Track disconnections (called from session destructor indirectly)
    void user_disconnected() {
        if (_connected_users > 0) --_connected_users;
        std::cout << "[SERVER] User disconnected. Total users: " << _connected_users << std::endl;
    }

    size_t get_connected_users() const { return _connected_users; }

private:
    void setup_middleware() {
        // CORS middleware for development
        auto cors_middleware = qb::http::CorsMiddleware<ChatSession>::dev();
        router().use(cors_middleware);
        
        // Logging middleware with more detailed output
        auto logging_middleware = std::make_shared<qb::http::LoggingMiddleware<ChatSession>>(
            [](qb::http::LogLevel level, const std::string& message) {
                std::string level_str;
                switch (level) {
                    case qb::http::LogLevel::Debug: level_str = "DEBUG"; break;
                    case qb::http::LogLevel::Info: level_str = "INFO"; break;
                    case qb::http::LogLevel::Warning: level_str = "WARNING"; break;
                    case qb::http::LogLevel::Error: level_str = "ERROR"; break;
                }
                std::cout << "[HTTP:" << level_str << "] " << message << std::endl;
            },
            qb::http::LogLevel::Info,
            qb::http::LogLevel::Debug
        );
        router().use(logging_middleware);
        
        // Static files middleware with /static prefix
        qb::http::StaticFilesOptions options(_static_root);
        options.with_path_prefix_to_strip("/static")
               .with_serve_index_file(true)
               .with_directory_listing(false)
               .with_etags(true)
               .with_last_modified(true)
               .with_cache_control(true, "public, max-age=3600");

        auto static_middleware = qb::http::static_files_middleware<ChatSession>(std::move(options));
        router().use(static_middleware);
    }

    void setup_routes() {
        // Main chat page
        router().get("/", [this](auto ctx) {
            // Redirect to static index.html
            ctx->redirect("/static/index.html");
        });
        
        // API endpoint for chat info and server statistics
        router().get("/api/info", [this](auto ctx) {
            qb::json info = {
                {"name", "QB WebSocket Chat Server"},
                {"version", "1.0.0"},
                {"framework", "QB Actor Framework"},
                {"module", "qbm-ws"},
                {"server_stats", {
                    {"connected_users", _connected_users},
                    {"port", _port},
                    {"uptime_info", "Server running"}
                }},
                {"features", {
                    "websocket_chat",
                    "static_file_serving",
                    "real_time_messaging",
                    "actor_based_architecture",
                    "user_tracking",
                    "message_validation"
                }},
                {"endpoints", {
                    {"chat_interface", "/"},
                    {"websocket", "/ws"},
                    {"api_info", "/api/info"},
                    {"health_check", "/health"}
                }},
                {"static_files", {
                    {"root", _static_root},
                    {"prefix", "/static"},
                    {"index", "/static/index.html"},
                    {"css", "/static/chat.css"},
                    {"js", "/static/chat.js"}
                }}
            };
            
            ctx->response().status() = qb::http::Status::OK;
            ctx->response().add_header("Content-Type", "application/json");
            ctx->response().body() = info;
            ctx->complete();
        });
        
        // Health check endpoint for monitoring
        router().get("/health", [this](auto ctx) {
            qb::json health = {
                {"status", "healthy"},
                {"connected_users", _connected_users},
                {"timestamp", qb::Timestamp::now().count()}
            };
            
            ctx->response().status() = qb::http::Status::OK;
            ctx->response().add_header("Content-Type", "application/json");
            ctx->response().body() = health;
            ctx->complete();
        });
        
        // WebSocket endpoint
        router().get("/ws", [this](auto ctx) {
            ctx->session()->switch_to_ws(ctx.get());
        });
    }

    void print_usage_info() {
        std::cout << "\n=== QB WebSocket Chat Server ===\n";
        std::cout << "Server running at: http://localhost:" << _port << "\n\n";
        
        std::cout << "Endpoints:\n";
        std::cout << "  GET  /                  - Chat interface (redirects to /static/index.html)\n";
        std::cout << "  GET  /static/index.html - Main chat page\n";
        std::cout << "  GET  /static/chat.css   - Chat stylesheet\n";
        std::cout << "  GET  /static/chat.js    - Chat JavaScript client\n";
        std::cout << "  GET  /api/info          - Server information (JSON)\n";
        std::cout << "  GET  /health            - Health check (JSON)\n";
        std::cout << "  GET  /ws                - WebSocket endpoint (FULLY IMPLEMENTED)\n\n";
        
        std::cout << "Features implemented:\n";
        std::cout << "  ✓ Static file serving for chat interface\n";
        std::cout << "  ✓ Modern responsive chat UI\n";
        std::cout << "  ✓ WebSocket client-side code\n";
        std::cout << "  ✓ CORS and logging middleware\n";
        std::cout << "  ✓ JSON API for server info\n";
        std::cout << "  ✓ Full WebSocket server-side handling\n";
        std::cout << "  ✓ Real-time message broadcasting\n";
        std::cout << "  ✓ User join/leave notifications\n";
        std::cout << "  ✓ Username change handling\n";
        std::cout << "  ✓ Error handling and validation\n";
        std::cout << "  ✓ User tracking and statistics\n";
        std::cout << "  ✓ Health check endpoint\n\n";
        
        std::cout << "WebSocket Message Types Supported:\n";
        std::cout << "  • message        - Chat messages from users\n";
        std::cout << "  • user_joined    - User connection notifications\n";
        std::cout << "  • username_update - Username change requests\n";
        std::cout << "  • system         - Server system messages\n";
        std::cout << "  • error          - Error notifications\n\n";
        
        std::cout << "How to test:\n";
        std::cout << "  1. Open http://localhost:" << _port << " in multiple browser tabs\n";
        std::cout << "  2. Change usernames in different tabs\n";
        std::cout << "  3. Send messages between tabs\n";
        std::cout << "  4. Watch real-time communication in action\n\n";
        
        std::cout << "Static files location: " << _static_root << "\n";
        std::cout << "Open in browser: http://localhost:" << _port << "\n\n";
    }

    void on(const qb::KillEvent& event) noexcept {
        std::cout << "Chat Server shutting down..." << std::endl;
        this->kill();
    }
};

// Implementation of ChatSession methods that use ChatServer
void ChatSession::broadcast_to_websockets(const std::string& message, ChatSession* exclude) {
    // Create WebSocket message object
    qb::http::ws::MessageText ws_message;
    ws_message.masked = false;
    ws_message << message;
    
    // Use server().stream_if() to send to all WebSocket sessions
    server().stream_if([exclude](const ChatSession& session) {
        // Only send to WebSocket sessions, excluding the specified session
        return session.is_ws() && &session != exclude;
    }, ws_message);
}

ChatSession::~ChatSession() {
    if (_user_announced && !_username.empty()) {
        std::cout << "[LEAVE] " << _username << " left the chat" << std::endl;
        
        // Notify other users about disconnection
        auto response = create_message(MSG_TYPE_USER_LEFT);
        response["username"] = _username;
        
        broadcast_to_websockets(response.dump(), this);
    }
}

int main(int argc, char* argv[]) {
    qb::Main engine;
    
    // Parse command line arguments for configuration
    std::string static_root = "./resources/chat";
    uint16_t port = 8080;
    
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--port" && i + 1 < argc) {
            port = static_cast<uint16_t>(std::atoi(argv[++i]));
        } else if (arg == "--static-root" && i + 1 < argc) {
            static_root = argv[++i];
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: " << argv[0] << " [options]\n";
            std::cout << "Options:\n";
            std::cout << "  --port PORT          Set server port (default: 8080)\n";
            std::cout << "  --static-root PATH   Set static files directory (default: ./resources/chat)\n";
            std::cout << "  --help, -h           Show this help message\n";
            return 0;
        }
    }
    
    std::cout << "QB WebSocket Chat Server Configuration:\n";
    std::cout << "  Port: " << port << "\n";
    std::cout << "  Static files: " << static_root << "\n\n";
    
    // Add the chat server actor with configuration
    engine.addActor<ChatServer>(0, static_root, port);
    
    // Start the engine
    engine.start();
    engine.join();
    
    if (engine.hasError()) {
        std::cerr << "Engine error occurred" << std::endl;
        return 1;
    }
    
    return 0;
} 