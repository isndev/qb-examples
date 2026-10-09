/**
 * @file examples/06-modules/http/08-static-files.cpp
 * @tier 06-modules
 * @teaches Serving a directory: the shipped static-files middleware, compression and security
 *          headers over it, and an upload endpoint beside it.
 * @demonstrates qb::http::static_files_middleware<S>, qb::http::StaticFilesOptions,
 *               qb::http::CompressionMiddleware<S>, qb::http::SecurityHeadersMiddleware<S>,
 *               qb::http::CorsMiddleware<S>, qb::http::LoggingMiddleware<S>, router(), use, group,
 *               get, compile, listen
 * @prerequisites 06-modules/http/05-rest-api-json
 * @expect "Starting Static File Server..."
 * @expect "Static File Server running on http://localhost:8080"
 * @brief Static file server with dynamic API using QB HTTP StaticFilesMiddleware
 *
 * This example demonstrates:
 * - Serving static files (HTML, CSS, JS, images) using StaticFilesMiddleware
 * - Configuring MIME types, caching, and security options
 * - Directory browsing with custom listings
 * - File upload API with validation
 * - Combining static content with dynamic REST API
 * - Security features (path traversal protection, safe file types)
 * - Performance optimizations (ETag, conditional requests, compression)
 *
 * @author qb - C++ Actor Framework
 * @copyright Copyright (c) 2011-2025 qb - isndev (cpp.actor)
 * Licensed under the Apache License, Version 2.0 (http://www.apache.org/licenses/LICENSE-2.0)
 * @ingroup Examples
 */

#include <iostream>
#include <filesystem>
#include <fstream>
#include <cstdint>
#include <ctime>
#include <system_error>
#include <qb/main.h>
#include <qb/io/system/file.h> // qb::io::sys::resolve_resource
#include <qb/system/parse.h>   // qb::to_number (non-throwing string-to-number)
#include <qbm/http/http.h>
#include <qbm/http/middleware/static_files.h>
#include <qbm/http/middleware/cors.h>
#include <qbm/http/middleware/compression.h>
#include <qbm/http/middleware/logging.h>
#include <qbm/http/middleware/security_headers.h>
#include <qbm/http/headers.h> // Pour parse_header_attributes
#include "upload_write.h"

// File metadata structure
struct FileMetadata {
    std::string                     filename;
    std::string                     path;
    std::string                     mime_type;
    size_t                          size;
    std::filesystem::file_time_type last_modified;
    std::string                     description;
    std::vector<std::string>        tags;

    qb::json
    to_json() const {
        auto last_modified_time_t = std::chrono::duration_cast<std::chrono::seconds>(
                                        last_modified.time_since_epoch() - std::filesystem::file_time_type::clock::now().time_since_epoch()
                                        + std::chrono::system_clock::now().time_since_epoch())
                                        .count();

        return qb::json{
            {"filename", filename},       {"path", path}, {"mime_type", mime_type}, {"size", size}, {"last_modified", last_modified_time_t},
            {"description", description}, {"tags", tags}
        };
    }

    static FileMetadata
    from_json(const qb::json &j) {
        FileMetadata meta;
        meta.filename    = j.value("filename", "");
        meta.path        = j.value("path", "");
        meta.mime_type   = j.value("mime_type", "");
        meta.size        = j.value("size", 0);
        meta.description = j.value("description", "");
        meta.tags        = j.value("tags", std::vector<std::string>{});
        return meta;
    }
};

