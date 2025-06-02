/**
 * @file examples/qbm/http/09_jwt_auth.cpp
 * @brief JWT Authentication example using QB HTTP auth module
 *
 * This example demonstrates:
 * - JWT token generation and validation using QB auth module
 * - User authentication with login/logout endpoints
 * - Role-based access control (RBAC)
 * - Protected routes requiring authentication
 * - User management with different permission levels
 * - Integration with QB Actor framework
 *
 * @author qb - C++ Actor Framework
 * @copyright Copyright (c) 2011-2025 qb - isndev (cpp.actor)
 * Licensed under the Apache License, Version 2.0 (http://www.apache.org/licenses/LICENSE-2.0)
 * @ingroup Examples
 */

#include <iostream>
#include <qb/main.h>
#include <http/http.h>
#include <http/middleware/cors.h>
#include <http/middleware/logging.h>
#include <http/middleware/auth.h>
#include <http/middleware/error_handling.h>
#include <http/auth/manager.h>
#include <http/auth/options.h>
#include <http/auth/user.h>

// Simple user database simulation
struct UserAccount {
    std::string username;
    std::string password_hash; // In real app, use proper password hashing
    std::string email;
    std::vector<std::string> roles;
    bool active = true;
    
    qb::json to_json() const {
        return qb::json{
            {"username", username},
            {"email", email},
            {"roles", roles},
            {"active", active}
        };
    }
};

class JwtAuthServer : public qb::Actor, public qb::http::Server<> {
private:
    std::shared_ptr<qb::http::auth::Manager> _auth_manager;
    std::shared_ptr<qb::http::AuthMiddleware<qb::http::DefaultSession>> _auth_middleware;
    qb::unordered_map<std::string, UserAccount> _users;

public:
    JwtAuthServer() = default;

    bool onInit() override {
        std::cout << "Initializing JWT Authentication Server..." << std::endl;
        
        // Setup authentication manager
        setup_auth_manager();
        
        // Initialize user database
        initialize_users();
        
        // Setup middleware and routes
        setup_middleware();
        setup_routes();
        
        // Compile router
        router().compile();
        
        // Start listening
        if (!listen({"tcp://0.0.0.0:8080"})) {
            std::cerr << "Failed to bind to port 8080" << std::endl;
            return false;
        }
        
        start();
        print_api_documentation();
        return true;
    }

private:
    void setup_auth_manager() {
        // Configure JWT authentication options
        qb::http::auth::Options auth_options;
        auth_options
            .algorithm(qb::http::auth::Options::Algorithm::HMAC_SHA256)
            .secret_key("super-secret-jwt-key-change-in-production")
            .token_expiration(std::chrono::hours(1))
            .token_issuer("qb-http-example")
            .token_audience("qb-http-api")
            .auth_header_name("Authorization")
            .auth_scheme("Bearer")
            .verify_expiration(true);
        
        // Create auth manager for token generation
        _auth_manager = std::make_shared<qb::http::auth::Manager>(auth_options);

        // Create auth middleware using auth options directly
        _auth_middleware = std::make_shared<qb::http::AuthMiddleware<qb::http::DefaultSession>>(auth_options);
    }
    
    void initialize_users() {
        // Admin user
        _users["admin"] = {
            "admin", 
            "admin_hash", // In real app: hash("admin123")
            "admin@example.com",
            {"admin", "user"},
            true
        };
        
        // Regular user
        _users["john"] = {
            "john",
            "john_hash", // In real app: hash("password123")
            "john@example.com", 
            {"user"},
            true
        };
        
        // Manager user
        _users["manager"] = {
            "manager",
            "manager_hash", // In real app: hash("manager123")
            "manager@example.com",
            {"manager", "user"},
            true
        };
        
        // Inactive user
        _users["inactive"] = {
            "inactive",
            "inactive_hash",
            "inactive@example.com",
            {"user"},
            false
        };
        
        std::cout << "Initialized " << _users.size() << " test users" << std::endl;
    }
    
