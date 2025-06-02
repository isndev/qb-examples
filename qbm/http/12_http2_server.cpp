/**
 * @file 12_http2_server.cpp
 * @brief HTTP/2 server with static file serving
 * 
 * This example demonstrates:
 * - HTTP/2 server with ALPN support
 * - Static file serving with proper MIME types
 * - HTTP/2 specific features demonstration
 * - SSL/TLS with certificate generation
 * - Multiplexing, stream prioritization demos
 */

#include <qb/main.h>
#include <http/http.h>
#include <http/middleware/all.h>
#include <http/2/http2.h>
#include <iostream>
#include <filesystem>
#include <fstream>
#include <cstdlib>
#include <chrono>
#include <random>

using qb::json;

/**
 * @class Http2StaticSession
 * @brief HTTP/2 session handling client connections
 */
class Http2StaticSession : public qb::http2::use<Http2StaticSession>::session<class Http2StaticServer> {
public:
    Http2StaticSession(Http2StaticServer &server_ref)
        : session(server_ref) {
    }
};

/**
 * @class Http2StaticServer
 * @brief HTTP/2 server implementing static file serving and HTTP/2 demos
 */
class Http2StaticServer : public qb::Actor, public qb::http2::use<Http2StaticServer>::server<Http2StaticSession> {
private:
    std::string _static_root;
    std::string _cert_file;
    std::string _key_file;

public:
    explicit Http2StaticServer(const std::string& static_root = "./resources/http2")
        : _static_root(static_root), _cert_file("./server.crt"), _key_file("./server.key") {}

    bool onInit() override {
        std::cout << "HTTP/2 server actor created successfully" << std::endl;
        std::cout << "Initializing HTTP/2 Static File Server..." << std::endl;
        
        if (!setup_directories_and_ssl()) {
            return false;
        }
        
        setup_middleware();
        setup_routes();
        
        return setup_and_start_server();
    }

private:
    bool setup_directories_and_ssl() {
        // Create static directory if it doesn't exist
        if (!std::filesystem::exists(_static_root)) {
            std::filesystem::create_directories(_static_root);
            std::cout << "Created static directory: " << _static_root << std::endl;
        }
        
        // Check for SSL certificates
        if (!std::filesystem::exists(_cert_file) || !std::filesystem::exists(_key_file)) {
            if (!generate_self_signed_certificate()) {
                return false;
            }
        } else {
            std::cout << "Using existing SSL certificates" << std::endl;
        }
        
        return true;
    }

    bool generate_self_signed_certificate() {
        std::cout << "Generating self-signed SSL certificate..." << std::endl;
        std::string cert_command = 
            "openssl req -x509 -newkey rsa:2048 -keyout server.key -out server.crt "
            "-days 365 -nodes -subj '/C=US/ST=CA/L=San Francisco/O=QB Framework/CN=localhost' 2>/dev/null";
        
        int result = std::system(cert_command.c_str());
        return result == 0;
    }

    void setup_middleware() {
        // CORS middleware
        auto cors_middleware = qb::http::CorsMiddleware<Http2StaticSession>::dev();
        router().use(cors_middleware);
        
        // Custom security middleware with relaxed CSP for demo
        router().use([](auto ctx, auto next) {
            // Set relaxed CSP to allow inline styles and scripts for demo
            ctx->response().add_header("Content-Security-Policy", 
                "default-src 'self' 'unsafe-inline' 'unsafe-eval'; "
                "script-src 'self' 'unsafe-inline' 'unsafe-eval'; "
                "style-src 'self' 'unsafe-inline' 'unsafe-hashes'; "
                "img-src 'self' data: blob:; "
                "font-src 'self' data:; "
                "connect-src 'self'");
            
            // Add other security headers but keep them relaxed for demo
            ctx->response().add_header("X-Content-Type-Options", "nosniff");
            ctx->response().add_header("X-Frame-Options", "SAMEORIGIN");
            ctx->response().add_header("X-XSS-Protection", "1; mode=block");
            ctx->response().add_header("Referrer-Policy", "strict-origin-when-cross-origin");
            ctx->response().add_header("X-Powered-By", "QB Framework HTTP/2");
            
            next();
        });
        
        // Logging middleware
        auto logging_middleware = std::make_shared<qb::http::LoggingMiddleware<Http2StaticSession>>(
            [](qb::http::LogLevel level, const std::string& message) {
                std::cout << "[HTTP/2 INFO] " << message << std::endl;
            },
            qb::http::LogLevel::Info,
            qb::http::LogLevel::Info
        );
        router().use(logging_middleware);
        
        // Static files middleware - configured for /static/* routes
        qb::http::StaticFilesOptions static_options(_static_root);
        static_options.with_path_prefix_to_strip("/static")
                     .with_etags(true)
                     .with_last_modified(true)
                     .with_cache_control(true, "public, max-age=3600");
        
        auto static_middleware = qb::http::static_files_middleware<Http2StaticSession>(std::move(static_options));
        router().use(static_middleware);
    }

