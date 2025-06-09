/**
 * @file 01_chat_server.cpp
 * @brief WebSocket Chat Server with HTTP Server Separation
 *
 * This example demonstrates:
 * - Separation of HTTP and WebSocket server responsibilities
 * - HttpServer for static files and API endpoints
 * - ChatServer as io_handler for WebSocket sessions
 * - Actor Event-based session transfer from HTTP to WebSocket
 * - Clean message dispatch system with unordered_map
 * - Modern chat UI with real-time messaging
 *
 * @author qb - C++ Actor Framework
 * @copyright Copyright (c) 2011-2025 qb - isndev (cpp.actor)
 * Licensed under the Apache License, Version 2.0 (http://www.apache.org/licenses/LICENSE-2.0)
 * @ingroup Examples
 */

#include <iostream>
#include <filesystem>
#include <functional>
#include <qb/main.h>
#include <qb/actor.h>
#include <http/http.h>
#include <ws/ws.h>
#include <http/middleware/static_files.h>
#include <http/middleware/cors.h>
#include <http/middleware/logging.h>

// Forward declarations
class HttpServer;
class ChatServer;

// ============================================================================
// Utility Function Declarations
// ============================================================================
std::pair<std::string, uint16_t> parse_command_line_arguments(int argc, char* argv[]);
void print_server_info(uint16_t port);

// ============================================================================
// Message Types Constants
// ============================================================================
namespace MessageType {
    static constexpr const char* MESSAGE = "message";
    static constexpr const char* USER_JOINED = "user_joined";
    static constexpr const char* USER_LEFT = "user_left";
    static constexpr const char* USERNAME_UPDATE = "username_update";
    static constexpr const char* USERNAME_CHANGED = "username_changed";
    static constexpr const char* SYSTEM = "system";
    static constexpr const char* ERROR = "error";
}

// ============================================================================
// Validation Constants
// ============================================================================
namespace Validation {
    static constexpr size_t MAX_MESSAGE_LENGTH = 1000;
    static constexpr size_t MAX_USERNAME_LENGTH = 50;
    static constexpr size_t MIN_USERNAME_LENGTH = 1;
}

// ============================================================================
// HTTP Session - for HTTP requests only
// ============================================================================
class HttpSession : public qb::http::use<HttpSession>::session<HttpServer> {
public:
    HttpSession(HttpServer &server) : session(server) {}
};

// ============================================================================
// Event for transferring HTTP session to WebSocket
// ============================================================================
struct TransferToWebSocketEvent : public qb::Event {
    using transport_type = HttpSession::transport_io_type;
    
    struct Data {
        transport_type transport;
        qb::http::Request request;
        qb::http::Response response;
    };

    std::unique_ptr<Data> data;
    
    TransferToWebSocketEvent() : data(std::make_unique<Data>()) {}
};

// ============================================================================
// WebSocket Session - for WebSocket connections only
// ============================================================================
class ChatSession : public qb::io::use<ChatSession>::tcp::client<ChatServer> {
public:
    using ws_protocol = qb::http::ws::protocol<ChatSession>;
    using message_handler_t = std::function<void(const qb::json&)>;

    ChatSession(ChatServer &server) : client(server) {
        init_message_handlers();
    }

    void on(ws_protocol::message &&event) {
        try {
            auto message_json = qb::json::parse(event.ws.data().view());
            std::string type = message_json.value("type", "");
            
            if (auto it = _message_handlers.find(type); it != _message_handlers.end()) {
                it->second(message_json);
            } else {
                std::cout << "Unknown message type: " << type << std::endl;
                send_error("Unknown message type: " + type);
            }
        } catch (const std::exception& e) {
            std::cout << "Error parsing WebSocket message: " << e.what() << std::endl;
            std::cout << "Raw message data: " << std::string_view{event.data, event.size} << std::endl;
            send_error("Invalid message format");
        }
    }

