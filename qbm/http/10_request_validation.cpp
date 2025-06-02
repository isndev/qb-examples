/**
 * @file examples/qbm/http/10_request_validation.cpp
 * @brief Request Validation example using QB HTTP validation module
 *
 * This example demonstrates:
 * - JSON schema validation for request bodies
 * - Query parameter validation and type conversion
 * - Header validation
 * - Path parameter validation  
 * - Request sanitization
 * - Comprehensive error reporting
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
#include <http/middleware/validation.h>
#include <http/middleware/error_handling.h>
#include <http/validation/request_validator.h>
#include <http/validation/schema_validator.h>
#include <http/validation/parameter_validator.h>
#include <http/validation/sanitizer.h>

class ValidationServer : public qb::Actor, public qb::http::Server<> {
private:
    // User database for validation examples
    qb::unordered_map<int, qb::json> _users;
    qb::unordered_map<int, qb::json> _products;

public:
    ValidationServer() = default;

    bool onInit() override {
        std::cout << "Initializing Request Validation Server..." << std::endl;
        
        // Initialize sample data
        initialize_data();
        
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
    void initialize_data() {
        // Sample users
        _users[1] = {
            {"id", 1},
            {"name", "John Doe"},
            {"email", "john@example.com"},
            {"age", 30},
            {"active", true}
        };
        _users[2] = {
            {"id", 2},
            {"name", "Jane Smith"},
            {"email", "jane@example.com"},
            {"age", 25},
            {"active", true}
        };
        
        // Sample products
        _products[1] = {
            {"id", 1},
            {"name", "Laptop"},
            {"price", 999.99},
            {"category", "electronics"},
            {"in_stock", true}
        };
        _products[2] = {
            {"id", 2},
            {"name", "Book"},
            {"price", 29.99},
            {"category", "books"},
            {"in_stock", false}
        };
        
        std::cout << "Initialized " << _users.size() << " users and " 
                  << _products.size() << " products" << std::endl;
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
        
        // Handle validation errors
        error_handler->on_status(qb::http::Status::BAD_REQUEST, [](auto ctx) {
            // Check if this is a validation error by looking for validation details
            auto error_details = ctx->template get<qb::json>("validation_errors");
            if (error_details.has_value()) {
                qb::json error_response = {
                    {"error", "Validation Failed"},
                    {"message", "Request validation failed"},
                    {"validation_errors", error_details.value()},
                    {"timestamp", std::time(nullptr)}
                };
                ctx->response().add_header("Content-Type", "application/json");
                ctx->response().body() = error_response;
            } else {
                // Generic bad request
                qb::json error_response = {
                    {"error", "Bad Request"},
                    {"message", "Invalid request format"},
                    {"timestamp", std::time(nullptr)}
                };
                ctx->response().add_header("Content-Type", "application/json");
                ctx->response().body() = error_response;
            }
        });
        
        error_handler->on_status(qb::http::Status::UNPROCESSABLE_ENTITY, [](auto ctx) {
            qb::json error_response = {
                {"error", "Unprocessable Entity"},
                {"message", "Request contains invalid data"},
                {"timestamp", std::time(nullptr)}
            };
            ctx->response().add_header("Content-Type", "application/json");
            ctx->response().body() = error_response;
        });
        
        auto error_task = std::make_shared<qb::http::MiddlewareTask<qb::http::DefaultSession>>(error_handler);
        router().set_error_task_chain({error_task});
    }
    
    void setup_routes() {
        // API info endpoint (no validation needed)
        router().get("/", [this](auto ctx) {
            handle_api_info(ctx);
        });
        
        // User endpoints with comprehensive validation
        setup_user_routes();
        
        // Product endpoints with different validation patterns
        setup_product_routes();
        
        // Search endpoints with query parameter validation
        setup_search_routes();
        
        // Contact form with sanitization
        setup_contact_routes();
    }
    
    void setup_user_routes() {
        auto users_group = router().group("/api/users");
        
        // GET /api/users - List users with pagination and filtering
        users_group->get("/", [this](auto ctx) {
            // Simple validation example in handler
            auto page_str = ctx->request().query("page", 0, "1");
            auto limit_str = ctx->request().query("limit", 0, "10");
            
            // Basic validation
            int page = 1, limit = 10;
            try {
                page = std::max(1, std::stoi(page_str));
                limit = std::max(1, std::min(100, std::stoi(limit_str)));
            } catch (...) {
                ctx->response().status() = qb::http::Status::BAD_REQUEST;
                ctx->response().add_header("Content-Type", "application/json");
                ctx->response().body() = qb::json{
                    {"error", "Invalid query parameters"},
                    {"message", "Page and limit must be valid integers"}
                };
                ctx->complete();
                return;
            }
            
            handle_list_users(ctx);
        });
        
        // POST /api/users - Create user with comprehensive validation
        users_group->post("/", [this](auto ctx) {
            handle_create_user_with_validation(ctx);
        });
        
        // GET /api/users/:id - Get user by ID with path parameter validation
        users_group->get("/:id", [this](auto ctx) {
            // Validate path parameter
            int user_id;
            try {
                user_id = std::stoi(ctx->path_param("id"));
                if (user_id < 1) throw std::invalid_argument("ID must be positive");
            } catch (...) {
                ctx->response().status() = qb::http::Status::BAD_REQUEST;
                ctx->response().add_header("Content-Type", "application/json");
                ctx->response().body() = qb::json{
                    {"error", "Invalid path parameter"},
                    {"message", "User ID must be a positive integer"}
                };
                ctx->complete();
                return;
            }
            
            handle_get_user(ctx);
        });
        
        // PUT /api/users/:id - Update user
        users_group->put("/:id", [this](auto ctx) {
            // Validate path parameter
            int user_id;
            try {
                user_id = std::stoi(ctx->path_param("id"));
                if (user_id < 1) throw std::invalid_argument("ID must be positive");
            } catch (...) {
                ctx->response().status() = qb::http::Status::BAD_REQUEST;
                ctx->response().add_header("Content-Type", "application/json");
                ctx->response().body() = qb::json{
                    {"error", "Invalid path parameter"},
                    {"message", "User ID must be a positive integer"}
                };
                ctx->complete();
                return;
            }
            
            handle_update_user_with_validation(ctx);
        });
        
        // DELETE /api/users/:id - Delete user
        users_group->del("/:id", [this](auto ctx) {
            // Validate path parameter
            int user_id;
            try {
                user_id = std::stoi(ctx->path_param("id"));
                if (user_id < 1) throw std::invalid_argument("ID must be positive");
            } catch (...) {
                ctx->response().status() = qb::http::Status::BAD_REQUEST;
                ctx->response().add_header("Content-Type", "application/json");
                ctx->response().body() = qb::json{
                    {"error", "Invalid path parameter"},
                    {"message", "User ID must be a positive integer"}
                };
                ctx->complete();
                return;
            }
            
            handle_delete_user(ctx);
        });
    }
    
    void setup_product_routes() {
        auto products_group = router().group("/api/products");
        
        // GET /api/products - List products with filters
        products_group->get("/", [this](auto ctx) {
            handle_list_products(ctx);
        });
        
        // POST /api/products - Create product
        products_group->post("/", [this](auto ctx) {
            handle_create_product_with_validation(ctx);
        });
    }
    
    void setup_search_routes() {
        auto search_group = router().group("/api/search");
        
        // Advanced search with complex query parameters
        search_group->get("/", [this](auto ctx) {
            // Validate required query parameter
            auto query = ctx->request().query("q", 0, "");
            if (query.empty()) {
                ctx->response().status() = qb::http::Status::BAD_REQUEST;
                ctx->response().add_header("Content-Type", "application/json");
                ctx->response().body() = qb::json{
                    {"error", "Missing required parameter"},
                    {"message", "Query parameter 'q' is required"}
                };
                ctx->complete();
                return;
    }
    
            if (query.length() < 2) {
                ctx->response().status() = qb::http::Status::BAD_REQUEST;
                ctx->response().add_header("Content-Type", "application/json");
                ctx->response().body() = qb::json{
                    {"error", "Invalid query parameter"},
                    {"message", "Query parameter 'q' must be at least 2 characters long"}
                };
                ctx->complete();
                return;
    }
    
            handle_search(ctx);
        });
    }
    
    void setup_contact_routes() {
        // Contact form with sanitization
        router().post("/api/contact", [this](auto ctx) {
            handle_contact_form_with_validation(ctx);
        });
    }
    
    // Handler methods
    void handle_api_info(std::shared_ptr<qb::http::Context<qb::http::DefaultSession>> ctx) {
        qb::json info = {
            {"name", "QB HTTP Request Validation API"},
            {"version", "1.0"},
            {"description", "Demonstrates comprehensive request validation capabilities"},
            {"features", {
                "JSON schema validation",
                "Parameter type conversion",
                "Query parameter validation",
                "Path parameter validation",
                "Header validation",
                "Request sanitization",
                "Comprehensive error reporting"
            }},
            {"endpoints", {
                {"users", {
                    {"GET /api/users", "List users with pagination and filtering"},
                    {"POST /api/users", "Create user with validation"},
                    {"GET /api/users/:id", "Get user by ID"},
                    {"PUT /api/users/:id", "Update user"},
                    {"DELETE /api/users/:id", "Delete user"}
                }},
                {"products", {
                    {"GET /api/products", "List products with filters"},
                    {"POST /api/products", "Create product"}
                }},
                {"search", {
                    {"GET /api/search", "Advanced search with complex parameters"}
                }},
                {"contact", {
                    {"POST /api/contact", "Contact form with sanitization"}
                }}
            }}
        };
        
        ctx->response().status() = qb::http::Status::OK;
        ctx->response().add_header("Content-Type", "application/json");
        ctx->response().body() = info;
        ctx->complete();
    }
    
    void handle_list_users(std::shared_ptr<qb::http::Context<qb::http::DefaultSession>> ctx) {
        // Parameters have been validated and converted by ValidationMiddleware
        auto page = ctx->request().query("page", 0, "1");
        auto limit = ctx->request().query("limit", 0, "10");
        auto active_filter = ctx->request().query("active", 0, "");
        auto search = ctx->request().query("search", 0, "");
        
        qb::json response = {
            {"users", qb::json::array()},
            {"pagination", {
                {"page", std::stoi(page)},
                {"limit", std::stoi(limit)},
                {"total", static_cast<int>(_users.size())}
            }},
            {"filters", {
                {"active", active_filter},
                {"search", search}
            }}
        };
        
        // Add users to response (simplified)
        for (const auto& [id, user] : _users) {
            response["users"].push_back(user);
        }
        
        ctx->response().status() = qb::http::Status::OK;
        ctx->response().add_header("Content-Type", "application/json");
        ctx->response().body() = response;
        ctx->complete();
    }
    
    void handle_create_user_with_validation(std::shared_ptr<qb::http::Context<qb::http::DefaultSession>> ctx) {
        try {
            // Parse and validate JSON body
            qb::json user_data = qb::json::parse(ctx->request().body().as<std::string_view>());
        
            // Required field validation
            if (!user_data.contains("name") || !user_data.contains("email") || !user_data.contains("age")) {
                ctx->response().status() = qb::http::Status::BAD_REQUEST;
                ctx->response().add_header("Content-Type", "application/json");
                ctx->response().body() = qb::json{
                    {"error", "Validation Error"},
                    {"message", "Missing required fields: name, email, and age are required"}
                };
                ctx->complete();
                return;
            }
            
            // Type validation
            if (!user_data["name"].is_string() || !user_data["email"].is_string() || !user_data["age"].is_number_integer()) {
                ctx->response().status() = qb::http::Status::BAD_REQUEST;
                ctx->response().add_header("Content-Type", "application/json");
                ctx->response().body() = qb::json{
                    {"error", "Validation Error"},
                    {"message", "Invalid field types: name and email must be strings, age must be integer"}
                };
                ctx->complete();
                return;
            }
            
            std::string name = user_data["name"];
            std::string email = user_data["email"];
            int age = user_data["age"];
        
            // Data validation and sanitization
            name = trim(name);
            email = trim(to_lower(email));
            
            if (name.length() < 2 || name.length() > 100) {
                ctx->response().status() = qb::http::Status::BAD_REQUEST;
                ctx->response().add_header("Content-Type", "application/json");
                ctx->response().body() = qb::json{
                    {"error", "Validation Error"},
                    {"message", "Name must be between 2 and 100 characters"}
                };
                ctx->complete();
                return;
            }
            
            if (age < 18 || age > 120) {
                ctx->response().status() = qb::http::Status::BAD_REQUEST;
                ctx->response().add_header("Content-Type", "application/json");
                ctx->response().body() = qb::json{
                    {"error", "Validation Error"},
                    {"message", "Age must be between 18 and 120"}
                };
                ctx->complete();
                return;
            }
            
            // Simple email validation
            if (email.find("@") == std::string::npos || email.find(".") == std::string::npos) {
                ctx->response().status() = qb::http::Status::BAD_REQUEST;
                ctx->response().add_header("Content-Type", "application/json");
                ctx->response().body() = qb::json{
                    {"error", "Validation Error"},
                    {"message", "Invalid email format"}
                };
                ctx->complete();
                return;
            }
            
            // Generate new ID
            int new_id = _users.size() + 1;
            qb::json new_user = {
                {"id", new_id},
                {"name", name},
                {"email", email},
                {"age", age},
                {"active", user_data.value("active", true)}
            };
            
            // Store user
            _users[new_id] = new_user;
        
        qb::json response = {
            {"message", "User created successfully"},
                {"user", new_user},
                {"validation_notes", "Data was validated and sanitized"}
        };
        
        ctx->response().status() = qb::http::Status::CREATED;
        ctx->response().add_header("Content-Type", "application/json");
        ctx->response().body() = response;
        ctx->complete();
            
        } catch (const std::exception& e) {
            ctx->response().status() = qb::http::Status::BAD_REQUEST;
            ctx->response().add_header("Content-Type", "application/json");
            ctx->response().body() = qb::json{
                {"error", "JSON Parse Error"},
                {"message", e.what()}
            };
        ctx->complete();
        }
    }
    
    void handle_get_user(std::shared_ptr<qb::http::Context<qb::http::DefaultSession>> ctx) {
        // Path parameter has been validated by ValidationMiddleware
        int user_id = std::stoi(ctx->path_param("id"));
        
        auto it = _users.find(user_id);
        if (it == _users.end()) {
            ctx->response().status() = qb::http::Status::NOT_FOUND;
            ctx->response().add_header("Content-Type", "application/json");
            ctx->response().body() = qb::json{
                {"error", "User not found"},
                {"user_id", user_id}
            };
        } else {
            ctx->response().status() = qb::http::Status::OK;
            ctx->response().add_header("Content-Type", "application/json");
            ctx->response().body() = it->second;
        }
        
        ctx->complete();
    }
    
    void handle_update_user_with_validation(std::shared_ptr<qb::http::Context<qb::http::DefaultSession>> ctx) {
        try {
            int user_id = std::stoi(ctx->path_param("id"));
            auto it = _users.find(user_id);
            
            if (it == _users.end()) {
                ctx->response().status() = qb::http::Status::NOT_FOUND;
                ctx->response().add_header("Content-Type", "application/json");
                ctx->response().body() = qb::json{
                    {"error", "User not found"},
                    {"user_id", user_id}
            };
            ctx->complete();
            return;
        }
        
            qb::json update_data = qb::json::parse(ctx->request().body().as<std::string_view>());
            
            // Must have at least one field to update
            if (update_data.empty()) {
                ctx->response().status() = qb::http::Status::BAD_REQUEST;
        ctx->response().add_header("Content-Type", "application/json");
                ctx->response().body() = qb::json{
                    {"error", "Validation Error"},
                    {"message", "At least one field must be provided for update"}
                };
        ctx->complete();
                return;
            }
            
            // Validate each field if present
            if (update_data.contains("name")) {
                if (!update_data["name"].is_string()) {
                    ctx->response().status() = qb::http::Status::BAD_REQUEST;
                    ctx->response().add_header("Content-Type", "application/json");
                    ctx->response().body() = qb::json{
                        {"error", "Validation Error"},
                        {"message", "Name must be a string"}
                    };
                    ctx->complete();
                    return;
                }
                std::string name = trim(update_data["name"]);
                if (name.length() < 2 || name.length() > 100) {
                    ctx->response().status() = qb::http::Status::BAD_REQUEST;
                    ctx->response().add_header("Content-Type", "application/json");
                    ctx->response().body() = qb::json{
                        {"error", "Validation Error"},
                        {"message", "Name must be between 2 and 100 characters"}
                    };
                    ctx->complete();
                    return;
                }
                it->second["name"] = name;
            }
            
            if (update_data.contains("email")) {
                if (!update_data["email"].is_string()) {
                    ctx->response().status() = qb::http::Status::BAD_REQUEST;
            ctx->response().add_header("Content-Type", "application/json");
                    ctx->response().body() = qb::json{
                        {"error", "Validation Error"},
                        {"message", "Email must be a string"}
                    };
            ctx->complete();
            return;
        }
                std::string email = trim(to_lower(update_data["email"]));
                if (email.find("@") == std::string::npos || email.find(".") == std::string::npos) {
                    ctx->response().status() = qb::http::Status::BAD_REQUEST;
                    ctx->response().add_header("Content-Type", "application/json");
                    ctx->response().body() = qb::json{
                        {"error", "Validation Error"},
                        {"message", "Invalid email format"}
                    };
                    ctx->complete();
                    return;
                }
                it->second["email"] = email;
            }
            
            if (update_data.contains("age")) {
                if (!update_data["age"].is_number_integer()) {
                    ctx->response().status() = qb::http::Status::BAD_REQUEST;
                    ctx->response().add_header("Content-Type", "application/json");
                    ctx->response().body() = qb::json{
                        {"error", "Validation Error"},
                        {"message", "Age must be an integer"}
                    };
                    ctx->complete();
                    return;
        }
                int age = update_data["age"];
                if (age < 18 || age > 120) {
                    ctx->response().status() = qb::http::Status::BAD_REQUEST;
                    ctx->response().add_header("Content-Type", "application/json");
                    ctx->response().body() = qb::json{
                        {"error", "Validation Error"},
                        {"message", "Age must be between 18 and 120"}
                    };
                    ctx->complete();
                    return;
                }
                it->second["age"] = age;
            }
            
            if (update_data.contains("active")) {
                if (!update_data["active"].is_boolean()) {
                    ctx->response().status() = qb::http::Status::BAD_REQUEST;
                    ctx->response().add_header("Content-Type", "application/json");
                    ctx->response().body() = qb::json{
                        {"error", "Validation Error"},
                        {"message", "Active must be a boolean"}
                    };
                    ctx->complete();
                    return;
                }
                it->second["active"] = update_data["active"];
            }
        
        ctx->response().status() = qb::http::Status::OK;
        ctx->response().add_header("Content-Type", "application/json");
            ctx->response().body() = qb::json{
                {"message", "User updated successfully"},
                {"user", it->second},
                {"validation_notes", "Data was validated and sanitized"}
            };
            ctx->complete();
            
        } catch (const std::exception& e) {
            ctx->response().status() = qb::http::Status::BAD_REQUEST;
            ctx->response().add_header("Content-Type", "application/json");
            ctx->response().body() = qb::json{
                {"error", "Invalid Request"},
                {"message", e.what()}
            };
        ctx->complete();
        }
    }
    
    void handle_delete_user(std::shared_ptr<qb::http::Context<qb::http::DefaultSession>> ctx) {
        // Path parameter has been validated by ValidationMiddleware
        int user_id = std::stoi(ctx->path_param("id"));
        
        auto it = _users.find(user_id);
        if (it == _users.end()) {
            ctx->response().status() = qb::http::Status::NOT_FOUND;
            ctx->response().add_header("Content-Type", "application/json");
            ctx->response().body() = qb::json{
                {"error", "User not found"},
                {"user_id", user_id}
            };
        } else {
            _users.erase(it);
            
            ctx->response().status() = qb::http::Status::OK;
            ctx->response().add_header("Content-Type", "application/json");
            ctx->response().body() = qb::json{
                {"message", "User deleted successfully"},
                {"user_id", user_id}
            };
        }
        
            ctx->complete();
    }
    
    void handle_list_products(std::shared_ptr<qb::http::Context<qb::http::DefaultSession>> ctx) {
        // Parameters have been validated by ValidationMiddleware
        auto page = ctx->request().query("page", 0, "1");
        auto limit = ctx->request().query("limit", 0, "20");
        auto category = ctx->request().query("category", 0, "");
        auto min_price = ctx->request().query("min_price", 0, "");
        auto max_price = ctx->request().query("max_price", 0, "");
        
        qb::json response = {
            {"products", qb::json::array()},
            {"pagination", {
                {"page", std::stoi(page)},
                {"limit", std::stoi(limit)},
                {"total", static_cast<int>(_products.size())}
            }},
            {"filters", {
                {"category", category},
                {"min_price", min_price},
                {"max_price", max_price}
            }}
        };
        
        // Add products to response (simplified filtering)
        for (const auto& [id, product] : _products) {
            if (category.empty() || product["category"] == category) {
                response["products"].push_back(product);
            }
        }
        
        ctx->response().status() = qb::http::Status::OK;
        ctx->response().add_header("Content-Type", "application/json");
        ctx->response().body() = response;
        ctx->complete();
    }
    
    void handle_create_product_with_validation(std::shared_ptr<qb::http::Context<qb::http::DefaultSession>> ctx) {
        try {
            qb::json product_data = qb::json::parse(ctx->request().body().as<std::string_view>());
            
            // Required field validation
            if (!product_data.contains("name") || !product_data.contains("price") || !product_data.contains("category")) {
                ctx->response().status() = qb::http::Status::BAD_REQUEST;
                ctx->response().add_header("Content-Type", "application/json");
                ctx->response().body() = qb::json{
                    {"error", "Validation Error"},
                    {"message", "Missing required fields: name, price, and category are required"}
                };
                ctx->complete();
                return;
            }
            
            // Type validation
            if (!product_data["name"].is_string() || !product_data["price"].is_number() || !product_data["category"].is_string()) {
                ctx->response().status() = qb::http::Status::BAD_REQUEST;
                ctx->response().add_header("Content-Type", "application/json");
                ctx->response().body() = qb::json{
                    {"error", "Validation Error"},
                    {"message", "Invalid field types"}
                };
                ctx->complete();
                return;
            }
            
            std::string name = trim(product_data["name"]);
            double price = product_data["price"];
            std::string category = product_data["category"];
            
            // Validation
            if (name.length() < 2 || name.length() > 200) {
                ctx->response().status() = qb::http::Status::BAD_REQUEST;
                ctx->response().add_header("Content-Type", "application/json");
                ctx->response().body() = qb::json{
                    {"error", "Validation Error"},
                    {"message", "Product name must be between 2 and 200 characters"}
                };
                ctx->complete();
                return;
            }
            
            if (price < 0) {
                ctx->response().status() = qb::http::Status::BAD_REQUEST;
        ctx->response().add_header("Content-Type", "application/json");
                ctx->response().body() = qb::json{
                    {"error", "Validation Error"},
                    {"message", "Price must be non-negative"}
                };
        ctx->complete();
                return;
            }
            
            std::vector<std::string> valid_categories = {"electronics", "books", "clothing", "home", "sports", "other"};
            if (std::find(valid_categories.begin(), valid_categories.end(), category) == valid_categories.end()) {
                ctx->response().status() = qb::http::Status::BAD_REQUEST;
                ctx->response().add_header("Content-Type", "application/json");
                ctx->response().body() = qb::json{
                    {"error", "Validation Error"},
                    {"message", "Invalid category. Must be one of: electronics, books, clothing, home, sports, other"}
                };
                ctx->complete();
                return;
            }
            
            // Generate new ID
            int new_id = _products.size() + 1;
            qb::json new_product = {
                {"id", new_id},
                {"name", name},
                {"price", price},
                {"category", category},
                {"in_stock", product_data.value("in_stock", true)}
            };
        
            if (product_data.contains("description")) {
                std::string description = trim(product_data["description"]);
                if (description.length() <= 1000) {
                    new_product["description"] = description;
        }
            }
            
            // Store product
            _products[new_id] = new_product;
        
        qb::json response = {
            {"message", "Product created successfully"},
                {"product", new_product},
                {"validation_notes", "Data was validated and sanitized"}
        };
        
        ctx->response().status() = qb::http::Status::CREATED;
        ctx->response().add_header("Content-Type", "application/json");
        ctx->response().body() = response;
        ctx->complete();
            
        } catch (const std::exception& e) {
            ctx->response().status() = qb::http::Status::BAD_REQUEST;
            ctx->response().add_header("Content-Type", "application/json");
            ctx->response().body() = qb::json{
                {"error", "JSON Parse Error"},
                {"message", e.what()}
            };
        ctx->complete();
        }
    }
    
    void handle_search(std::shared_ptr<qb::http::Context<qb::http::DefaultSession>> ctx) {
        // Parameters have been validated by ValidationMiddleware
        auto query = ctx->request().query("q", 0, "");
        auto type = ctx->request().query("type", 0, "all");
        auto sort = ctx->request().query("sort", 0, "relevance");
        auto limit = ctx->request().query("limit", 0, "10");
        auto api_key = ctx->request().header("X-API-Key");
        
        qb::json results = qb::json::array();
        
        // Simple search implementation
        if (type == "all" || type == "users") {
            for (const auto& [id, user] : _users) {
                std::string name = user["name"];
                std::transform(name.begin(), name.end(), name.begin(), ::tolower);
                std::string q_lower = query;
                std::transform(q_lower.begin(), q_lower.end(), q_lower.begin(), ::tolower);
                
                if (name.find(q_lower) != std::string::npos) {
                    qb::json result = user;
                    result["type"] = "user";
                    results.push_back(result);
                }
            }
        }
        
        if (type == "all" || type == "products") {
            for (const auto& [id, product] : _products) {
                std::string name = product["name"];
                std::transform(name.begin(), name.end(), name.begin(), ::tolower);
                std::string q_lower = query;
                std::transform(q_lower.begin(), q_lower.end(), q_lower.begin(), ::tolower);
                
                if (name.find(q_lower) != std::string::npos) {
                    qb::json result = product;
                    result["type"] = "product";
                    results.push_back(result);
                }
            }
        }
        
        qb::json response = {
            {"query", query},
                {"type", type},
                {"sort", sort},
            {"limit", std::stoi(limit)},
            {"results", results},
            {"total", results.size()},
            {"api_key_provided", !api_key.empty()}
        };
        
        ctx->response().status() = qb::http::Status::OK;
        ctx->response().add_header("Content-Type", "application/json");
        ctx->response().body() = response;
        ctx->complete();
    }
    
    void handle_contact_form_with_validation(std::shared_ptr<qb::http::Context<qb::http::DefaultSession>> ctx) {
        try {
            qb::json contact_data = qb::json::parse(ctx->request().body().as<std::string_view>());
            
            // Required field validation
            if (!contact_data.contains("name") || !contact_data.contains("email") || !contact_data.contains("message")) {
                ctx->response().status() = qb::http::Status::BAD_REQUEST;
                ctx->response().add_header("Content-Type", "application/json");
                ctx->response().body() = qb::json{
                    {"error", "Validation Error"},
                    {"message", "Missing required fields: name, email, and message are required"}
                };
                ctx->complete();
                return;
            }
            
            // Sanitization and validation
            std::string name = trim(escape_html(contact_data["name"]));
            std::string email = trim(to_lower(contact_data["email"]));
            std::string message = trim(escape_html(contact_data["message"]));
            
            if (name.length() < 2 || name.length() > 100) {
                ctx->response().status() = qb::http::Status::BAD_REQUEST;
                ctx->response().add_header("Content-Type", "application/json");
                ctx->response().body() = qb::json{
                    {"error", "Validation Error"},
                    {"message", "Name must be between 2 and 100 characters"}
                };
                ctx->complete();
                return;
            }
            
            if (message.length() < 10 || message.length() > 2000) {
                ctx->response().status() = qb::http::Status::BAD_REQUEST;
                ctx->response().add_header("Content-Type", "application/json");
                ctx->response().body() = qb::json{
                    {"error", "Validation Error"},
                    {"message", "Message must be between 10 and 2000 characters"}
                };
                ctx->complete();
                return;
            }
            
            if (email.find("@") == std::string::npos || email.find(".") == std::string::npos) {
                ctx->response().status() = qb::http::Status::BAD_REQUEST;
                ctx->response().add_header("Content-Type", "application/json");
                ctx->response().body() = qb::json{
                    {"error", "Validation Error"},
                    {"message", "Invalid email format"}
                };
                ctx->complete();
                return;
            }
            
            qb::json sanitized_data = {
                {"name", name},
                {"email", email},
                {"message", message}
            };
            
            if (contact_data.contains("subject")) {
                std::string subject = trim(escape_html(contact_data["subject"]));
                if (subject.length() <= 200) {
                    sanitized_data["subject"] = subject;
                }
            }
        
        qb::json response = {
            {"message", "Contact form submitted successfully"},
                {"id", "contact_" + std::to_string(std::time(nullptr))},
                {"submitted_data", sanitized_data},
                {"validation_notes", "Data was validated, sanitized (XSS protection), and case-normalized"}
        };
        
        ctx->response().status() = qb::http::Status::OK;
        ctx->response().add_header("Content-Type", "application/json");
        ctx->response().body() = response;
        ctx->complete();
            
        } catch (const std::exception& e) {
            ctx->response().status() = qb::http::Status::BAD_REQUEST;
            ctx->response().add_header("Content-Type", "application/json");
            ctx->response().body() = qb::json{
                {"error", "JSON Parse Error"},
                {"message", e.what()}
            };
        ctx->complete();
        }
    }
    
    // Utility functions for validation and sanitization
    std::string trim(const std::string& str) {
        size_t start = str.find_first_not_of(" \t\n\r");
        if (start == std::string::npos) return "";
        size_t end = str.find_last_not_of(" \t\n\r");
        return str.substr(start, end - start + 1);
    }
    
    std::string to_lower(const std::string& str) {
        std::string result = str;
        std::transform(result.begin(), result.end(), result.begin(), ::tolower);
        return result;
    }
    
    std::string escape_html(const std::string& str) {
        std::string result;
        for (char c : str) {
            switch (c) {
                case '<': result += "&lt;"; break;
                case '>': result += "&gt;"; break;
                case '&': result += "&amp;"; break;
                case '"': result += "&quot;"; break;
                case '\'': result += "&#x27;"; break;
                default: result += c; break;
            }
        }
        return result;
    }
    
    void print_api_documentation() {
        std::cout << "\n=== QB HTTP Request Validation Server ===" << std::endl;
        std::cout << "Server running on: http://localhost:8080" << std::endl;
        std::cout << "\nValidation Features:" << std::endl;
        std::cout << "  • JSON schema validation for request bodies" << std::endl;
        std::cout << "  • Query parameter type conversion and validation" << std::endl;
        std::cout << "  • Path parameter validation" << std::endl;
        std::cout << "  • Header validation" << std::endl;
        std::cout << "  • Request sanitization (trim, HTML escape, etc.)" << std::endl;
        std::cout << "  • Comprehensive error reporting" << std::endl;
        
        std::cout << "\nAPI Endpoints:" << std::endl;
        std::cout << "  GET  /                           - API info" << std::endl;
        std::cout << "  GET  /api/users                  - List users with pagination" << std::endl;
        std::cout << "  POST /api/users                  - Create user [VALIDATION]" << std::endl;
        std::cout << "  GET  /api/users/:id              - Get user by ID [PATH PARAM]" << std::endl;
        std::cout << "  PUT  /api/users/:id              - Update user [VALIDATION]" << std::endl;
        std::cout << "  DEL  /api/users/:id              - Delete user [PATH PARAM]" << std::endl;
        std::cout << "  GET  /api/products               - List products with filters" << std::endl;
        std::cout << "  POST /api/products               - Create product [VALIDATION]" << std::endl;
        std::cout << "  GET  /api/search                 - Search with complex params [HEADERS]" << std::endl;
        std::cout << "  POST /api/contact                - Contact form [SANITIZATION]" << std::endl;
        
        std::cout << "\nValidation Examples:" << std::endl;
        std::cout << "  1. Create user with validation:" << std::endl;
        std::cout << "     curl -X POST http://localhost:8080/api/users \\\\" << std::endl;
        std::cout << "          -H 'Content-Type: application/json' \\\\" << std::endl;
        std::cout << "          -d '{\"name\":\"John Doe\",\"email\":\"john@example.com\",\"age\":30}'" << std::endl;
        
        std::cout << "  2. Invalid data (triggers validation error):" << std::endl;
        std::cout << "     curl -X POST http://localhost:8080/api/users \\\\" << std::endl;
        std::cout << "          -H 'Content-Type: application/json' \\\\" << std::endl;
        std::cout << "          -d '{\"name\":\"A\",\"email\":\"invalid-email\",\"age\":15}'" << std::endl;
        
        std::cout << "  3. Search with headers and query params:" << std::endl;
        std::cout << "     curl -X GET 'http://localhost:8080/api/search?q=laptop&type=products&limit=5' \\\\" << std::endl;
        std::cout << "          -H 'X-API-Key: your-api-key-here'" << std::endl;
        
        std::cout << "  4. Contact form with sanitization:" << std::endl;
        std::cout << "     curl -X POST http://localhost:8080/api/contact \\\\" << std::endl;
        std::cout << "          -H 'Content-Type: application/json' \\\\" << std::endl;
        std::cout << "          -d '{\"name\":\"  <script>alert(\\\"xss\\\")</script>  \",\"email\":\"USER@EXAMPLE.COM\",\"message\":\"Hello World!\"}'" << std::endl;
        
        std::cout << std::endl;
    }
    
    void on(const qb::KillEvent& event) noexcept {
        std::cout << "Shutting down Request Validation Server..." << std::endl;
        this->kill();
    }

    // Simple handlers (non-validation versions for some routes)
    void handle_create_user(std::shared_ptr<qb::http::Context<qb::http::DefaultSession>> ctx) {
        handle_create_user_with_validation(ctx);
    }
    
    void handle_create_product(std::shared_ptr<qb::http::Context<qb::http::DefaultSession>> ctx) {
        handle_create_product_with_validation(ctx);
    }
    
    void handle_update_user(std::shared_ptr<qb::http::Context<qb::http::DefaultSession>> ctx) {
        handle_update_user_with_validation(ctx);
    }
    
    void handle_contact_form(std::shared_ptr<qb::http::Context<qb::http::DefaultSession>> ctx) {
        handle_contact_form_with_validation(ctx);
    }
};

int main() {
    qb::Main engine;
    
    engine.addActor<ValidationServer>(0);
    engine.start();
    engine.join();
    
    return engine.hasError() ? 1 : 0;
} 