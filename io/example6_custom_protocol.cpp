/**
 * @file example6_custom_protocol.cpp
 * @brief Example implementing a custom protocol using qb-io
 *
 * This example demonstrates how to build a custom protocol on top of qb-io:
 * - Protocol message format definition
 * - Protocol serialization/deserialization
 * - Client-server communication using the protocol
 * - Asynchronous message handling
 */

#include <iostream>
#include <chrono>
#include <string>
#include <atomic>
#include <thread>
#include <map>
#include <mutex>
#include <condition_variable>
#include <cstring>
#include <vector>
#include <functional>

#include <qb/io/async.h>
#include <qb/system/allocator/pipe.h>

// ═══════════════════════════════════════════════════════════════════
// Custom Protocol Definition
// ═══════════════════════════════════════════════════════════════════

/**
 * Custom message protocol:
 * 
 * Format:
 * - Magic (2 bytes): 'QP' (0x5150)
 * - Version (1 byte): 0x01
 * - Type (1 byte): Message type
 * - ID (4 bytes): Message ID
 * - Length (4 bytes): Length of payload
 * - Payload: Variable-length data
 */

namespace qb_protocol {
    // Protocol constants
    constexpr uint16_t MAGIC = 0x5150;      // 'QP' in ASCII
    constexpr uint8_t VERSION = 0x01;       // Protocol version 1
    
    // Message types
    enum class MessageType : uint8_t {
        HELLO = 0x01,
        ECHO_REQUEST = 0x02,
        ECHO_REPLY = 0x03,
        ACK = 0x04,
        ERROR = 0xFF
    };
    
    // Message header (12 bytes)
    #pragma pack(push, 1)
    struct MessageHeader {
        uint16_t magic;
        uint8_t version;
        uint8_t type;
        uint32_t id;
        uint32_t length;
    };
    #pragma pack(pop)
}

// Shared state for synchronization
std::atomic<bool> server_ready{false};
std::atomic<bool> client_connected{false};
std::atomic<bool> server_running{true};

// ═══════════════════════════════════════════════════════════════════
// Custom Protocol Implementation
// ═══════════════════════════════════════════════════════════════════

namespace qb {

// Message structure for our protocol
struct custom_message {
    qb_protocol::MessageType type;
    uint32_t id;
    std::string payload;
};

} // namespace qb

// Add serialization specialization
namespace qb::allocator {

// Template specialization for pipe<char>::put
template <>
pipe<char>& pipe<char>::put<qb::custom_message>(const qb::custom_message& msg) {
    // Calculate header size
    constexpr size_t HEADER_SIZE = sizeof(qb_protocol::MessageHeader);
    
    // Create and fill header structure
    qb_protocol::MessageHeader header;
    header.magic = qb_protocol::MAGIC;
    header.version = qb_protocol::VERSION;
    header.type = static_cast<uint8_t>(msg.type);
    header.id = msg.id;
    header.length = static_cast<uint32_t>(msg.payload.size());
    
    // Write header to buffer
    this->put(reinterpret_cast<const char*>(&header), HEADER_SIZE);
    
    // Write payload if not empty
    if (!msg.payload.empty()) {
        this->put(msg.payload.data(), msg.payload.size());
    }
    
    return *this;
}

} // namespace qb::allocator

// ═══════════════════════════════════════════════════════════════════
// Custom Protocol Implementation
// ═══════════════════════════════════════════════════════════════════

namespace qb {

// Implementation of our custom protocol
template <typename IO_>
class custom_protocol : public io::async::AProtocol<IO_> {
private:
    static constexpr size_t HEADER_SIZE = sizeof(qb_protocol::MessageHeader);
    bool _reading_header = true;
    qb_protocol::MessageHeader _header{};
    std::vector<char> _payload;
    
public:
    using message = custom_message;
    
    // Required constructor
    custom_protocol() = delete;
    
    explicit custom_protocol(IO_ &io) noexcept
        : io::async::AProtocol<IO_>(io) {}
        
