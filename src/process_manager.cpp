#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include <atomic>
#include <cstdio>
#include <unistd.h>
#include <fstream>
#include <ctime>
#include <filesystem>
#include <vector>
#include <mutex> // 添加 mutex 头文件

class Logger {
private:
    static std::ofstream log_file;
    static std::vector<std::string> log_buffer; // 日志缓冲区
    static std::mutex buffer_mutex; // 用于保护缓冲区的互斥锁
    static const size_t buffer_size_threshold = 6; // 缓冲区大小阈值

    static void ensureDirectoryExists(const std::string& path) {
        std::filesystem::path dir = std::filesystem::path(path).parent_path();
        if (!std::filesystem::exists(dir)) {
            std::filesystem::create_directories(dir);
        }
    }

    // 将缓冲区内容写入文件
    static void flushBuffer() {
        std::lock_guard<std::mutex> lock(buffer_mutex); // 获取锁
        if (!log_file.is_open() || log_buffer.empty()) return;

        for (const auto& msg : log_buffer) {
            log_file << msg << std::endl;
        }
        log_file.flush(); // 确保写入磁盘
        log_buffer.clear(); // 清空缓冲区
    }

public:
    static void init() {
        const std::string log_path = "/data/adb/modules/DeepSuppressor/logs/process_manager.log";
        ensureDirectoryExists(log_path);
        log_file.open(log_path, std::ios::out | std::ios::app);
    }

    static void log(const std::string& level, const std::string& message) {
        time_t now = time(nullptr);
        std::string time_str = ctime(&now);
        time_str = time_str.substr(0, time_str.length() - 1); // 移除换行符

        std::string formatted_message = time_str + " [" + level + "] " + message;

        // 同时输出到控制台
        std::cout << formatted_message << std::endl;

        // 将日志添加到缓冲区
        {
            std::lock_guard<std::mutex> lock(buffer_mutex); // 获取锁
            log_buffer.push_back(formatted_message);

            // 检查缓冲区是否达到阈值
            if (log_buffer.size() >= buffer_size_threshold) {
                // 释放锁后刷新缓冲区，避免持有锁进行IO操作
                // 注意：这里在锁内检查，但在锁外调用 flushBuffer，
                // flushBuffer 内部会再次获取锁。
                // 也可以直接在锁内调用，但持有锁进行IO可能影响性能。
                // 为了简化，先在锁内调用。
                if (log_file.is_open()) {
                     for (const auto& msg : log_buffer) {
                        log_file << msg << std::endl;
                    }
                    log_file.flush();
                    log_buffer.clear();
                }
            }
        } // 锁在这里释放
    }

    static void close() {
        flushBuffer(); // 关闭前确保刷新缓冲区
        if (log_file.is_open()) {
            log_file.close();
        }
    }
};

std::ofstream Logger::log_file;
std::vector<std::string> Logger::log_buffer; // 初始化静态成员
std::mutex Logger::buffer_mutex; // 初始化静态成员

class ProcessManager {
private:
    struct Target {
        std::string package_name;
        std::vector<std::string> process_names;
        bool is_foreground;
        std::chrono::steady_clock::time_point last_background_time;
        
        Target(std::string pkg, std::vector<std::string> procs)
            : package_name(std::move(pkg))
            , process_names(std::move(procs))
            , is_foreground(true) {}
    };

    std::vector<Target> targets;
    std::atomic<bool> running{true};
    static constexpr auto BACKGROUND_THRESHOLD = std::chrono::seconds(20); // 增加到 20 秒
    static constexpr auto CHECK_INTERVAL = std::chrono::seconds(15); // 增加到 15 秒

    static std::string exec(const std::string& cmd) {
        std::array<char, 128> buffer;
        std::string result;
        
        FILE* pipe = popen(cmd.c_str(), "r");
        if (!pipe) {
            Logger::log("ERROR", "Failed to execute command: " + cmd);
            return "";
        }
        
        while (fgets(buffer.data(), buffer.size(), pipe) != nullptr) {
            result += buffer.data();
        }
        
        pclose(pipe);
        return result;
    }

    bool isProcessForeground(const std::string& package_name) {
        Logger::log("DEBUG", "Checking if package " + package_name + " is in foreground");
        std::string cmd = "dumpsys activity activities | grep -E 'mResumedActivity|mFocusedActivity' | grep " + package_name;
        std::string result = exec(cmd);
        Logger::log("DEBUG", "dumpsys result: " + result);
        // 添加更严格的检查，确保确实是目标包名在前台
        bool foreground = !result.empty() && result.find(package_name) != std::string::npos;
        Logger::log("DEBUG", "Package " + package_name + " is in foreground: " + (foreground ? "true" : "false"));
        return foreground;
    }

    // 检查特定包名的特定进程是否在运行
    bool isProcessRunning(const std::string& process_name, const std::string& package_name) {
        // 使用ps命令获取进程信息，并检查是否包含包名和进程名
        std::string cmd = "ps -A | grep \"" + process_name + "\" | grep \"" + package_name + "\" | grep -v grep";
        return !exec(cmd).empty();
    }