    const std::string& get_username() const { return _username; }
    bool is_user_announced() const { return _user_announced; }

private:
    // Member variables
    std::string _username = "Anonymous";
    bool _user_announced = false;
    qb::unordered_map<std::string, message_handler_t> _message_handlers;

    // ========================================================================
    // Message Handler Initialization
    // ========================================================================
    void init_message_handlers() {
        _message_handlers[MessageType::MESSAGE] = 
            [this](const qb::json& msg) { handle_chat_message(msg); };
        
        _message_handlers[MessageType::USER_JOINED] = 
            [this](const qb::json& msg) { handle_user_joined(msg); };
        
        _message_handlers[MessageType::USERNAME_UPDATE] = 
            [this](const qb::json& msg) { handle_username_update(msg); };
    }

    // ========================================================================
    // Message Handlers
    // ========================================================================
    void handle_chat_message(const qb::json& message_json) {
        auto [username, message, timestamp] = extract_chat_message_data(message_json);
        
        if (!validate_chat_message(message)) return;
        
        _username = username;
        log_chat_message(username, message);
        
        auto response = create_message(MessageType::MESSAGE, timestamp);
        response["username"] = username;
        response["message"] = message;
        
        broadcast_to_all(response.dump());
    }
    
    void handle_user_joined(const qb::json& message_json) {
        std::string username = message_json.value("username", "Anonymous");
        _username = username;
        
        if (!_user_announced) {
            _user_announced = true;
            log_user_joined(username);
            
            // Notify other users
            auto response = create_message(MessageType::USER_JOINED, 
                message_json.value("timestamp", 0ULL));
            response["username"] = username;
            broadcast_to_others(response.dump());
            
            // Send welcome message to this user
            send_welcome_message(username);
        }
    }
    
    void handle_username_update(const qb::json& message_json) {
        std::string new_username = message_json.value("username", "Anonymous");
        std::string old_username = _username;
        
        if (!validate_username(new_username)) return;
        
        if (new_username != old_username && _user_announced) {
            _username = new_username;
            log_username_change(old_username, new_username);
            
            auto response = create_message(MessageType::USERNAME_CHANGED, 
                message_json.value("timestamp", 0ULL));
            response["old_username"] = old_username;
            response["new_username"] = new_username;
            
            broadcast_to_all(response.dump());
        } else {
            _username = new_username;
        }
    }

    // ========================================================================
    // Data Extraction Helpers
    // ========================================================================
    std::tuple<std::string, std::string, uint64_t> extract_chat_message_data(const qb::json& msg) {
        return {
            msg.value("username", "Anonymous"),
            msg.value("message", ""),
            msg.value("timestamp", 0ULL)
        };
    }

    // ========================================================================
    // Validation Helpers
    // ========================================================================
    bool validate_chat_message(const std::string& message) {
        if (message.empty()) {
            send_error("Empty message");
            return false;
        }
        
        if (message.length() > Validation::MAX_MESSAGE_LENGTH) {
            send_error("Message too long (max " + 
                std::to_string(Validation::MAX_MESSAGE_LENGTH) + " characters)");
            return false;
        }
        
        return true;
    }

    bool validate_username(const std::string& username) {
        if (username.empty() || username.length() > Validation::MAX_USERNAME_LENGTH) {
            send_error("Invalid username (" + 
                std::to_string(Validation::MIN_USERNAME_LENGTH) + "-" + 
                std::to_string(Validation::MAX_USERNAME_LENGTH) + " characters required)");
            return false;
        }
        return true;
    }

    // ========================================================================
    // Logging Helpers
    // ========================================================================
    void log_chat_message(const std::string& username, const std::string& message) {
        std::cout << "[CHAT] " << username << ": " << message << std::endl;
    }

    void log_user_joined(const std::string& username) {
        std::cout << "[JOIN] " << username << " joined the chat" << std::endl;
    }

    void log_username_change(const std::string& old_name, const std::string& new_name) {
        std::cout << "[USERNAME] " << old_name << " is now " << new_name << std::endl;
    }

