#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <map>
#include <chrono>
#include <thread>
#include <mutex>
#include <condition_variable>
// #include <filesystem> // <-- Removed
#include <atomic>
#include <cstring> // <-- For strlen, strerror
#include <ctime>
#include <csignal>
#include <memory>
#include <optional>

// Use namespace to simplify code
// Add these includes at the top
#include <sys/stat.h> // <-- For stat, mkdir, chmod
#include <dirent.h>   // <-- For opendir, readdir, closedir
#include <unistd.h>   // <-- For access, remove, rename, rmdir, umask
#include <cerrno>     // <-- For errno

// Remove or modify the filesystem namespace
// namespace fs = std::filesystem;  // Remove this line

// Log level definitions
enum LogLevel {
    LOG_ERROR = 1,
    LOG_WARN = 2,
    LOG_INFO = 3,
    LOG_DEBUG = 4
};

// High-performance, low-power logging system
class Logger {
private:
    // Use atomic variables to reduce lock contention
    std::atomic<bool> running{true};
    std::atomic<bool> low_power_mode{false};
    std::atomic<unsigned int> max_idle_time{30000}; // ms
    std::atomic<size_t> buffer_max_size{8192};      // bytes
    std::atomic<size_t> log_size_limit{102400};     // bytes
    std::atomic<int> log_level{LOG_INFO};           // default level

    // Use shared pointers to manage resources (log_dir doesn't need a shared pointer)
    std::string log_dir;

    // Mutex and condition variable
    std::mutex log_mutex;
    std::condition_variable cv;

    // File cache
    struct LogFile {
        std::ofstream stream;
        std::chrono::steady_clock::time_point last_access;
        size_t current_size{0};
    };
    std::map<std::string, std::unique_ptr<LogFile>> log_files;

    // Optimized buffer - use pre-allocated memory
    struct LogBuffer {
        std::string content;
        size_t size{0};
        std::chrono::steady_clock::time_point last_write;

        LogBuffer() {
            // Pre-allocate memory to reduce reallocations
            content.reserve(16384); // Initial pre-allocation of 16KB
        }
    };
    std::map<std::string, std::unique_ptr<LogBuffer>> log_buffers;

    // Thread control
    std::unique_ptr<std::thread> flush_thread;

    // Time formatting cache - reduce time formatting overhead
    char time_buffer[32];
    std::chrono::system_clock::time_point last_time_format;
    std::mutex time_mutex;

public:
    Logger(const std::string& dir, int level = LOG_INFO, size_t size_limit = 102400)
        : log_size_limit(size_limit)
        , log_level(level)
        , log_dir(dir) {

        // Create log directory
        create_log_directory();

        // Initialize time cache (ensure completion before multi-threaded access)
        {
            std::lock_guard<std::mutex> lock(time_mutex);
            last_time_format = std::chrono::system_clock::now(); // Initialize to current time
            update_time_cache();
        }


        // Start flush thread (use make_unique)
        flush_thread = std::make_unique<std::thread>(&Logger::flush_thread_func, this);
    }

    ~Logger() {
        stop(); // Ensure stop and flush

        if (flush_thread && flush_thread->joinable()) {
            flush_thread->join(); // Wait for flush thread to finish
        }
    }

    // Stop the logging system
    void stop() {
        bool expected = true;
        // Use compare_exchange_strong to ensure stopping only once
        if (running.compare_exchange_strong(expected, false, std::memory_order_relaxed)) {
            cv.notify_all(); // Wake up potentially waiting flush thread

            // Flush all buffers before waiting for the flush thread to finish
            // Note: Acquiring the lock here might conflict with join in the destructor. A better approach is to call stop() before the destructor join
            // or join the thread inside stop() (but beware of deadlocks)
            // Simplified handling: Assume stop() is called before the destructor, or the destructor handles the join
            {
                std::lock_guard<std::mutex> lock(log_mutex);
                for (auto& buffer_pair : log_buffers) {
                    if (buffer_pair.second && buffer_pair.second->size > 0) {
                        flush_buffer_internal(buffer_pair.first);
                    }
                }
                // Close all file streams
                log_files.clear(); // unique_ptr will automatically manage closing
            }
        }
    }

