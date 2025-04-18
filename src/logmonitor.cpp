#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <map>
#include <chrono>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <cstring>
#include <ctime>
#include <csignal>

// Log level definitions
enum LogLevel {
    LOG_ERROR = 1,
    LOG_WARN = 2,
    LOG_INFO = 3,
    LOG_DEBUG = 4
};

// 优化的 Logger 类
class Logger {
private:
    std::mutex log_mutex;
    std::condition_variable cv;
    bool running;
    std::string log_dir;
    int log_level;
    size_t log_size_limit;
    std::map<std::string, std::ofstream> log_files;
    
    // 优化的缓冲区管理
    struct LogBuffer {
        std::string content;
        size_t size;
        std::chrono::steady_clock::time_point last_write;
    };
    std::map<std::string, LogBuffer> log_buffers;
    size_t buffer_max_size;
    
    // 新增：最大空闲时间（毫秒）
    unsigned int max_idle_time;
    // 新增：低功耗模式标志
    bool low_power_mode;

public:
    Logger(const std::string& dir, int level = 3, size_t size_limit = 102400)
        : log_mutex(), cv(), running(true), log_dir(dir), log_level(level), 
          log_size_limit(size_limit), log_files(), log_buffers(), buffer_max_size(8192), 
          max_idle_time(30000), low_power_mode(false) {
        
        // 创建日志目录 - 使用递归创建
        std::string cmd = "mkdir -p " + log_dir;
        system(cmd.c_str());
        
        // 确认目录是否存在
        struct stat st;
        if (stat(log_dir.c_str(), &st) != 0 || !S_ISDIR(st.st_mode)) {
            std::cerr << "无法创建或访问日志目录: " << log_dir << std::endl;
            // 尝试使用当前目录
            log_dir = "./logs";
            system(("mkdir -p " + log_dir).c_str());
        }
        
        // 启动优化的定期刷新线程
        std::thread flush_thread(&Logger::flush_thread_func, this);
        flush_thread.detach();
    }
    
    ~Logger() {
        stop();
        
        // Close all log files
        for (auto& file : log_files) {
            if (file.second.is_open()) {
                file.second.close();
            }
        }
    }
    
    // 停止日志系统
    void stop() {
        running = false;
        cv.notify_all();
        
        // Flush all buffers
        std::lock_guard<std::mutex> lock(log_mutex);
        for (const auto& buffer_pair : log_buffers) {
            flush_buffer(buffer_pair.first);
        }
    }
    
    // 设置最大空闲时间（毫秒）
    void set_max_idle_time(unsigned int ms) {
        max_idle_time = ms;
    }
    
    // 设置缓冲区大小
    void set_buffer_size(size_t size) {
        buffer_max_size = size;
    }
    
    // 设置低功耗模式
    void set_low_power_mode(bool enabled) {
        low_power_mode = enabled;
        // 在低功耗模式下，增加刷新间隔和缓冲区大小
        if (enabled) {
            max_idle_time = 60000; // 1分钟
            buffer_max_size = 16384; // 16KB
        } else {
            max_idle_time = 30000; // 30秒
            buffer_max_size = 8192; // 8KB
        }
    }
    
    // Write log entry - 优化版本
    void write_log(const std::string& log_name, LogLevel level, const std::string& message) {
        // 判断日志级别
        if (static_cast<int>(level) > log_level) return;
        
        std::string level_str;
        switch (level) {
            case LOG_ERROR: level_str = "ERROR"; break;
            case LOG_WARN:  level_str = "WARN";  break;
            case LOG_INFO:  level_str = "INFO";  break;
            case LOG_DEBUG: level_str = "DEBUG"; break;
            default:        level_str = "INFO";  break;
        }
        
        // 获取当前时间
        auto now = std::chrono::system_clock::now();
        std::time_t now_time = std::chrono::system_clock::to_time_t(now);
        char time_str[32]; // 减小缓冲区大小
        std::strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", std::localtime(&now_time));
        
        // 格式化日志消息 - 使用预分配的字符串减少内存分配
        std::string log_entry;
        log_entry.reserve(message.size() + 50); // 预分配足够空间
        log_entry.append(time_str).append(" [").append(level_str).append("] ").append(message).append("\n");
        
        std::lock_guard<std::mutex> lock(log_mutex);
        
        // Add to buffer
        if (log_buffers.find(log_name) == log_buffers.end()) {
            log_buffers[log_name] = {"", 0, std::chrono::steady_clock::now()};
        }
        
        auto& buffer = log_buffers[log_name];
        buffer.content += log_entry;
        buffer.size += log_entry.size();
        buffer.last_write = std::chrono::steady_clock::now();
        
        // 如果是错误级别日志或缓冲区达到阈值，立即刷新
        // 在低功耗模式下，只有错误日志才立即刷新
        if ((level == LOG_ERROR) || (!low_power_mode && buffer.size >= buffer_max_size)) {
            flush_buffer(log_name);
        }
    }
    
