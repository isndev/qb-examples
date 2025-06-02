/**
 * @file examples/qbm/http/11_https_server.cpp
 * @brief HTTPS Server example using QB HTTP with SSL/TLS
 *
 * This example demonstrates:
 * - HTTPS server configuration with SSL/TLS
 * - SSL certificate handling (self-signed for demo)
 * - Secure endpoints with proper headers
 * - HTTP to HTTPS redirection
 * - SSL context configuration
 * - Integration with QB Actor framework
 *
 * @author qb - C++ Actor Framework
 * @copyright Copyright (c) 2011-2025 qb - isndev (cpp.actor)
 * Licensed under the Apache License, Version 2.0 (http://www.apache.org/licenses/LICENSE-2.0)
 * @ingroup Examples
 */

#include <iostream>
#include <fstream>
#include <filesystem>
#include <qb/main.h>
#include <http/http.h>
#include <http/middleware/cors.h>
#include <http/middleware/logging.h>
#include <http/middleware/security_headers.h>
#include <http/middleware/error_handling.h>

class HttpsServer : public qb::Actor, public qb::http::ssl::Server<> {
private:
    std::unique_ptr<qb::http::Server<>> _http_redirect_server;
    bool _certificates_ready = false;

public:
    HttpsServer() = default;

    bool onInit() override {
        std::cout << "Initializing HTTPS Server..." << std::endl;
        
        // Generate certificates if needed
        if (!ensure_certificates_exist()) {
            std::cerr << "Failed to ensure SSL certificates exist" << std::endl;
            return false;
        }
        
        // Create HTTP redirect server
        _http_redirect_server = std::make_unique<qb::http::Server<>>();
        if (!_http_redirect_server) {
            std::cerr << "Failed to create HTTP redirect server" << std::endl;
            return false;
        }
        
        // Setup HTTPS server
        setup_https_server();
        
        // Setup HTTP redirect server
        setup_http_redirect_server();
        
        // Start both servers
        if (!start_servers()) {
            std::cerr << "Failed to start servers" << std::endl;
            return false;
        }
        
        print_server_info();
        return true;
    }

private:
    bool ensure_certificates_exist() {
        // Check if certificates already exist
        if (std::filesystem::exists("server.crt") && std::filesystem::exists("server.key")) {
            std::cout << "Using existing SSL certificates" << std::endl;
            _certificates_ready = true;
            return true;
        }
        
        std::cout << "Generating self-signed SSL certificate..." << std::endl;
        
        // Generate certificate using OpenSSL command
        std::string cert_command = 
            "openssl req -x509 -newkey rsa:2048 -keyout server.key -out server.crt "
            "-days 365 -nodes -subj '/C=US/ST=CA/L=San Francisco/O=QB Framework/CN=localhost' 2>/dev/null";
        
        int result = std::system(cert_command.c_str());
        if (result != 0) {
            std::cerr << "Failed to generate certificate. Please ensure openssl is installed." << std::endl;
            return false;
        }
        
        _certificates_ready = true;
        std::cout << "Self-signed certificate generated successfully" << std::endl;
        return true;
    }
    
    void setup_https_server() {
        // Setup middleware
        setup_https_middleware();
        setup_https_error_handling();
        setup_secure_api_routes();
        
        // Compile router
        router().compile();
    }
    
    void setup_http_redirect_server() {
        // Simple redirect to HTTPS
        _http_redirect_server->router().get("/*path", [](auto ctx) {
            std::string host_header = std::string(ctx->request().header("host"));
            std::string path = std::string(ctx->request().uri().path());
            std::string https_url = "https://" + host_header + path;
            
            std::string encoded_queries = std::string(ctx->request().uri().encoded_queries());
            if (!encoded_queries.empty()) {
                https_url += "?" + encoded_queries;
            }
            
            ctx->response().status() = qb::http::Status::MOVED_PERMANENTLY;
            ctx->response().add_header("Location", https_url);
            ctx->response().add_header("Content-Type", "text/html");
            ctx->response().body() = 
                "<html><body>"
                "<h1>Redirecting to HTTPS</h1>"
                "<p>This site requires a secure connection. You are being redirected to: "
                "<a href=\"" + https_url + "\">" + https_url + "</a></p>"
                "</body></html>";
            ctx->complete();
        });
        
        _http_redirect_server->router().compile();
    }
    