    void setup_middleware() {
        // CORS for development
        auto cors_middleware = qb::http::CorsMiddleware<qb::http::DefaultSession>::dev();
        router().use(cors_middleware);
        
        // Logging middleware
        auto logging_middleware = std::make_shared<qb::http::LoggingMiddleware<qb::http::DefaultSession>>(
            [](qb::http::LogLevel level, const std::string& message) {
                std::string level_str;
                switch (level) {
                    case qb::http::LogLevel::Debug: level_str = "DEBUG"; break;
                    case qb::http::LogLevel::Info: level_str = "INFO"; break;
                    case qb::http::LogLevel::Warning: level_str = "WARNING"; break;
                    case qb::http::LogLevel::Error: level_str = "ERROR"; break;
                }
                std::cout << "[" << level_str << "] " << message << std::endl;
            },
            qb::http::LogLevel::Info,
            qb::http::LogLevel::Info
        );
        router().use(logging_middleware);
        
        // Error handling
        setup_error_handling();
    }
    
    void setup_error_handling() {
        auto error_handler = qb::http::error_handling_middleware<qb::http::DefaultSession>();
        
        // Handle authentication errors
        error_handler->on_status(qb::http::Status::UNAUTHORIZED, [](auto ctx) {
            qb::json error_response = {
                {"error", "Unauthorized"},
                {"message", "Authentication required. Please provide a valid JWT token."},
                {"timestamp", std::time(nullptr)}
            };
            ctx->response().add_header("Content-Type", "application/json");
            ctx->response().body() = error_response;
        });
        
        // Handle forbidden access
        error_handler->on_status(qb::http::Status::FORBIDDEN, [](auto ctx) {
            qb::json error_response = {
                {"error", "Forbidden"},
                {"message", "Insufficient permissions to access this resource."},
                {"timestamp", std::time(nullptr)}
            };
            ctx->response().add_header("Content-Type", "application/json");
            ctx->response().body() = error_response;
        });
        
        auto error_task = std::make_shared<qb::http::MiddlewareTask<qb::http::DefaultSession>>(error_handler);
        router().set_error_task_chain({error_task});
    }
    
    void setup_routes() {
        // Public routes
        router().get("/", [this](auto ctx) {
            handle_api_info(ctx);
        });
        
        router().post("/auth/login", [this](auto ctx) {
            handle_login(ctx);
        });
        
        router().post("/auth/register", [this](auto ctx) {
            handle_register(ctx);
        });
        
        // Protected routes - require authentication
        auto protected_group = router().group("/api");
        
        // Add auth middleware to protected group
        protected_group->use(_auth_middleware);
        
        // User info endpoint
        protected_group->get("/profile", [this](auto ctx) {
            handle_get_profile(ctx);
        });
        
        protected_group->put("/profile", [this](auto ctx) {
            handle_update_profile(ctx);
        });
        
        // Admin-only routes
        auto admin_group = protected_group->group("/admin");
        admin_group->use([](auto ctx, auto next) {
            // Check for admin role
            auto user_opt = ctx->template get<qb::http::auth::User>("user");
            if (!user_opt.has_value() || !user_opt.value().has_role("admin")) {
                ctx->response().status() = qb::http::Status::FORBIDDEN;
                ctx->response().add_header("Content-Type", "application/json");
                ctx->response().body() = qb::json{
                    {"error", "Admin access required"},
                    {"message", "This endpoint requires admin privileges"}
                };
                ctx->complete();
                return;
            }
            next();
        });
        
        admin_group->get("/users", [this](auto ctx) {
            handle_list_users(ctx);
        });
        
        admin_group->put("/users/:username/status", [this](auto ctx) {
            handle_update_user_status(ctx);
        });
        
        // Manager-level routes
        auto manager_group = protected_group->group("/manager");
        manager_group->use([](auto ctx, auto next) {
            // Check for manager or admin role
            auto user_opt = ctx->template get<qb::http::auth::User>("user");
            if (!user_opt.has_value() || 
                (!user_opt.value().has_role("manager") && !user_opt.value().has_role("admin"))) {
                ctx->response().status() = qb::http::Status::FORBIDDEN;
                ctx->response().add_header("Content-Type", "application/json");
                ctx->response().body() = qb::json{
                    {"error", "Manager access required"},
                    {"message", "This endpoint requires manager or admin privileges"}
                };
                ctx->complete();
                return;
            }
            next();
        });
        
        manager_group->get("/reports", [this](auto ctx) {
            handle_get_reports(ctx);
        });
        
        // Logout endpoint (optional - client can just discard token)
        protected_group->post("/auth/logout", [this](auto ctx) {
            handle_logout(ctx);
        });
        
        // Token refresh endpoint
        protected_group->post("/auth/refresh", [this](auto ctx) {
            handle_refresh_token(ctx);
        });
    }
    