    // 批量写入日志 - 新增方法，更省电
    void batch_write(const std::string& log_name, const std::vector<std::pair<LogLevel, std::string>>& entries) {
        if (entries.empty()) return;
        
        std::lock_guard<std::mutex> lock(log_mutex);
        
        // 确保缓冲区存在
        if (log_buffers.find(log_name) == log_buffers.end()) {
            log_buffers[log_name] = {"", 0, std::chrono::steady_clock::now()};
        }
        
        auto& buffer = log_buffers[log_name];
        bool has_error = false;
        
        // 获取当前时间
        auto now = std::chrono::system_clock::now();
        std::time_t now_time = std::chrono::system_clock::to_time_t(now);
        char time_str[32];
        std::strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", std::localtime(&now_time));
        
        // 预分配足够的空间
        size_t total_size = 0;
        for (const auto& entry : entries) {
            if (static_cast<int>(entry.first) <= log_level) {
                total_size += entry.second.size() + 50; // 估计每条日志的额外字符
            }
        }
        
        if (total_size == 0) return;
        
        // 预分配内存以减少重新分配
        std::string batch_content;
        batch_content.reserve(total_size);
        
        // 处理每条日志
        for (const auto& entry : entries) {
            LogLevel level = entry.first;
            const std::string& message = entry.second;
            
            // 跳过超出日志级别的条目
            if (static_cast<int>(level) > log_level) continue;
            
            // 检查是否有错误级别日志
            if (level == LOG_ERROR) has_error = true;
            
            std::string level_str;
            switch (level) {
                case LOG_ERROR: level_str = "ERROR"; break;
                case LOG_WARN:  level_str = "WARN";  break;
                case LOG_INFO:  level_str = "INFO";  break;
                case LOG_DEBUG: level_str = "DEBUG"; break;
                default:        level_str = "INFO";  break;
            }
            
            // 添加到批量内容
            batch_content.append(time_str).append(" [").append(level_str).append("] ").append(message).append("\n");
        }
        
        // 添加到缓冲区
        buffer.content += batch_content;
        buffer.size += batch_content.size();
        buffer.last_write = std::chrono::steady_clock::now();
        
        // 如果有错误日志、缓冲区达到阈值或批量内容较大，立即刷新
        if (has_error || (!low_power_mode && buffer.size >= buffer_max_size)) {
            flush_buffer(log_name);
        }
    }
    
    // Flush specific log buffer - 优化版本
    void flush_buffer(const std::string& log_name) {
        if (log_buffers.find(log_name) == log_buffers.end() || log_buffers[log_name].size == 0) {
            return;
        }
        
        std::string log_path = log_dir + "/" + log_name + ".log";
        
        // 检查文件大小并处理轮转
        bool need_reopen = false;
        struct stat st;
        if (stat(log_path.c_str(), &st) == 0) {
            if (static_cast<size_t>(st.st_size) > log_size_limit) {
                // Rotate log file
                std::string old_log = log_path + ".old";
                rename(log_path.c_str(), old_log.c_str());
                need_reopen = true;
                
                // Close log file if open
                if (log_files.find(log_name) != log_files.end() && log_files[log_name].is_open()) {
                    log_files[log_name].close();
                }
            }
        }
        
        // 确保日志文件已打开
        if (need_reopen || log_files.find(log_name) == log_files.end() || !log_files[log_name].is_open()) {
            log_files[log_name].open(log_path, std::ios::app | std::ios::binary);
            if (!log_files[log_name].is_open()) {
                std::cerr << "Failed to open log file: " << log_path << std::endl;
                return;
            }
        }
        
        // 写入缓冲区内容
        log_files[log_name].write(log_buffers[log_name].content.c_str(), log_buffers[log_name].content.size());
        log_files[log_name].flush();
        
        // 清空缓冲区
        log_buffers[log_name].content.clear();
        log_buffers[log_name].size = 0;
    }
    