    bool start_servers() {
        if (!_certificates_ready) {
            std::cerr << "SSL certificates not ready" << std::endl;
            return false;
        }
        
        // Start HTTPS server using proper SSL listen method
        qb::io::uri https_uri("https://0.0.0.0:8443");
        std::filesystem::path cert_file("server.crt");
        std::filesystem::path key_file("server.key");
        
        if (!listen(https_uri, cert_file, key_file)) {
            std::cerr << "Failed to start HTTPS server on port 8443" << std::endl;
            return false;
        }
        
        // Start HTTP redirect server
        if (!_http_redirect_server->listen({"tcp://0.0.0.0:8080"})) {
            std::cerr << "Failed to start HTTP redirect server on port 8080" << std::endl;
            return false;
        }
        
        start();
        _http_redirect_server->start();
        
        std::cout << "HTTPS server started on port 8443" << std::endl;
        std::cout << "HTTP redirect server started on port 8080" << std::endl;
        return true;
    }
    
    void setup_https_middleware() {
        // CORS middleware
        auto cors_middleware = qb::http::CorsMiddleware<qb::http::ssl::DefaultSecureSession>::dev();
        router().use(cors_middleware);
        
        // Security headers middleware
        auto security_options = qb::http::SecurityHeadersOptions::secure_defaults();
        auto security_middleware = std::make_shared<qb::http::SecurityHeadersMiddleware<qb::http::ssl::DefaultSecureSession>>(security_options);
        router().use(security_middleware);
        
        // Logging middleware
        auto logging_middleware = std::make_shared<qb::http::LoggingMiddleware<qb::http::ssl::DefaultSecureSession>>(
            [](qb::http::LogLevel level, const std::string& message) {
                std::string level_str;
                switch (level) {
                    case qb::http::LogLevel::Debug: level_str = "DEBUG"; break;
                    case qb::http::LogLevel::Info: level_str = "INFO"; break;
                    case qb::http::LogLevel::Warning: level_str = "WARNING"; break;
                    case qb::http::LogLevel::Error: level_str = "ERROR"; break;
                }
                std::cout << "[HTTPS " << level_str << "] " << message << std::endl;
            },
            qb::http::LogLevel::Info,
            qb::http::LogLevel::Info
        );
        router().use(logging_middleware);
        
        // Custom security middleware
        router().use([](auto ctx, auto next) {
            // Add custom security headers
            ctx->response().add_header("X-Powered-By", "QB Framework HTTPS");
            ctx->response().add_header("Strict-Transport-Security", "max-age=31536000; includeSubDomains; preload");
            next();
        });
    }
    
    void setup_https_error_handling() {
        auto error_handler = qb::http::error_handling_middleware<qb::http::ssl::DefaultSecureSession>();
        
        error_handler->on_status(qb::http::Status::NOT_FOUND, [](auto ctx) {
            qb::json error_response = {
                {"error", "Not Found"},
                {"message", "The requested HTTPS resource was not found"},
                {"timestamp", std::time(nullptr)}
            };
            ctx->response().add_header("Content-Type", "application/json");
            ctx->response().body() = error_response;
        });
        
        error_handler->on_status(qb::http::Status::INTERNAL_SERVER_ERROR, [](auto ctx) {
            qb::json error_response = {
                {"error", "Internal Server Error"},
                {"message", "An unexpected error occurred on the secure server"},
                {"timestamp", std::time(nullptr)}
            };
            ctx->response().add_header("Content-Type", "application/json");
            ctx->response().body() = error_response;
        });
        
        auto error_task = std::make_shared<qb::http::MiddlewareTask<qb::http::ssl::DefaultSecureSession>>(error_handler);
        router().set_error_task_chain({error_task});
    }
    