    void log_user_left(const std::string& username) {
        std::cout << "[LEAVE] " << username << " left the chat" << std::endl;
    }

    // ========================================================================
    // Message Creation Helpers
    // ========================================================================
    qb::json create_message(const std::string& type, uint64_t timestamp = 0) {
        return qb::json{
            {"type", type},
            {"timestamp", timestamp == 0 ? qb::Timestamp::now().count() : timestamp}
        };
    }

    // ========================================================================
    // Messaging Helpers
    // ========================================================================
    void send_welcome_message(const std::string& username) {
        auto welcome = create_message(MessageType::SYSTEM);
        welcome["message"] = "Welcome to QB WebSocket Chat, " + username + "!";
        send_ws_message(welcome.dump());
    }
    
    void send_ws_message(const std::string& message) {
        qb::http::ws::MessageText ws_message;
        ws_message << message;
        *this << ws_message;
    }
    
    void send_error(const std::string& error_message) {
        auto error_response = create_message(MessageType::ERROR);
        error_response["message"] = error_message;
        send_ws_message(error_response.dump());
    }
    
    void broadcast_to_all(const std::string& message) {
        broadcast_message(message, nullptr);
    }
    
    void broadcast_to_others(const std::string& message) {
        broadcast_message(message, this);
    }
    
    void broadcast_message(const std::string& message, ChatSession* exclude);

public:
    ~ChatSession();
};