    void handle_api_info(std::shared_ptr<qb::http::Context<qb::http::DefaultSession>> ctx) {
        qb::json info = {
            {"name", "QB HTTP JWT Authentication API"},
            {"version", "1.0"},
            {"description", "Demonstrates JWT authentication with role-based access control"},
            {"endpoints", {
                {"public", {
                    {"POST /auth/login", "Login with username/password"},
                    {"POST /auth/register", "Register new user"},
                    {"GET /", "API information"}
                }},
                {"authenticated", {
                    {"GET /api/profile", "Get user profile"},
                    {"PUT /api/profile", "Update user profile"},
                    {"POST /api/auth/logout", "Logout (optional)"},
                    {"POST /api/auth/refresh", "Refresh JWT token"}
                }},
                {"manager", {
                    {"GET /api/manager/reports", "Access management reports"}
                }},
                {"admin", {
                    {"GET /api/admin/users", "List all users"},
                    {"PUT /api/admin/users/:username/status", "Update user status"}
                }}
            }},
            {"authentication", {
                {"type", "JWT"},
                {"header", "Authorization: Bearer <token>"},
                {"algorithm", "HS256"},
                {"expires_in", "1 hour"}
            }},
            {"test_users", {
                {"admin", {"password", "admin123", "roles", {"admin", "user"}}},
                {"john", {"password", "password123", "roles", {"user"}}},
                {"manager", {"password", "manager123", "roles", {"manager", "user"}}}
            }}
        };
        
        ctx->response().status() = qb::http::Status::OK;
        ctx->response().add_header("Content-Type", "application/json");
        ctx->response().body() = info;
        ctx->complete();
    }
    
    void handle_login(std::shared_ptr<qb::http::Context<qb::http::DefaultSession>> ctx) {
        try {
            auto login_data = ctx->request().body().as<qb::json>();
            
            if (!login_data.contains("username") || !login_data.contains("password")) {
                ctx->response().status() = qb::http::Status::BAD_REQUEST;
                ctx->response().add_header("Content-Type", "application/json");
                ctx->response().body() = qb::json{
                    {"error", "Missing credentials"},
                    {"message", "Username and password are required"}
                };
                ctx->complete();
                return;
            }
            
            std::string username = login_data["username"];
            std::string password = login_data["password"];
            
            // Find user
            auto user_it = _users.find(username);
            if (user_it == _users.end()) {
                ctx->response().status() = qb::http::Status::UNAUTHORIZED;
                ctx->response().add_header("Content-Type", "application/json");
                ctx->response().body() = qb::json{
                    {"error", "Invalid credentials"},
                    {"message", "Username or password is incorrect"}
                };
                ctx->complete();
                return;
            }
            
            const auto& user_account = user_it->second;
            
            // Check if user is active
            if (!user_account.active) {
                ctx->response().status() = qb::http::Status::FORBIDDEN;
                ctx->response().add_header("Content-Type", "application/json");
                ctx->response().body() = qb::json{
                    {"error", "Account disabled"},
                    {"message", "Your account has been disabled"}
                };
                ctx->complete();
                return;
            }
            
            // In real app, verify password hash properly
            std::string expected_password;
            if (username == "admin") expected_password = "admin123";
            else if (username == "john") expected_password = "password123";
            else if (username == "manager") expected_password = "manager123";
            else expected_password = "wrong";
            
            if (password != expected_password) {
                ctx->response().status() = qb::http::Status::UNAUTHORIZED;
                ctx->response().add_header("Content-Type", "application/json");
                ctx->response().body() = qb::json{
                    {"error", "Invalid credentials"},
                    {"message", "Username or password is incorrect"}
                };
                ctx->complete();
                return;
            }
            
            // Create user object for token generation
            qb::http::auth::User user;
            user.id = username; // In real app, use UUID
            user.username = user_account.username;
            user.roles = user_account.roles;
            user.metadata["email"] = user_account.email;
            
            // Generate JWT token
            try {
                std::string token = _auth_manager->generate_token(user);
                
                qb::json response = {
                    {"success", true},
                    {"message", "Login successful"},
                    {"token", token},
                    {"user", user_account.to_json()},
                    {"expires_in", 3600} // 1 hour
                };
                
                ctx->response().status() = qb::http::Status::OK;
                ctx->response().add_header("Content-Type", "application/json");
                ctx->response().body() = response;
                
                std::cout << "User '" << username << "' logged in successfully" << std::endl;
                
            } catch (const std::exception& e) {
                ctx->response().status() = qb::http::Status::INTERNAL_SERVER_ERROR;
                ctx->response().add_header("Content-Type", "application/json");
                ctx->response().body() = qb::json{
                    {"error", "Token generation failed"},
                    {"message", e.what()}
                };
            }
            
        } catch (const std::exception& e) {
            ctx->response().status() = qb::http::Status::BAD_REQUEST;
            ctx->response().add_header("Content-Type", "application/json");
            ctx->response().body() = qb::json{
                {"error", "Invalid JSON"},
                {"message", e.what()}
            };
        }
        
        ctx->complete();
    }
    
