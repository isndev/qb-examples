/**
 * @file example3_tcp_networking.cpp
 * @brief QB-IO TCP Networking Example
 *
 * This example demonstrates TCP networking capabilities in QB-IO:
 * - Creating TCP servers and clients
 * - Implementing text-based protocols
 * - Handling multiple client connections
 * - Asynchronous communication between network components
 *
 * @author QB Framework Team
 * @copyright Apache-2.0 License
 */

#include <qb/actor.h>
#include <qb/main.h>
#include <qb/io/async.h>
#include <qb/io/tcp/socket.h>
#include <qb/io/tcp/listener.h>
#include <qb/io/protocol/text.h>
#include <iostream>
#include <chrono>
#include <thread>
#include <string>
#include <atomic>
#include <memory>
#include <ctime>
#include <mutex>
#include <iomanip>
#include <functional>
#include <condition_variable>

namespace {
    // Configuration constants
    constexpr unsigned short SERVER_PORT = 8888;
    constexpr const char* SERVER_HOST = "127.0.0.1";
    constexpr int CONNECTION_TIMEOUT_MS = 5000;
    constexpr int RESPONSE_TIMEOUT_MS = 1000;
    
    // Shared state
    std::atomic<int> g_message_count{0};
    std::atomic<bool> g_client_connected{false};
    std::atomic<bool> g_server_running{true};
    
    // Utility function to get the current timestamp string
    std::string getCurrentTimestamp() {
        auto now = std::chrono::system_clock::now();
        auto now_time_t = std::chrono::system_clock::to_time_t(now);
        std::stringstream ss;
        ss << std::put_time(std::localtime(&now_time_t), "%Y-%m-%d %H:%M:%S");
        return ss.str();
    }
    
    // Utility function to print section headers
    void printSection(const std::string& title) {
        std::cout << "\n=== " << title << " ===\n";
    }
    
    // Utility function to print info with component name
    void printInfo(const std::string& component, const std::string& message) {
        std::cout << component << ": " << message << std::endl;
    }
    
    // Utility function to print error with component name
    void printError(const std::string& component, const std::string& message) {
        std::cerr << component << " ERROR: " << message << std::endl;
    }
}

// Forward declarations
class TCPServer;
class TCPClient;

/**
 * @brief Client handler for the server-side
 * 
 * Handles client connections on the server side
 */
class ServerClientHandler : public qb::io::use<ServerClientHandler>::tcp::client<TCPServer> {
public:
    using Protocol = qb::protocol::text::command<ServerClientHandler>;
    
    explicit ServerClientHandler(IOServer& server)
        : client(server) {
        printInfo("ServerClientHandler", "New client connected");
        g_client_connected = true;
        
        // Send welcome message to the client
        *this << "Welcome to QB TCP Server!" << Protocol::end;
        *this << "Type 'help' for available commands." << Protocol::end;
    }
    
    ~ServerClientHandler() {
        printInfo("ServerClientHandler", "Client disconnected");
        g_client_connected = false;
    }
    
    /**
     * @brief Handle incoming message from client
     */
    void on(Protocol::message&& msg) {
        g_message_count++;
        printInfo("ServerClientHandler", "Received: " + msg.text);
        
        // Process commands
        processCommand(msg.text);
    }
    
private:
    /**
     * @brief Process client commands
     * 
     * @param command The command text from the client
     */
    void processCommand(const std::string& command) {
        if (command == "help") {
            handleHelpCommand();
        }
        else if (command == "time") {
            handleTimeCommand();
        }
        else if (command.substr(0, 5) == "echo ") {
            handleEchoCommand(command.substr(5));
        }
        else if (command == "stats") {
            handleStatsCommand();
        }
        else if (command == "quit") {
            handleQuitCommand();
        }
        else if (command == "shutdown") {
            handleShutdownCommand();
        }
        else {
            *this << "Unknown command. Type 'help' for available commands." << Protocol::end;
        }
    }
    
    void handleHelpCommand() {
        *this << "Available commands:" << Protocol::end;
        *this << "  help     - Show this help" << Protocol::end;
        *this << "  time     - Get current server time" << Protocol::end;
        *this << "  echo <text> - Echo back the provided text" << Protocol::end;
        *this << "  stats    - Show server statistics" << Protocol::end;
        *this << "  quit     - Disconnect from server" << Protocol::end;
        *this << "  shutdown - Shutdown the server" << Protocol::end;
    }
    
    void handleTimeCommand() {
        *this << "Server time: " << getCurrentTimestamp() << Protocol::end;
    }
    
    void handleEchoCommand(const std::string& text) {
        *this << "ECHO: " << text << Protocol::end;
    }
    
    void handleStatsCommand() {
        *this << "Server statistics:" << Protocol::end;
        *this << "  Messages processed: " << g_message_count.load() << Protocol::end;
        *this << "  Clients connected: " << (g_client_connected ? "1" : "0") << Protocol::end;
        *this << "  Server uptime: Since " << getCurrentTimestamp() << Protocol::end;
    }
    
    void handleQuitCommand() {
        *this << "Goodbye! Disconnecting..." << Protocol::end;
        // The client will close the connection after receiving this message
    }
    
    void handleShutdownCommand() {
        *this << "Server shutting down..." << Protocol::end;
        g_server_running = false;
    }
};

