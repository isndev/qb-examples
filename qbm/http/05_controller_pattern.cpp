/**
 * @file examples/qbm/http/05_controller_pattern.cpp
 * @brief HTTP/1.1 controller pattern demonstration using QB Actor system
 *
 * This example demonstrates:
 * - Creating an HTTP server actor with controller-based route organization
 * - Class-based route handlers (controllers)
 * - Controller-specific middleware
 * - RESTful API design with controllers
 * - State management within controllers
 *
 * @author qb - C++ Actor Framework
 * @copyright Copyright (c) 2011-2025 qb - isndev (cpp.actor)
 * Licensed under the Apache License, Version 2.0 (http://www.apache.org/licenses/LICENSE-2.0)
 * @ingroup Examples
 */

#include <iostream>
#include <qb/main.h>
#include <qb/system/parse.h>
#include <qbm/http/http.h>

// User Controller - manages user-related operations
class UserController : public qb::http::Controller<qb::http::DefaultSession> {
private:
    // Simulated user database
    qb::unordered_map<int, qb::json> _users;
    int                              _next_id = 1;

public:
    UserController() {
        // Initialize with some sample users
        _users[1] = {{"id", 1}, {"name", "Alice"}, {"email", "alice@example.com"}, {"role", "admin"}};
        _users[2] = {{"id", 2}, {"name", "Bob"}, {"email", "bob@example.com"}, {"role", "user"}};
        _users[3] = {{"id", 3}, {"name", "Charlie"}, {"email", "charlie@example.com"}, {"role", "user"}};
        _next_id  = 4;
    }

    void
    initialize_routes() override {
        // Controller-specific middleware
        use(
            [](auto ctx, auto next) {
                std::cout << "[UserController] Processing request: " << std::to_string(ctx->request().method()) << " "
                          << ctx->request().uri().path() << std::endl;

                // Add controller identification header
                ctx->response().add_header("X-Controller", "UserController");
                next();
            },
            "name");

        // Define routes for this controller
        get("/", this, &UserController::list_users);
        get("/:id", this, &UserController::get_user);
        post("/", this, &UserController::create_user);
        put("/:id", this, &UserController::update_user);
        del("/:id", this, &UserController::delete_user);
    }

    void
    list_users(std::shared_ptr<qb::http::Context<qb::http::DefaultSession>> ctx) {
        ctx->response().status() = qb::http::Status::OK;
        ctx->response().add_header("Content-Type", "application/json");

        qb::json users_array = qb::json::array();
        for (const auto &[id, user] : _users) {
            users_array.push_back(user);
        }

        qb::json response = {{"users", users_array}, {"total", _users.size()}, {"controller", "UserController"}};

        ctx->response().body() = response;
        ctx->complete(qb::http::AsyncTaskResult::COMPLETE);
    }

    void
    get_user(std::shared_ptr<qb::http::Context<qb::http::DefaultSession>> ctx) {
        // Path params are untrusted: parse without throwing and reject bad input with 400.
        const auto user_id = qb::to_number<int>(ctx->path_param("id"));
        if (!user_id) {
            ctx->response().status() = qb::http::Status::BAD_REQUEST;
            ctx->response().add_header("Content-Type", "application/json");
            ctx->response().body() = qb::json{{"error", "Invalid user id"}};
            ctx->complete(qb::http::AsyncTaskResult::COMPLETE);
            return;
        }

        auto it = _users.find(*user_id);
        if (it != _users.end()) {
            ctx->response().status() = qb::http::Status::OK;
            ctx->response().add_header("Content-Type", "application/json");
            ctx->response().body() = it->second;
        } else {
            ctx->response().status() = qb::http::Status::NOT_FOUND;
            ctx->response().add_header("Content-Type", "application/json");
            ctx->response().body() = qb::json{{"error", "User not found"}};
        }

        ctx->complete(qb::http::AsyncTaskResult::COMPLETE);
    }