    void handle_register(std::shared_ptr<qb::http::Context<qb::http::DefaultSession>> ctx) {
        try {
            auto register_data = ctx->request().body().as<qb::json>();
            
            if (!register_data.contains("username") || !register_data.contains("password") || 
                !register_data.contains("email")) {
                ctx->response().status() = qb::http::Status::BAD_REQUEST;
                ctx->response().add_header("Content-Type", "application/json");
                ctx->response().body() = qb::json{
                    {"error", "Missing fields"},
                    {"message", "Username, password, and email are required"}
                };
                ctx->complete();
                return;
            }
            
            std::string username = register_data["username"];
            std::string email = register_data["email"];
            
            // Check if user already exists
            if (_users.find(username) != _users.end()) {
                ctx->response().status() = qb::http::Status::CONFLICT;
                ctx->response().add_header("Content-Type", "application/json");
                ctx->response().body() = qb::json{
                    {"error", "User exists"},
                    {"message", "Username already taken"}
                };
                ctx->complete();
                return;
            }
            
            // Create new user (in real app, hash password properly)
            UserAccount new_user;
            new_user.username = username;
            new_user.password_hash = "hashed_" + register_data["password"].get<std::string>();
            new_user.email = email;
            new_user.roles = {"user"}; // Default role
            new_user.active = true;
            
            _users[username] = new_user;
            
            qb::json response = {
                {"success", true},
                {"message", "User registered successfully"},
                {"user", new_user.to_json()}
            };
            
            ctx->response().status() = qb::http::Status::CREATED;
            ctx->response().add_header("Content-Type", "application/json");
            ctx->response().body() = response;
            
            std::cout << "New user '" << username << "' registered" << std::endl;
            
        } catch (const std::exception& e) {
            ctx->response().status() = qb::http::Status::BAD_REQUEST;
            ctx->response().add_header("Content-Type", "application/json");
            ctx->response().body() = qb::json{
                {"error", "Invalid JSON"},
                {"message", e.what()}
            };
        }
        
        ctx->complete();
    }
    
    void handle_get_profile(std::shared_ptr<qb::http::Context<qb::http::DefaultSession>> ctx) {
        // Get authenticated user from context (set by auth middleware)
        auto user_opt = ctx->template get<qb::http::auth::User>("user");
        if (!user_opt.has_value()) {
            ctx->response().status() = qb::http::Status::UNAUTHORIZED;
            ctx->response().add_header("Content-Type", "application/json");
            ctx->response().body() = qb::json{
                {"error", "No user information"},
                {"message", "User not found in context"}
            };
            ctx->complete();
            return;
        }
        
        const auto& user = user_opt.value();
        
        qb::json profile = {
            {"id", user.id},
            {"username", user.username},
            {"roles", user.roles},
            {"metadata", user.metadata},
            {"token_info", {
                {"valid", true},
                {"remaining_time", "calculated_by_client"}
            }}
        };
        
        ctx->response().status() = qb::http::Status::OK;
        ctx->response().add_header("Content-Type", "application/json");
        ctx->response().body() = profile;
        ctx->complete();
    }
    