    // Determine if we have a complete message and its size
    std::size_t getMessageSize() noexcept override {
        auto& buffer = this->_io.in();
        
        // Check if buffer contains any data
        if (buffer.empty()) {
            return 0;
        }
        
        // First, try to read the header
        if (_reading_header) {
            if (buffer.size() < HEADER_SIZE) {
                return 0; // Not enough data for header
            }
            
            // Copy header from buffer
            std::memcpy(&_header, buffer.cbegin(), HEADER_SIZE);
            
            // Debug print received header
            std::cout << "DEBUG: Received header - Magic: 0x" << std::hex << _header.magic 
                      << " Version: 0x" << static_cast<int>(_header.version)
                      << " Type: 0x" << static_cast<int>(_header.type)
                      << " ID: " << std::dec << _header.id
                      << " Length: " << _header.length << std::endl;
            
            // Validate magic and version
            if (_header.magic != qb_protocol::MAGIC || _header.version != qb_protocol::VERSION) {
                std::cerr << "Invalid header magic or version - Received Magic: 0x" << std::hex << _header.magic 
                          << " Version: 0x" << static_cast<int>(_header.version) << std::dec << std::endl;
                reset();
                return 0;
            }
            
            // Header is valid, prepare for payload
            _reading_header = false;
            
            // Verify payload length to prevent buffer overflow
            if (_header.length > 1024 * 1024) { // 1MB safety limit
                std::cerr << "Payload size too large: " << _header.length << " bytes" << std::endl;
                reset();
                return 0;
            }
            
            // Resize payload buffer to expected size
            _payload.resize(_header.length);
            
            // Return expected total message size
            return HEADER_SIZE + _header.length;
        }
        
        // We're already reading a payload, check if we have the full message
        if (buffer.size() < HEADER_SIZE + _header.length) {
            return 0; // Not enough data yet
        }
        
        // We have a complete message
        return HEADER_SIZE + _header.length;
    }
    
    // Process a complete message
    void onMessage(std::size_t size) noexcept override {
        auto& buffer = this->_io.in();
        
        // Final safety check
        if (buffer.size() < HEADER_SIZE + _header.length) {
            std::cerr << "Buffer too small in onMessage: " << buffer.size() 
                     << " vs expected " << (HEADER_SIZE + _header.length) << std::endl;
            reset();
            return;
        }
        
        // Copy payload from buffer
        if (_header.length > 0) {
            std::cout << "DEBUG: Processing message with payload size " << _header.length << std::endl;
            if (_payload.size() != _header.length) {
                // Ensure payload buffer is sized correctly
                _payload.resize(_header.length);
            }
            std::memcpy(_payload.data(), buffer.cbegin() + HEADER_SIZE, _header.length);
        }
        
        // Create the message from the data
        message msg;
        msg.type = static_cast<qb_protocol::MessageType>(_header.type);
        msg.id = _header.id;
        
        if (_header.length > 0) {
            msg.payload.assign(_payload.data(), _header.length);
        }
        
        std::cout << "DEBUG: Creating message object - Type: " << static_cast<int>(msg.type) 
                  << " ID: " << msg.id << " Payload: '" << msg.payload << "'" << std::endl;
        
        // NOTE: Do not free the buffer here! The framework will automatically free 
        // the amount of bytes returned by getMessageSize() after this function returns
        
        // Reset for next message
        _reading_header = true;
        
        // Notify the handler
        this->_io.on(std::move(msg));
        
        std::cout << "DEBUG: Message handler called successfully" << std::endl;
    }
    
    // Reset protocol state
    void reset() noexcept override {
        _reading_header = true;
        _payload.clear();
    }
};

} // namespace qb

// ═══════════════════════════════════════════════════════════════════
// Server Implementation
// ═══════════════════════════════════════════════════════════════════

// Forward declaration
class EchoServer;

// Server client handler
class EchoServerClient : public qb::io::use<EchoServerClient>::tcp::client<EchoServer> {
public:
    using Protocol = qb::custom_protocol<EchoServerClient>;
    