    // Flush all log buffers
    void flush_all() {
        std::lock_guard<std::mutex> lock(log_mutex);
        for (const auto& buffer_pair : log_buffers) {
            flush_buffer(buffer_pair.first);
        }
    }
    
    // Clean all logs
    void clean_logs() {
        std::lock_guard<std::mutex> lock(log_mutex);
        
        // Close all log files
        for (auto& file : log_files) {
            if (file.second.is_open()) {
                file.second.close();
            }
        }
        log_files.clear();
        
        // Clear all buffers
        log_buffers.clear();
        
        // Delete log files
        std::string cmd = "rm -f " + log_dir + "/*.log " + log_dir + "/*.log.old";
        system(cmd.c_str());
    }
    
private:
    // 优化的定期刷新线程函数
    void flush_thread_func() {
        while (running) {
            // 使用条件变量等待，可以被提前唤醒
            {
                std::unique_lock<std::mutex> lock(log_mutex);
                // 低功耗模式下增加等待时间
                auto wait_time = low_power_mode ? std::chrono::seconds(30) : std::chrono::seconds(15);
                cv.wait_for(lock, wait_time, [this] { return !running; });
                
                if (!running) break;
                
                // 检查每个缓冲区，只刷新需要刷新的
                auto now = std::chrono::steady_clock::now();
                for (auto it = log_buffers.begin(); it != log_buffers.end(); ++it) {
                    // 如果缓冲区有内容且超过最大空闲时间或达到一定大小，则刷新
                    auto& buffer = it->second;
                    auto idle_time = std::chrono::duration_cast<std::chrono::milliseconds>(now - buffer.last_write).count();
                    
                    if (buffer.size > 0 && (idle_time > max_idle_time || buffer.size > buffer_max_size / 2)) {
                        flush_buffer(it->first);
                    }
                }
                
                // 关闭长时间未使用的文件句柄
                for (auto it = log_files.begin(); it != log_files.end();) {
                    if (log_buffers.find(it->first) == log_buffers.end() || 
                        std::chrono::duration_cast<std::chrono::milliseconds>(
                            now - log_buffers[it->first].last_write).count() > max_idle_time * 2) {
                        if (it->second.is_open()) {
                            it->second.close();
                        }
                        it = log_files.erase(it);
                    } else {
                        ++it;
                    }
                }
            }
        }
    }
};

// Global logger instance
static Logger* g_logger = nullptr;

// Signal handler
void signal_handler(int sig) {
    if (g_logger) {
        g_logger->flush_all();
    }
    
    if (sig == SIGTERM || sig == SIGINT) {
        exit(0);
    }
}

