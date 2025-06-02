/**
 * @file 07_rest_api_json.cpp
 * @brief Complete REST API server with JSON using standard QB HTTP middleware
 *
 * This example demonstrates:
 * - Complete CRUD REST API with JSON data
 * - Standard middleware usage (CORS, Logging, Compression, Security Headers, Rate Limiting)
 * - JSON request/response handling
 * - Error handling with proper HTTP status codes
 * - Data validation and sanitization
 * - Database simulation with in-memory storage
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
#include <http/middleware/compression.h>
#include <http/middleware/security_headers.h>
#include <http/middleware/rate_limit.h>
#include <http/middleware/error_handling.h>

// Book model for our REST API
struct Book {
    int id;
    std::string title;
    std::string author;
    std::string isbn;
    int year;
    bool available;
    double price;
    std::vector<std::string> categories;

    // Convert to JSON
    qb::json to_json() const {
        return qb::json{
            {"id", id},
            {"title", title},
            {"author", author},
            {"isbn", isbn},
            {"year", year},
            {"available", available},
            {"price", price},
            {"categories", categories}
        };
    }

    // Create from JSON
    static Book from_json(const qb::json& j) {
        Book book;
        book.id = j.value("id", 0);
        book.title = j.value("title", "");
        book.author = j.value("author", "");
        book.isbn = j.value("isbn", "");
        book.year = j.value("year", 0);
        book.available = j.value("available", true);
        book.price = j.value("price", 0.0);
        book.categories = j.value("categories", std::vector<std::string>{});
        return book;
    }
};

class RestApiServer : public qb::Actor, public qb::http::Server<> {
private:
    qb::unordered_map<int, Book> _books;
    int _next_id = 1;

public:
    RestApiServer() = default;

    bool onInit() override {
        std::cout << "Initializing REST API Server with JSON support..." << std::endl;

        setup_standard_middleware();
        setup_error_handling();
        setup_routes();
        initialize_sample_data();

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
    void setup_standard_middleware() {
        // 1. CORS middleware - Development configuration
        auto cors_middleware = qb::http::CorsMiddleware<qb::http::DefaultSession>::dev();
        router().use(cors_middleware);
        
        // 2. Compression middleware with balanced settings  
        auto compression_options = qb::http::CompressionOptions()
            .compress_responses(true)
            .decompress_requests(true)
            .min_size_to_compress(1024)
            .preferred_encodings({"gzip", "deflate"});
        auto compression_middleware = std::make_shared<qb::http::CompressionMiddleware<qb::http::DefaultSession>>();
        compression_middleware->update_options(compression_options);
        router().use(compression_middleware);
        
        // 3. Security headers with secure defaults
        auto security_options = qb::http::SecurityHeadersOptions::secure_defaults();
        auto security_middleware = std::make_shared<qb::http::SecurityHeadersMiddleware<qb::http::DefaultSession>>(security_options);
        router().use(security_middleware);
        
        // 4. Request logging middleware
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
        
        // 5. Rate limiting with permissive settings for API
        auto rate_limit_options = qb::http::RateLimitOptions::permissive()
            .max_requests(100)
            .window(std::chrono::minutes(1))
            .message("API rate limit exceeded. Please try again later.");
        auto rate_limit_middleware = std::make_shared<qb::http::RateLimitMiddleware<qb::http::DefaultSession>>(rate_limit_options);
        router().use(rate_limit_middleware);
    }

    void setup_error_handling() {
        auto error_handler = qb::http::error_handling_middleware<qb::http::DefaultSession>();
        
        // Handle validation errors (400)
        error_handler->on_status(qb::http::Status::BAD_REQUEST, [](auto ctx) {
            qb::json error_response = {
                {"error", "Bad Request"},
                {"message", "Invalid request data"},
                {"timestamp", std::time(nullptr)}
            };
            ctx->response().add_header("Content-Type", "application/json");
            ctx->response().body() = error_response;
        });

        // Handle not found (404)
        error_handler->on_status(qb::http::Status::NOT_FOUND, [](auto ctx) {
            qb::json error_response = {
                {"error", "Not Found"},
                {"message", "The requested resource was not found"},
                {"timestamp", std::time(nullptr)}
            };
            ctx->response().add_header("Content-Type", "application/json");
            ctx->response().body() = error_response;
        });

        // Handle method not allowed (405)
        error_handler->on_status(qb::http::Status::METHOD_NOT_ALLOWED, [](auto ctx) {
            qb::json error_response = {
                {"error", "Method Not Allowed"},
                {"message", "HTTP method not supported for this endpoint"},
                {"timestamp", std::time(nullptr)}
            };
            ctx->response().add_header("Content-Type", "application/json");
            ctx->response().body() = error_response;
        });

        // Handle rate limit exceeded (429)
        error_handler->on_status(qb::http::Status::TOO_MANY_REQUESTS, [](auto ctx) {
            qb::json error_response = {
                {"error", "Too Many Requests"},
                {"message", "Rate limit exceeded. Please try again later."},
                {"timestamp", std::time(nullptr)}
            };
            ctx->response().add_header("Content-Type", "application/json");
            ctx->response().body() = error_response;
        });

        // Handle server errors (500)
        error_handler->on_status_range(500, 599, [](auto ctx) {
            qb::json error_response = {
                {"error", "Internal Server Error"},
                {"message", "An unexpected error occurred"},
                {"timestamp", std::time(nullptr)}
            };
            ctx->response().add_header("Content-Type", "application/json");
            ctx->response().body() = error_response;
        });

        // Convert ErrorHandlingMiddleware to IAsyncTask for error chain
        auto error_task = std::make_shared<qb::http::MiddlewareTask<qb::http::DefaultSession>>(error_handler);
        router().set_error_task_chain({error_task});
    }

    void setup_routes() {
        // API documentation endpoint
        router().get("/", [this](auto ctx) {
            handle_api_documentation(ctx);
        });

        // Health check endpoint
        router().get("/health", [this](auto ctx) {
            handle_health_check(ctx);
        });

        // Books API endpoints
        auto api_group = router().group("/api/v1");
        
        // GET /api/v1/books - List all books with filtering
        api_group->get("/books", [this](auto ctx) {
            handle_get_books(ctx);
        });

        // POST /api/v1/books - Create new book
        api_group->post("/books", [this](auto ctx) {
            handle_create_book(ctx);
        });

        // GET /api/v1/books/:id - Get specific book
        api_group->get("/books/:id", [this](auto ctx) {
            handle_get_book(ctx);
        });

        // PUT /api/v1/books/:id - Update book
        api_group->put("/books/:id", [this](auto ctx) {
            handle_update_book(ctx);
        });

        // DELETE /api/v1/books/:id - Delete book
        api_group->del("/books/:id", [this](auto ctx) {
            handle_delete_book(ctx);
        });

        // PATCH /api/v1/books/:id - Partial update
        api_group->patch("/books/:id", [this](auto ctx) {
            handle_patch_book(ctx);
        });

        // Search endpoint
        api_group->get("/books/search", [this](auto ctx) {
            handle_search_books(ctx);
        });

        // Statistics endpoint
        api_group->get("/stats", [this](auto ctx) {
            handle_get_stats(ctx);
        });
    }

    void initialize_sample_data() {
        // Add some sample books
        _books[_next_id++] = {1, "The C++ Programming Language", "Bjarne Stroustrup", "978-0321563842", 2013, true, 79.99, {"Programming", "C++", "Technical"}};
        _books[_next_id++] = {2, "Clean Code", "Robert C. Martin", "978-0132350884", 2008, true, 49.99, {"Programming", "Software Engineering"}};
        _books[_next_id++] = {3, "Design Patterns", "Gang of Four", "978-0201633610", 1994, false, 59.99, {"Programming", "Design Patterns"}};
        _books[_next_id++] = {4, "Effective C++", "Scott Meyers", "978-0321334879", 2005, true, 44.99, {"Programming", "C++"}};
        _books[_next_id++] = {5, "The Pragmatic Programmer", "Andy Hunt", "978-0135957059", 2019, true, 54.99, {"Programming", "Software Engineering"}};
    }

    void handle_api_documentation(std::shared_ptr<qb::http::Context<qb::http::DefaultSession>> ctx) {
        qb::json api_docs = {
            {"name", "Books REST API"},
            {"version", "1.0"},
            {"description", "A complete REST API for managing books with JSON"},
            {"base_url", "http://localhost:8080/api/v1"},
            {"endpoints", {
                {{"method", "GET"}, {"path", "/"}, {"description", "API documentation"}},
                {{"method", "GET"}, {"path", "/health"}, {"description", "Health check"}},
                {{"method", "GET"}, {"path", "/api/v1/books"}, {"description", "List all books (supports filtering)"}},
                {{"method", "POST"}, {"path", "/api/v1/books"}, {"description", "Create a new book"}},
                {{"method", "GET"}, {"path", "/api/v1/books/{id}"}, {"description", "Get book by ID"}},
                {{"method", "PUT"}, {"path", "/api/v1/books/{id}"}, {"description", "Update book by ID"}},
                {{"method", "PATCH"}, {"path", "/api/v1/books/{id}"}, {"description", "Partially update book by ID"}},
                {{"method", "DELETE"}, {"path", "/api/v1/books/{id}"}, {"description", "Delete book by ID"}},
                {{"method", "GET"}, {"path", "/api/v1/books/search"}, {"description", "Search books"}},
                {{"method", "GET"}, {"path", "/api/v1/stats"}, {"description", "Get API statistics"}}
            }},
            {"middleware", {"CORS", "Security Headers", "Rate Limiting", "Compression", "Logging"}},
            {"sample_book", {
                {"title", "Example Book"},
                {"author", "John Doe"},
                {"isbn", "978-1234567890"},
                {"year", 2024},
                {"available", true},
                {"price", 29.99},
                {"categories", {"Fiction", "Adventure"}}
            }}
        };

        ctx->response().status() = qb::http::Status::OK;
        ctx->response().add_header("Content-Type", "application/json");
        ctx->response().body() = api_docs;
        ctx->complete();
    }

    void handle_health_check(std::shared_ptr<qb::http::Context<qb::http::DefaultSession>> ctx) {
        qb::json health = {
            {"status", "healthy"},
            {"timestamp", std::time(nullptr)},
            {"uptime", "calculated_in_real_app"},
            {"books_count", _books.size()},
            {"memory_usage", "calculated_in_real_app"}
        };

        ctx->response().status() = qb::http::Status::OK;
        ctx->response().add_header("Content-Type", "application/json");
        ctx->response().body() = health;
        ctx->complete();
    }

    void handle_get_books(std::shared_ptr<qb::http::Context<qb::http::DefaultSession>> ctx) {
        // Support filtering by query parameters using the correct API
        auto author_filter = ctx->request().query("author", 0, "");
        auto available_filter = ctx->request().query("available", 0, "");
        auto category_filter = ctx->request().query("category", 0, "");
        auto limit_str = ctx->request().query("limit", 0, "");
        auto offset_str = ctx->request().query("offset", 0, "");

        // Parse pagination
        int limit = limit_str.empty() ? 10 : std::stoi(limit_str);
        int offset = offset_str.empty() ? 0 : std::stoi(offset_str);
        limit = std::max(1, std::min(limit, 100)); // Clamp between 1 and 100

        qb::json books = qb::json::array();
        int current_offset = 0;
        int added_count = 0;

        for (const auto& [id, book] : _books) {
            // Apply filters
            if (!author_filter.empty() && book.author.find(author_filter) == std::string::npos) continue;
            if (!available_filter.empty() && std::to_string(book.available) != available_filter) continue;
            if (!category_filter.empty()) {
                bool has_category = std::find(book.categories.begin(), book.categories.end(), category_filter) != book.categories.end();
                if (!has_category) continue;
            }

            // Apply pagination
            if (current_offset < offset) {
                current_offset++;
                continue;
            }
            if (added_count >= limit) break;

            books.push_back(book.to_json());
            added_count++;
        }

        qb::json response = {
            {"books", books},
            {"pagination", {
                {"offset", offset},
                {"limit", limit},
                {"total", static_cast<int>(_books.size())},
                {"returned", added_count}
            }},
            {"filters", {
                {"author", author_filter},
                {"available", available_filter},
                {"category", category_filter}
            }}
        };

        ctx->response().status() = qb::http::Status::OK;
        ctx->response().add_header("Content-Type", "application/json");
        ctx->response().body() = response;
        ctx->complete();
    }

    void handle_create_book(std::shared_ptr<qb::http::Context<qb::http::DefaultSession>> ctx) {
        try {
            auto json_body = ctx->request().body().as<qb::json>();
            
            // Validate required fields
            if (!json_body.contains("title") || !json_body.contains("author")) {
                ctx->response().status() = qb::http::Status::BAD_REQUEST;
                ctx->response().add_header("Content-Type", "application/json");
                ctx->response().body() = qb::json{
                    {"error", "Validation Error"},
                    {"message", "Missing required fields: title and author are required"}
                };
                ctx->complete();
                return;
            }

            // Create new book
            Book new_book = Book::from_json(json_body);
            new_book.id = _next_id++;

            // Validate data
            if (new_book.title.empty() || new_book.author.empty()) {
                ctx->response().status() = qb::http::Status::BAD_REQUEST;
                ctx->response().add_header("Content-Type", "application/json");
                ctx->response().body() = qb::json{
                    {"error", "Validation Error"},
                    {"message", "Title and author cannot be empty"}
                };
                ctx->complete();
                return;
            }

            if (new_book.year < 0 || new_book.year > 2025) {
                ctx->response().status() = qb::http::Status::BAD_REQUEST;
                ctx->response().add_header("Content-Type", "application/json");
                ctx->response().body() = qb::json{
                    {"error", "Validation Error"},
                    {"message", "Year must be between 0 and 2025"}
                };
                ctx->complete();
                return;
            }

            if (new_book.price < 0) {
                ctx->response().status() = qb::http::Status::BAD_REQUEST;
                ctx->response().add_header("Content-Type", "application/json");
                ctx->response().body() = qb::json{
                    {"error", "Validation Error"},
                    {"message", "Price cannot be negative"}
                };
                ctx->complete();
                return;
            }

            // Store the book
            _books[new_book.id] = new_book;

            qb::json response = {
                {"message", "Book created successfully"},
                {"book", new_book.to_json()}
            };

            ctx->response().status() = qb::http::Status::CREATED;
            ctx->response().add_header("Content-Type", "application/json");
            ctx->response().add_header("Location", "/api/v1/books/" + std::to_string(new_book.id));
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

    void handle_get_book(std::shared_ptr<qb::http::Context<qb::http::DefaultSession>> ctx) {
        try {
            int book_id = std::stoi(ctx->path_param("id"));
            
            auto it = _books.find(book_id);
            if (it == _books.end()) {
                ctx->response().status() = qb::http::Status::NOT_FOUND;
                ctx->response().add_header("Content-Type", "application/json");
                ctx->response().body() = qb::json{
                    {"error", "Not Found"},
                    {"message", "Book with ID " + std::to_string(book_id) + " not found"}
                };
                ctx->complete();
                return;
            }

            ctx->response().status() = qb::http::Status::OK;
            ctx->response().add_header("Content-Type", "application/json");
            ctx->response().body() = it->second.to_json();
            ctx->complete();

        } catch (const std::exception& e) {
            ctx->response().status() = qb::http::Status::BAD_REQUEST;
            ctx->response().add_header("Content-Type", "application/json");
            ctx->response().body() = qb::json{
                {"error", "Invalid ID"},
                {"message", "Book ID must be a valid integer"}
            };
            ctx->complete();
        }
    }

    void handle_update_book(std::shared_ptr<qb::http::Context<qb::http::DefaultSession>> ctx) {
        try {
            int book_id = std::stoi(ctx->path_param("id"));
            auto it = _books.find(book_id);
            
            if (it == _books.end()) {
                ctx->response().status() = qb::http::Status::NOT_FOUND;
                ctx->response().add_header("Content-Type", "application/json");
                ctx->response().body() = qb::json{
                    {"error", "Not Found"},
                    {"message", "Book with ID " + std::to_string(book_id) + " not found"}
                };
                ctx->complete();
                return;
            }

            auto json_body = ctx->request().body().as<qb::json>();
            Book updated_book = Book::from_json(json_body);
            updated_book.id = book_id; // Preserve the ID

            // Validate updated data
            if (updated_book.title.empty() || updated_book.author.empty()) {
                ctx->response().status() = qb::http::Status::BAD_REQUEST;
                ctx->response().add_header("Content-Type", "application/json");
                ctx->response().body() = qb::json{
                    {"error", "Validation Error"},
                    {"message", "Title and author cannot be empty"}
                };
                ctx->complete();
                return;
            }

            // Update the book
            _books[book_id] = updated_book;

            qb::json response = {
                {"message", "Book updated successfully"},
                {"book", updated_book.to_json()}
            };

            ctx->response().status() = qb::http::Status::OK;
            ctx->response().add_header("Content-Type", "application/json");
            ctx->response().body() = response;
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

    void handle_patch_book(std::shared_ptr<qb::http::Context<qb::http::DefaultSession>> ctx) {
        try {
            int book_id = std::stoi(ctx->path_param("id"));
            auto it = _books.find(book_id);
            
            if (it == _books.end()) {
                ctx->response().status() = qb::http::Status::NOT_FOUND;
                ctx->response().add_header("Content-Type", "application/json");
                ctx->response().body() = qb::json{
                    {"error", "Not Found"},
                    {"message", "Book with ID " + std::to_string(book_id) + " not found"}
                };
                ctx->complete();
                return;
            }

            auto json_body = ctx->request().body().as<qb::json>();
            Book& book = it->second;

            // Partially update only provided fields
            if (json_body.contains("title")) book.title = json_body["title"];
            if (json_body.contains("author")) book.author = json_body["author"];
            if (json_body.contains("isbn")) book.isbn = json_body["isbn"];
            if (json_body.contains("year")) book.year = json_body["year"];
            if (json_body.contains("available")) book.available = json_body["available"];
            if (json_body.contains("price")) book.price = json_body["price"];
            if (json_body.contains("categories")) book.categories = json_body["categories"];

            qb::json response = {
                {"message", "Book partially updated successfully"},
                {"book", book.to_json()}
            };

            ctx->response().status() = qb::http::Status::OK;
            ctx->response().add_header("Content-Type", "application/json");
            ctx->response().body() = response;
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

    void handle_delete_book(std::shared_ptr<qb::http::Context<qb::http::DefaultSession>> ctx) {
        try {
            int book_id = std::stoi(ctx->path_param("id"));
            auto it = _books.find(book_id);
            
            if (it == _books.end()) {
                ctx->response().status() = qb::http::Status::NOT_FOUND;
                ctx->response().add_header("Content-Type", "application/json");
                ctx->response().body() = qb::json{
                    {"error", "Not Found"},
                    {"message", "Book with ID " + std::to_string(book_id) + " not found"}
                };
                ctx->complete();
                return;
            }

            Book deleted_book = it->second;
            _books.erase(it);

            qb::json response = {
                {"message", "Book deleted successfully"},
                {"deleted_book", deleted_book.to_json()}
            };

            ctx->response().status() = qb::http::Status::OK;
            ctx->response().add_header("Content-Type", "application/json");
            ctx->response().body() = response;
            ctx->complete();

        } catch (const std::exception& e) {
            ctx->response().status() = qb::http::Status::BAD_REQUEST;
            ctx->response().add_header("Content-Type", "application/json");
            ctx->response().body() = qb::json{
                {"error", "Invalid ID"},
                {"message", "Book ID must be a valid integer"}
            };
            ctx->complete();
        }
    }

    void handle_search_books(std::shared_ptr<qb::http::Context<qb::http::DefaultSession>> ctx) {
        auto query = ctx->request().query("q", 0, "");
        if (query.empty()) {
            ctx->response().status() = qb::http::Status::BAD_REQUEST;
            ctx->response().add_header("Content-Type", "application/json");
            ctx->response().body() = qb::json{
                {"error", "Missing Parameter"},
                {"message", "Search query parameter 'q' is required"}
            };
            ctx->complete();
            return;
        }

        qb::json results = qb::json::array();
        std::string search_term = query;
        std::transform(search_term.begin(), search_term.end(), search_term.begin(), ::tolower);

        for (const auto& [id, book] : _books) {
            std::string title_lower = book.title;
            std::string author_lower = book.author;
            std::transform(title_lower.begin(), title_lower.end(), title_lower.begin(), ::tolower);
            std::transform(author_lower.begin(), author_lower.end(), author_lower.begin(), ::tolower);

            if (title_lower.find(search_term) != std::string::npos ||
                author_lower.find(search_term) != std::string::npos ||
                book.isbn.find(search_term) != std::string::npos) {
                results.push_back(book.to_json());
            }
        }

        qb::json response = {
            {"query", query},
            {"results", results},
            {"count", results.size()}
        };

        ctx->response().status() = qb::http::Status::OK;
        ctx->response().add_header("Content-Type", "application/json");
        ctx->response().body() = response;
        ctx->complete();
    }

    void handle_get_stats(std::shared_ptr<qb::http::Context<qb::http::DefaultSession>> ctx) {
        int available_count = 0;
        int unavailable_count = 0;
        double total_value = 0.0;
        qb::unordered_map<std::string, int> category_counts;

        for (const auto& [id, book] : _books) {
            if (book.available) available_count++;
            else unavailable_count++;
            
            total_value += book.price;
            
            for (const auto& category : book.categories) {
                category_counts[category]++;
            }
        }

        qb::json stats = {
            {"total_books", static_cast<int>(_books.size())},
            {"available_books", available_count},
            {"unavailable_books", unavailable_count},
            {"total_inventory_value", total_value},
            {"categories", category_counts},
            {"average_price", _books.empty() ? 0.0 : total_value / _books.size()}
        };

        ctx->response().status() = qb::http::Status::OK;
        ctx->response().add_header("Content-Type", "application/json");
        ctx->response().body() = stats;
        ctx->complete();
    }

    void print_api_documentation() {
        std::cout << "\n=== REST API Server with JSON ===\n"
                  << "Server running on: http://localhost:8080\n\n"
                  << "Standard Middleware Active:\n"
                  << "  • CORS (Cross-Origin Resource Sharing)\n"
                  << "  • Security Headers (XSS, CSRF protection)\n"
                  << "  • Rate Limiting (100 requests/minute per IP)\n"
                  #ifdef QB_IO_WITH_ZLIB
                  << "  • Compression (gzip/deflate)\n"
                  #endif
                  << "  • Request/Response Logging\n"
                  << "  • Error Handling with JSON responses\n\n"
                  << "Available Endpoints:\n"
                  << "  GET    /                     - API documentation\n"
                  << "  GET    /health               - Health check\n"
                  << "  GET    /api/v1/books         - List books (supports filters)\n"
                  << "  POST   /api/v1/books         - Create book\n"
                  << "  GET    /api/v1/books/:id     - Get book by ID\n"
                  << "  PUT    /api/v1/books/:id     - Update book\n"
                  << "  PATCH  /api/v1/books/:id     - Partial update\n"
                  << "  DELETE /api/v1/books/:id     - Delete book\n"
                  << "  GET    /api/v1/books/search  - Search books (?q=query)\n"
                  << "  GET    /api/v1/stats         - API statistics\n\n"
                  << "Query Parameters for /api/v1/books:\n"
                  << "  ?author=name    - Filter by author\n"
                  << "  ?available=true - Filter by availability\n"
                  << "  ?category=name  - Filter by category\n"
                  << "  ?limit=10       - Limit results (1-100)\n"
                  << "  ?offset=0       - Offset for pagination\n\n"
                  << "Try: curl -X GET http://localhost:8080/api/v1/books\n"
                  << "Or:  curl -X POST http://localhost:8080/api/v1/books \\\n"
                  << "       -H 'Content-Type: application/json' \\\n"
                  << "       -d '{\"title\":\"Test Book\",\"author\":\"Test Author\"}'\n"
                  << std::endl;
    }

    void on(const qb::KillEvent& event) noexcept {
        std::cout << "Shutting down REST API server..." << std::endl;
        qb::Actor::on(event);
    }
};

int main() {
    qb::Main engine;
    
    engine.addActor<RestApiServer>(0);
    engine.start(false);
    engine.join();
    
    return engine.hasError();
} 