    // Constructor
    explicit EchoServerClient(IOServer& server)
        : client(server) {
        // Register protocol handler for incoming messages
        this->template switch_protocol<Protocol>(*this);
        std::cout << "New client connected to server" << std::endl;
        client_connected = true;
    }
    
    ~EchoServerClient() {
        std::cout << "Client disconnected from server" << std::endl;
        client_connected = false;
    }
    
    // Handle incoming message
    void on(Protocol::message&& msg) {
        std::cout << "Server received message, type: " 
                 << static_cast<int>(msg.type)
                 << ", ID: " << msg.id 
                 << ", payload: " << msg.payload << std::endl;
        
        switch (msg.type) {
            case qb_protocol::MessageType::HELLO: {
                Protocol::message ack{qb_protocol::MessageType::ACK, msg.id, ""};
                *this << ack;
                std::cout << "DEBUG: After sending ACK for HELLO message" << std::endl;
                break;
            }
            case qb_protocol::MessageType::ECHO_REQUEST: {
                Protocol::message reply{qb_protocol::MessageType::ECHO_REPLY, msg.id, msg.payload};
                *this << reply;
                break;
            }
            default:
                std::cerr << "Unknown message type: " << static_cast<int>(msg.type) << std::endl;
                break;
        }
    }
    
    // Handle disconnection event
    void on(qb::io::async::event::disconnected&) {
        std::cout << "Server client handler disconnected" << std::endl;
    }
};

// Echo server
class EchoServer : public qb::io::use<EchoServer>::tcp::server<EchoServerClient> {
private:
    std::atomic<size_t> _connections{0};

public:
    EchoServer() {
        std::cout << "EchoServer initialized" << std::endl;
    }
    
    // Handle new connection
    void on(IOSession& session) {
        std::cout << "New connection from client " << ++_connections << std::endl;
    }
    
    // Destructor
    ~EchoServer() {
        std::cout << "Server shutting down, handled " 
                 << _connections << " connection(s)" << std::endl;
    }
};

// ═══════════════════════════════════════════════════════════════════
// Client Implementation
// ═══════════════════════════════════════════════════════════════════

// Echo client
class EchoClient : public qb::io::use<EchoClient>::tcp::client<> {
private:
    std::atomic<uint32_t> _next_id{1};
    std::mutex _mutex;
    struct ResponseCallback {
        std::function<void(const qb::custom_message&)> callback;
        std::chrono::steady_clock::time_point expiry;
        bool active = true;
    };
    std::map<uint32_t, ResponseCallback> _callbacks;
    std::atomic<bool> _connected{false};
    std::atomic<bool> _running{true};
    std::thread _input_thread;
    
public:
    using Protocol = qb::custom_protocol<EchoClient>;
    
    // Constructor
    EchoClient() {
        std::cout << "EchoClient initialized" << std::endl;
    }
    
    // Destructor - ensure thread is stopped
    ~EchoClient() {
        _running = false;
        if (_input_thread.joinable()) {
            _input_thread.join();
        }
    }
    
    // Called when the client connects
    void start_connection() {
        // Register protocol handler for incoming messages - must be done before start()
        this->template switch_protocol<Protocol>(*this);
        
        // Start client processing
        this->start();
    }
    
    // Handle incoming message
    void on(Protocol::message&& msg) {
        std::cout << "Client received message, type: " 
                 << static_cast<int>(msg.type)
                 << ", ID: " << msg.id 
                 << ", payload: " << msg.payload << std::endl;
        
        // First message from server means we're connected
        if (!_connected && msg.type == qb_protocol::MessageType::ACK) {
            std::cout << "Connection established with server" << std::endl;
            _connected = true;
        }
        
        // Call registered callback for this message ID
        {
            std::lock_guard<std::mutex> lock(_mutex);
            auto it = _callbacks.find(msg.id);
            if (it != _callbacks.end() && it->second.active) {
                // Execute callback with the message
                it->second.callback(msg);
                // Remove the callback after processing
                _callbacks.erase(it);
            }
        }
    }
    