    void
    create_user(std::shared_ptr<qb::http::Context<qb::http::DefaultSession>> ctx) {
        try {
            auto user_data = ctx->request().body().as<qb::json>();

            if (!user_data.contains("name") || !user_data.contains("email")) {
                ctx->response().status() = qb::http::Status::BAD_REQUEST;
                ctx->response().add_header("Content-Type", "application/json");
                ctx->response().body() = qb::json{{"error", "Missing required fields: name, email"}};
                ctx->complete(qb::http::AsyncTaskResult::COMPLETE);
                return;
            }

            qb::json new_user = {
                {"id", _next_id}, {"name", user_data["name"]}, {"email", user_data["email"]}, {"role", user_data.value("role", "user")}
            };

            _users[_next_id] = new_user;
            _next_id++;

            ctx->response().status() = qb::http::Status::CREATED;
            ctx->response().add_header("Content-Type", "application/json");
            ctx->response().body() = new_user;

        } catch (const std::exception &) {
            ctx->response().status() = qb::http::Status::BAD_REQUEST;
            ctx->response().add_header("Content-Type", "application/json");
            ctx->response().body() = qb::json{{"error", "Invalid JSON data"}};
        }

        ctx->complete(qb::http::AsyncTaskResult::COMPLETE);
    }

    void
    update_user(std::shared_ptr<qb::http::Context<qb::http::DefaultSession>> ctx) {
        // Path params are untrusted: parse without throwing and reject bad input with 400.
        const auto user_id = qb::to_number<int>(ctx->path_param("id"));
        if (!user_id) {
            ctx->response().status() = qb::http::Status::BAD_REQUEST;
            ctx->response().add_header("Content-Type", "application/json");
            ctx->response().body() = qb::json{{"error", "Invalid user id"}};
            ctx->complete(qb::http::AsyncTaskResult::COMPLETE);
            return;
        }

        auto it = _users.find(*user_id);
        if (it == _users.end()) {
            ctx->response().status() = qb::http::Status::NOT_FOUND;
            ctx->response().add_header("Content-Type", "application/json");
            ctx->response().body() = qb::json{{"error", "User not found"}};
            ctx->complete(qb::http::AsyncTaskResult::COMPLETE);
            return;
        }

        try {
            auto update_data = ctx->request().body().as<qb::json>();

            if (update_data.contains("name"))
                it->second["name"] = update_data["name"];
            if (update_data.contains("email"))
                it->second["email"] = update_data["email"];
            if (update_data.contains("role"))
                it->second["role"] = update_data["role"];

            ctx->response().status() = qb::http::Status::OK;
            ctx->response().add_header("Content-Type", "application/json");
            ctx->response().body() = it->second;

        } catch (const std::exception &) {
            ctx->response().status() = qb::http::Status::BAD_REQUEST;
            ctx->response().add_header("Content-Type", "application/json");
            ctx->response().body() = qb::json{{"error", "Invalid JSON data"}};
        }

        ctx->complete(qb::http::AsyncTaskResult::COMPLETE);
    }

    void
    delete_user(std::shared_ptr<qb::http::Context<qb::http::DefaultSession>> ctx) {
        // Path params are untrusted: parse without throwing and reject bad input with 400.
        const auto user_id = qb::to_number<int>(ctx->path_param("id"));
        if (!user_id) {
            ctx->response().status() = qb::http::Status::BAD_REQUEST;
            ctx->response().add_header("Content-Type", "application/json");
            ctx->response().body() = qb::json{{"error", "Invalid user id"}};
            ctx->complete(qb::http::AsyncTaskResult::COMPLETE);
            return;
        }

        auto it = _users.find(*user_id);
        if (it != _users.end()) {
            _users.erase(it);
            ctx->response().status() = qb::http::Status::NO_CONTENT;
        } else {
            ctx->response().status() = qb::http::Status::NOT_FOUND;
            ctx->response().add_header("Content-Type", "application/json");
            ctx->response().body() = qb::json{{"error", "User not found"}};
        }

        ctx->complete(qb::http::AsyncTaskResult::COMPLETE);
    }
};

// Product Controller - manages product-related operations
class ProductController : public qb::http::Controller<qb::http::DefaultSession> {
private:
    qb::unordered_map<int, qb::json> _products;
    int                              _next_id = 1;

public:
    ProductController() {
        // Initialize with some sample products
        _products[1] = {{"id", 1}, {"name", "Laptop"}, {"price", 999.99}, {"category", "Electronics"}};
        _products[2] = {{"id", 2}, {"name", "Book"}, {"price", 19.99}, {"category", "Education"}};
        _products[3] = {{"id", 3}, {"name", "Coffee"}, {"price", 4.99}, {"category", "Food"}};
        _next_id     = 4;
    }

