/**
 * @file example2_file_io.cpp
 * @brief QB-IO File I/O Operations Example
 *
 * This example demonstrates efficient file I/O operations using QB-IO:
 * - Reading and writing files with proper error handling
 * - Memory-mapped file access for high-performance operations
 * - Working with file streams and buffers
 * - File system operations (creating directories, checking file existence)
 *
 * @author QB Framework Team
 * @copyright Apache-2.0 License
 */

#include <iostream>
#include <string>
#include <vector>
#include <fstream>
#include <filesystem>
#include <iomanip>

// Add necessary system headers
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>

#include <qb/actor.h>
#include <qb/main.h>
#include <qb/event.h>
#include <qb/io.h>
#include <qb/io/async.h>
#include <qb/io/system/file.h>
#include <chrono>
#include <thread>
#include <random>

namespace fs = std::filesystem;

// Message pour lire un fichier
struct ReadFileMsg : public qb::Event {
    std::string file_path;
    
    ReadFileMsg(const std::string& path) : file_path(path) {}
};

// Message pour écrire dans un fichier
struct WriteFileMsg : public qb::Event {
    std::string file_path;
    std::string content;
    
    WriteFileMsg(const std::string& path, const std::string& data) 
        : file_path(path), content(data) {}
};

// Message avec le contenu du fichier lu
struct FileContentMsg : public qb::Event {
    std::string file_path;
    std::string content;
    bool success;
    std::string error_message;
    
    FileContentMsg(const std::string& path, const std::string& data, 
                  bool ok, const std::string& error = "")
        : file_path(path), content(data), success(ok), error_message(error) {}
};

// Message de confirmation d'écriture de fichier
struct FileWrittenMsg : public qb::Event {
    std::string file_path;
    bool success;
    std::string error_message;
    
    FileWrittenMsg(const std::string& path, bool ok, const std::string& error = "")
        : file_path(path), success(ok), error_message(error) {}
};

// Message pour lister un répertoire
struct ListDirectoryMsg : public qb::Event {
    std::string directory_path;
    
    ListDirectoryMsg(const std::string& path) : directory_path(path) {}
};

// Message avec le contenu d'un répertoire
struct DirectoryContentMsg : public qb::Event {
    std::string directory_path;
    std::vector<std::string> entries;
    bool success;
    std::string error_message;
    
    DirectoryContentMsg(const std::string& path, const std::vector<std::string>& list,
                       bool ok, const std::string& error = "")
        : directory_path(path), entries(list), success(ok), error_message(error) {}
};

// Message de planification différée
struct DelayedActionMsg : public qb::Event {
    qb::ActorId target_id;
    int delay_ms;
    
    DelayedActionMsg(qb::ActorId id, int delay)
        : target_id(id), delay_ms(delay) {}
};

namespace {
    // Constants for the example
    constexpr const char* TEST_DIR = "qb_fileio_test";
    constexpr const char* TEST_FILE = "test_file.dat";
    constexpr const char* TEST_MMAP_FILE = "test_mmap.dat";
    constexpr const char* TEST_COPY_FILE = "test_copy.dat";
    constexpr size_t BUFFER_SIZE = 4096;
    constexpr size_t FILE_SIZE = 1024 * 1024; // 1MB
    
    // Helper function to print section headers
    void printSection(const std::string& title) {
        qb::io::cout() << "\n=== " << title << " ===\n";
    }
    
    // Helper function to print operation results
    void printResult(const std::string& operation, bool success, const std::string& details = "") {
        qb::io::cout() << operation << ": " << (success ? "Success" : "Failed");
        if (!details.empty()) {
            qb::io::cout() << " - " << details;
        }
        qb::io::cout() << std::endl;
    }
    
    // Utility function to get current timestamp
    std::string getCurrentTimestamp() {
        auto now = std::chrono::system_clock::now();
        auto now_c = std::chrono::system_clock::to_time_t(now);
        char buf[64];
        std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", std::localtime(&now_c));
        return std::string(buf);
    }
    
    // Create a directory with appropriate error handling
    bool createDirectory(const std::string& path) {
        try {
            if (std::filesystem::exists(path)) {
                qb::io::cout() << "Directory already exists: " << path << std::endl;
                return true;
            }
            return std::filesystem::create_directory(path);
        } catch (const std::filesystem::filesystem_error& e) {
            qb::io::cerr() << "Error creating directory: " << e.what() << std::endl;
            return false;
        }
    }
    
    // Get full path for a test file
    std::string getTestFilePath(const std::string& dir, const std::string& filename) {
        std::filesystem::path path(dir);
        path /= filename;
        return path.string();
    }
}