    // Set maximum idle time (milliseconds)
    void set_max_idle_time(unsigned int ms) {
        max_idle_time.store(ms, std::memory_order_relaxed);
    }

    // Set buffer size
    void set_buffer_size(size_t size) {
        buffer_max_size.store(size, std::memory_order_relaxed);
    }

    // Set log level
    void set_log_level(int level) {
        log_level.store(level, std::memory_order_relaxed);
    }

    // Set log file size limit
    void set_log_size_limit(size_t limit) {
        log_size_limit.store(limit, std::memory_order_relaxed);
    }

    // Set low power mode
    void set_low_power_mode(bool enabled) {
        low_power_mode.store(enabled, std::memory_order_relaxed);

        // In low power mode, increase flush interval and buffer size
        if (enabled) {
            max_idle_time.store(60000, std::memory_order_relaxed); // 1 minute
            buffer_max_size.store(32768, std::memory_order_relaxed); // 32KB
        } else {
            // Restore default or previous settings, simplified here to restore hardcoded defaults
            max_idle_time.store(30000, std::memory_order_relaxed); // 30 seconds
            buffer_max_size.store(8192, std::memory_order_relaxed); // 8KB
        }
        // Can notify_one() here to let the flush thread apply new settings immediately
        cv.notify_one();
    }

    // Write log - optimized version
    void write_log(const std::string& log_name, LogLevel level, const std::string& message) {
        // Check log level
        if (static_cast<int>(level) > log_level.load(std::memory_order_relaxed)) {
            return;
        }
        if (!running.load(std::memory_order_relaxed)) { // Don't write if already stopped
             return;
        }

        // Get level string
        const char* level_str = get_level_string(level);

        // Get formatted time
        const char* time_str = get_formatted_time();

        // Optimize log entry construction - reduce intermediate string creation
        std::string log_entry;
        // Estimate size to reduce reallocations
        log_entry.reserve(strlen(time_str) + strlen(level_str) + message.size() + 10); // +10 for " [] \n" etc.

        log_entry += time_str;
        log_entry += " [";
        log_entry += level_str;
        log_entry += "] ";
        log_entry += message;
        log_entry += "\n";

        // Add to buffer
        add_to_buffer(log_name, std::move(log_entry), level);
    }

    // Batch write logs - efficient version
    void batch_write(const std::string& log_name, const std::vector<std::pair<LogLevel, std::string>>& entries) {
        if (entries.empty() || !running.load(std::memory_order_relaxed)) return;

        // Filter valid entries and calculate total size
        std::vector<std::pair<LogLevel, const std::string*>> valid_entries;
        valid_entries.reserve(entries.size());

        size_t total_size = 0;
        bool has_error = false; // Used to determine if immediate flush is needed (if policy dictates)
        int current_log_level = log_level.load(std::memory_order_relaxed);

        for (const auto& entry : entries) {
            if (static_cast<int>(entry.first) <= current_log_level) {
                valid_entries.emplace_back(entry.first, &entry.second);
                // Estimate size, including timestamp, level, spaces, newline, etc.
                total_size += entry.second.size() + 50; // 50 is a rough estimate
                if (entry.first == LOG_ERROR) has_error = true;
            }
        }

        if (valid_entries.empty()) return;

        // Get formatted time (get once at the start of batch operation)
        const char* time_str = get_formatted_time();

        // Build batch log content
        std::string batch_content;
        batch_content.reserve(total_size);

        for (const auto& entry : valid_entries) {
            const char* level_str = get_level_string(entry.first);
            batch_content += time_str;
            batch_content += " [";
            batch_content += level_str;
            batch_content += "] ";
            batch_content += *entry.second;
            batch_content += "\n";
        }

        // Add to buffer
        // For batch writes, the highest level (e.g., error) can be passed to add_to_buffer
        add_to_buffer(log_name, std::move(batch_content), has_error ? LOG_ERROR : LOG_INFO);
    }

    // Flush specified log buffer
    void flush_buffer(const std::string& log_name) {
        std::lock_guard<std::mutex> lock(log_mutex);
        flush_buffer_internal(log_name);
    }