    void
    initialize_routes() override {
        // Controller-specific middleware
        use([](auto ctx, auto next) {
            std::cout << "[ProductController] Processing request: " << std::to_string(ctx->request().method()) << " "
                      << ctx->request().uri().path() << std::endl;

            ctx->response().add_header("X-Controller", "ProductController");

            next();
        });

        get("/", this, &ProductController::list_products);
        get("/:id", this, &ProductController::get_product);
        get("/category/:category", this, &ProductController::get_by_category);
        post("/", this, &ProductController::create_product);
        put("/:id", this, &ProductController::update_product);
        del("/:id", this, &ProductController::delete_product);
    }

    void
    list_products(std::shared_ptr<qb::http::Context<qb::http::DefaultSession>> ctx) {
        ctx->response().status() = qb::http::Status::OK;
        ctx->response().add_header("Content-Type", "application/json");

        qb::json products_array = qb::json::array();
        for (const auto &[id, product] : _products) {
            products_array.push_back(product);
        }

        qb::json response = {{"products", products_array}, {"total", _products.size()}, {"controller", "ProductController"}};

        ctx->response().body() = response;
        ctx->complete(qb::http::AsyncTaskResult::COMPLETE);
    }

    void
    get_product(std::shared_ptr<qb::http::Context<qb::http::DefaultSession>> ctx) {
        // Path params are untrusted: parse without throwing and reject bad input with 400.
        const auto product_id = qb::to_number<int>(ctx->path_param("id"));
        if (!product_id) {
            ctx->response().status() = qb::http::Status::BAD_REQUEST;
            ctx->response().add_header("Content-Type", "application/json");
            ctx->response().body() = qb::json{{"error", "Invalid product id"}};
            ctx->complete(qb::http::AsyncTaskResult::COMPLETE);
            return;
        }

        auto it = _products.find(*product_id);
        if (it != _products.end()) {
            ctx->response().status() = qb::http::Status::OK;
            ctx->response().add_header("Content-Type", "application/json");
            ctx->response().body() = it->second;
        } else {
            ctx->response().status() = qb::http::Status::NOT_FOUND;
            ctx->response().add_header("Content-Type", "application/json");
            ctx->response().body() = qb::json{{"error", "Product not found"}};
        }

        ctx->complete(qb::http::AsyncTaskResult::COMPLETE);
    }

    void
    get_by_category(std::shared_ptr<qb::http::Context<qb::http::DefaultSession>> ctx) {
        std::string category = ctx->path_param("category");

        qb::json filtered_products = qb::json::array();
        for (const auto &[id, product] : _products) {
            if (product["category"] == category) {
                filtered_products.push_back(product);
            }
        }

        ctx->response().status() = qb::http::Status::OK;
        ctx->response().add_header("Content-Type", "application/json");

        qb::json response = {{"products", filtered_products}, {"category", category}, {"total", filtered_products.size()}};

        ctx->response().body() = response;
        ctx->complete(qb::http::AsyncTaskResult::COMPLETE);
    }

    void
    create_product(std::shared_ptr<qb::http::Context<qb::http::DefaultSession>> ctx) {
        try {
            auto product_data = ctx->request().body().as<qb::json>();

            if (!product_data.contains("name") || !product_data.contains("price")) {
                ctx->response().status() = qb::http::Status::BAD_REQUEST;
                ctx->response().add_header("Content-Type", "application/json");
                ctx->response().body() = qb::json{{"error", "Missing required fields: name, price"}};
                ctx->complete(qb::http::AsyncTaskResult::COMPLETE);
                return;
            }

            qb::json new_product = {
                {"id", _next_id},
                {"name", product_data["name"]},
                {"price", product_data["price"]},
                {"category", product_data.value("category", "General")}
            };

            _products[_next_id] = new_product;
            _next_id++;

            ctx->response().status() = qb::http::Status::CREATED;
            ctx->response().add_header("Content-Type", "application/json");
            ctx->response().body() = new_product;

        } catch (const std::exception &) {
            ctx->response().status() = qb::http::Status::BAD_REQUEST;
            ctx->response().add_header("Content-Type", "application/json");
            ctx->response().body() = qb::json{{"error", "Invalid JSON data"}};
        }

        ctx->complete(qb::http::AsyncTaskResult::COMPLETE);
    }