/**
 * @brief File Operations Manager
 * 
 * This class demonstrates various file operations using QB-IO
 */
class FileOperationsManager {
private:
    std::string _test_dir;
    
public:
    explicit FileOperationsManager(const std::string& test_dir) : _test_dir(test_dir) {
        // Ensure test directory exists
        if (!createDirectory(_test_dir)) {
            qb::io::cerr() << "Failed to create test directory: " << _test_dir << std::endl;
        }
    }
    
    ~FileOperationsManager() {
        // Clean up the test directory when done
        cleanupTestFiles();
    }
    
    /**
     * @brief Run a comprehensive demonstration of file operations
     */
    void runDemonstration() {
        printSection("Starting File Operations Demo");
        qb::io::cout() << "Test directory: " << _test_dir << std::endl;
        
        // Write a binary file with random data
        demonstrateWritingBinaryFile();
        
        // Read back the file contents
        demonstrateReadingBinaryFile();
        
        // Demonstrate memory-mapped file operations
        demonstrateMemoryMappedIO();
        
        // Demonstrate file copy operations
        demonstrateFileCopy();
        
        // Demonstrate file information
        demonstrateFileInfo();
        
        printSection("File Operations Demo Completed");
    }
    
private:
    /**
     * @brief Demonstrate writing a binary file using QB-IO
     * 
     * This method shows how to efficiently write binary data to a file
     */
    void demonstrateWritingBinaryFile() {
        printSection("Writing Binary File");
        
        // Generate random data for the file
        std::vector<uint8_t> data(FILE_SIZE);
        std::mt19937 rng(std::random_device{}());
        std::uniform_int_distribution<uint8_t> dist(0, 255);
        
        // Fill the data buffer with random bytes
        qb::io::cout() << "Generating " << FILE_SIZE << " bytes of random data..." << std::endl;
        for (auto& byte : data) {
            byte = dist(rng);
        }
        
        // Create the file path
        std::string file_path = getTestFilePath(_test_dir, TEST_FILE);
        qb::io::cout() << "Writing data to: " << file_path << std::endl;
        
        // Use QB-IO file API to write the file
        qb::io::sys::file file;
        auto start_time = std::chrono::high_resolution_clock::now();
        
        if (file.open(file_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644) >= 0) {
            // Write the data - Cast unsigned char* to char* to match the API
            ssize_t written = file.write(reinterpret_cast<const char*>(data.data()), data.size());
            file.close();
            
            auto end_time = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
            
            if (written == static_cast<ssize_t>(data.size())) {
                double throughput = (data.size() / 1024.0) / (duration.count() / 1000000.0);
                qb::io::cout() << "Successfully wrote " << written << " bytes in "
                          << duration.count() / 1000.0 << " ms" << std::endl;
                qb::io::cout() << "Write throughput: " << throughput << " KB/s" << std::endl;
            } else {
                qb::io::cerr() << "Failed to write all data. Only wrote " << written
                          << " of " << data.size() << " bytes" << std::endl;
            }
        } else {
            qb::io::cerr() << "Error opening file for writing: " << strerror(errno) << std::endl;
        }
    }
    
    /**
     * @brief Demonstrate reading a binary file using QB-IO
     * 
     * This method shows how to efficiently read binary data from a file
     */
    void demonstrateReadingBinaryFile() {
        printSection("Reading Binary File");
        
        // Create the file path
        std::string file_path = getTestFilePath(_test_dir, TEST_FILE);
        qb::io::cout() << "Reading data from: " << file_path << std::endl;
        
        // Use standard file operations for getting file size
        struct stat file_stat;
        if (stat(file_path.c_str(), &file_stat) != 0) {
            qb::io::cerr() << "Error getting file stats: " << strerror(errno) << std::endl;
                return;
            }
        size_t file_size = file_stat.st_size;
        
        // Use QB-IO file API to read the file
        qb::io::sys::file file;
        auto start_time = std::chrono::high_resolution_clock::now();
        
        if (file.open(file_path.c_str(), O_RDONLY) >= 0) {
            // Allocate buffer for file content
            std::vector<char> buffer(file_size);
            
            // Read the file
            ssize_t bytes_read = file.read(buffer.data(), buffer.size());
            file.close();
            
            auto end_time = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
            
            if (bytes_read == static_cast<ssize_t>(file_size)) {
                double throughput = (file_size / 1024.0) / (duration.count() / 1000000.0);
                qb::io::cout() << "Successfully read " << bytes_read << " bytes in "
                          << duration.count() / 1000.0 << " ms" << std::endl;
                qb::io::cout() << "Read throughput: " << throughput << " KB/s" << std::endl;
                
                // Display a sample of the data
                qb::io::cout() << "First 16 bytes: ";
                for (size_t i = 0; i < std::min<size_t>(16, buffer.size()); ++i) {
                    qb::io::cout() << std::hex << std::setw(2) << std::setfill('0')
                              << static_cast<int>(static_cast<unsigned char>(buffer[i])) << " ";
                }
                qb::io::cout() << std::dec << std::endl;
            } else {
                qb::io::cerr() << "Failed to read file. Read " << bytes_read
                          << " of " << file_size << " bytes" << std::endl;
            }
        } else {
            qb::io::cerr() << "Error opening file for reading: " << strerror(errno) << std::endl;
        }
    }
    