    // Flush all log buffers
    void flush_all() {
        std::lock_guard<std::mutex> lock(log_mutex);
        for (auto it = log_buffers.begin(); it != log_buffers.end(); ++it) {
             if (it->second && it->second->size > 0) {
                 flush_buffer_internal(it->first);
             }
        }
        // Can also consider flushing all open file streams here
        for (auto& file_pair : log_files) {
            if (file_pair.second && file_pair.second->stream.is_open()) {
                file_pair.second->stream.flush();
            }
        }
    }

    // Clean all logs
    void clean_logs() {
        std::lock_guard<std::mutex> lock(log_mutex);

        // Close and clean all files and buffers
        log_files.clear(); // unique_ptr handles stream closing
        log_buffers.clear();

        // Delete log files - use POSIX API
        DIR *dir = opendir(log_dir.c_str());
        if (!dir) {
            std::cerr << "Cannot open log directory for cleaning: " << log_dir << " (" << strerror(errno) << ")" << std::endl;
            // Try using system command as fallback
            std::string cmd = "rm -f \"" + log_dir + "\"/*.log \"" + log_dir + "\"/*.log.old";
            system(cmd.c_str());
            return;
        }

        struct dirent *entry;
        while ((entry = readdir(dir)) != nullptr) {
            std::string filename = entry->d_name;
            if (filename == "." || filename == "..") {
                continue;
            }

            // Check if it's a .log or .log.old file
            bool is_log_file = false;
            size_t len = filename.length();
            if (len > 4 && filename.substr(len - 4) == ".log") {
                is_log_file = true;
            } else if (len > 7 && filename.substr(len - 7) == ".log.old") {
                 is_log_file = true;
            }

            if (is_log_file) {
                std::string full_path = log_dir + "/" + filename;
                if (remove(full_path.c_str()) != 0) {
                    std::cerr << "Cannot delete log file: " << full_path << " (" << strerror(errno) << ")" << std::endl;
                } else {
                    // Optional: Print deleted file info
                    // std::cout << "Deleted log file: " << full_path << std::endl;
                }
            }
        }
        closedir(dir);
    }

    // Helper function for main loop check
    bool is_running() const {
        return running.load(std::memory_order_relaxed);
    }


private:
    // Create log directory - use POSIX API
    void create_log_directory() {
        struct stat st;
        // Check if directory exists
        if (stat(log_dir.c_str(), &st) == 0) {
            if (S_ISDIR(st.st_mode)) {
                // Directory exists, check permissions (optional)
                if (access(log_dir.c_str(), W_OK | X_OK) != 0) {
                     std::cerr << "Warning: Insufficient permissions for log directory: " << log_dir << " (" << strerror(errno) << ")" << std::endl;
                     // Try to fix permissions
                     chmod(log_dir.c_str(), 0755);
                }
                return;
            } else {
                std::cerr << "Error: Log path exists but is not a directory: " << log_dir << std::endl;
                // Try using an alternative path or exit
                log_dir = "./logs"; // Switch to logs in the current directory
                std::cerr << "Trying alternative log directory: " << log_dir << std::endl;
                // Try creating the alternative directory again (avoid infinite recursion, try only once)
                if (stat(log_dir.c_str(), &st) != 0) {
                     // Alternative directory doesn't exist either, proceed with creation logic
                } else if (!S_ISDIR(st.st_mode)) {
                     std::cerr << "Error: Alternative log path also exists but is not a directory: " << log_dir << std::endl;
                     // Cannot proceed, can throw an exception or set an error state
                     throw std::runtime_error("Cannot initialize log directory");
                } else {
                     // Alternative directory exists and is a directory
                     return;
                }
            }
        }

        // Try creating the directory (including parent directories) - use system("mkdir -p")
        std::string cmd = "mkdir -p \"" + log_dir + "\"";
        int ret = system(cmd.c_str());
        if (ret != 0) {
             std::cerr << "Cannot create log directory (using system): " << log_dir << std::endl;
             // Check if creation actually failed
             if (stat(log_dir.c_str(), &st) != 0 || !S_ISDIR(st.st_mode)) {
                 std::cerr << "Error: Failed to create log directory, please check permissions or path." << std::endl;
                 // Throw exception or set error state
                 throw std::runtime_error("Cannot create log directory");
             }
        }
         // Ensure directory permissions (e.g., rwxr-xr-x)
        if (chmod(log_dir.c_str(), 0755) != 0) {
            std::cerr << "Warning: Cannot set log directory permissions: " << log_dir << " (" << strerror(errno) << ")" << std::endl;
        }
    }

