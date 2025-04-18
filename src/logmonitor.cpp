#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <map>
#include <chrono>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <memory>
#include <sstream>
#include <fcntl.h>
#include <sys/mman.h>

// 版本信息
constexpr const char* VERSION = "2.1.0";

// 日志级别
enum class LogLevel : uint8_t {
    ERROR = 1,
    WARN  = 2,
    INFO  = 3,
    DEBUG = 4
};

// 日志条目
struct LogEntry {
    std::string name;
    LogLevel level;
    std::string message;
    std::chrono::system_clock::time_point timestamp;
    
    LogEntry(std::string n, LogLevel l, std::string m)
        : name(std::move(n)), level(l), message(std::move(m)),
          timestamp(std::chrono::system_clock::now()) {}
};

// 日志管理器
class Logger {
private:
    std::mutex mtx;
    std::condition_variable cv;
    std::atomic<bool> running{true};
    std::string log_dir;
    int log_level;
    
    // 文件缓存
    static constexpr size_t BUFFER_SIZE = 1024 * 1024;  // 1MB 缓冲区
    
    struct MappedFile {
        int fd{-1};
        char* data{nullptr};
        size_t size{0};
        size_t used{0};
        
        ~MappedFile() {
            if (data && data != MAP_FAILED) {
                munmap(data, size);
            }
            if (fd != -1) {
                close(fd);
            }
        }
    };
    
    std::map<std::string, std::unique_ptr<MappedFile>> mapped_files;
    std::vector<LogEntry> queue;
    
    // 配置参数（省电模式）
    static constexpr size_t MAX_QUEUE_SIZE = 16384;  // 16KB
    static constexpr std::chrono::minutes::rep FLUSH_INTERVAL_MINUTES = 1;
    static constexpr std::chrono::milliseconds::rep WRITE_DELAY_MS = 500;
    
    std::thread worker;
    std::atomic<uint64_t> logs_count{0};
    std::atomic<uint64_t> bytes_written{0};
    std::chrono::system_clock::time_point start_time;

public:
    Logger(std::string dir, int level = 3)
        : log_dir(std::move(dir)), log_level(level),
          start_time(std::chrono::system_clock::now()) {
        // 创建日志目录
        mkdir(log_dir.c_str(), 0755);
        worker = std::thread(&Logger::process_queue, this);
    }
    
    ~Logger() {
        stop();
    }
    
    void write_log(const std::string& name, LogLevel level, const std::string& message) {
        if (static_cast<int>(level) > log_level) return;
        
        std::lock_guard<std::mutex> lock(mtx);
        queue.emplace_back(name, level, message);
        
        if (level == LogLevel::ERROR || queue.size() >= MAX_QUEUE_SIZE) {
            cv.notify_one();
        }
    }
    
    void flush() {
        std::lock_guard<std::mutex> lock(mtx);
        flush_queue();
    }
    
    void stop() {
        if (running.exchange(false)) {
            cv.notify_all();
            if (worker.joinable()) {
                worker.join();
            }
            flush();
            close_files();
        }
    }
    
    void clean() {
        std::lock_guard<std::mutex> lock(mtx);
        close_files();
        queue.clear();
        
        DIR* dir = opendir(log_dir.c_str());
        if (dir) {
            struct dirent* entry;
            while ((entry = readdir(dir)) != nullptr) {
                std::string filename = entry->d_name;
                if (filename.length() > 4 && 
                    filename.substr(filename.length() - 4) == ".log") {
                    std::string filepath = log_dir + "/" + filename;
                    unlink(filepath.c_str());
                }
            }
            closedir(dir);
        }
    }
    
    std::string get_stats() const {
        auto uptime = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now() - start_time).count();
        
        std::ostringstream oss;
        oss << "日志统计:\n"
            << "- 运行时间: " << uptime << "秒\n"
            << "- 处理日志: " << logs_count << "条\n"
            << "- 写入大小: " << bytes_written << "字节\n"
            << "- 当前队列: " << queue.size() << "条\n";
        return oss.str();
    }