    /**
     * @brief Demonstrate memory-mapped file I/O using QB-IO
     * 
     * This method shows how to use memory mapping for very efficient I/O
     */
    void demonstrateMemoryMappedIO() {
        printSection("Memory-Mapped File I/O");
        
        // Create the file path
        std::string file_path = getTestFilePath(_test_dir, TEST_MMAP_FILE);
        qb::io::cout() << "Creating memory-mapped file: " << file_path << std::endl;
        
        // For memory mapping, we need to use standard POSIX file operations
        int fd = open(file_path.c_str(), O_RDWR | O_CREAT | O_TRUNC, 0644);
        if (fd < 0) {
            qb::io::cerr() << "Error opening file for memory mapping: " << strerror(errno) << std::endl;
            return;
        }
        
        // Set the file size for mapping
        size_t map_size = 1024 * 1024; // 1MB
        if (ftruncate(fd, map_size) != 0) {
            qb::io::cerr() << "Error setting file size: " << strerror(errno) << std::endl;
            close(fd);
            return;
        }
        
        // Map the file into memory
        void* mapped_region = mmap(nullptr, map_size, PROT_READ | PROT_WRITE, 
                               MAP_SHARED, fd, 0);
        
        if (mapped_region == MAP_FAILED) {
            qb::io::cerr() << "Memory mapping failed: " << strerror(errno) << std::endl;
            close(fd);
            return;
        }
        
        // Now we can work with the file as if it were memory
        qb::io::cout() << "File mapped to memory at address: " << mapped_region << std::endl;
        
        // Write data to the mapped region
        auto start_time = std::chrono::high_resolution_clock::now();
        uint8_t* data = static_cast<uint8_t*>(mapped_region);
        
        // Fill with a pattern (0 to 255 repeated)
        for (size_t i = 0; i < map_size; ++i) {
            data[i] = static_cast<uint8_t>(i % 256);
        }
        
        // Ensure data is flushed to disk
        if (msync(mapped_region, map_size, MS_SYNC) != 0) {
            qb::io::cerr() << "Error syncing mapped memory: " << strerror(errno) << std::endl;
        }
        
        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
        
        double throughput = (map_size / 1024.0) / (duration.count() / 1000000.0);
        qb::io::cout() << "Memory-mapped write completed in " << duration.count() / 1000.0
                  << " ms" << std::endl;
        qb::io::cout() << "Memory-mapped write throughput: " << throughput << " KB/s" << std::endl;
        
        // Read back a sample of the data
        qb::io::cout() << "First 16 bytes from memory-mapped file: ";
        for (size_t i = 0; i < 16; ++i) {
            qb::io::cout() << std::hex << std::setw(2) << std::setfill('0')
                      << static_cast<int>(data[i]) << " ";
        }
        qb::io::cout() << std::dec << std::endl;
        
        // Unmap the file
        if (munmap(mapped_region, map_size) != 0) {
            qb::io::cerr() << "Error unmapping file: " << strerror(errno) << std::endl;
        }
        
        // Close the file descriptor
        close(fd);
    }
    