    void
    update_product(std::shared_ptr<qb::http::Context<qb::http::DefaultSession>> ctx) {
        // Path params are untrusted: parse without throwing and reject bad input with 400.
        const auto product_id = qb::to_number<int>(ctx->path_param("id"));
        if (!product_id) {
            ctx->response().status() = qb::http::Status::BAD_REQUEST;
            ctx->response().add_header("Content-Type", "application/json");
            ctx->response().body() = qb::json{{"error", "Invalid product id"}};
            ctx->complete(qb::http::AsyncTaskResult::COMPLETE);
            return;
        }

        auto it = _products.find(*product_id);
        if (it == _products.end()) {
            ctx->response().status() = qb::http::Status::NOT_FOUND;
            ctx->response().add_header("Content-Type", "application/json");
            ctx->response().body() = qb::json{{"error", "Product not found"}};
            ctx->complete(qb::http::AsyncTaskResult::COMPLETE);
            return;
        }

        try {
            auto update_data = ctx->request().body().as<qb::json>();

            if (update_data.contains("name"))
                it->second["name"] = update_data["name"];
            if (update_data.contains("price"))
                it->second["price"] = update_data["price"];
            if (update_data.contains("category"))
                it->second["category"] = update_data["category"];

            ctx->response().status() = qb::http::Status::OK;
            ctx->response().add_header("Content-Type", "application/json");
            ctx->response().body() = it->second;

        } catch (const std::exception &) {
            ctx->response().status() = qb::http::Status::BAD_REQUEST;
            ctx->response().add_header("Content-Type", "application/json");
            ctx->response().body() = qb::json{{"error", "Invalid JSON data"}};
        }

        ctx->complete(qb::http::AsyncTaskResult::COMPLETE);
    }

    void
    delete_product(std::shared_ptr<qb::http::Context<qb::http::DefaultSession>> ctx) {
        // Path params are untrusted: parse without throwing and reject bad input with 400.
        const auto product_id = qb::to_number<int>(ctx->path_param("id"));
        if (!product_id) {
            ctx->response().status() = qb::http::Status::BAD_REQUEST;
            ctx->response().add_header("Content-Type", "application/json");
            ctx->response().body() = qb::json{{"error", "Invalid product id"}};
            ctx->complete(qb::http::AsyncTaskResult::COMPLETE);
            return;
        }

        auto it = _products.find(*product_id);
        if (it != _products.end()) {
            _products.erase(it);
            ctx->response().status() = qb::http::Status::NO_CONTENT;
        } else {
            ctx->response().status() = qb::http::Status::NOT_FOUND;
            ctx->response().add_header("Content-Type", "application/json");
            ctx->response().body() = qb::json{{"error", "Product not found"}};
        }

        ctx->complete(qb::http::AsyncTaskResult::COMPLETE);
    }
};