    void setup_secure_api_routes() {
        // Main page
        router().get("/", [this](auto ctx) {
            handle_main_page(ctx);
        });
        
        // Certificate info
        router().get("/certificate", [this](auto ctx) {
            handle_certificate_info(ctx);
        });
        
        // Security headers test
        router().get("/security/headers", [this](auto ctx) {
            handle_security_headers_test(ctx);
        });
        
        // Security test endpoint
        router().get("/security/test", [this](auto ctx) {
            handle_security_test(ctx);
        });
        
        // API endpoints requiring API key
        auto api_group = router().group("/api");
        
        // API key authentication middleware
        api_group->use([](auto ctx, auto next) {
            std::string api_key = std::string(ctx->request().header("X-API-Key"));
            if (api_key != "secure-api-key-12345") {
                ctx->response().status() = qb::http::Status::UNAUTHORIZED;
                qb::json error_response = {
                    {"error", "Invalid or missing API key"},
                    {"message", "Please provide a valid X-API-Key header"}
                };
                ctx->response().add_header("Content-Type", "application/json");
                ctx->response().body() = error_response;
                ctx->complete();
                return;
            }
            next();
        });
        
        api_group->get("/user/profile", [this](auto ctx) {
            handle_secure_user_profile(ctx);
        });
        
        api_group->post("/payment/process", [this](auto ctx) {
            handle_secure_payment(ctx);
        });
        
        // Admin endpoints requiring admin token
        auto admin_group = api_group->group("/admin");
        admin_group->use([](auto ctx, auto next) {
            std::string admin_token = std::string(ctx->request().header("X-Admin-Token"));
            if (admin_token != "admin-super-secret-token") {
                ctx->response().status() = qb::http::Status::FORBIDDEN;
                qb::json error_response = {
                    {"error", "Admin access denied"},
                    {"message", "Please provide a valid X-Admin-Token header"}
                };
                ctx->response().add_header("Content-Type", "application/json");
                ctx->response().body() = error_response;
                ctx->complete();
                return;
            }
            next();
        });
        
        admin_group->get("/config", [this](auto ctx) {
            handle_admin_config(ctx);
        });
        
        api_group->post("/upload/secure", [this](auto ctx) {
            handle_secure_upload(ctx);
        });
    }
    
    void handle_main_page(std::shared_ptr<qb::http::Context<qb::http::ssl::DefaultSecureSession>> ctx) {
        qb::json response = {
            {"message", "Welcome to QB HTTPS Server"},
            {"version", "1.0"},
            {"ssl_enabled", true},
            {"security_features", {
                "HSTS enabled",
                "Secure headers",
                "SSL/TLS encryption",
                "API key authentication",
                "Admin token protection"
            }},
            {"endpoints", {
                {"GET /", "This main page"},
                {"GET /certificate", "SSL certificate information"},
                {"GET /security/headers", "Security headers test"},
                {"GET /security/test", "Comprehensive security test"},
                {"GET /api/user/profile", "User profile [API KEY]"},
                {"POST /api/payment/process", "Payment processing [API KEY]"},
                {"GET /api/admin/config", "Admin configuration [ADMIN TOKEN]"},
                {"POST /api/upload/secure", "Secure file upload [API KEY]"}
            }},
            {"authentication", {
                {"api_key", "X-API-Key: secure-api-key-12345"},
                {"admin_token", "X-Admin-Token: admin-super-secret-token"}
            }},
            {"timestamp", std::time(nullptr)}
        };
        
        ctx->response().status() = qb::http::Status::OK;
        ctx->response().add_header("Content-Type", "application/json");
        ctx->response().body() = response;
        ctx->complete();
    }
    