    // Get log level string - use const char* to avoid string copying
    const char* get_level_string(LogLevel level) {
        switch (level) {
            case LOG_ERROR: return "ERROR";
            case LOG_WARN:  return "WARN";
            case LOG_INFO:  return "INFO";
            case LOG_DEBUG: return "DEBUG";
            default:        return "UNKNOWN"; // Handle invalid level
        }
    }

    // Get formatted time string - cache recent time to reduce formatting overhead
    const char* get_formatted_time() {
        // Use fine-grained lock to protect time cache
        std::lock_guard<std::mutex> lock(time_mutex);

        auto now = std::chrono::system_clock::now();
        // If time hasn't changed much (e.g., within the same second), reuse cached time string
        if (now - last_time_format < std::chrono::seconds(1)) {
            return time_buffer;
        }

        // Update time cache
        last_time_format = now;
        update_time_cache(); // This function doesn't need a lock internally as it's only called by get_formatted_time
        return time_buffer;
    }

    // Update time cache (assume time_mutex is held)
    void update_time_cache() {
        auto now_time = std::chrono::system_clock::to_time_t(last_time_format);
        // Use localtime_r or equivalent thread-safe version (if available and necessary)
        // std::tm now_tm;
        // localtime_r(&now_time, &now_tm);
        // std::strftime(time_buffer, sizeof(time_buffer), "%Y-%m-%d %H:%M:%S", &now_tm);
        // In many modern C++ implementations, std::localtime might already be thread-safe or have internal locks
        std::strftime(time_buffer, sizeof(time_buffer), "%Y-%m-%d %H:%M:%S", std::localtime(&now_time));
    }

    // Add content to buffer
    void add_to_buffer(const std::string& log_name, std::string&& content, LogLevel level) {
        std::lock_guard<std::mutex> lock(log_mutex); // Protect log_buffers

        // Ensure buffer exists
        auto buffer_it = log_buffers.find(log_name);
        if (buffer_it == log_buffers.end()) {
            // Use make_unique to create LogBuffer
            buffer_it = log_buffers.emplace(log_name, std::make_unique<LogBuffer>()).first;
        }

        auto& buffer = buffer_it->second;
        buffer->content.append(std::move(content)); // Use move semantics
        buffer->size = buffer->content.size(); // Get size directly
        buffer->last_write = std::chrono::steady_clock::now();

        // Consider immediate flush if it's an error log or buffer reaches threshold
        bool is_low_power = low_power_mode.load(std::memory_order_relaxed);
        size_t current_max_size = buffer_max_size.load(std::memory_order_relaxed);

        // Conditions for immediate flush: error log, or buffer full in non-low-power mode
        if ((level == LOG_ERROR) || (!is_low_power && buffer->size >= current_max_size)) {
            flush_buffer_internal(log_name); // Internal method, lock already held
        }

        // Notify flush thread that there might be work to do (even if not flushed immediately)
        cv.notify_one();
    }

