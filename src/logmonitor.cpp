#include <android/log.h>
#include <string>
#include <vector>
#include <queue>
#include <map>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <chrono>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/system_properties.h>
#include <linux/fs.h>
#include <signal.h>
#include <algorithm>
#include <memory>
#include <iostream>

constexpr const char* VERSION = "3.0.0";

enum class LogLevel : uint8_t {
    ERROR = 1,
    WARN  = 2,
    INFO  = 3,
    DEBUG = 4
};

class LogBuffer {
private:
    static constexpr size_t BUFFER_SIZE = 256 * 1024;  // 256KB
    char buffer[BUFFER_SIZE];
    size_t used = 0;
    std::mutex mtx;
    std::condition_variable cv;
    bool need_flush = false;
    static constexpr size_t FLUSH_THRESHOLD = BUFFER_SIZE * 3 / 4;

public:
    bool write(const char* data, size_t len) {
        if (len > BUFFER_SIZE) return false;
        
        std::unique_lock<std::mutex> lock(mtx);
        if (used + len > BUFFER_SIZE) {
            need_flush = true;
            cv.notify_one();
            return false;
        }
        
        memcpy(buffer + used, data, len);
        used += len;
        
        if (used >= FLUSH_THRESHOLD) {
            need_flush = true;
            cv.notify_one();
        }
        return true;
    }
    
    size_t read(char* out, size_t max_len) {
        std::unique_lock<std::mutex> lock(mtx);
        if (used == 0) {
            cv.wait_for(lock, std::chrono::seconds(5), [this] { 
                return need_flush || used >= FLUSH_THRESHOLD; 
            });
        }
        
        size_t to_read = std::min(used, max_len);
        if (to_read > 0) {
            memcpy(out, buffer, to_read);
            used -= to_read;
            if (used > 0) {
                memmove(buffer, buffer + to_read, used);
            }
            need_flush = false;
        }
        return to_read;
    }
};

class LogFile {
private:
    static constexpr size_t MAX_FILE_SIZE = 10 * 1024 * 1024;
    static constexpr size_t ROTATE_COUNT = 3;
    