    void setup_routes() {
        // Root route - serve index.html
        router().get("/", [this](auto ctx) {
            std::filesystem::path index_path = std::filesystem::path(_static_root) / "index.html";
            if (std::filesystem::exists(index_path)) {
                std::ifstream file(index_path);
                std::string content((std::istreambuf_iterator<char>(file)),
                                  std::istreambuf_iterator<char>());
                ctx->response().body() = std::move(content);
                ctx->response().add_header("Content-Type", "text/html; charset=utf-8");
                ctx->complete();
            } else {
                ctx->response().status() = qb::http::Status::NOT_FOUND;
                ctx->response().body() = "Index file not found";
                ctx->complete();
            }
        });

        // Favicon route
        router().get("/favicon.ico", [this](auto ctx) {
            std::filesystem::path favicon_path = std::filesystem::path(_static_root) / "favicon.ico";
            if (std::filesystem::exists(favicon_path)) {
                std::ifstream file(favicon_path, std::ios::binary);
                std::string content((std::istreambuf_iterator<char>(file)),
                                  std::istreambuf_iterator<char>());
                ctx->response().body() = std::move(content);
                ctx->response().add_header("Content-Type", "image/x-icon");
                ctx->response().add_header("Cache-Control", "public, max-age=86400");
                ctx->complete();
            } else {
                ctx->response().status() = qb::http::Status::NOT_FOUND;
                ctx->response().add_header("Content-Type", "text/plain");
                ctx->response().body() = "Favicon not found";
                ctx->complete();
            }
        });

        // API routes group
        auto api_group = router().group("/api");
        
        api_group->get("/multiplexing-demo", [](auto ctx) {
            qb::json response;
            response["feature"] = "HTTP/2 Multiplexing";
            response["description"] = "Multiple requests over single connection";
            response["timestamp"] = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            response["request_id"] = ctx->request().query("request", 0, "unknown");
            response["stream_id"] = rand() % 1000 + 1;
            response["benefits"] = qb::json::array({
                "Reduced latency", "Better resource utilization", "Improved page load times"
            });
            
            ctx->response().body() = response;
            ctx->response().add_header("Content-Type", "application/json");
            ctx->complete();
        });

        api_group->get("/stream-priority/:level", [](auto ctx) {
            std::string level = ctx->path_param("level");
            qb::json response;
            response["feature"] = "Stream Prioritization";
            response["priority_level"] = level;
            response["timestamp"] = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            
            int weight = 16; // Default weight
            if (level == "critical") weight = 256;
            else if (level == "high") weight = 128;
            else if (level == "medium") weight = 64;
            else if (level == "low") weight = 32;
            
            response["weight"] = weight;
            response["description"] = "Higher weight = higher priority";
            response["processing_time_ms"] = weight / 8;
            
            ctx->response().body() = response;
            ctx->response().add_header("Content-Type", "application/json");
            ctx->complete();
        });

        api_group->get("/server-push-demo", [](auto ctx) {
            qb::json response;
            response["feature"] = "Server Push Simulation";
            response["pushed_resources"] = qb::json::array({
                "/static/styles.css", "/static/http2-demo.js", "/static/data.json"
            });
            response["description"] = "Server can push resources before client requests";
            response["timestamp"] = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            
            std::string resource = ctx->request().query("resource", 0, "");
            if (!resource.empty()) {
                response["pushed_resource"] = resource;
                response["size_bytes"] = resource.length() * 100;
            }
            
            ctx->response().body() = response;
            ctx->response().add_header("Content-Type", "application/json");
            ctx->complete();
        });

        api_group->get("/performance/:iterations", [](auto ctx) {
            std::string iterations_str = ctx->path_param("iterations");
            int iterations = std::stoi(iterations_str);
            
            qb::json response;
            response["feature"] = "Performance Testing";
            response["total_iterations"] = iterations;
            response["current_iteration"] = std::stoi(ctx->request().query("iteration", 0, "1"));
            response["timestamp"] = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            response["latency_ms"] = rand() % 50 + 10;
            response["throughput_rps"] = 1000 + rand() % 500;
            
            ctx->response().body() = response;
            ctx->response().add_header("Content-Type", "application/json");
            ctx->complete();
        });

        api_group->get("/data/:size", [](auto ctx) {
            std::string size_str = ctx->path_param("size");
            int size_kb = std::stoi(size_str);
            
            std::string data(size_kb * 1024, 'A');
            
            qb::json response;
            response["feature"] = "Bulk Data Transfer";
            response["requested_size_kb"] = size_kb;
            response["actual_size_bytes"] = data.length();
            response["data"] = data.substr(0, 100) + "...";
            response["timestamp"] = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            
            ctx->response().body() = response;
            ctx->response().add_header("Content-Type", "application/json");
            ctx->complete();
        });

        api_group->post("/echo", [](auto ctx) {
            qb::json response;
            response["feature"] = "Echo Service";
            response["method"] = "POST";
            response["timestamp"] = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            
            try {
                qb::json request_body = qb::json::parse(ctx->request().body().template as<std::string>());
                response["echoed_data"] = request_body;
                response["data_size_bytes"] = ctx->request().body().template as<std::string>().length();
            } catch (const std::exception& e) {
                response["echoed_data"] = ctx->request().body().template as<std::string>();
                response["note"] = "Non-JSON data echoed as string";
            }
            
            ctx->response().body() = response;
            ctx->response().add_header("Content-Type", "application/json");
            ctx->complete();
        });
        
        // Compile the router
        router().compile();
    }