    void handle_update_profile(std::shared_ptr<qb::http::Context<qb::http::DefaultSession>> ctx) {
        auto user_opt = ctx->template get<qb::http::auth::User>("user");
        if (!user_opt.has_value()) {
            ctx->response().status() = qb::http::Status::UNAUTHORIZED;
            ctx->complete();
            return;
        }
        
        try {
            auto update_data = ctx->request().body().as<qb::json>();
            const auto& user = user_opt.value();
            
            // Find user account
            auto user_it = _users.find(user.username);
            if (user_it == _users.end()) {
                ctx->response().status() = qb::http::Status::NOT_FOUND;
                ctx->response().add_header("Content-Type", "application/json");
                ctx->response().body() = qb::json{{"error", "User not found"}};
                ctx->complete();
                return;
            }
            
            // Update allowed fields (email only for regular users)
            if (update_data.contains("email")) {
                user_it->second.email = update_data["email"];
            }
            
            qb::json response = {
                {"success", true},
                {"message", "Profile updated successfully"},
                {"user", user_it->second.to_json()}
            };
            
            ctx->response().status() = qb::http::Status::OK;
            ctx->response().add_header("Content-Type", "application/json");
            ctx->response().body() = response;
            
        } catch (const std::exception& e) {
            ctx->response().status() = qb::http::Status::BAD_REQUEST;
            ctx->response().add_header("Content-Type", "application/json");
            ctx->response().body() = qb::json{
                {"error", "Invalid JSON"},
                {"message", e.what()}
            };
        }
        
        ctx->complete();
    }
    
    void handle_list_users(std::shared_ptr<qb::http::Context<qb::http::DefaultSession>> ctx) {
        // Admin-only endpoint
        qb::json users_list = qb::json::array();
        
        for (const auto& [username, user_account] : _users) {
            users_list.push_back(user_account.to_json());
        }
        
        qb::json response = {
            {"users", users_list},
            {"total", _users.size()}
        };
        
        ctx->response().status() = qb::http::Status::OK;
        ctx->response().add_header("Content-Type", "application/json");
        ctx->response().body() = response;
        ctx->complete();
    }
    
    void handle_update_user_status(std::shared_ptr<qb::http::Context<qb::http::DefaultSession>> ctx) {
        try {
            std::string username = ctx->path_param("username");
            auto status_data = ctx->request().body().as<qb::json>();
            
            auto user_it = _users.find(username);
            if (user_it == _users.end()) {
                ctx->response().status() = qb::http::Status::NOT_FOUND;
                ctx->response().add_header("Content-Type", "application/json");
                ctx->response().body() = qb::json{{"error", "User not found"}};
                ctx->complete();
                return;
            }
            
            if (status_data.contains("active")) {
                user_it->second.active = status_data["active"];
            }
            
            qb::json response = {
                {"success", true},
                {"message", "User status updated"},
                {"user", user_it->second.to_json()}
            };
            
            ctx->response().status() = qb::http::Status::OK;
            ctx->response().add_header("Content-Type", "application/json");
            ctx->response().body() = response;
            
        } catch (const std::exception& e) {
            ctx->response().status() = qb::http::Status::BAD_REQUEST;
            ctx->response().add_header("Content-Type", "application/json");
            ctx->response().body() = qb::json{
                {"error", "Invalid request"},
                {"message", e.what()}
            };
        }
        
        ctx->complete();
    }
    
    void handle_get_reports(std::shared_ptr<qb::http::Context<qb::http::DefaultSession>> ctx) {
        // Manager-level endpoint
        qb::json reports = {
            {"user_stats", {
                {"total_users", _users.size()},
                {"active_users", std::count_if(_users.begin(), _users.end(), 
                    [](const auto& pair) { return pair.second.active; })},
                {"admin_users", std::count_if(_users.begin(), _users.end(),
                    [](const auto& pair) { 
                        const auto& roles = pair.second.roles;
                        return std::find(roles.begin(), roles.end(), "admin") != roles.end();
                    })}
            }},
            {"system_info", {
                {"uptime", "calculated_in_real_app"},
                {"memory_usage", "calculated_in_real_app"},
                {"active_sessions", "tracked_in_real_app"}
            }}
        };
        
        ctx->response().status() = qb::http::Status::OK;
        ctx->response().add_header("Content-Type", "application/json");
        ctx->response().body() = reports;
        ctx->complete();
    }
    
