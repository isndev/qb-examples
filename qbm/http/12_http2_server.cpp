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
#include <qb/io/system/file.h> // qb::io::sys::resolve_resource
#include <qb/system/parse.h>   // qb::to_number
#include <qbm/http/http.h>
#include <qbm/http/middleware/all.h>
#include <qbm/http/2/http2.h>
#include <iostream>
#include <filesystem>
#include <fstream>
#include <cstdlib>
#include <chrono>
#include <random>

using qb::json;

// Locate the directory that contains the HTTP/2 demo site. An explicit
// $HTTP2_STATIC_ROOT wins; otherwise the bundled resources/http2 is resolved next to the
// executable, so the example serves the demo from any working directory.
static std::filesystem::path
resolve_static_root() {
    if (const char *env = std::getenv("HTTP2_STATIC_ROOT"); env && *env)
        return std::filesystem::path(env);
    return qb::io::sys::resolve_resource("./resources/http2");
}

// Parse a decimal integer from an untrusted path/query param and require it to
// fall within [lo, hi]. Returns false (without throwing) on any non-numeric,
// out-of-range, or trailing-garbage input so handlers can answer 400 instead of
// letting std::stoi throw into the framework (which surfaced as a 500).
static bool
parse_bounded_int(std::string_view s, int lo, int hi, int &out) {
    // qb::to_number is STRICT: the whole field must be one canonical integer
    // (no surrounding whitespace, no leading '+', no trailing garbage) and an
    // out-of-range magnitude yields nullopt — exactly the contract above, with
    // no throw to catch.
    const std::optional<long> v = qb::to_number<long>(s);
    if (!v || *v < lo || *v > hi)
        return false;
    out = static_cast<int>(*v);
    return true;
}

// Reply 400 Bad Request with a small JSON error body and complete the context.
template <typename Ctx>
static void
send_bad_request(Ctx &ctx, const char *message) {
    qb::json err;
    err["error"] = message;
    ctx->response().status() = qb::http::Status::BAD_REQUEST;
    ctx->response().add_header("Content-Type", "application/json");
    ctx->response().body() = err;
    ctx->complete();
}

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
    std::filesystem::path _static_root;
    std::filesystem::path _cert_file;
    std::filesystem::path _key_file;

public:
    explicit Http2StaticServer(std::filesystem::path static_root = "./resources/http2")
        : _static_root(std::move(static_root)), _cert_file("resources/ssl/cert.pem"), _key_file("resources/ssl/key.pem") {}

    qb::io::async::task<bool> onInit() override {
        std::cout << "HTTP/2 server actor created successfully" << std::endl;
        std::cout << "Initializing HTTP/2 Static File Server..." << std::endl;

        if (!setup_directories_and_ssl()) {
            co_return false;
        }

        setup_middleware();
        setup_routes();

        co_return setup_and_start_server();
    }

private:
    bool setup_directories_and_ssl() {
        // Create static directory if it doesn't exist
        if (!std::filesystem::exists(_static_root)) {
            std::filesystem::create_directories(_static_root);
            std::cout << "Created static directory: " << _static_root << std::endl;
        }
        
        // The self-signed dev certificate is bundled under resources/ssl next to the binary.
        // Resolve it relative to the executable so the server runs from any working directory.
        _cert_file = qb::io::sys::resolve_resource(_cert_file);
        _key_file  = qb::io::sys::resolve_resource(_key_file);
        if (!std::filesystem::exists(_cert_file) || !std::filesystem::exists(_key_file)) {
            std::cerr << "SSL certificate not found (" << _cert_file << "). Run the "
                         "example from its build output directory." << std::endl;
            return false;
        }
        std::cout << "Using SSL certificate: " << _cert_file << std::endl;

        return true;
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
            std::filesystem::path index_path = _static_root / "index.html";
            if (std::filesystem::exists(index_path)) {
                std::ifstream file(index_path);
                std::string content((std::istreambuf_iterator<char>(file)),
                                  std::istreambuf_iterator<char>());
                ctx->response().body() = std::move(content);
                ctx->response().add_header("Content-Type", "text/html; charset=utf-8");
                ctx->complete();
            } else {
                // Always set a Content-Type: with X-Content-Type-Options: nosniff
                // (from the security middleware) a typeless body makes the browser
                // download the response instead of displaying it.
                ctx->response().status() = qb::http::Status::NOT_FOUND;
                ctx->response().add_header("Content-Type", "text/plain; charset=utf-8");
                ctx->response().body() = "Index file not found (static root: " + _static_root.string() + ")";
                ctx->complete();
            }
        });

        // Favicon route
        router().get("/favicon.ico", [this](auto ctx) {
            std::filesystem::path favicon_path = _static_root / "favicon.ico";
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
            response["request_id"] = ctx->request().query_or("request", "unknown");
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
            
            std::string resource = ctx->request().query("resource");
            if (!resource.empty()) {
                response["pushed_resource"] = resource;
                response["size_bytes"] = resource.length() * 100;
            }
            
            ctx->response().body() = response;
            ctx->response().add_header("Content-Type", "application/json");
            ctx->complete();
        });

        api_group->get("/performance/:iterations", [](auto ctx) {
            // path/query params are attacker-controlled: a non-numeric or huge value
            // must yield 400, never an uncaught std::stoi exception (which became a 500).
            int iterations = 0, current = 1;
            if (!parse_bounded_int(ctx->path_param("iterations"), 1, 1'000'000, iterations) ||
                !parse_bounded_int(ctx->request().query_or("iteration", "1"), 1, 1'000'000, current)) {
                send_bad_request(ctx, "iterations and iteration must be integers in [1, 1000000]");
                return;
            }

            qb::json response;
            response["feature"] = "Performance Testing";
            response["total_iterations"] = iterations;
            response["current_iteration"] = current;
            response["timestamp"] = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            response["latency_ms"] = rand() % 50 + 10;
            response["throughput_rps"] = 1000 + rand() % 500;

            ctx->response().body() = response;
            ctx->response().add_header("Content-Type", "application/json");
            ctx->complete();
        });

        api_group->get("/data/:size", [](auto ctx) {
            // Bound the size to avoid both an uncaught std::stoi throw and an
            // unbounded allocation (e.g. /api/data/999999999).
            int size_kb = 0;
            if (!parse_bounded_int(ctx->path_param("size"), 0, 1024, size_kb)) {
                send_bad_request(ctx, "size must be an integer in [0, 1024] (KB)");
                return;
            }

            std::string data(static_cast<size_t>(size_kb) * 1024, 'A');

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
            } catch (const std::exception&) {
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
        engine.addActor<Http2StaticServer>(0, resolve_static_root());
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