    void handle_certificate_info(std::shared_ptr<qb::http::Context<qb::http::ssl::DefaultSecureSession>> ctx) {
        qb::json cert_info = {
            {"certificate_status", "self-signed"},
            {"subject", "/C=US/ST=CA/L=San Francisco/O=QB Framework/CN=localhost"},
            {"validity", "365 days from generation"},
            {"key_size", "2048 bits"},
            {"algorithm", "RSA"},
            {"files", {
                {"certificate", "server.crt"},
                {"private_key", "server.key"}
            }},
            {"note", "This is a self-signed certificate for development only"},
            {"timestamp", std::time(nullptr)}
        };
        
        ctx->response().status() = qb::http::Status::OK;
        ctx->response().add_header("Content-Type", "application/json");
        ctx->response().body() = cert_info;
        ctx->complete();
    }
    
    void handle_security_headers_test(std::shared_ptr<qb::http::Context<qb::http::ssl::DefaultSecureSession>> ctx) {
        qb::json headers_info = qb::json::object();
        
        // Collect response headers
        qb::json response_headers = qb::json::object();
        for (const auto& [name, values] : ctx->response().headers()) {
            if (!values.empty()) {
                response_headers[name] = values[0]; // Take first value
            }
        }
        
        qb::json security_analysis = {
            {"response_headers", response_headers},
            {"security_evaluation", {
                {"hsts_enabled", response_headers.contains("Strict-Transport-Security")},
                {"x_frame_options", response_headers.contains("X-Frame-Options")},
                {"x_content_type_options", response_headers.contains("X-Content-Type-Options")},
                {"csp_enabled", response_headers.contains("Content-Security-Policy")},
                {"referrer_policy", response_headers.contains("Referrer-Policy")}
            }},
            {"recommendations", {
                "All security headers are properly configured",
                "HSTS ensures HTTPS-only connections",
                "X-Frame-Options prevents clickjacking",
                "Content Security Policy mitigates XSS attacks"
            }},
            {"timestamp", std::time(nullptr)}
        };
        
        ctx->response().status() = qb::http::Status::OK;
        ctx->response().add_header("Content-Type", "application/json");
        ctx->response().body() = security_analysis;
        ctx->complete();
    }
    
    void handle_security_test(std::shared_ptr<qb::http::Context<qb::http::ssl::DefaultSecureSession>> ctx) {
        // Comprehensive security test
        std::string user_agent = std::string(ctx->request().header("User-Agent"));
        std::string host = std::string(ctx->request().header("Host"));
        
        qb::json security_test = {
            {"connection_security", {
                {"protocol", "HTTPS"},
                {"encryption", "TLS/SSL"},
                {"certificate_type", "self-signed"},
                {"secure_connection", true}
            }},
            {"headers_security", {
                {"host_header", host},
                {"user_agent_present", !user_agent.empty()},
                {"secure_headers_applied", true}
            }},
            {"api_protection", {
                {"endpoint_requires_api_key", true},
                {"admin_endpoints_protected", true},
                {"rate_limiting", "not_implemented_in_demo"},
                {"input_validation", "implemented_per_endpoint"}
            }},
            {"recommendations", {
                "Connection is properly encrypted",
                "Security headers are configured",
                "API endpoints require authentication",
                "Consider implementing rate limiting for production"
            }},
            {"test_timestamp", std::time(nullptr)}
        };
        
        ctx->response().status() = qb::http::Status::OK;
        ctx->response().add_header("Content-Type", "application/json");
        ctx->response().body() = security_test;
        ctx->complete();
    }
    
