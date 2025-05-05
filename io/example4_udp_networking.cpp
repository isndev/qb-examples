/**
 * @file example4_udp_networking.cpp
 * @brief Example showing UDP networking with qb::io::udp::socket class
 *
 * This example demonstrates how to use the qb::io::udp::socket class to send and receive
 * UDP datagrams, showing both client and server functionality.
 */

#include <chrono>
#include <iostream>
#include <thread>

#include <qb/io/udp/socket.h>

// Helper function to print endpoint information
void print_endpoint_info(const qb::io::endpoint& ep, const std::string& prefix) {
    std::cout << prefix << ep.to_string() << " (family: ";
    switch (ep.af()) {
        case AF_INET:
            std::cout << "IPv4";
            break;
        case AF_INET6:
            std::cout << "IPv6";
            break;
        default:
            std::cout << "Unknown";
    }
    std::cout << ")" << std::endl;
}

// Server function - receives UDP datagrams
void udp_server(uint16_t port) {
    qb::io::udp::socket server_socket;
    
    // Initialize the socket
    if (!server_socket.init(AF_INET)) {
        std::cerr << "Failed to initialize server socket" << std::endl;
        return;
    }
    
    // Bind to any local address on the specified port
    int result = server_socket.bind_v4(port);
    if (result != 0) {
        std::cerr << "Failed to bind server socket: " << result << std::endl;
        return;
    }
    
    // Print server information
    print_endpoint_info(server_socket.local_endpoint(), "Server listening on: ");
    
    // Set socket to non-blocking mode
    server_socket.set_nonblocking(true);
    
    std::cout << "UDP server started. Waiting for messages..." << std::endl;
    
    char buffer[qb::io::udp::socket::MaxDatagramSize];
    qb::io::endpoint client_endpoint;
    
    // Loop to receive datagrams
    for (int i = 0; i < 5; ++i) {
        // Try to receive a datagram
        int bytes_read = server_socket.read(buffer, sizeof(buffer), client_endpoint);
        
        if (bytes_read > 0) {
            // Ensure null termination for string data
            buffer[bytes_read] = '\0';
            
            // Print information about the received datagram
            print_endpoint_info(client_endpoint, "Received from: ");
            std::cout << "Data: " << buffer << " (" << bytes_read << " bytes)" << std::endl;
            
            // Send a response
            std::string response = "Hello from server!";
            int bytes_sent = server_socket.write(response.c_str(), response.size(), client_endpoint);
            
            if (bytes_sent > 0) {
                std::cout << "Sent response: " << response << " (" << bytes_sent << " bytes)" << std::endl;
            } else {
                std::cerr << "Failed to send response: " << bytes_sent << std::endl;
            }
        } else if (bytes_read < 0 && bytes_read != -EAGAIN && bytes_read != -EWOULDBLOCK) {
            std::cerr << "Error receiving datagram: " << bytes_read << std::endl;
        }
        
        // Sleep briefly to avoid busy-waiting
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    std::cout << "Server shutting down" << std::endl;
    server_socket.close();
}

// Client function - sends UDP datagrams
void udp_client(const std::string& server_host, uint16_t server_port) {
    qb::io::udp::socket client_socket;
    
    // Initialize the socket
    if (!client_socket.init(AF_INET)) {
        std::cerr << "Failed to initialize client socket" << std::endl;
        return;
    }
    
    // Create an endpoint for the server
    qb::io::endpoint server_endpoint(server_host.c_str(), server_port);
    if (!server_endpoint) {
        std::cerr << "Failed to create server endpoint" << std::endl;
        return;
    }
    
    print_endpoint_info(server_endpoint, "Sending to server: ");
    
    // Set socket to non-blocking mode
    client_socket.set_nonblocking(true);
    
    // Send a message to the server
    std::string message = "Hello from client!";
    int bytes_sent = client_socket.write(message.c_str(), message.size(), server_endpoint);
    
    if (bytes_sent > 0) {
        std::cout << "Sent: " << message << " (" << bytes_sent << " bytes)" << std::endl;
    } else {
        std::cerr << "Failed to send message: " << bytes_sent << std::endl;
        return;
    }
    
    // Wait for a response
    char buffer[qb::io::udp::socket::MaxDatagramSize];
    qb::io::endpoint response_endpoint;
    
    // Try to receive the response several times
    for (int i = 0; i < 10; ++i) {
        int bytes_read = client_socket.read(buffer, sizeof(buffer), response_endpoint);
        
        if (bytes_read > 0) {
            // Ensure null termination for string data
            buffer[bytes_read] = '\0';
            
            // Print information about the received response
            print_endpoint_info(response_endpoint, "Response from: ");
            std::cout << "Data: " << buffer << " (" << bytes_read << " bytes)" << std::endl;
            break;
        } else if (bytes_read < 0 && bytes_read != -EAGAIN && bytes_read != -EWOULDBLOCK) {
            std::cerr << "Error receiving response: " << bytes_read << std::endl;
            break;
        }
        
        // Sleep briefly to avoid busy-waiting
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    client_socket.close();
}

int main() {
    const uint16_t port = 12345;
    
    // Start the server in a separate thread
    std::thread server_thread(udp_server, port);
    
    // Give the server time to start
    std::this_thread::sleep_for(std::chrono::seconds(1));
    
    // Run the client
    udp_client("127.0.0.1", port);
    
    // Wait for the server to finish
    server_thread.join();
    
    return 0;
} 