private:
    void process_queue() {
        while (running) {
            std::unique_lock<std::mutex> lock(mtx);
            cv.wait_for(lock, 
                std::chrono::minutes(FLUSH_INTERVAL_MINUTES), 
                [this] { return !running || !queue.empty(); });
            
            if (!running && queue.empty()) break;
            flush_queue();
        }
    }
    
    void flush_queue() {
        if (queue.empty()) return;
        
        std::map<std::string, std::string> contents;
        
        for (const auto& entry : queue) {
            std::string& content = contents[entry.name];
            
            char time_str[32];
            auto time = std::chrono::system_clock::to_time_t(entry.timestamp);
            strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", localtime(&time));
            
            const char* level_str;
            switch (entry.level) {
                case LogLevel::ERROR: level_str = "ERROR"; break;
                case LogLevel::WARN:  level_str = "WARN";  break;
                case LogLevel::INFO:  level_str = "INFO";  break;
                case LogLevel::DEBUG: level_str = "DEBUG"; break;
                default: level_str = "UNKNOWN";
            }
            
            content.reserve(content.size() + entry.message.size() + 50);
            content += time_str;
            content += " [";
            content += level_str;
            content += "] ";
            content += entry.message;
            content += '\n';
        }
        
        logs_count += queue.size();
        queue.clear();
        
        for (const auto& item : contents) {
            write_to_file(item.first, item.second);
            bytes_written += item.second.size();
        }
    }
    
    private:
        void write_to_file(const std::string& name, const std::string& content) {
            std::string path = log_dir + "/" + name + ".log";
            
            auto& file_ptr = mapped_files[name];
            if (!file_ptr) {
                file_ptr.reset(new MappedFile());
            }
            auto& file = *file_ptr;
            
            if (file.fd == -1) {
                file.fd = open(path.c_str(), O_RDWR | O_CREAT | O_APPEND, 0644);
                if (file.fd == -1) return;
                
                // 获取当前文件大小
                struct stat st;
                if (fstat(file.fd, &st) == -1) {
                    close(file.fd);
                    file.fd = -1;
                    return;
                }
                
                size_t needed_size = st.st_size + content.size();
                size_t map_size = ((needed_size + BUFFER_SIZE - 1) / BUFFER_SIZE) * BUFFER_SIZE;
                
                // 确保文件大小足够
                if (ftruncate(file.fd, map_size) == -1) {
                    close(file.fd);
                    file.fd = -1;
                    return;
                }
                
                file.data = (char*)mmap(nullptr, map_size, 
                    PROT_READ | PROT_WRITE, MAP_SHARED, file.fd, 0);
                if (file.data == MAP_FAILED) {
                    close(file.fd);
                    file.fd = -1;
                    file.data = nullptr;
                    return;
                }
                
                file.size = map_size;
                file.used = st.st_size;
            }
        
            if (file.fd != -1 && file.data && file.data != MAP_FAILED) {
                // 检查是否需要扩展映射
                if (file.used + content.size() > file.size) {
                    size_t new_size = ((file.used + content.size() + BUFFER_SIZE - 1) 
                        / BUFFER_SIZE) * BUFFER_SIZE;
                    
                    munmap(file.data, file.size);
                    if (ftruncate(file.fd, new_size) == -1) {
                        close(file.fd);
                        file.fd = -1;
                        file.data = nullptr;
                        return;
                    }
                    
                    file.data = (char*)mmap(nullptr, new_size, 
                        PROT_READ | PROT_WRITE, MAP_SHARED, file.fd, 0);
                    if (file.data == MAP_FAILED) {
                        close(file.fd);
                        file.fd = -1;
                        file.data = nullptr;
                        return;
                    }
                    
                    file.size = new_size;
                }
        
                // 写入数据
                memcpy(file.data + file.used, content.data(), content.size());
                file.used += content.size();
        
                // 定期同步到磁盘
                msync(file.data + file.used - content.size(), content.size(), MS_ASYNC);
            }
        }
        
        void close_files() {
            mapped_files.clear();  // 智能指针会自动清理资源
        }
};

// 全局实例
static std::unique_ptr<Logger> g_logger;

int main(int argc, char* argv[]) {
    std::string log_dir = "/data/adb/modules/AMMF2/logs";
    int log_level = 3;
    std::string command = "daemon";
    std::string log_name = "system";
    std::string message;
    
    auto process_quoted_arg = [](const char* arg) -> std::string {
        std::string result;
        size_t len = strlen(arg);
        // 如果参数被双引号包围，去除双引号
        if (len >= 2 && arg[0] == '"' && arg[len-1] == '"') {
            result = std::string(arg + 1, len - 2);
        } else {
            result = arg;
        }
        return result;
    };

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "-d" && i + 1 < argc) {
            log_dir = process_quoted_arg(argv[++i]);
        } else if (arg == "-l" && i + 1 < argc) {
            try {
                int level = std::stoi(argv[++i]);
                log_level = (level < 1) ? 1 : (level > 4) ? 4 : level;
            } catch (const std::exception&) {
                std::cerr << "Invalid log level, using default (3)" << std::endl;
            }
        } else if (arg == "-c" && i + 1 < argc) {
            command = process_quoted_arg(argv[++i]);
        } else if (arg == "-n" && i + 1 < argc) {
            log_name = process_quoted_arg(argv[++i]);
        } else if (arg == "-m" && i + 1 < argc) {
            message = process_quoted_arg(argv[++i]);
            // 如果下一个参数不是新的选项，则认为是消息的继续
            while (i + 1 < argc && argv[i + 1][0] != '-') {
                message += " " + process_quoted_arg(argv[++i]);
            }
        } else if (arg == "-v") {
            std::cout << "logmonitor 版本 " << VERSION << std::endl;
            return 0;
        }
    }
    
    if (!g_logger) {
        g_logger = std::unique_ptr<Logger>(new Logger(log_dir, log_level));
    }
    
    if (command == "daemon") {
        g_logger->write_log("system", LogLevel::INFO, 
            "日志系统启动 (版本 " + std::string(VERSION) + ")");
        
        std::mutex mtx;
        std::condition_variable cv;
        std::unique_lock<std::mutex> lock(mtx);
        cv.wait(lock);
    } else if (command == "write" && !message.empty()) {
        g_logger->write_log(log_name, static_cast<LogLevel>(log_level), message);
    } else if (command == "flush") {
        g_logger->flush();
    } else if (command == "clean") {
        g_logger->clean();
    } else if (command == "stats") {
        std::cout << g_logger->get_stats();
    }
    
    return 0;
}