/**
 * @brief TCP Server implementation
 * 
 * Listens for client connections and creates handlers for them
 */
class TCPServer : public qb::io::use<TCPServer>::tcp::server<ServerClientHandler> {
private:
    std::atomic<int> _connection_count{0};
    
public:
    TCPServer() {
        printInfo("TCPServer", "Initialized");
    }
    
    ~TCPServer() {
        printInfo("TCPServer", "Server shutting down, handled " + 
                 std::to_string(_connection_count) + " connections");
    }
    
    /**
     * @brief Handler for new client sessions
     */
    void on(IOSession& session) {
        printInfo("TCPServer", "New connection from client " + 
                 std::to_string(++_connection_count));
    }
};

/**
 * @brief TCP Client implementation
 * 
 * Connects to TCP server and sends/receives messages
 */
class TCPClient : public qb::io::use<TCPClient>::tcp::client<> {
public:
    using Protocol = qb::protocol::text::command<TCPClient>;
    
    TCPClient() {
        printInfo("TCPClient", "Initialized");
    }
    
    /**
     * @brief Handler for received messages
     */
    void on(Protocol::message&& msg) {
        printInfo("TCPClient", "Received from server: " + msg.text);
        
        // Check for special server messages
        if (msg.text == "Goodbye! Disconnecting...") {
            printInfo("TCPClient", "Server said goodbye, disconnecting");
            disconnect();
        }
        else if (msg.text == "Server shutting down...") {
            printInfo("TCPClient", "Server is shutting down, disconnecting");
            disconnect();
            g_server_running = false;
        }
    }
    
    /**
     * @brief Send a command to the server
     */
    void sendCommand(const std::string& command) {
        printInfo("TCPClient", "Sending command: " + command);
        *this << command << Protocol::end;
    }
};

/**
 * @brief Run the TCP server in a separate thread
 */
void runServer() {
    printSection("Starting TCP Server");
    
    // Initialize async I/O system
    qb::io::async::init();
    
    // Create the server
    TCPServer server;
    
    // Configure and start the server
    printInfo("Server", "Listening on port " + std::to_string(SERVER_PORT));
    server.transport().listen_v4(SERVER_PORT);
    server.start();
    
    // Run the event loop until server_running is false
    while (g_server_running) {
        qb::io::async::run(EVRUN_NOWAIT);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    printInfo("Server", "Shutting down gracefully");
}

/**
 * @brief Run a TCP client in a separate thread
 */
void runClient() {
    // Wait a short time for the server to start
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    printSection("Starting TCP Client");
    
    // Initialize async I/O system
    qb::io::async::init();
    
    // Create the client
    TCPClient client;
    
    // Connect to the server
    printInfo("Client", "Connecting to server at " + std::string(SERVER_HOST) + ":" + 
             std::to_string(SERVER_PORT));
             
    auto status = client.transport().connect_v4(SERVER_HOST, SERVER_PORT);
    
    if (status != qb::io::SocketStatus::Done) {
        printError("Client", "Failed to connect to server");
        return;
    }
    
    // Start the client
    client.start();
    printInfo("Client", "Connected to server successfully");
    
    // Give time for the welcome message to arrive
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    qb::io::async::run(EVRUN_NOWAIT);
    
    // Send test commands
    std::vector<std::string> commands = {
        "help",
        "time",
        "echo Hello from QB-IO client!",
        "stats",
        "unknown_command",
        "quit"
    };
    
    for (const auto& cmd : commands) {
        // Send the command
        client.sendCommand(cmd);
        
        // Wait for and process the response
        for (int i = 0; i < 10; ++i) {
            qb::io::async::run(EVRUN_NOWAIT);
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }
    
    // Wait for the connection to close
    printInfo("Client", "Waiting for disconnection");
    for (int i = 0; i < 100 && g_server_running; ++i) {
        qb::io::async::run(EVRUN_NOWAIT);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    
    printInfo("Client", "Client session completed");
}

/**
 * @brief Main function
 */
int main() {
    printSection("QB-IO TCP Networking Example");
    std::cout << "Starting at: " << getCurrentTimestamp() << std::endl;
    
    // Start the server in a separate thread
    std::thread server_thread(runServer);
    
    // Start the client in a separate thread
    std::thread client_thread(runClient);
    
    // Wait for the client thread to finish
    client_thread.join();
    
    // If the server is still running, send a shutdown command
    if (g_server_running && g_client_connected) {
        printSection("Shutting Down Server");
        
        // Create a temporary client to send the shutdown command
        qb::io::async::init();
        TCPClient shutdown_client;
        
        if (shutdown_client.transport().connect_v4(SERVER_HOST, SERVER_PORT) == qb::io::SocketStatus::Done) {
            shutdown_client.start();
            shutdown_client.sendCommand("shutdown");
            
            // Process the command
            for (int i = 0; i < 10; ++i) {
                qb::io::async::run(EVRUN_NOWAIT);
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
            
            shutdown_client.disconnect();
        }
    }
    
    // Wait for the server thread to finish
    server_thread.join();
    
    printSection("Example Completed");
    std::cout << "Total messages processed: " << g_message_count << std::endl;
    std::cout << "Finished at: " << getCurrentTimestamp() << std::endl;
    
    return 0;
} 