    bool setup_and_start_server() {
        try {
            std::filesystem::path cert_file(_cert_file);
            std::filesystem::path key_file(_key_file);
            
            qb::io::uri server_uri("https://0.0.0.0:8443");
            
            if (!listen(server_uri, cert_file, key_file)) {
                std::cerr << "Failed to start HTTP/2 server on port 8443" << std::endl;
                return false;
            }
            
            start();
            print_server_info();
            return true;
            
        } catch (const std::exception& e) {
            std::cerr << "Failed to start HTTP/2 server: " << e.what() << std::endl;
            return false;
        }
    }

    void print_server_info() {
        std::cout << "==================================================" << std::endl;
        std::cout << "🚀 QB HTTP/2 Server with Static Files" << std::endl;
        std::cout << "==================================================" << std::endl;
        std::cout << "Server URL: https://localhost:8443/" << std::endl;
        std::cout << "Static Root: " << _static_root << std::endl;
        std::cout << "HTTP/2 Features:" << std::endl;
        std::cout << "  ✓ Request Multiplexing" << std::endl;
        std::cout << "  ✓ Server Push (simulated)" << std::endl;
        std::cout << "  ✓ Stream Prioritization" << std::endl;
        std::cout << "  ✓ Header Compression (HPACK)" << std::endl;
        std::cout << "  ✓ Flow Control" << std::endl;
        std::cout << "  ✓ SSL/TLS with ALPN" << std::endl;
        std::cout << "Static File Endpoints:" << std::endl;
        std::cout << "  GET  /                    - HTTP/2 Demo Page" << std::endl;
        std::cout << "  GET  /static/*            - Static Resources" << std::endl;
        std::cout << "API Endpoints:" << std::endl;
        std::cout << "  GET  /api/multiplexing-demo      - Multiplexing demonstration" << std::endl;
        std::cout << "  GET  /api/stream-priority/:level - Stream priority testing" << std::endl;
        std::cout << "  GET  /api/server-push-demo       - Server push simulation" << std::endl;
        std::cout << "  GET  /api/performance/:iterations - Performance testing" << std::endl;
        std::cout << "  GET  /api/data/:size             - Bulk data transfer" << std::endl;
        std::cout << "  POST /api/echo                   - Echo service" << std::endl;
        std::cout << "Browser Testing:" << std::endl;
        std::cout << "  Open: https://localhost:8443/" << std::endl;
        std::cout << "  Use browser dev tools to observe HTTP/2 features" << std::endl;
        std::cout << "CURL Examples:" << std::endl;
        std::cout << "  curl -k --http2 https://localhost:8443/api/multiplexing-demo" << std::endl;
        std::cout << "  curl -k --http2 https://localhost:8443/api/stream-priority/high" << std::endl;
        std::cout << "  curl -k --http2 -X POST -d '{\"test\":\"data\"}' https://localhost:8443/api/echo" << std::endl;
        std::cout << "==================================================" << std::endl;
    }

    void on(const qb::KillEvent& event) noexcept {
        std::cout << "HTTP/2 server shutting down..." << std::endl;
        qb::Actor::kill();
    }
};

int main() {
    // qb::io::log::setLevel(qb::io::log::Level::DEBUG);
    qb::Main engine;
    
    try {
        engine.addActor<Http2StaticServer>(0);
        engine.start(false);
        engine.join();
        
        if (engine.hasError()) {
            std::cerr << "Engine encountered an error" << std::endl;
            return 1;
        }
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
} 