    // Check for expired callbacks
    void checkTimeouts() {
        auto now = std::chrono::steady_clock::now();
        std::lock_guard<std::mutex> lock(_mutex);
        
        for (auto it = _callbacks.begin(); it != _callbacks.end();) {
            if (it->second.active && now > it->second.expiry) {
                std::cerr << "Timeout for message ID: " << it->first << std::endl;
                // Create a timeout message to pass to the callback
                qb::custom_message timeout_msg;
                timeout_msg.type = qb_protocol::MessageType::ERROR;
                timeout_msg.id = it->first;
                timeout_msg.payload = "Timeout";
                
                // Call the callback with the timeout message
                if (it->second.callback) {
                    it->second.callback(timeout_msg);
                }
                
                // Remove the callback
                it = _callbacks.erase(it);
            } else {
                ++it;
            }
        }
    }
    
    // Handle disconnection event
    void on(qb::io::async::event::disconnected&) {
        std::cout << "Client disconnected from server" << std::endl;
        _connected = false;
        _running = false;
    }
    
    bool isConnected() const {
        return _connected;
    }
    
    bool isRunning() const {
        return _running;
    }
    
    // Send a hello message
    void sendHello(std::function<void(bool)> on_connected = nullptr) {
        uint32_t msg_id = _next_id++;
        Protocol::message hello{qb_protocol::MessageType::HELLO, msg_id, "Client connecting"};
        
        std::cout << "DEBUG: Sending HELLO message, ID: " << msg_id << std::endl;
        
        // Register callback for ACK response
        if (on_connected) {
            std::lock_guard<std::mutex> lock(_mutex);
            _callbacks[msg_id] = {
                [on_connected](const qb::custom_message& msg) {
                    if (msg.type == qb_protocol::MessageType::ACK) {
                        on_connected(true);
                    } else {
                        on_connected(false);
                    }
                },
                std::chrono::steady_clock::now() + std::chrono::seconds(5),
                true
            };
        }
        
        // Send the request
        *this << hello;
        std::cout << "Client sent HELLO, ID: " << msg_id << std::endl;
    }
    
    // Send an echo request asynchronously
    void sendEchoRequest(const std::string& data, 
                         std::function<void(bool success, const std::string& response)> callback,
                         int timeout_ms = 5000) {
        if (!_connected) {
            std::cerr << "Cannot send echo request: not connected" << std::endl;
            if (callback) callback(false, "");
            return;
        }
        
        uint32_t msg_id = _next_id++;
        Protocol::message request{qb_protocol::MessageType::ECHO_REQUEST, msg_id, data};
        
        std::cout << "DEBUG: Sending ECHO_REQUEST message, ID: " << msg_id 
                  << ", Payload: '" << data << "'" << std::endl;
        
        // Register callback for response with timeout
        {
            std::lock_guard<std::mutex> lock(_mutex);
            _callbacks[msg_id] = {
                [callback](const qb::custom_message& msg) {
                    if (msg.type == qb_protocol::MessageType::ECHO_REPLY) {
                        std::cout << "DEBUG: Received successful response" << std::endl;
                        if (callback) callback(true, msg.payload);
                    } else {
                        // Timeout or error
                        std::cerr << "Echo request failed or timed out" << std::endl;
                        if (callback) callback(false, "");
                    }
                },
                std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms),
                true
            };
        }
        
        // Send the request
        *this << request;
        std::cout << "Client sent ECHO_REQUEST, ID: " << msg_id << std::endl;
    }
    
    // Start interactive mode in a separate thread
    void startInteractiveMode() {
        _input_thread = std::thread([this]() {
            // Main client loop for user input
            std::string line;
            std::cout << "Connected to server. Type message to echo (or 'quit' to exit)" << std::endl;
            
            while (_running && _connected) {
                std::cout << "> ";
                std::getline(std::cin, line);
                
                if (!_running || !_connected) break;
                
                if (line == "quit") {
                    _running = false;
                    break;
                }
                
                if (!line.empty()) {
                    sendEchoRequest(line, [](bool success, const std::string& response) {
                        if (success) {
                            std::cout << "Server response: " << response << std::endl;
                        } else {
                            std::cerr << "Echo request failed" << std::endl;
                        }
                    });
                }
            }
            
            std::cout << "Input thread exiting" << std::endl;
            _running = false;
        });
    }
};