class StaticFileServer
    : public qb::Actor
    , public qb::http::Server<> {
private:
    std::filesystem::path                        _static_root;
    std::filesystem::path                        _upload_dir;
    qb::unordered_map<std::string, FileMetadata> _file_metadata;
    std::uint64_t                                _next_upload_id = 0;

public:
    explicit StaticFileServer(std::filesystem::path static_root = "./resources/static", std::filesystem::path upload_dir = "./uploads")
        // Resolve the static root next to the executable so the example serves its bundled
        // assets from any working directory (the upload dir stays relative to the cwd).
        : _static_root(qb::io::sys::resolve_resource(static_root))
        , _upload_dir(std::move(upload_dir)) {}

    qb::io::async::task<bool>
    onInit() override {
        // Shutdown wiring. Event dispatch is by SUBSCRIPTION, not by vtable: qb::Actor's
        // constructor already subscribed its own default handlers for these two, and
        // re-registering here is what replaces them with OURS. Without these two lines
        // the handlers below compile, are never called, and their cleanup is lost.
        registerEvent<qb::KillEvent>(*this);
        registerEvent<qb::SignalEvent>(*this);

        std::cout << "Starting Static File Server..." << std::endl;

        // Create necessary directories
        create_directories();

        // Create sample static files
        create_sample_files();

        // Setup middleware and routes in correct order
        setup_standard_middleware();    // Add logging, timing, etc. first
        setup_static_file_middleware(); // Then add static files middleware
        setup_api_routes();             // Finally add API routes

        // Compile the router
        router().compile();

        // Start listening. `listen()` returns false when the bind fails (port already in
        // use, permission denied, ...). Discarding it starts an actor that serves nothing
        // and never exits — fail the init instead, which the engine reports via hasError().
        if (!listen({"tcp://0.0.0.0:8080"})) {
            std::cerr << "Failed to start listening server" << std::endl;
            co_return false;
        }
        start();

        std::cout << "Static File Server running on http://localhost:8080" << std::endl;
        std::cout << "Static files served from: " << _static_root << std::endl;
        std::cout << "Upload directory: " << _upload_dir << std::endl;

        print_available_endpoints();

        co_return true;
    }

private:
    void
    create_directories() {
        // Create static files directory structure
        std::filesystem::create_directories(_static_root);

        // Create upload directory
        std::filesystem::create_directories(_upload_dir);

        std::cout << "Created directories:\n";
        std::cout << "  Static files: " << _static_root << "\n";
        std::cout << "  Uploads: " << _upload_dir << "\n";
    }

    void
    create_sample_files() {
        // Check if static files exist, if not copy from resources or create minimal ones
        if (!std::filesystem::exists(_static_root / "index.html")) {
            std::cout << "Static files not found. Creating minimal samples...\n";

            // Create a minimal index.html if static resources are not available
            std::ofstream index(_static_root / "index.html");
            index << R"(<!DOCTYPE html>
<html><head><title>QB HTTP Server</title></head>
<body><h1>QB HTTP Static File Server</h1>
<p>Resources not found. Please ensure resources are copied correctly.</p>
<p><a href="/api/files">View API</a></p></body></html>)";
        } else {
            std::cout << "Using static resources from: " << _static_root << "\n";
        }

        // Always ensure we have the data files for the API demonstrations
        if (!std::filesystem::exists(_static_root / "data.json")) {
            std::cout << "Creating sample data.json...\n";
            qb::json data = {
                {"message", "QB HTTP Framework Sample Data"},
                {"version", "2.0.0"},
                {"features", {"static_files", "file_upload", "json_api"}},
                {"timestamp", std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count()}
            };

            std::ofstream json_file(_static_root / "data.json");
            json_file << data.dump(2);
        }
    }

    void
    setup_standard_middleware() {
        // 1. CORS middleware for development
        auto cors_middleware = qb::http::CorsMiddleware<qb::http::DefaultSession>::dev();
        router().use(cors_middleware);

        // 2. Security headers
        auto security_options    = qb::http::SecurityHeadersOptions::secure_defaults();
        auto security_middleware = std::make_shared<qb::http::SecurityHeadersMiddleware<qb::http::DefaultSession>>(security_options);
        router().use(security_middleware);

        // 3. Compression middleware
        auto compression_options =
            qb::http::CompressionOptions().compress_responses(true).min_size_to_compress(512).preferred_encodings({"gzip", "deflate"});
        auto compression_middleware = std::make_shared<qb::http::CompressionMiddleware<qb::http::DefaultSession>>();
        compression_middleware->update_options(compression_options);
        router().use(compression_middleware);

        // 4. Logging middleware
        auto logging_middleware = std::make_shared<qb::http::LoggingMiddleware<qb::http::DefaultSession>>(
            [](qb::http::LogLevel level, const std::string &message) {
                std::string level_str;
                switch (level) {
                    case qb::http::LogLevel::Debug:
                        level_str = "DEBUG";
                        break;
                    case qb::http::LogLevel::Info:
                        level_str = "INFO";
                        break;
                    case qb::http::LogLevel::Warning:
                        level_str = "WARNING";
                        break;
                    case qb::http::LogLevel::Error:
                        level_str = "ERROR";
                        break;
                }
                std::cout << "[" << level_str << "] " << message << std::endl;
            },
            qb::http::LogLevel::Info, qb::http::LogLevel::Debug);
        router().use(logging_middleware);
    }

    void
    setup_static_file_middleware() {
        // Configure static files middleware with real QB HTTP options
        qb::http::StaticFilesOptions options(_static_root);
        options
            .with_path_prefix_to_strip("/static") // Strip /static prefix to serve from root dir
            .with_directory_listing(true)
            .with_etags(true)
            .with_last_modified(true)
            .with_range_requests(true)
            .with_cache_control(true, "public, max-age=3600");

        auto static_middleware = qb::http::static_files_middleware<qb::http::DefaultSession>(std::move(options));

        // Add static files middleware - it will only handle /static/* paths due to path_prefix
        router().use(static_middleware);

        // Also serve uploaded files under /uploads prefix
        qb::http::StaticFilesOptions upload_options(_upload_dir);
        upload_options.with_path_prefix_to_strip("/uploads")
            .with_serve_index_file(false) // Don't serve index.html for uploads
            .with_directory_listing(true)
            .with_etags(true)
            .with_last_modified(true)
            .with_range_requests(true)
            .with_cache_control(true, "public, max-age=1800"); // Shorter cache for uploads

        auto upload_middleware = qb::http::static_files_middleware<qb::http::DefaultSession>(std::move(upload_options));
        router().use(upload_middleware);
    }

    void
    setup_api_routes() {
        auto api_group = router().group("/api");

        // List all files in upload directory
        api_group->get("/files", [this](auto ctx) { handle_list_files(ctx); });

        // Get file metadata
        api_group->get("/files/:filename", [this](auto ctx) { handle_get_file_metadata(ctx); });

        // Upload file endpoint
        api_group->post("/upload", [this](auto ctx) { handle_file_upload(ctx); });

        // Delete uploaded file
        api_group->del("/files/:filename", [this](auto ctx) { handle_delete_file(ctx); });

        // Update file metadata
        api_group->put("/files/:filename/metadata", [this](auto ctx) { handle_update_metadata(ctx); });

        // Directory browsing API
        router().get("/browse", [this](auto ctx) { handle_browse_directory(ctx); });

        router().get("/browse/*path", [this](auto ctx) { handle_browse_directory(ctx); });
    }

    void
    handle_list_files(std::shared_ptr<qb::http::Context<qb::http::DefaultSession>> ctx) {
        try {
            qb::json file_list = qb::json::array();

            if (std::filesystem::exists(_upload_dir)) {
                for (const auto &entry : std::filesystem::directory_iterator(_upload_dir)) {
                    if (entry.is_regular_file()) {
                        std::string filename = entry.path().filename().string();

                        qb::json file_info = {
                            {"filename", filename}, {"size", std::filesystem::file_size(entry)}, {"path", "/uploads/" + filename}
                        };

                        // Add metadata if available
                        auto meta_it = _file_metadata.find(filename);
                        if (meta_it != _file_metadata.end()) {
                            file_info["metadata"] = meta_it->second.to_json();
                        }

                        file_list.push_back(file_info);
                    }
                }
            }

            qb::json response = {{"files", file_list}, {"count", file_list.size()}, {"upload_directory", _upload_dir.string()}};

            ctx->response().status() = qb::http::Status::OK;
            ctx->response().add_header("Content-Type", "application/json");
            ctx->response().body() = response;
            ctx->complete();

        } catch (const std::exception &e) {
            ctx->response().status() = qb::http::Status::INTERNAL_SERVER_ERROR;
            ctx->response().add_header("Content-Type", "application/json");
            ctx->response().body() = qb::json{{"error", "Internal server error"}, {"message", e.what()}};
            ctx->complete();
        }
    }

    void
    handle_get_file_metadata(std::shared_ptr<qb::http::Context<qb::http::DefaultSession>> ctx) {
        std::string           filename = ctx->path_param("filename");
        std::filesystem::path filepath = _upload_dir / filename;

        if (!std::filesystem::exists(filepath)) {
            ctx->response().status() = qb::http::Status::NOT_FOUND;
            ctx->response().add_header("Content-Type", "application/json");
            ctx->response().body() = qb::json{{"error", "File not found"}, {"filename", filename}};
            ctx->complete();
            return;
        }

        auto meta_it = _file_metadata.find(filename);
        if (meta_it != _file_metadata.end()) {
            ctx->response().status() = qb::http::Status::OK;
            ctx->response().add_header("Content-Type", "application/json");
            ctx->response().body() = meta_it->second.to_json();
        } else {
            ctx->response().status() = qb::http::Status::NOT_FOUND;
            ctx->response().add_header("Content-Type", "application/json");
            ctx->response().body() = qb::json{{"error", "Metadata not found"}, {"filename", filename}};
        }
        ctx->complete();
    }

    void
    handle_file_upload(std::shared_ptr<qb::http::Context<qb::http::DefaultSession>> ctx) {
        try {
            // Check content length first (optional but good practice)
            constexpr size_t MAX_FILE_SIZE      = 50 * 1024 * 1024; // 50MB limit
            std::string      content_length_str = ctx->request().header("Content-Length");
            if (!content_length_str.empty()) {
                // Content-Length is untrusted client input; parse without throwing.
                // A malformed header yields 0, which skips this early check — the
                // authoritative size enforcement happens after parsing (below).
                size_t content_length = qb::to_number<size_t>(content_length_str).value_or(0);
                if (content_length > MAX_FILE_SIZE) {
                    ctx->response().status() = qb::http::Status::PAYLOAD_TOO_LARGE;
                    ctx->response().add_header("Content-Type", "application/json");
                    ctx->response().body() =
                        qb::json{{"error", "File too large"}, {"message", "Maximum file size is 50MB"}, {"max_size", MAX_FILE_SIZE}};
                    ctx->complete();
                    return;
                }
            }

            // Parse multipart form data
            auto multipart = ctx->request().body().as<qb::http::Multipart>();

            std::string uploaded_filename;
            std::string description;
            std::string file_content;
            std::string content_type = "application/octet-stream";

            // Process each part
            for (const auto &part : multipart.parts()) {
                // Get Content-Disposition header to identify the field
                std::string disposition = part.header("Content-Disposition");
                auto        attrs       = qb::http::parse_header_attributes(disposition);

                if (attrs.has("name")) {
                    std::string field_name = attrs.at("name");

                    if (field_name == "file" && attrs.has("filename")) {
                        // This is the file upload field
                        uploaded_filename = attrs.at("filename");
                        file_content      = part.body;
                        if (part.has_header("Content-Type")) {
                            content_type = part.header("Content-Type");
                        }
                    } else if (field_name == "description") {
                        // This is the description field
                        description = part.body;
                    }
                }
            }

            if (uploaded_filename.empty() || file_content.empty()) {
                ctx->response().status() = qb::http::Status::BAD_REQUEST;
                ctx->response().add_header("Content-Type", "application/json");
                ctx->response().body() =
                    qb::json{{"error", "No file uploaded or empty file"}, {"details", "Make sure to include a 'file' field with a valid file"}};
                ctx->complete();
                return;
            }

            // A stored name must remain one path component on every platform.
            if (uploaded_filename.find_first_of("/\\") != std::string::npos || uploaded_filename.find('\0') != std::string::npos
                || uploaded_filename == "." || uploaded_filename == "..") {
                ctx->response().status() = qb::http::Status::BAD_REQUEST;
                ctx->response().add_header("Content-Type", "application/json");
                ctx->response().body() = qb::json{{"error", "Invalid filename"}};
                ctx->complete();
                return;
            }

            // Additional size check after parsing
            if (file_content.size() > MAX_FILE_SIZE) {
                ctx->response().status() = qb::http::Status::PAYLOAD_TOO_LARGE;
                ctx->response().add_header("Content-Type", "application/json");
                ctx->response().body() = qb::json{
                    {"error", "File too large"},
                    {"message", "Maximum file size is 50MB"},
                    {"actual_size", file_content.size()},
                    {"max_size", MAX_FILE_SIZE}
                };
                ctx->complete();
                return;
            }

            // One actor handles these requests in order. Include a sequence and skip
            // names already on disk so two same-name uploads (or a restart within one
            // second) cannot truncate an earlier file.
            const std::string     prefix = "upload_" + std::to_string(std::time(nullptr)) + "_";
            std::string           safe_filename;
            std::filesystem::path filepath;
            do {
                safe_filename = prefix + std::to_string(_next_upload_id++) + "_" + uploaded_filename;
                filepath      = _upload_dir / safe_filename;
            } while (std::filesystem::exists(filepath));

            // Write file to disk
            std::ofstream outfile(filepath, std::ios::binary);
            if (!outfile.is_open()) {
                ctx->response().status() = qb::http::Status::INTERNAL_SERVER_ERROR;
                ctx->response().add_header("Content-Type", "application/json");
                ctx->response().body() = qb::json{{"error", "Failed to save file"}, {"message", "Could not create file on server"}};
                ctx->complete();
                return;
            }

            const bool write_ok = write_upload_contents(outfile, file_content);
            outfile.close();

            std::error_code file_error;
            const auto      stored_size = std::filesystem::file_size(filepath, file_error);
            if (!write_ok || !outfile || file_error || stored_size != file_content.size()) {
                std::error_code cleanup_error;
                std::filesystem::remove(filepath, cleanup_error);
                ctx->response().status() = qb::http::Status::INTERNAL_SERVER_ERROR;
                ctx->response().add_header("Content-Type", "application/json");
                ctx->response().body() = qb::json{{"error", "Failed to save file"}, {"message", "File write was incomplete"}};
                ctx->complete();
                return;
            }

            // Store metadata
            FileMetadata metadata;
            metadata.filename      = safe_filename;
            metadata.path          = filepath.string();
            metadata.mime_type     = content_type;
            metadata.size          = file_content.size();
            metadata.last_modified = std::filesystem::last_write_time(filepath);
            metadata.description   = description.empty() ? "Uploaded via API" : description;
            metadata.tags          = {"uploaded", "api"};

            _file_metadata[safe_filename] = metadata;

            qb::json response = {
                {"success", true},
                {"filename", safe_filename},
                {"original_filename", uploaded_filename},
                {"size", metadata.size},
                {"content_type", content_type},
                {"description", metadata.description},
                {"path", "/uploads/" + safe_filename},
                {"message", "File uploaded successfully"}
            };

            ctx->response().status() = qb::http::Status::CREATED;
            ctx->response().add_header("Content-Type", "application/json");
            ctx->response().add_header("Location", "/api/files/" + safe_filename);
            ctx->response().body() = response;
            ctx->complete();

        } catch (const std::exception &e) {
            ctx->response().status() = qb::http::Status::BAD_REQUEST;
            ctx->response().add_header("Content-Type", "application/json");
            ctx->response().body() = qb::json{
                {"error", "Failed to parse multipart data"},
                {"message", e.what()},
                {"details", "Make sure Content-Type is multipart/form-data with valid boundary"}
            };
            ctx->complete();
        }
    }

    void
    handle_delete_file(std::shared_ptr<qb::http::Context<qb::http::DefaultSession>> ctx) {
        std::string           filename = ctx->path_param("filename");
        std::filesystem::path filepath = _upload_dir / filename;

        try {
            if (!std::filesystem::exists(filepath)) {
                ctx->response().status() = qb::http::Status::NOT_FOUND;
                ctx->response().add_header("Content-Type", "application/json");
                ctx->response().body() = qb::json{{"error", "File not found"}, {"filename", filename}};
                ctx->complete();
                return;
            }

            std::filesystem::remove(filepath);
            _file_metadata.erase(filename);

            ctx->response().status() = qb::http::Status::OK;
            ctx->response().add_header("Content-Type", "application/json");
            ctx->response().body() = qb::json{{"success", true}, {"message", "File deleted successfully"}, {"filename", filename}};
            ctx->complete();

        } catch (const std::exception &e) {
            ctx->response().status() = qb::http::Status::INTERNAL_SERVER_ERROR;
            ctx->response().add_header("Content-Type", "application/json");
            ctx->response().body() = qb::json{{"error", "Delete failed"}, {"message", e.what()}};
            ctx->complete();
        }
    }

    void
    handle_update_metadata(std::shared_ptr<qb::http::Context<qb::http::DefaultSession>> ctx) {
        std::string filename = ctx->path_param("filename");

        try {
            auto body_json = ctx->request().body().as<qb::json>();

            auto it = _file_metadata.find(filename);
            if (it == _file_metadata.end()) {
                ctx->response().status() = qb::http::Status::NOT_FOUND;
                ctx->response().add_header("Content-Type", "application/json");
                ctx->response().body() = qb::json{{"error", "File metadata not found"}, {"filename", filename}};
                ctx->complete();
                return;
            }

            // Stage every conversion before changing the stored metadata.
            FileMetadata updated = it->second;
            if (body_json.contains("description")) {
                updated.description = body_json["description"];
            }
            if (body_json.contains("tags")) {
                updated.tags = body_json["tags"];
            }

            qb::json response = {{"success", true}, {"message", "Metadata updated successfully"}, {"metadata", updated.to_json()}};
            ctx->response().add_header("Content-Type", "application/json");
            ctx->response().body()   = std::move(response);
            it->second               = std::move(updated);
            ctx->response().status() = qb::http::Status::OK;
            ctx->complete();

        } catch (const std::exception &e) {
            ctx->response().status() = qb::http::Status::BAD_REQUEST;
            ctx->response().add_header("Content-Type", "application/json");
            ctx->response().body() = qb::json{{"error", "Invalid JSON"}, {"message", e.what()}};
            ctx->complete();
        }
    }

    void
    handle_browse_directory(std::shared_ptr<qb::http::Context<qb::http::DefaultSession>> ctx) {
        std::string path_param = ctx->path_param("path");

        try {
            const auto root        = std::filesystem::canonical(_static_root);
            const auto requested   = std::filesystem::path(path_param);
            const auto browse_path = std::filesystem::weakly_canonical(root / requested);

            // The router has already percent-decoded path_param. Compare path
            // components after resolving symlinks, rather than string prefixes
            // ("static-other" must not count as a child of "static").
            auto root_part = root.begin();
            auto path_part = browse_path.begin();
            while (root_part != root.end() && path_part != browse_path.end() && *root_part == *path_part) {
                ++root_part;
                ++path_part;
            }
            if (requested.is_absolute() || root_part != root.end()) {
                ctx->response().status() = qb::http::Status::FORBIDDEN;
                ctx->response().add_header("Content-Type", "application/json");
                ctx->response().body() = qb::json{{"error", "Path outside static root"}};
                ctx->complete();
                return;
            }

            if (!std::filesystem::exists(browse_path) || !std::filesystem::is_directory(browse_path)) {
                ctx->response().status() = qb::http::Status::NOT_FOUND;
                ctx->response().add_header("Content-Type", "application/json");
                ctx->response().body() = qb::json{{"error", "Directory not found"}, {"path", path_param}};
                ctx->complete();
                return;
            }

            qb::json entries = qb::json::array();

            for (const auto &entry : std::filesystem::directory_iterator(browse_path)) {
                qb::json entry_info = {{"name", entry.path().filename().string()}, {"type", entry.is_directory() ? "directory" : "file"}};

                if (entry.is_regular_file()) {
                    entry_info["size"] = std::filesystem::file_size(entry);
                }

                entries.push_back(entry_info);
            }

            qb::json response = {{"path", path_param.empty() ? "/" : "/" + path_param}, {"entries", entries}, {"count", entries.size()}};

            ctx->response().status() = qb::http::Status::OK;
            ctx->response().add_header("Content-Type", "application/json");
            ctx->response().body() = response;
            ctx->complete();

        } catch (const std::exception &e) {
            ctx->response().status() = qb::http::Status::INTERNAL_SERVER_ERROR;
            ctx->response().add_header("Content-Type", "application/json");
            ctx->response().body() = qb::json{{"error", "Browse failed"}, {"message", e.what()}};
            ctx->complete();
        }
    }

    void
    print_available_endpoints() {
        std::cout << "\n=== QB HTTP Static File Server ===\n";
        std::cout << "Server running at: http://localhost:8080\n\n";

        std::cout << "Static File Endpoints:\n";
        std::cout << "  GET  /static/           - Main page (index.html)\n";
        std::cout << "  GET  /static/about.html - About page\n";
        std::cout << "  GET  /static/sample.txt - Sample text file\n";
        std::cout << "  GET  /static/data.json  - JSON data file\n";
        std::cout << "  GET  /static/styles.css - CSS stylesheet\n";
        std::cout << "  GET  /static/main.js    - JavaScript file\n\n";

        std::cout << "Upload Directory:\n";
        std::cout << "  GET  /uploads/          - Browse uploaded files\n";
        std::cout << "  GET  /uploads/<file>    - Download specific uploaded file\n\n";

        std::cout << "API Endpoints:\n";
        std::cout << "  GET  /api/files         - List all uploaded files (JSON)\n";
        std::cout << "  GET  /api/files/:name   - Get file metadata (JSON)\n";
        std::cout << "  POST /api/upload        - Upload a new file (multipart/form-data)\n";
        std::cout << "  PUT  /api/files/:name/metadata - Update file metadata\n";
        std::cout << "  DEL  /api/files/:name   - Delete uploaded file\n\n";

        std::cout << "Directory Browsing:\n";
        std::cout << "  GET  /browse            - List static root (JSON)\n";
        std::cout << "  GET  /browse/*path      - List a static subdirectory (JSON)\n\n";

        std::cout << "Features demonstrated:\n";
        std::cout << "  • Static file serving with path prefixes\n";
        std::cout << "  • Real multipart/form-data file upload parsing\n";
        std::cout << "  • Interactive HTML upload form with JavaScript\n";
        std::cout << "  • File size validation (50MB limit)\n";
        std::cout << "  • MIME type detection and custom headers\n";
        std::cout << "  • Directory listing and browsing\n";
        std::cout << "  • ETag and conditional requests\n";
        std::cout << "  • Range requests (partial content)\n";
        std::cout << "  • File metadata management API\n";
        std::cout << "  • Security protections (path traversal)\n";
        std::cout << "  • Proper caching headers\n\n";

        std::cout << "Examples:\n";
        std::cout << "  Open in browser: http://localhost:8080/static/\n";
        std::cout << "  List files API:  curl http://localhost:8080/api/files\n";
        std::cout << "  Upload file:     curl -X POST -F \"file=@yourfile.txt\" \\\n";
        std::cout << "                        -F \"description=My file description\" \\\n";
        std::cout << "                        http://localhost:8080/api/upload\n";
        std::cout << "  Browse uploads:  http://localhost:8080/uploads/\n\n";
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
        std::cout << "Static File Server shutting down..." << std::endl;
        this->kill();
    }
};

int
main() {
    qb::Main engine;

    // Add the static file server actor
    engine.addActor<StaticFileServer>(0);

    // Start the engine
    engine.start();
    engine.join();

    if (engine.hasError()) {
        std::cerr << "Engine error occurred" << std::endl;
        return 1;
    }

    return 0;
}