    // Internal buffer flush method - lock-free version (caller must hold log_mutex)
    void flush_buffer_internal(const std::string& log_name) {
        auto buffer_it = log_buffers.find(log_name);
        if (buffer_it == log_buffers.end() || !buffer_it->second || buffer_it->second->size == 0) {
            return; // Buffer doesn't exist or is empty
        }

        auto& buffer = buffer_it->second;

        // Build log file path
        std::string log_path = log_dir + "/" + log_name + ".log";

        // Get or create log file object
        auto file_it = log_files.find(log_name);
        if (file_it == log_files.end()) {
            file_it = log_files.emplace(log_name, std::make_unique<LogFile>()).first;
        }
        auto& log_file = file_it->second;

        // Check file size and handle rotation
        size_t current_log_size_limit = log_size_limit.load(std::memory_order_relaxed);
        if (log_file->stream.is_open() && log_file->current_size > current_log_size_limit) {
            log_file->stream.close(); // Close current file

            // Rotate log file - use POSIX API
            std::string old_log_path = log_path + ".old";

            // Check if old file exists, delete if it does
            if (access(old_log_path.c_str(), F_OK) == 0) {
                if (remove(old_log_path.c_str()) != 0) {
                     std::cerr << "Cannot delete old file during log rotation: " << old_log_path << " (" << strerror(errno) << ")" << std::endl;
                     // Can choose to continue trying to rename, or log the error and continue
                }
            }

            // Rename current log file to old file
            if (access(log_path.c_str(), F_OK) == 0) { // Ensure current log file exists
                if (rename(log_path.c_str(), old_log_path.c_str()) != 0) {
                    std::cerr << "Cannot rename file during log rotation: " << log_path << " -> " << old_log_path << " (" << strerror(errno) << ")" << std::endl;
                    // Try using system command as fallback
                    std::string cmd = "mv -f \"" + log_path + "\" \"" + old_log_path + "\"";
                    system(cmd.c_str());
                }
            }

            log_file->current_size = 0; // Reset size counter
            // File is closed, will be reopened on next write
        }

        // Ensure file is open (if not open or closed after rotation)
        if (!log_file->stream.is_open()) {
            // Try opening file in append mode
            log_file->stream.open(log_path, std::ios::app | std::ios::binary);
            if (!log_file->stream.is_open()) {
                std::cerr << "Cannot open log file for writing: " << log_path << " (" << strerror(errno) << ")" << std::endl;
                // Clear buffer to prevent memory growth
                buffer->content.clear();
                buffer->size = 0;
                // Consider removing corresponding buffer and file entry to prevent repeated failures
                // log_buffers.erase(buffer_it); // Careful with iterator invalidation
                // log_files.erase(file_it);
                return; // Cannot write, return directly
            }

            // Get current file size (after opening)
            log_file->stream.seekp(0, std::ios::end);
            std::streampos pos = log_file->stream.tellp();
            if (pos == static_cast<std::streampos>(-1)) { // Check tellp error
                 log_file->current_size = 0; // Assume empty file if error occurs
                 std::cerr << "Warning: Cannot get log file size: " << log_path << std::endl;
            } else {
                 log_file->current_size = static_cast<size_t>(pos);
            }
        }

        // Write buffer content
        log_file->stream.write(buffer->content.c_str(), buffer->content.size());
        if (log_file->stream.fail()) {
             std::cerr << "Failed to write to log file: " << log_path << std::endl;
             // Consider closing the file and retrying, or logging the error
             log_file->stream.close(); // Close potentially problematic file stream
             log_file->current_size = 0; // Size unknown
             // Clear buffer to avoid writing erroneous data repeatedly
             buffer->content.clear();
             buffer->size = 0;
        } else {
            log_file->stream.flush(); // Ensure data is written to disk
            // Update file size and last access time
            log_file->current_size += buffer->size;
            log_file->last_access = std::chrono::steady_clock::now();

            // Clear buffer
            buffer->content.clear();
            // Optimization: If pre-allocated capacity is much larger than needed, shrink to save memory
            // if (buffer->content.capacity() > buffer_max_size.load(std::memory_order_relaxed) * 4) { // e.g., exceeds 4 times
            //     buffer->content.shrink_to_fit();
            // }
            buffer->size = 0;
        }
    }

