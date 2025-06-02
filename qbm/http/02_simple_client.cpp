/**
 * @file examples/qbm/http/02_simple_client.cpp
 * @brief Simple HTTP/1.1 client example using QB Actor system
 *
 * This example demonstrates:
 * - Creating an HTTP client actor using the QB Actor framework
 * - Making asynchronous HTTP requests using qb::http::GET, qb::http::POST
 * - Handling responses with callbacks
 * - Different HTTP methods with proper actor lifecycle
 *
 * @author qb - C++ Actor Framework
 * @copyright Copyright (c) 2011-2025 qb - isndev (cpp.actor)
 * Licensed under the Apache License, Version 2.0 (http://www.apache.org/licenses/LICENSE-2.0)
 * @ingroup Examples
 */

#include <iostream>
#include <qb/main.h>
#include <http/http.h>

// HTTP Client Actor that makes various requests
class HttpClientActor : public qb::Actor {
private:
    int _request_count = 0;
    
public:
    HttpClientActor() = default;
    
    bool onInit() override {
        std::cout << "Initializing HTTP Client Actor..." << std::endl;
        
        // Start making requests after a short delay
        qb::io::async::callback([this]() {
            if (this->is_alive()) {
                make_requests();
            }
        }, 1.0); // Wait 1 second before starting
        
        return true;
    }
    
private:
    void make_requests() {
        std::cout << "Starting HTTP requests..." << std::endl;
        std::cout << "=========================" << std::endl;
        
        // Example 1: Simple GET request
        make_get_request();
    }
    
    void make_get_request() {
        std::cout << "Making GET request to httpbin.org..." << std::endl;
        
        // Create request
        qb::http::Request request(qb::io::uri("http://httpbin.org/get?param1=value1&param2=value2"));
        request.add_header("User-Agent", "QB-HTTP-Client/1.0");
        request.add_header("Accept", "application/json");
        
        // Make async request
        qb::http::GET(std::move(request), [this](qb::http::async::Reply&& reply) {
            if (!this->is_alive()) return;
            
            ++_request_count;
            
            if (reply.response.status() == qb::http::Status::OK) {
                std::cout << "GET Response received:" << std::endl;
                std::cout << "   Status: " << reply.response.status().code() << " " << reply.response.status().str() << std::endl;
                std::cout << "   Content-Type: " << reply.response.header("Content-Type") << std::endl;
                std::cout << "   Body size: " << reply.response.body().size() << " bytes" << std::endl;
                if (reply.response.body().size() < 500) {
                    auto body_str = reply.response.body().as<std::string>();
                    std::cout << "   Body: " << body_str.substr(0, 200) << "..." << std::endl;
                }
            } else {
                std::cout << "GET request failed with status: " << reply.response.status().code() << std::endl;
            }
            
            // Continue with POST request
            make_post_request();
        }, 10.0); // 10 second timeout
    }
    
    void make_post_request() {
        std::cout << "\nMaking POST request to httpbin.org..." << std::endl;
        
        // Create request with JSON body
        qb::http::Request request(qb::http::Method::POST, qb::io::uri("http://httpbin.org/post"));
        request.add_header("User-Agent", "QB-HTTP-Client/1.0");
        request.add_header("Content-Type", "application/json");
        request.add_header("Accept", "application/json");
        
        // JSON body
        qb::json json_data = {
            {"message", "Hello from QB HTTP Client!"},
            {"framework", "qb-http"},
            {"timestamp", std::time(nullptr)},
            {"request_id", _request_count + 1}
        };
        request.body() = json_data;
        
        // Make async request
        qb::http::POST(std::move(request), [this](qb::http::async::Reply&& reply) {
            if (!this->is_alive()) return;
            
            ++_request_count;
            
            if (reply.response.status() == qb::http::Status::OK) {
                std::cout << "POST Response received:" << std::endl;
                std::cout << "   Status: " << reply.response.status().code() << " " << reply.response.status().str() << std::endl;
                std::cout << "   Content-Type: " << reply.response.header("Content-Type") << std::endl;
                std::cout << "   Body size: " << reply.response.body().size() << " bytes" << std::endl;
            } else {
                std::cout << "POST request failed with status: " << reply.response.status().code() << std::endl;
            }
            
            // Continue with headers request
            make_headers_request();
        }, 10.0);
    }
    
    void make_headers_request() {
        std::cout << "\nMaking request to test custom headers..." << std::endl;
        
        qb::http::Request request(qb::io::uri("http://httpbin.org/headers"));
        request.add_header("User-Agent", "QB-HTTP-Client/1.0");
        request.add_header("X-Custom-Header", "QB-Framework-Test");
        request.add_header("X-Request-ID", std::to_string(_request_count + 1));
        
        qb::http::GET(std::move(request), [this](qb::http::async::Reply&& reply) {
            if (!this->is_alive()) return;
            
            ++_request_count;
            
            if (reply.response.status() == qb::http::Status::OK) {
                std::cout << "Headers Response received:" << std::endl;
                std::cout << "   Status: " << reply.response.status().code() << std::endl;
                std::cout << "   Server echoed our custom headers!" << std::endl;
            } else {
                std::cout << "Headers request failed with status: " << reply.response.status().code() << std::endl;
            }
            
            // Finish the demo
            finish_demo();
        }, 10.0);
    }
    
    void finish_demo() {
        std::cout << "\nHTTP Client demo completed!" << std::endl;
        std::cout << "Total requests made: " << _request_count << std::endl;
        std::cout << "Shutting down client actor..." << std::endl;
        
        // Schedule shutdown after a brief pause
        qb::io::async::callback([this]() {
            if (this->is_alive()) {
                qb::Main::stop();
            }
        }, 2.0);
    }
    
    void on(const qb::KillEvent& event) noexcept {
        std::cout << "HTTP Client Actor shutting down..." << std::endl;
        this->kill();
    }
};

int main() {
    try {
        std::cout << "QB HTTP Client Example" << std::endl;
        std::cout << "======================" << std::endl;
        
        // Initialize the QB Actor framework
        qb::Main engine;
        
        // Add our HTTP client actor to core 0
        auto client_id = engine.addActor<HttpClientActor>(0);
        
        if (!client_id.is_valid()) {
            std::cerr << "Failed to create client actor" << std::endl;
            return 1;
        }
        
        std::cout << "Client actor created with ID: " << client_id.sid() << std::endl;
        
        // Start the engine (blocks until stopped)
        engine.start();
        engine.join();
        
        std::cout << "Client demo finished" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Client error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
} 