// Main HTTP Server Actor using controllers
class ControllerServerActor
    : public qb::Actor
    , public qb::http::Server<> {
public:
    ControllerServerActor() = default;

    qb::io::async::task<bool>
    onInit() override {
        // Shutdown wiring. Event dispatch is by SUBSCRIPTION, not by vtable: qb::Actor's
        // constructor already subscribed its own default handlers for these two, and
        // re-registering here is what replaces them with OURS. Without these two lines
        // the handlers below compile, are never called, and their cleanup is lost.
        registerEvent<qb::KillEvent>(*this);
        registerEvent<qb::SignalEvent>(*this);

        std::cout << "Initializing Controller Pattern Server Actor..." << std::endl;

        setup_global_middleware();
        setup_controllers();

        // Compile the router
        router().compile();

        // Start listening on port 8080
        if (listen({"tcp://0.0.0.0:8080"})) {
            start();
            std::cout << "Controller server listening on http://localhost:8080" << std::endl;
            print_available_routes();
            std::cout << "Press Ctrl+C to stop the server" << std::endl;
        } else {
            std::cerr << "Failed to start listening server" << std::endl;
            co_return false;
        }

        co_return true;
    }

private:
    void
    setup_global_middleware() {
        // Global request logging
        router().use([](auto ctx, auto next) {
            std::cout << "[GLOBAL] " << std::to_string(ctx->request().method()) << " " << ctx->request().uri().path() << std::endl;
            next();
        });
    }

    void
    setup_controllers() {
        // Root endpoint
        router().get("/", [this](auto ctx) { handle_api_info(ctx); });

        // Mount controllers at specific paths
        (void) router().controller<UserController>("/api/users");
        (void) router().controller<ProductController>("/api/products");
    }

    void
    handle_api_info(std::shared_ptr<qb::http::Context<qb::http::DefaultSession>> ctx) {
        ctx->response().status() = qb::http::Status::OK;
        ctx->response().add_header("Content-Type", "application/json");

        qb::json response = {
            {"message", "QB HTTP Controller Pattern Demo"},
            {"version", "1.0"},
            {"controllers", {{"UserController", "/api/users"}, {"ProductController", "/api/products"}}},
            {"endpoints",
             {"GET / - This API info", "User Management:", "  GET    /api/users     - List all users",
              "  GET    /api/users/:id - Get user by ID", "  POST   /api/users     - Create new user", "  PUT    /api/users/:id - Update user",
              "  DELETE /api/users/:id - Delete user", "Product Management:", "  GET    /api/products           - List all products",
              "  GET    /api/products/:id       - Get product by ID", "  GET    /api/products/category/:category - Get products by category",
              "  POST   /api/products           - Create new product", "  PUT    /api/products/:id       - Update product",
              "  DELETE /api/products/:id       - Delete product"}}
        };

        ctx->response().body() = response;
        ctx->complete(qb::http::AsyncTaskResult::COMPLETE);
    }

    void
    print_available_routes() {
        std::cout << "Available API endpoints:" << std::endl;
        std::cout << "   GET  /                              - API information" << std::endl;
        std::cout << "\n   User Management (UserController):" << std::endl;
        std::cout << "   GET    /api/users                  - List all users" << std::endl;
        std::cout << "   GET    /api/users/:id              - Get user by ID" << std::endl;
        std::cout << "   POST   /api/users                  - Create new user" << std::endl;
        std::cout << "   PUT    /api/users/:id              - Update user" << std::endl;
        std::cout << "   DELETE /api/users/:id              - Delete user" << std::endl;
        std::cout << "\n   Product Management (ProductController):" << std::endl;
        std::cout << "   GET    /api/products               - List all products" << std::endl;
        std::cout << "   GET    /api/products/:id           - Get product by ID" << std::endl;
        std::cout << "   GET    /api/products/category/:cat - Get products by category" << std::endl;
        std::cout << "   POST   /api/products               - Create new product" << std::endl;
        std::cout << "   PUT    /api/products/:id           - Update product" << std::endl;
        std::cout << "   DELETE /api/products/:id           - Delete product" << std::endl;
    }

public:
    // Ctrl+C / SIGTERM. qb::Main::start() installs both, so every actor receives a
    // qb::SignalEvent; routing it into the KillEvent below keeps ONE shutdown path.
    // Both handlers must be PUBLIC — the router's dispatch trampoline calls
    // `handler.on(event)` from outside the class (qb/system/event/router.h).
    void
    on(const qb::SignalEvent &event) noexcept {
        std::cout << "Signal " << event.signum << " received." << std::endl;
        push<qb::KillEvent>(id());
    }

    void
    on(const qb::KillEvent &event) noexcept {
        std::cout << "Shutting down Controller Server..." << std::endl;
        this->kill();
    }
};

int
main() {
    try {
        // Initialize the QB Actor framework
        qb::Main engine;

        // Add our HTTP server actor to core 0
        auto server_id = engine.addActor<ControllerServerActor>(0);

        if (!server_id.is_valid()) {
            std::cerr << "Failed to create server actor" << std::endl;
            return 1;
        }

        std::cout << "Controller server actor created with ID: " << server_id.sid() << std::endl;

        // Start the engine (blocks until stopped)
        engine.start();
        engine.join();

        // A failed onInit() (bind refused, missing certificate, ...) removes the actor
        // but does not make the process fail. Report it, so a supervisor cannot read a
        // server that never bound its port as a clean shutdown.
        if (engine.hasError()) {
            std::cerr << "Engine reported an error" << std::endl;
            return 1;
        }

        std::cout << "Controller server stopped gracefully" << std::endl;

    } catch (const std::exception &e) {
        std::cerr << "Server error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}