    // Optimized flush thread function
    void flush_thread_func() {
        while (running.load(std::memory_order_relaxed)) {
            std::unique_lock<std::mutex> lock(log_mutex); // Acquire lock to access shared resources

            // Increase wait time in low power mode
            bool is_low_power = low_power_mode.load(std::memory_order_relaxed);
            auto wait_time = is_low_power ? std::chrono::seconds(60) : std::chrono::seconds(15);

            // Wait for specified time or until woken up (e.g., new log written, mode change, or stop signal)
            // Use running flag as exit condition
            cv.wait_for(lock, wait_time, [this] {
                return !running.load(std::memory_order_relaxed);
            });

            // Recheck running state, as it might have been woken up by stop()
            if (!running.load(std::memory_order_relaxed)) {
                break; // Exit loop
            }

            // Get current configuration (under lock protection)
            unsigned int current_idle_ms = max_idle_time.load(std::memory_order_relaxed);
            size_t current_max_buffer_size = buffer_max_size.load(std::memory_order_relaxed);
            auto now = std::chrono::steady_clock::now();

            // Check each buffer, flush if conditions met
            // Use iterator to traverse map, note that deleting elements in loop can invalidate iterators
            for (auto it = log_buffers.begin(); it != log_buffers.end(); /* no increment here */) {
                // Pre-increment iterator to prevent invalidation when deleting current element
                auto current_it = it++;
                if (!current_it->second) continue; // Invalid pointer

                auto& buffer = current_it->second;
                if (buffer->size == 0) continue; // Empty buffer

                auto idle_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                    now - buffer->last_write);

                // Flush condition: exceeded max idle time, or buffer size exceeds half (avoid frequent small writes)
                if (idle_duration.count() > current_idle_ms || buffer->size > current_max_buffer_size / 2) {
                    flush_buffer_internal(current_it->first); // Flush
                }
            }

            // Close file handles unused for a long time
            unsigned int file_idle_ms = current_idle_ms * 3; // e.g., 3 times the buffer idle time
            for (auto it = log_files.begin(); it != log_files.end(); /* no increment here */) {
                 auto current_it = it++;
                 if (!current_it->second) {
                     // log_files.erase(current_it); // Invalid pointer, can remove
                     continue;
                 }

                 if (!current_it->second->stream.is_open()) {
                     // File not open, no need to check idle time, but can consider removing map entry
                     // auto buffer_check_it = log_buffers.find(current_it->first);
                     // if (buffer_check_it == log_buffers.end() || buffer_check_it->second->size == 0) {
                     //      log_files.erase(current_it); // Remove file entry if corresponding buffer is also empty
                     // }
                     continue;
                 }

                 auto file_idle_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                     now - current_it->second->last_access);

                 if (file_idle_duration.count() > file_idle_ms) {
                     current_it->second->stream.close(); // Close file stream
                     // Can choose whether to remove from map, if you want it reopened on next write
                     // log_files.erase(current_it);
                 }
            }
            // Lock will be automatically released when unique_lock is destructed
        }
         // Final flush before thread exits (optional, depends on stop() logic)
         // flush_all(); // Might need to reacquire lock
    }
};

// Global logger instance (use smart pointer for lifetime management)
static std::unique_ptr<Logger> g_logger;

// Signal handler function
void signal_handler(int sig) {
    if (g_logger) {
        // Try to flush before exiting
        if (sig == SIGTERM || sig == SIGINT) {
            g_logger->flush_all();  // Flush all buffers
            g_logger->stop();       // Stop the logging system
        }
    }

    if (sig == SIGTERM || sig == SIGINT) {
        _exit(0);
    }
}