    void handle_secure_user_profile(std::shared_ptr<qb::http::Context<qb::http::ssl::DefaultSecureSession>> ctx) {
        // Mock user profile data
        qb::json profile = {
            {"user_id", "secure_user_12345"},
            {"username", "secure_demo_user"},
            {"email", "user@secure-demo.com"},
            {"profile_type", "premium"},
            {"account_status", "active"},
            {"permissions", {"read", "write", "api_access"}},
            {"security_settings", {
                {"two_factor_enabled", true},
                {"api_key_active", true},
                {"last_login", "2024-01-15T10:30:00Z"}
            }},
            {"accessed_via", "HTTPS API with valid API key"},
            {"timestamp", std::time(nullptr)}
        };
        
        ctx->response().status() = qb::http::Status::OK;
        ctx->response().add_header("Content-Type", "application/json");
        ctx->response().body() = profile;
        ctx->complete();
    }
    
    void handle_secure_payment(std::shared_ptr<qb::http::Context<qb::http::ssl::DefaultSecureSession>> ctx) {
        try {
            auto payment_data = ctx->request().body().as<qb::json>();
            
            // Mock payment processing
            qb::json payment_response = {
                {"transaction_id", "txn_" + std::to_string(std::time(nullptr))},
                {"status", "processed"},
                {"amount", payment_data.value("amount", 0.0)},
                {"currency", payment_data.value("currency", "USD")},
                {"payment_method", payment_data.value("payment_method", "credit_card")},
                {"security", {
                    {"encrypted_connection", true},
                    {"pci_compliant", true},
                    {"fraud_check", "passed"}
                }},
                {"processing_time", "0.234s"},
                {"timestamp", std::time(nullptr)}
            };
            
            ctx->response().status() = qb::http::Status::OK;
            ctx->response().add_header("Content-Type", "application/json");
            ctx->response().body() = payment_response;
            
        } catch (const std::exception& e) {
            qb::json error_response = {
                {"error", "Invalid payment data"},
                {"message", e.what()},
                {"timestamp", std::time(nullptr)}
            };
            ctx->response().status() = qb::http::Status::BAD_REQUEST;
            ctx->response().add_header("Content-Type", "application/json");
            ctx->response().body() = error_response;
        }
        
        ctx->complete();
    }
    
    void handle_admin_config(std::shared_ptr<qb::http::Context<qb::http::ssl::DefaultSecureSession>> ctx) {
        qb::json admin_config = {
            {"server_config", {
                {"https_port", 8443},
                {"http_redirect_port", 8080},
                {"ssl_certificate", "server.crt"},
                {"ssl_private_key", "server.key"}
            }},
            {"security_config", {
                {"hsts_max_age", 31536000},
                {"api_key_required", true},
                {"admin_token_required", true},
                {"secure_headers_enabled", true}
            }},
            {"performance_metrics", {
                {"active_connections", "calculated_in_real_app"},
                {"requests_per_second", "calculated_in_real_app"},
                {"cpu_usage", "calculated_in_real_app"},
                {"memory_usage", "calculated_in_real_app"}
            }},
            {"admin_access", true},
            {"timestamp", std::time(nullptr)}
        };
        
        ctx->response().status() = qb::http::Status::OK;
        ctx->response().add_header("Content-Type", "application/json");
        ctx->response().body() = admin_config;
        ctx->complete();
    }
    
    void handle_secure_upload(std::shared_ptr<qb::http::Context<qb::http::ssl::DefaultSecureSession>> ctx) {
        try {
            std::string content_type = std::string(ctx->request().header("Content-Type"));
            std::string content_length_str = std::string(ctx->request().header("Content-Length"));
            
            size_t content_length = 0;
            if (!content_length_str.empty()) {
                content_length = std::stoull(content_length_str);
            }
            
            // Mock secure file upload processing
            qb::json upload_response = {
                {"upload_id", "upload_" + std::to_string(std::time(nullptr))},
                {"status", "received"},
                {"content_type", content_type},
                {"content_length", content_length},
                {"security_scan", "pending"},
                {"encryption", "in_transit_and_at_rest"},
                {"storage_location", "secure_encrypted_storage"},
                {"virus_scan", "scheduled"},
                {"access_control", "api_key_protected"},
                {"timestamp", std::time(nullptr)}
            };
            
            ctx->response().status() = qb::http::Status::ACCEPTED;
            ctx->response().add_header("Content-Type", "application/json");
            ctx->response().body() = upload_response;
            
        } catch (const std::exception& e) {
            qb::json error_response = {
                {"error", "Upload processing failed"},
                {"message", e.what()},
                {"timestamp", std::time(nullptr)}
            };
            ctx->response().status() = qb::http::Status::BAD_REQUEST;
            ctx->response().add_header("Content-Type", "application/json");
            ctx->response().body() = error_response;
        }
        
        ctx->complete();
    }
    
