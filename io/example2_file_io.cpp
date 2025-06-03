/**
 * @file examples/io/example2_file_io.cpp
 * @example Comprehensive File I/O Operations with QB-IO and POSIX
 *
 * @brief This example demonstrates a variety of file input/output operations,
 * primarily utilizing `qb::io::system::file` for basic read/write tasks,
 * and supplementing with standard C++/POSIX calls for memory-mapped files
 * and detailed file statistics.
 *
 * @details
 * The example is structured around a `FileOperationsManager` class that performs several demonstrations:
 * 1.  **Writing Binary File**:
 *     -   Generates a buffer of random binary data.
 *     -   Writes this data to a test file using `qb::io::system::file::open()` (with `O_WRONLY | O_CREAT | O_TRUNC`)
 *         and `qb::io::system::file::write()`.
 *     -   Measures and prints write throughput.
 * 2.  **Reading Binary File**:
 *     -   Reads the content of the previously written binary file using `qb::io::system::file::open()` (with `O_RDONLY`)
 *         and `qb::io::system::file::read()`.
 *     -   Measures and prints read throughput.
 *     -   Displays a sample of the read data.
 * 3.  **Memory-Mapped I/O**:
 *     -   Creates a new file and sets its size using POSIX `open()` and `ftruncate()`.
 *     -   Maps the file into memory using POSIX `mmap()`.
 *     -   Writes a pattern directly to the memory-mapped region.
 *     -   Ensures data is flushed to disk using POSIX `msync()`.
 *     -   Unmaps the file using POSIX `munmap()`.
 *     -   This section illustrates high-performance I/O via memory mapping, using standard OS facilities.
 * 4.  **File Copy**:
 *     -   Copies a source file to a destination file by reading from the source and writing to the
 *         destination in chunks, using `qb::io::system::file` for both.
 *     -   Measures and prints copy throughput.
 * 5.  **File Information**:
 *     -   Retrieves and displays metadata for a file (size, permissions, last modified time)
 *         using POSIX `stat()`.
 *
 * The `main` function orchestrates these demonstrations and handles the creation and cleanup
 * of a test directory and files. This example primarily showcases synchronous file operations.
 *
 * QB-IO Features Demonstrated:
 * - Synchronous File Operations: `qb::io::system::file` for `open()`, `read()`, `write()`, and `close()`.
 * - Thread-Safe Output: `qb::io::cout()` and `qb::io::cerr()`.
 *
 * Other POSIX/Standard C++ Features Shown:
 * - Memory-Mapped Files: `mmap`, `munmap`, `msync`, `ftruncate` (POSIX).
 * - File Statistics: `stat` (POSIX).
 * - Filesystem Operations: `std::filesystem` for directory creation and existence checks.
 */

#include <qb/io.h>
#include <qb/io/async.h>
#include <qb/io/system/file.h>
#include <filesystem>
#include <iostream>
#include <iomanip>
#include <sys/mman.h>

namespace fs = std::filesystem;

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
        //qb::io::cout() << std::endl;
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
            auto written = file.write(reinterpret_cast<const char*>(data.data()), data.size());
            file.close();
            
            auto end_time = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
            
            if (written == static_cast<int>(data.size())) {
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
            auto bytes_read = file.read(buffer.data(), buffer.size());
            file.close();
            
            auto end_time = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
            
            if (bytes_read == static_cast<int>(file_size)) {
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