    void handle_logout(std::shared_ptr<qb::http::Context<qb::http::DefaultSession>> ctx) {
        // In JWT, logout is typically handled client-side by discarding the token
        // Server-side logout would require token blacklisting
        
        qb::json response = {
            {"success", true},
            {"message", "Logout successful"},
            {"note", "Please discard your JWT token on the client side"}
        };
        
        ctx->response().status() = qb::http::Status::OK;
        ctx->response().add_header("Content-Type", "application/json");
        ctx->response().body() = response;
        ctx->complete();
    }
    
    void handle_refresh_token(std::shared_ptr<qb::http::Context<qb::http::DefaultSession>> ctx) {
        auto user_opt = ctx->template get<qb::http::auth::User>("user");
        if (!user_opt.has_value()) {
            ctx->response().status() = qb::http::Status::UNAUTHORIZED;
            ctx->complete();
            return;
        }
        
        try {
            const auto& user = user_opt.value();
            std::string new_token = _auth_manager->generate_token(user);
            
            qb::json response = {
                {"success", true},
                {"message", "Token refreshed successfully"},
                {"token", new_token},
                {"expires_in", 3600}
            };
            
            ctx->response().status() = qb::http::Status::OK;
            ctx->response().add_header("Content-Type", "application/json");
            ctx->response().body() = response;
            
        } catch (const std::exception& e) {
            ctx->response().status() = qb::http::Status::INTERNAL_SERVER_ERROR;
            ctx->response().add_header("Content-Type", "application/json");
            ctx->response().body() = qb::json{
                {"error", "Token refresh failed"},
                {"message", e.what()}
            };
        }
        
        ctx->complete();
    }
    
    void print_api_documentation() {
        std::cout << "\n=== JWT Authentication Server ===\n";
        std::cout << "Server running on: http://localhost:8080\n\n";
        
        std::cout << "Authentication Features:\n";
        std::cout << "  • JWT token generation and validation\n";
        std::cout << "  • Role-based access control (RBAC)\n";
        std::cout << "  • User management system\n";
        std::cout << "  • Protected routes with middleware\n";
        std::cout << "  • Token refresh capability\n\n";
        
        std::cout << "Test Users:\n";
        std::cout << "  • admin/admin123     - Admin privileges\n";
        std::cout << "  • manager/manager123 - Manager privileges\n";
        std::cout << "  • john/password123   - Regular user\n\n";
        
        std::cout << "API Endpoints:\n";
        std::cout << "  POST /auth/login                     - Login (get JWT token)\n";
        std::cout << "  POST /auth/register                  - Register new user\n";
        std::cout << "  GET  /api/profile                    - Get user profile [AUTH]\n";
        std::cout << "  PUT  /api/profile                    - Update profile [AUTH]\n";
        std::cout << "  POST /api/auth/refresh               - Refresh JWT token [AUTH]\n";
        std::cout << "  GET  /api/manager/reports            - Management reports [MANAGER]\n";
        std::cout << "  GET  /api/admin/users                - List all users [ADMIN]\n";
        std::cout << "  PUT  /api/admin/users/:user/status   - Update user status [ADMIN]\n\n";
        
        std::cout << "Usage Examples:\n";
        std::cout << "  1. Login:\n";
        std::cout << "     curl -X POST http://localhost:8080/auth/login \\\n";
        std::cout << "          -H 'Content-Type: application/json' \\\n";
        std::cout << "          -d '{\"username\":\"admin\",\"password\":\"admin123\"}'\n\n";
        
        std::cout << "  2. Access protected endpoint:\n";
        std::cout << "     curl -X GET http://localhost:8080/api/profile \\\n";
        std::cout << "          -H 'Authorization: Bearer <your-jwt-token>'\n\n";
        
        std::cout << "  3. Admin endpoint:\n";
        std::cout << "     curl -X GET http://localhost:8080/api/admin/users \\\n";
        std::cout << "          -H 'Authorization: Bearer <admin-jwt-token>'\n\n";
    }
    
    void on(const qb::KillEvent& event) noexcept {
        std::cout << "JWT Auth Server shutting down..." << std::endl;
        this->kill();
    }
};

int main() {
    qb::Main engine;
    
    engine.addActor<JwtAuthServer>(0);
    engine.start();
    engine.join();
    
    return engine.hasError();
} 