    std::string path;
    int fd;
    size_t current_size;
    std::vector<char> write_buffer;
    static constexpr size_t WRITE_BUFFER_SIZE = 512 * 1024;  // 512KB

public:
    explicit LogFile(const std::string& p) 
        : path(p), fd(-1), current_size(0), write_buffer(WRITE_BUFFER_SIZE) {
        rotate_files();
        fd = open(path.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
        if (fd != -1) {
            struct stat st;
            if (fstat(fd, &st) == 0) {
                current_size = st.st_size;
            }
            posix_fadvise(fd, 0, 0, POSIX_FADV_DONTNEED);
        }
    }

    ~LogFile() {
        if (fd != -1) {
            close(fd);
        }
    }

    bool write(const char* data, size_t len) {
        if (fd == -1) return false;
        
        if (current_size + len > MAX_FILE_SIZE) {
            rotate_files();
        }
        
        ssize_t written = ::write(fd, data, len);
        if (written > 0) {
            current_size += written;
            if (current_size % (1024 * 1024) == 0) {  // 每1MB同步一次
                fdatasync(fd);
            }
        }
        return written == static_cast<ssize_t>(len);
    }

private:
    void rotate_files() {
        if (fd != -1) {
            close(fd);
            fd = -1;
        }

        for (int i = ROTATE_COUNT - 1; i > 0; --i) {
            std::string old_name = path + "." + std::to_string(i);
            std::string new_name = path + "." + std::to_string(i + 1);
            rename(old_name.c_str(), new_name.c_str());
        }

        rename(path.c_str(), (path + ".1").c_str());
        fd = open(path.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
        current_size = 0;
    }
};

class Logger {
private:
    std::string log_dir;
    int log_level;
    std::map<std::string, std::unique_ptr<LogFile>> log_files;
    std::mutex files_mutex;
    std::atomic<bool> running{true};
    std::thread worker;
    std::unique_ptr<LogBuffer> buffer;
    std::chrono::steady_clock::time_point start_time;
    static constexpr size_t MAX_BATCH_SIZE = 1024 * 1024;  // 1MB

public:
    Logger(const std::string& dir, int level)
        : log_dir(dir), log_level(level),
          buffer(new LogBuffer()),
          start_time(std::chrono::steady_clock::now()) {
        mkdir(log_dir.c_str(), 0755);
        worker = std::thread(&Logger::process_logs, this);
        
        pthread_setname_np(worker.native_handle(), "log_worker");
        
        struct sched_param param;
        param.sched_priority = sched_get_priority_min(SCHED_BATCH);
        pthread_setschedparam(worker.native_handle(), SCHED_BATCH, &param);
    }

    ~Logger() {
        stop();
    }

    void stop() {
        running = false;
        if (worker.joinable()) {
            worker.join();
        }
    }

    void write_log(const std::string& name, LogLevel level, const std::string& message) {
        if (static_cast<int>(level) > log_level) return;
    
        time_t now;
        time(&now);
        tm* ltm = localtime(&now);
        
        char timestamp[32];
        strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", ltm);
        
        std::string formatted = std::string(timestamp) + " [" + name + "] [" + 
            level_to_string(level) + "] " + message + "\n";
        
        buffer->write(formatted.c_str(), formatted.length());
    }

private:
    static const char* level_to_string(LogLevel level) {
        switch (level) {
            case LogLevel::ERROR: return "ERROR";
            case LogLevel::WARN:  return "WARN";
            case LogLevel::INFO:  return "INFO";
            case LogLevel::DEBUG: return "DEBUG";
            default: return "UNKNOWN";
        }
    }

    void process_logs() {
        std::vector<char> batch_buffer(MAX_BATCH_SIZE);
        size_t batch_size = 0;
        
        while (running) {
            size_t bytes_read = buffer->read(batch_buffer.data() + batch_size, 
                                           MAX_BATCH_SIZE - batch_size);
            if (bytes_read > 0) {
                batch_size += bytes_read;
                
                if (batch_size >= MAX_BATCH_SIZE / 2) {
                    write_batch(batch_buffer.data(), batch_size);
                    batch_size = 0;
                }
            } else if (batch_size > 0) {
                write_batch(batch_buffer.data(), batch_size);
                batch_size = 0;
            }
        }
        
        if (batch_size > 0) {
            write_batch(batch_buffer.data(), batch_size);
        }
    }

    void write_batch(const char* data, size_t len) {
        std::lock_guard<std::mutex> lock(files_mutex);
        auto& file = log_files["system"];
        if (!file) {
            std::string path = log_dir + "/system.log";
            file.reset(new LogFile(path));
        }
        file->write(data, len);
    }
};

std::unique_ptr<Logger> g_logger;

template<typename T>
T clamp(T value, T min, T max) {
    return value < min ? min : (value > max ? max : value);
}

int main(int argc, char* argv[]) {
    std::string log_dir = "/data/adb/modules/AMMF2/logs";
    int log_level = 3;
    std::string command = "daemon";
    std::string log_name = "system";
    std::string message;
    
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "-d" && i + 1 < argc) {
            log_dir = argv[++i];
        } else if (arg == "-l" && i + 1 < argc) {
            try {
                int level = std::stoi(argv[++i]);
                log_level = clamp(level, 1, 4);
            } catch (...) {
                std::cerr << "Invalid log level, using default (3)" << std::endl;
            }
        } else if (arg == "-c" && i + 1 < argc) {
            command = argv[++i];
        } else if (arg == "-n" && i + 1 < argc) {
            log_name = argv[++i];
        } else if (arg == "-m" && i + 1 < argc) {
            message = argv[++i];
            while (i + 1 < argc && argv[i + 1][0] != '-') {
                message += " " + std::string(argv[++i]);
            }
        } else if (arg == "-v") {
            std::cout << "logmonitor version " << VERSION << std::endl;
            return 0;
        }
    }
    
    if (!g_logger) {
        g_logger.reset(new Logger(log_dir, log_level));
    }
    
    if (command == "daemon") {
        g_logger->write_log("system", LogLevel::INFO, 
            "Log system started (version " + std::string(VERSION) + ")");
        
        sigset_t mask;
        sigemptyset(&mask);
        sigaddset(&mask, SIGTERM);
        sigaddset(&mask, SIGINT);
        
        int sig;
        sigwait(&mask, &sig);
        
        g_logger->stop();
    } else if (command == "write" && !message.empty()) {
        g_logger->write_log(log_name, static_cast<LogLevel>(log_level), message);
    }
    
    return 0;
}