// Main function
int main(int argc, char* argv[]) {
    std::string log_dir = "/data/adb/modules/AMMF2/logs";
    int log_level = 3;
    std::string command;
    std::string log_name = "system";
    std::string message;
    std::string batch_file;
    bool low_power = false;
    
    // 解析命令行参数
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "-d" && i + 1 < argc) {
            log_dir = argv[++i];
        } else if (arg == "-l" && i + 1 < argc) {
            try {
                log_level = std::stoi(argv[++i]);
            } catch (const std::exception& e) {
                std::cerr << "Invalid log level: " << argv[i] << std::endl;
                log_level = 3; // 使用默认值
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
            std::cout << "  -l LEVEL  Set log level (1=error, 2=warning, 3=info, 4=debug, default: 3)" << std::endl;
            std::cout << "  -c CMD    Execute command (daemon, write, batch, flush, clean)" << std::endl;
            std::cout << "  -n NAME   Specify log name (for write command, default: system)" << std::endl;
            std::cout << "  -m MSG    Log message content (for write command)" << std::endl;
            std::cout << "  -b FILE   Batch input file with format: LEVEL|MESSAGE per line" << std::endl;
            std::cout << "  -p        Enable low power mode (less frequent writes)" << std::endl;
            std::cout << "  -h        Show help information" << std::endl;
            std::cout << "Examples:" << std::endl;
            std::cout << "  Start daemon: " << argv[0] << " -c daemon" << std::endl;
            std::cout << "  Write log: " << argv[0] << " -c write -n main -m \"Test message\" -l 3" << std::endl;
            std::cout << "  Batch write: " << argv[0] << " -c batch -n main -b batch_logs.txt" << std::endl;
            std::cout << "  Flush logs: " << argv[0] << " -c flush" << std::endl;
            std::cout << "  Clean logs: " << argv[0] << " -c clean" << std::endl;
            return 0;
        }
    }
    
    // If no command specified, start daemon by default
    if (command.empty()) {
        command = "daemon";
    }
    
    // Create logger
    if (!g_logger) {
        g_logger = new Logger(log_dir, log_level);
        if (low_power) {
            g_logger->set_low_power_mode(true);
        }
    }
    
    // Execute command
    if (command == "daemon") {
        // Set signal handlers
        signal(SIGTERM, signal_handler);
        signal(SIGINT, signal_handler);
        signal(SIGUSR1, signal_handler);  // Can be used to trigger log flush
        
        // Write startup log
        std::string startup_msg = "Log system started";
        if (low_power) {
            startup_msg += " (low power mode)";
        }
        g_logger->write_log("system", LOG_INFO, startup_msg);
        
        // 优化的主循环 - 使用条件变量等待而不是频繁唤醒
        std::mutex main_mutex;
        std::condition_variable main_cv;
        std::unique_lock<std::mutex> lock(main_mutex);
        while (true) {
            // 低功耗模式下减少唤醒频率
            auto wait_time = low_power ? std::chrono::minutes(10) : std::chrono::minutes(5);
            main_cv.wait_for(lock, wait_time);
            g_logger->flush_all();
        }
    } else if (command == "write") {
        // Write log
        if (message.empty()) {
            std::cerr << "Error: Writing log requires message content (-m)" << std::endl;
            return 1;
        }
        
        LogLevel level = static_cast<LogLevel>(log_level);
        g_logger->write_log(log_name, level, message);
        
        // 在非低功耗模式下立即刷新
        if (!low_power) {
            g_logger->flush_all();
        }
        return 0;
    } else if (command == "batch") {
        // 批量写入日志
        if (batch_file.empty()) {
            std::cerr << "Error: Batch writing requires input file (-b)" << std::endl;
            return 1;
        }
        
        // 读取批量文件
        std::ifstream batch_in(batch_file);
        if (!batch_in.is_open()) {
            std::cerr << "Error: Cannot open batch file: " << batch_file << std::endl;
            return 1;
        }
        
        std::vector<std::pair<LogLevel, std::string>> entries;
        std::string line;
        
        // 从文件读取日志条目
        while (std::getline(batch_in, line)) {
            // 跳过空行和注释
            if (line.empty() || line[0] == '#') continue;
            
            // 查找分隔符
            size_t pos = line.find('|');
            if (pos == std::string::npos) continue;
            
            // 解析日志级别
            std::string level_str = line.substr(0, pos);
            LogLevel level;
            
            if (level_str == "ERROR" || level_str == "1") {
                level = LOG_ERROR;
            } else if (level_str == "WARN" || level_str == "2") {
                level = LOG_WARN;
            } else if (level_str == "INFO" || level_str == "3") {
                level = LOG_INFO;
            } else if (level_str == "DEBUG" || level_str == "4") {
                level = LOG_DEBUG;
            } else {
                level = LOG_INFO;
            }
            
            // 获取消息内容
            std::string message = line.substr(pos + 1);
            
            // 添加到条目列表
            entries.emplace_back(level, message);
        }
        
        batch_in.close();
        
        // 批量写入日志
        if (!entries.empty()) {
            g_logger->batch_write(log_name, entries);
            
            // 在非低功耗模式下立即刷新
            if (!low_power) {
                g_logger->flush_all();
            }
        }
        
        return 0;
    } else if (command == "flush") {
        // Flush logs
        g_logger->flush_all();
        return 0;
    } else if (command == "clean") {
        // Clean logs
        g_logger->clean_logs();
        return 0;
    } else {
        std::cerr << "Error: Unknown command '" << command << "'" << std::endl;
        return 1;
    }
    
    delete g_logger;
    return 0;
}