    void print_server_info() {
        std::cout << "\n=== QB HTTPS Server with SSL/TLS ===" << std::endl;
        std::cout << "HTTPS Server: https://localhost:8443" << std::endl;
        std::cout << "HTTP Redirect: http://localhost:8080 -> https://localhost:8443" << std::endl;
        
        std::cout << "\nSecurity Features:" << std::endl;
        std::cout << "  • SSL/TLS encryption with self-signed certificate" << std::endl;
        std::cout << "  • HTTP Strict Transport Security (HSTS)" << std::endl;
        std::cout << "  • Security headers (CSP, X-Frame-Options, etc.)" << std::endl;
        std::cout << "  • API key authentication for sensitive endpoints" << std::endl;
        std::cout << "  • Admin token protection for administrative functions" << std::endl;
        
        std::cout << "\nPublic Endpoints:" << std::endl;
        std::cout << "  GET  https://localhost:8443/             - Server information" << std::endl;
        std::cout << "  GET  https://localhost:8443/certificate  - Certificate details" << std::endl;
        std::cout << "  GET  https://localhost:8443/security/headers - Security headers test" << std::endl;
        std::cout << "  GET  https://localhost:8443/security/test    - Security analysis" << std::endl;
        
        std::cout << "\nAPI Endpoints (require X-API-Key: secure-api-key-12345):" << std::endl;
        std::cout << "  GET  https://localhost:8443/api/user/profile   - User profile" << std::endl;
        std::cout << "  POST https://localhost:8443/api/payment/process - Payment processing" << std::endl;
        std::cout << "  POST https://localhost:8443/api/upload/secure   - Secure file upload" << std::endl;
        
        std::cout << "\nAdmin Endpoints (require X-Admin-Token: admin-super-secret-token):" << std::endl;
        std::cout << "  GET  https://localhost:8443/api/admin/config - Server configuration" << std::endl;
        
        std::cout << "\nTest Commands:" << std::endl;
        std::cout << "  # Test main page" << std::endl;
        std::cout << "  curl -k https://localhost:8443/" << std::endl;
        std::cout << "  # Test API with key" << std::endl;
        std::cout << "  curl -k -H 'X-API-Key: secure-api-key-12345' https://localhost:8443/api/user/profile" << std::endl;
        std::cout << "  # Test admin endpoint" << std::endl;
        std::cout << "  curl -k -H 'X-API-Key: secure-api-key-12345' -H 'X-Admin-Token: admin-super-secret-token' https://localhost:8443/api/admin/config" << std::endl;
        std::cout << "  # Test HTTP redirect" << std::endl;
        std::cout << "  curl -v http://localhost:8080/" << std::endl;
        
        std::cout << "\nNote: Use -k flag with curl to accept self-signed certificate" << std::endl;
        std::cout << std::endl;
    }
    
    void on(const qb::KillEvent& event) noexcept {
        std::cout << "HTTPS Server shutting down..." << std::endl;
        this->kill();
    }
};

int main() {
    qb::Main engine;
    
    engine.addActor<HttpsServer>(0);
    engine.start();
    engine.join();
    
    return engine.hasError() ? 1 : 0;
} 