// ============================================================================
// HTTP Server - handles static files and API endpoints only
// ============================================================================
class HttpServer 
 : public qb::Actor
 , public qb::http::use<HttpServer>::template server<HttpSession> {
private:
    std::string _static_root;
    uint16_t _port;
    qb::ActorId _chat_server_id;
    
public:
    explicit HttpServer(const std::string& static_root, uint16_t port, qb::ActorId chat_server_id) 
        : _static_root(static_root), _port(port), _chat_server_id(chat_server_id) {}

    bool onInit() override {        
        std::cout << "Starting HTTP Server..." << std::endl;
        
        ensure_static_directory_exists();
        setup_middleware();
        setup_routes();
        
        router().compile();
        
        if (!start_listening()) {
            return false;
        }
        
        std::cout << "HTTP Server running on http://localhost:" << _port << std::endl;
        return true;
    }

    void on(HttpSession& session) {
        std::cout << "[HTTP] New HTTP connection established" << std::endl;
    }

private:
    void ensure_static_directory_exists() {
        if (!std::filesystem::exists(_static_root)) {
            std::cerr << "Static root directory does not exist: " << _static_root << std::endl;
            std::cerr << "Creating directory..." << std::endl;
            std::filesystem::create_directories(_static_root);
        }
    }

    bool start_listening() {
        const std::string bind_address = "tcp://0.0.0.0:" + std::to_string(_port);
        if (!listen({bind_address})) {
            std::cerr << "Failed to start HTTP server on port " << _port << std::endl;
            std::cerr << "Port may already be in use. Try a different port." << std::endl;
            return false;
        }
        start();
        return true;
    }

    void setup_middleware() {
        // CORS middleware for development
        auto cors_middleware = qb::http::CorsMiddleware<HttpSession>::dev();
        router().use(cors_middleware);
        
        // Logging middleware
        auto logging_middleware = create_logging_middleware();
        router().use(logging_middleware);
        
        // Static files middleware
        auto static_middleware = create_static_files_middleware();
        router().use(static_middleware);
    }

    std::shared_ptr<qb::http::LoggingMiddleware<HttpSession>> create_logging_middleware() {
        return std::make_shared<qb::http::LoggingMiddleware<HttpSession>>(
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
    }

    std::shared_ptr<qb::http::StaticFilesMiddleware<HttpSession>> create_static_files_middleware() {
        qb::http::StaticFilesOptions options(_static_root);
        options.with_path_prefix_to_strip("/static")
               .with_serve_index_file(true)
               .with_directory_listing(false)
               .with_etags(true)
               .with_last_modified(true)
               .with_cache_control(true, "public, max-age=3600");

        return qb::http::static_files_middleware<HttpSession>(std::move(options));
    }

    void setup_routes() {
        setup_main_routes();
        setup_api_routes();
        setup_websocket_route();
    }

    void setup_main_routes() {
        router().get("/", [this](auto ctx) {
            ctx->redirect("/static/index.html");
        });
    }

    void setup_api_routes() {
        router().get("/api/info", [this](auto ctx) {
            qb::json info = create_api_info();
            ctx->response().status() = qb::http::Status::OK;
            ctx->response().add_header("Content-Type", "application/json");
            ctx->response().body() = info;
            ctx->complete();
        });
        
        router().get("/health", [this](auto ctx) {
            qb::json health = create_health_info();
            ctx->response().status() = qb::http::Status::OK;
            ctx->response().add_header("Content-Type", "application/json");
            ctx->response().body() = health;
            ctx->complete();
        });
    }

    void setup_websocket_route() {
        router().get("/ws", [this](auto ctx) {
            handle_websocket_upgrade(ctx);
        });
    }

    qb::json create_api_info() {
        return qb::json{
            {"name", "QB WebSocket Chat Server"},
            {"version", "2.0.0"},
            {"framework", "QB Actor Framework"},
            {"module", "qbm-websocket"},
            {"architecture", "Separated HTTP/WebSocket Servers"},
            {"features", {
                "websocket_chat",
                "static_file_serving",
                "real_time_messaging",
                "actor_based_architecture",
                "separated_responsibilities",
                "clean_message_dispatch"
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
    }

    qb::json create_health_info() {
        return qb::json{
            {"status", "healthy"},
            {"server_type", "HTTP"},
            {"timestamp", qb::Timestamp::now().count()}
        };
    }

    template<typename ContextPtr>
    void handle_websocket_upgrade(ContextPtr ctx) {
        std::cout << "[HTTP] WebSocket upgrade requested, transferring to ChatServer..." << std::endl;
        
        auto session_ptr = ctx->session();
        auto session_id = session_ptr->id();
        
        auto [transport, success] = extractSession(session_id);
        
        if (success) {
            std::cout << "[HTTP] Session extracted successfully, sending to ChatServer" << std::endl;
            transfer_session_to_websocket(std::move(transport), ctx);
        } else {
            std::cerr << "[HTTP] Failed to extract session for WebSocket upgrade" << std::endl;
            send_websocket_upgrade_error(ctx);
        }
    }

    template<typename Transport, typename ContextPtr>
    void transfer_session_to_websocket(Transport&& transport, ContextPtr ctx) {
        auto &event = push<TransferToWebSocketEvent>(_chat_server_id);
        event.data->transport = std::forward<Transport>(transport);
        event.data->request = std::move(ctx->request());
        event.data->response = std::move(ctx->response());
    }

    template<typename ContextPtr>
    void send_websocket_upgrade_error(ContextPtr ctx) {
        ctx->response().status() = qb::http::Status::INTERNAL_SERVER_ERROR;
        ctx->response().body() = "Failed to transfer to WebSocket";
        ctx->complete();
    }

    void on(const qb::KillEvent& event) noexcept {
        std::cout << "HTTP Server shutting down..." << std::endl;
        this->kill();
    }
};

// ============================================================================
// Chat Server - handles WebSocket sessions only via io_handler
// ============================================================================
class ChatServer 
 : public qb::Actor
 , public qb::io::use<ChatServer>::tcp::io_handler<ChatSession> {
private:
    size_t _connected_users = 0;
    
public:
    explicit ChatServer() = default;

    bool onInit() override {
        registerEvent<TransferToWebSocketEvent>(*this);
        std::cout << "Starting WebSocket Chat Server..." << std::endl;
        return true;
    }

    void on(TransferToWebSocketEvent& event) {
        std::cout << "[WS] Received session transfer from HTTP server" << std::endl;
        
        auto& chat_session = registerSession(std::move(event.data->transport));
        
        // The switch_protocol call attempts the WebSocket handshake.
        // It returns true on success and populates the response object.
        if (chat_session.switch_protocol<ChatSession::ws_protocol>(chat_session, event.data->request, event.data->response)) {
            // Handshake successful, send the 101 response to finalize.
            chat_session << event.data->response;
            
            ++_connected_users;
            std::cout << "[WS] WebSocket protocol switch successful" << std::endl;
            std::cout << "[WS] User connected. Total WebSocket users: " << _connected_users << std::endl;
        } else {
            // The handshake failed (e.g., it wasn't a valid WebSocket request).
            std::cerr << "[WS] WebSocket handshake failed, disconnecting." << std::endl;
            chat_session.disconnect();
        }
    }

    void on(ChatSession& session) {
        std::cout << "[WS] New WebSocket session registered" << std::endl;
    }

    void user_disconnected() {
        if (_connected_users > 0) --_connected_users;
        std::cout << "[WS] User disconnected. Total WebSocket users: " << _connected_users << std::endl;
    }

    size_t get_connected_users() const { return _connected_users; }

private:
    void on(const qb::KillEvent& event) noexcept {
        std::cout << "WebSocket Chat Server shutting down..." << std::endl;
        this->kill();
    }
};

// ============================================================================
// ChatSession method implementations
// ============================================================================
void ChatSession::broadcast_message(const std::string& message, ChatSession* exclude) {
    qb::http::ws::MessageText ws_message;
    ws_message << message;
    
    server().stream_if([exclude](const ChatSession& session) {
        return &session != exclude;
    }, ws_message);
}

ChatSession::~ChatSession() {
    if (_user_announced && !_username.empty()) {
        log_user_left(_username);
        
        auto response = create_message(MessageType::USER_LEFT);
        response["username"] = _username;
        
        broadcast_to_others(response.dump());
        server().user_disconnected();
    }
}

// ============================================================================
// Main function
// ============================================================================
int main(int argc, char* argv[]) {
    qb::Main engine;
    
    auto [static_root, port] = parse_command_line_arguments(argc, argv);
    
    std::cout << "QB Separated HTTP/WebSocket Chat Server Configuration:\n";
    std::cout << "  Port: " << port << "\n";
    std::cout << "  Static files: " << static_root << "\n\n";
    
    auto chat_server_id = engine.addActor<ChatServer>(0);
    engine.addActor<HttpServer>(0, static_root, port, chat_server_id);
    
    engine.start();
    print_server_info(port);
    engine.join();
    
    if (engine.hasError()) {
        std::cerr << "Engine error occurred" << std::endl;
        return 1;
    }
    
    return 0;
}

// ============================================================================
// Utility Functions
// ============================================================================
std::pair<std::string, uint16_t> parse_command_line_arguments(int argc, char* argv[]) {
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
            exit(0);
        }
    }
    
    return {static_root, port};
}

void print_server_info(uint16_t port) {
    std::cout << "\n=== QB Separated HTTP/WebSocket Chat Server ===\n";
    std::cout << "Architecture:\n";
    std::cout << "  HttpServer  - Handles static files and API endpoints\n";
    std::cout << "  ChatServer  - Handles WebSocket sessions via io_handler\n";
    std::cout << "  Event Transfer - HTTP sessions transferred to WebSocket via Actor events\n\n";
    
    std::cout << "Server running at: http://localhost:" << port << "\n";
    std::cout << "Chat interface: http://localhost:" << port << "/\n";
    std::cout << "WebSocket endpoint: ws://localhost:" << port << "/ws\n\n";
    
    std::cout << "Features:\n";
    std::cout << "  ✓ Separated HTTP/WebSocket server responsibilities\n";
    std::cout << "  ✓ Actor-based session transfer\n";
    std::cout << "  ✓ io_handler for WebSocket session management\n";
    std::cout << "  ✓ Clean message dispatch system\n";
    std::cout << "  ✓ Modular and maintainable code structure\n";
    std::cout << "  ✓ Real-time chat functionality\n\n";
} 