// ═══════════════════════════════════════════════════════════════════
// Main Application
// ═══════════════════════════════════════════════════════════════════

void displayHelp() {
    std::cout << "Usage:\n"
              << "  server <port>        - Run as server\n"
              << "  client <host> <port> - Run as client\n";
}

// Run server in a thread
void runServer(uint16_t port) {
    std::cout << "Starting echo server on port " << port << std::endl;
    
    // Initialize async environment
    qb::io::async::init();
    
    // Create server
    EchoServer server;
    
    // Bind to port and check result
    auto status = server.transport().listen_v4(port);
    if (status != 0) {
        std::cerr << "Failed to listen on port " << port << " (error: " << status << ")" << std::endl;
        return;
    }
    
    std::cout << "Server bound to port " << port << " successfully" << std::endl;
    
    // Start the server
    server.start();
    server_ready = true;
    std::cout << "Server running. Press Enter to stop." << std::endl;
    
    // Process events until Enter is pressed
    std::thread input_thread([]() {
        std::cin.get();
        server_running = false;
    });
    
    // Run event loop until server is stopped
    while (server_running) {
        qb::io::async::run(EVRUN_NOWAIT);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    // Cleanup
    std::cout << "Stopping server..." << std::endl;
    input_thread.join();
}

// Run client with interactive input
void runClient(const std::string& host, uint16_t port) {
    std::cout << "Starting echo client connecting to " << host << ":" << port << std::endl;
    
    // Initialize async environment
    qb::io::async::init();
    
    // Create client
    EchoClient client;
    
    // Connect to server
    auto status = client.transport().connect_v4(host.c_str(), port);
    if (status != 0) {
        std::cerr << "Failed to connect to server (error: " << status << ")" << std::endl;
        return;
    }
    
    // Initialize client protocol and start
    client.start_connection();
    std::cout << "Connected to server" << std::endl;
    
    // Send initial hello message to establish connection
    client.sendHello([&client](bool success) {
        if (success) {
            std::cout << "Connection protocol completed successfully" << std::endl;
            // Start interactive mode in separate thread
            client.startInteractiveMode();
        } else {
            std::cerr << "Failed to complete connection protocol" << std::endl;
        }
    });
    
    // Main event loop - keep running until client disconnects
    while (client.isRunning()) {
        // Process IO events
        qb::io::async::run(EVRUN_ONCE);
        
        // Check for timeouts in callbacks
        client.checkTimeouts();
        
        // Small sleep to avoid busy waiting
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    std::cout << "Client shutting down" << std::endl;
}

int main(int argc, char* argv[]) {
    // Default port
    constexpr uint16_t DEFAULT_PORT = 9876;
    
    if (argc < 2) {
        displayHelp();
        return 1;
    }
    
    std::string mode = argv[1];
    
    if (mode == "server") {
        // Run as server
        uint16_t port = DEFAULT_PORT;
        if (argc > 2) {
            port = static_cast<uint16_t>(std::stoi(argv[2]));
        }
        runServer(port);
    } 
    else if (mode == "client") {
        // Run as client
        if (argc < 4) {
            std::cerr << "Missing host and/or port for client mode" << std::endl;
            displayHelp();
            return 1;
        }
        
        std::string host = argv[2];
        uint16_t port = static_cast<uint16_t>(std::stoi(argv[3]));
        runClient(host, port);
    }
    else {
        std::cerr << "Invalid mode: " << mode << std::endl;
        displayHelp();
        return 1;
    }
    
    return 0;
} 