// Main function
int main(int argc, char* argv[]) {
    // Modify default path to module path
    std::string log_dir = "/data/adb/modules/AMMF2/logs"; // This is usually correct on Android
    int log_level_int = LOG_INFO; // Use int to store parsed result
    std::string command;
    std::string log_name = "system";
    std::string message;
    std::string batch_file;
    bool low_power = false;

    // Parse command line arguments
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-d" && i + 1 < argc) {
            log_dir = argv[++i];
        } else if (arg == "-l" && i + 1 < argc) {
            try {
                log_level_int = std::stoi(argv[++i]);
                if (log_level_int < LOG_ERROR || log_level_int > LOG_DEBUG) {
                    std::cerr << "Warning: Log level must be between " << LOG_ERROR << "-" << LOG_DEBUG
                              << ", using default " << LOG_INFO << std::endl;
                    log_level_int = LOG_INFO;
                }
            } catch (const std::invalid_argument& e) {
                std::cerr << "Error: Invalid log level argument: " << argv[i] << std::endl;
                return 1; // Argument error, exit
            } catch (const std::out_of_range& e) {
                 std::cerr << "Error: Log level argument out of range: " << argv[i] << std::endl;
                 return 1; // Argument error, exit
            }
        } else if (arg == "-c" && i + 1 < argc) {
            command = argv[++i];
        } else if (arg == "-n" && i + 1 < argc) {
            log_name = argv[++i];
        } else if (arg == "-m" && i + 1 < argc) {
            message = argv[++i];
        } else if (arg == "-b" && i + 1 < argc) {
            batch_file = argv[++i];
        } else if (arg == "-p") {
            low_power = true;
        } else if (arg == "-h" || arg == "--help") {
            std::cout << "Usage: " << argv[0] << " [options]" << std::endl;
            std::cout << "Options:" << std::endl;
            std::cout << "  -d DIR    Specify log directory (default: /data/adb/modules/AMMF2/logs)" << std::endl;
            std::cout << "  -l LEVEL  Set log level (1=Error, 2=Warn, 3=Info, 4=Debug, default: 3)" << std::endl;
            std::cout << "  -c CMD    Execute command (daemon, write, batch, flush, clean)" << std::endl;
            std::cout << "  -n NAME   Specify log name (for write/batch commands, default: system)" << std::endl;
            std::cout << "  -m MSG    Log message content (for write command)" << std::endl;
            std::cout << "  -b FILE   Batch input file, format: level|message (one per line, for batch command)" << std::endl;
            std::cout << "  -p        Enable low power mode (reduce write frequency)" << std::endl;
            std::cout << "  -h        Show help information" << std::endl;
            std::cout << "Example:" << std::endl;
            std::cout << "  Start daemon: " << argv[0] << " -c daemon -d /path/to/logs -l 4 -p" << std::endl;
            std::cout << "  Write log: " << argv[0] << " -c write -n main -m \"Test message\" -l 3" << std::endl;
            std::cout << "  Batch write: " << argv[0] << " -c batch -n errors -b batch_logs.txt" << std::endl;
            std::cout << "  Flush logs: " << argv[0] << " -c flush -d /path/to/logs" << std::endl;
            std::cout << "  Clean logs: " << argv[0] << " -c clean -d /path/to/logs" << std::endl;
            return 0;
        } else {
             std::cerr << "Error: Unknown or invalid argument: " << arg << std::endl;
             return 1;
        }
    }

    // If no command is specified, default to starting the daemon
    if (command.empty()) {
        command = "daemon";
    }

    // Create logger (use try-catch to capture exceptions in constructor)
    try {
        if (!g_logger) {
            g_logger = std::make_unique<Logger>(log_dir, log_level_int);
            if (low_power) {
                g_logger->set_low_power_mode(true);
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "Failed to initialize logging system: " << e.what() << std::endl;
        return 1;
    }


    // Execute command
    if (command == "daemon") {
        // Set file permission mask (ensure correct log file permissions)
        umask(0022); // rwxr-xr-x for files created by daemon

        // Set signal handling
        signal(SIGTERM, signal_handler);
        signal(SIGINT, signal_handler);
        // signal(SIGUSR1, signal_handler); // SIGUSR1 is often used for user-defined signals, possibly for flushing here
        signal(SIGPIPE, SIG_IGN);  // Ignore pipe errors to prevent program exit when writing to a closed pipe

        // Write startup log to file
        std::string startup_msg = "Logging system daemon started";
        if (low_power) {
            startup_msg += " (Low power mode)";
        }
        g_logger->write_log("system", LOG_INFO, startup_msg);

        // Ensure correct log directory permissions (handled in create_log_directory, this is a double check)
        // chmod(log_dir.c_str(), 0755);

        // Optimized main loop - wait using condition variable
        std::mutex main_mutex;
        std::condition_variable main_cv;
        std::unique_lock<std::mutex> lock(main_mutex);

        // Daemon main loop
        while (g_logger && g_logger->is_running()) {
            // Wait indefinitely using condition variable until woken up by signal handler or other mechanism and logger stops
            // Or can set a very long timeout for periodic checks (e.g., health checks)
            // cv.wait(lock, [&]{ return !g_logger || !g_logger->is_running(); });
            // Changed here to use wait_for for periodic wakeup, even without external signals
            // This allows adding periodic maintenance tasks (if needed)
            auto wait_duration = std::chrono::hours(1); // e.g., wake up once per hour
            main_cv.wait_for(lock, wait_duration);

            // If woken up, check if exit is needed
            if (!g_logger || !g_logger->is_running()) {
                break;
            }
            // Can add periodic tasks for the daemon here
            // e.g., check disk space, reload configuration, etc.
        }
         // Cleanup before daemon exits
        if (g_logger) {
            g_logger->write_log("system", LOG_INFO, "Logging system daemon is stopping...");
            g_logger->stop(); // Ensure stop and flush
        }
        return 0; // Daemon exits normally

    } else if (command == "write") {
        // Write log
        if (message.empty()) {
            std::cerr << "Error: Writing log requires message content (-m)" << std::endl;
            if (g_logger) g_logger->stop(); // Ensure resource release
            return 1;
        }

        LogLevel level = static_cast<LogLevel>(log_level_int);
        g_logger->write_log(log_name, level, message);

        // For single write commands, usually want to see results immediately, so perform flush
        g_logger->flush_buffer(log_name); // Flush only the relevant log file
        g_logger->stop(); // Stop and release resources
        return 0;

    } else if (command == "batch") {
        // Batch write logs
        if (batch_file.empty()) {
            std::cerr << "Error: Batch write requires input file (-b)" << std::endl;
             if (g_logger) g_logger->stop();
            return 1;
        }

        // Read batch file
        std::ifstream batch_in(batch_file);
        if (!batch_in.is_open()) {
            std::cerr << "Error: Cannot open batch file: " << batch_file << " (" << strerror(errno) << ")" << std::endl;
             if (g_logger) g_logger->stop();
            return 1;
        }

        std::vector<std::pair<LogLevel, std::string>> entries;
        std::string line;
        int line_num = 0;

        // Read log entries from file
        while (std::getline(batch_in, line)) {
            line_num++;
            // Skip empty lines and comments
            if (line.empty() || line[0] == '#') continue;

            // Find separator '|'
            size_t pos = line.find('|');
            if (pos == std::string::npos) {
                 std::cerr << "Warning: Batch file line " << line_num << " format error (missing '|'): " << line << std::endl;
                 continue; // Skip incorrectly formatted lines
            }

            // Parse log level
            std::string level_str = line.substr(0, pos);
            // Remove possible spaces
            level_str.erase(0, level_str.find_first_not_of(" \t"));
            level_str.erase(level_str.find_last_not_of(" \t") + 1);

            LogLevel level = LOG_INFO; // Default level
            try {
                int lvl_int = std::stoi(level_str);
                 if (lvl_int >= LOG_ERROR && lvl_int <= LOG_DEBUG) {
                     level = static_cast<LogLevel>(lvl_int);
                 } else {
                      std::cerr << "Warning: Batch file line " << line_num << " invalid level (" << level_str << "), using INFO" << std::endl;
                 }
            } catch (const std::invalid_argument&) {
                // If not a number, try matching string
                if (level_str == "ERROR") level = LOG_ERROR;
                else if (level_str == "WARN") level = LOG_WARN;
                else if (level_str == "INFO") level = LOG_INFO;
                else if (level_str == "DEBUG") level = LOG_DEBUG;
                else {
                     std::cerr << "Warning: Batch file line " << line_num << " unrecognized level (" << level_str << "), using INFO" << std::endl;
                }
            } catch (const std::out_of_range&) {
                 std::cerr << "Warning: Batch file line " << line_num << " level out of range (" << level_str << "), using INFO" << std::endl;
            }


            // Get message content
            std::string msg = line.substr(pos + 1);
            // Remove leading spaces from message
            msg.erase(0, msg.find_first_not_of(" \t"));

            // Add to entry list
            entries.emplace_back(level, std::move(msg)); // Use move semantics
        }

        batch_in.close();

        // Batch write logs
        if (!entries.empty()) {
            g_logger->batch_write(log_name, entries);
            // Also flush immediately after batch write
            g_logger->flush_buffer(log_name);
        }
        g_logger->stop(); // Stop and release resources
        return 0;

    } else if (command == "flush") {
        // Flush logs
        g_logger->flush_all();
        g_logger->stop(); // Also stop and clean up resources after flushing
        return 0;

    } else if (command == "clean") {
        // Clean logs
        g_logger->clean_logs();
        g_logger->stop(); // Also stop after cleaning
        return 0;

    } else {
        std::cerr << "Error: Unknown command '" << command << "'" << std::endl;
        std::cerr << "Use -h for help." << std::endl;
         if (g_logger) g_logger->stop();
        return 1;
    }

    // For non-daemon commands, reaching here means the command is complete
    // g_logger will be automatically destructed and call stop() when main ends
    // But explicitly calling stop() can release resources earlier
    // if (command != "daemon" && g_logger) {
    //     g_logger->stop();
    // }

    return 0; // Normal exit
}