    /**
     * @brief Demonstrate file copy operation
     */
    void demonstrateFileCopy() {
        printSection("File Copy Operation");
        
        std::string source_path = getTestFilePath(_test_dir, TEST_FILE);
        std::string dest_path = getTestFilePath(_test_dir, TEST_COPY_FILE);
        
        qb::io::cout() << "Copying " << source_path << " to " << dest_path << std::endl;
        
        // Get source file size using standard file operations
        struct stat file_stat;
        if (stat(source_path.c_str(), &file_stat) != 0) {
            qb::io::cerr() << "Error getting source file stats: " << strerror(errno) << std::endl;
            return;
        }
        size_t file_size = file_stat.st_size;
        
        qb::io::sys::file source_file;
        qb::io::sys::file dest_file;
        
        auto start_time = std::chrono::high_resolution_clock::now();
        
        if (source_file.open(source_path.c_str(), O_RDONLY) < 0) {
            qb::io::cerr() << "Error opening source file: " << strerror(errno) << std::endl;
            return;
        }
        
        if (dest_file.open(dest_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644) < 0) {
            qb::io::cerr() << "Error opening destination file: " << strerror(errno) << std::endl;
            source_file.close();
            return;
        }
        
        // Use a buffer for efficient copy
        std::vector<char> buffer(BUFFER_SIZE);
        size_t total_bytes_copied = 0;
        ssize_t bytes_read, bytes_written;
        
        while ((bytes_read = source_file.read(buffer.data(), buffer.size())) > 0) {
            bytes_written = dest_file.write(buffer.data(), bytes_read);
            if (bytes_written != bytes_read) {
                qb::io::cerr() << "Error writing to destination file: " << strerror(errno) << std::endl;
                source_file.close();
                dest_file.close();
                return;
            }
            total_bytes_copied += bytes_written;
        }
        
        source_file.close();
        dest_file.close();
        
        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
        
        if (total_bytes_copied == file_size) {
            double throughput = (file_size / 1024.0) / (duration.count() / 1000000.0);
            qb::io::cout() << "Successfully copied " << total_bytes_copied << " bytes in "
                      << duration.count() / 1000.0 << " ms" << std::endl;
            qb::io::cout() << "Copy throughput: " << throughput << " KB/s" << std::endl;
        } else {
            qb::io::cerr() << "Failed to copy entire file. Copied " << total_bytes_copied
                      << " of " << file_size << " bytes" << std::endl;
        }
    }
    
    /**
     * @brief Demonstrate file information retrieval
     */
    void demonstrateFileInfo() {
        printSection("File Information");
        
        std::string file_path = getTestFilePath(_test_dir, TEST_FILE);
        qb::io::cout() << "Getting information for: " << file_path << std::endl;
        
        // Use standard file operations to get file info
        struct stat file_stat;
        if (stat(file_path.c_str(), &file_stat) == 0) {
            qb::io::cout() << "File size: " << file_stat.st_size << " bytes" << std::endl;
            qb::io::cout() << "File permissions: " << std::oct << (file_stat.st_mode & 0777)
                      << std::dec << std::endl;
            
            auto mtime = std::chrono::system_clock::from_time_t(file_stat.st_mtime);
            auto mtime_c = std::chrono::system_clock::to_time_t(mtime);
            qb::io::cout() << "Last modified: " << std::ctime(&mtime_c);
            
            qb::io::cout() << "Is regular file: " << (S_ISREG(file_stat.st_mode) ? "Yes" : "No") << std::endl;
            qb::io::cout() << "Is directory: " << (S_ISDIR(file_stat.st_mode) ? "Yes" : "No") << std::endl;
        } else {
            qb::io::cerr() << "Error getting file info: " << strerror(errno) << std::endl;
        }
    }
    
    /**
     * @brief Clean up test files and directory
     */
    void cleanupTestFiles() {
        printSection("Cleaning Up");
        
        // List of files to clean up
        std::vector<std::string> files_to_clean = {
            getTestFilePath(_test_dir, TEST_FILE),
            getTestFilePath(_test_dir, TEST_MMAP_FILE),
            getTestFilePath(_test_dir, TEST_COPY_FILE)
        };
        
        // Remove each test file
        for (const auto& file : files_to_clean) {
            if (std::filesystem::exists(file)) {
                try {
                    std::filesystem::remove(file);
                    qb::io::cout() << "Removed: " << file << std::endl;
                } catch (const std::filesystem::filesystem_error& e) {
                    qb::io::cerr() << "Error removing file " << file << ": " << e.what() << std::endl;
                }
            }
        }
        
        // Remove the test directory
        try {
            if (std::filesystem::exists(_test_dir)) {
                std::filesystem::remove(_test_dir);
                qb::io::cout() << "Removed directory: " << _test_dir << std::endl;
            }
        } catch (const std::filesystem::filesystem_error& e) {
            qb::io::cerr() << "Error removing directory " << _test_dir << ": " << e.what() << std::endl;
        }
    }
};

int main() {
    printSection("QB-IO File I/O Operations Example");
    qb::io::cout() << "Starting at: " << getCurrentTimestamp() << std::endl;
    
    // Create and run the file operations manager
    FileOperationsManager file_ops(TEST_DIR);
    file_ops.runDemonstration();
    
    printSection("Example Completed");
    qb::io::cout() << "Finished at: " << getCurrentTimestamp() << std::endl;
    
    return 0;
} 