    void killProcess(const std::string& process_name, const std::string& package_name) {
        if (isProcessRunning(process_name, package_name)) {
            // 使用更精确的方式杀死进程，确保只杀死特定包名的进程
            std::string cmd = "ps -A | grep \"" + process_name + "\" | grep \"" + package_name + 
                             "\" | grep -v grep | awk '{print $2}' | xargs kill -9";
            if (system(cmd.c_str()) == 0) {
                Logger::log("INFO", "Successfully killed process: " + process_name + " for package: " + package_name);
            } else {
                Logger::log("ERROR", "Failed to kill process: " + process_name + " for package: " + package_name);
            }
        } else {
            Logger::log("DEBUG", "Process: " + process_name + " for package: " + package_name + " is not running");
        }
    }

public:
    explicit ProcessManager(const std::vector<std::pair<std::string, std::vector<std::string>>>& initial_targets) {
        for (const auto& [pkg, procs] : initial_targets) {
            if (!pkg.empty() && !procs.empty()) {
                targets.emplace_back(pkg, procs);
                Logger::log("INFO", "Added target: " + pkg + " with " + 
                          std::to_string(procs.size()) + " processes");
            }
        }
    }

    void start() {
        Logger::log("INFO", "Process manager started with " + std::to_string(targets.size()) + " targets");
        
        while (running) {
            for (auto& target : targets) {
                try {
                    bool current_foreground = isProcessForeground(target.package_name);
                    Logger::log("DEBUG", "Package " + target.package_name + " is foreground: " + (current_foreground ? "true" : "false"));

                    if (current_foreground) {
                        // 应用当前在前台
                        if (!target.is_foreground) {
                            // 刚从后台切换到前台
                            target.is_foreground = true;
                            target.last_background_time = std::chrono::steady_clock::now(); // 更新时间
                            Logger::log("INFO", "Package " + target.package_name + " moved to foreground. Setting is_foreground to true");
                        }
                        // 如果本来就在前台，则无需操作，跳过杀死逻辑
                    } else {
                        // 应用当前在后台
                        if (target.is_foreground) {
                            // 刚从前台切换到后台
                            target.is_foreground = false;
                            target.last_background_time = std::chrono::steady_clock::now();
                            Logger::log("INFO", "Package " + target.package_name + " moved to background. Setting is_foreground to false");
                        } 
                        // 之前已经在后台, 或者刚切换到后台
                        auto now = std::chrono::steady_clock::now();
                        if (!target.is_foreground && (now - target.last_background_time >= BACKGROUND_THRESHOLD)) {
                            Logger::log("INFO", "Background threshold met for " + target.package_name + ". Checking and killing processes.");
                            
                            // 再次确认应用确实在后台
                            if (!isProcessForeground(target.package_name)) {
                                for (const auto& proc : target.process_names) {
                                    // 确保只杀死属于该包名的进程
                                    killProcess(proc, target.package_name);
                                }
                                // 重置计时器，避免频繁杀死进程
                                target.last_background_time = now;
                                Logger::log("INFO", "Reset background timer for " + target.package_name);
                            } else {
                                Logger::log("INFO", "Package " + target.package_name + " is now in foreground, skipping kill");
                                target.is_foreground = true;
                            }
                        }
                    }
                } catch (const std::exception& e) {
                    Logger::log("ERROR", "Error processing target " + target.package_name + ": " + std::string(e.what()));
                }
            }
            std::this_thread::sleep_for(CHECK_INTERVAL);
        }
    }

    void stop() {
        running = false;
    }
};

class ArgumentParser {
public:
    static std::vector<std::pair<std::string, std::vector<std::string>>> parse(int argc, char* argv[], int start_index) {
        std::vector<std::pair<std::string, std::vector<std::string>>> result;
        std::string current_package;
        std::vector<std::string> current_processes;

        for (int i = start_index; i < argc; ++i) {
            std::string arg = argv[i];
            
            // 如果参数包含冒号，则为进程名
            if (arg.find(':') != std::string::npos) {
                if (!current_package.empty()) {
                    current_processes.push_back(arg);
                }
            } else {
                // 如果已有包名和进程，保存它们
                if (!current_package.empty() && !current_processes.empty()) {
                    result.emplace_back(current_package, current_processes);
                    current_processes.clear();
                }
                current_package = arg;
            }
        }

        // 添加最后一组
        if (!current_package.empty() && !current_processes.empty()) {
            result.emplace_back(current_package, current_processes);
        }

        return result;
    }
};

int main(int argc, char* argv[]) {
    try {
        Logger::init();
        Logger::log("INFO", "Process manager starting...");

        if (argc < 3) {
            Logger::log("ERROR", "Usage: " + std::string(argv[0]) + " [-d] <package_name> <process_name_1> [<process_name_2> ...]");
            return 1;
        }

        int arg_offset = 1;

        if (strcmp(argv[1], "-d") == 0) {
            // daemon_mode is implicitly handled by the fork logic
            arg_offset = 2;

            if (fork() > 0) {
                return 0;
            }
            setsid();
        }

        auto targets = ArgumentParser::parse(argc, argv, arg_offset);

        if (targets.empty()) {
            Logger::log("ERROR", "No valid targets specified");
            return 1;
        }

        ProcessManager manager(targets);
        manager.start();

    } catch (const std::exception& e) {
        Logger::log("ERROR", "Fatal error: " + std::string(e.what()));
        return 1;
    }

    Logger::log("INFO", "Process manager shutting down");
    Logger::close();
    return 0;
}
