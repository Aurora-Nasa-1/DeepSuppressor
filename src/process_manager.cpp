#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include <vector>
#include <sstream>
#include <cstdio>
#include <cstdlib>
#include <unistd.h>
#include <algorithm>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <memory>
#include <functional>
#include <fstream>
#include <deque>
#include <cstdarg>

// 日志工具类
class Logger {
private:
    static std::ofstream log_file;
    static std::deque<std::string> log_buffer;
    static std::mutex log_mutex;
    static const size_t BUFFER_SIZE = 20;

    static void flushBuffer() {
        if (log_buffer.empty()) return;
        
        if (!log_file.is_open()) {
            log_file.open("/data/adb/modules/AMMF/logs/process_manager.log", 
                         std::ios::out | std::ios::app);
            if (!log_file) return;
        }
        
        for (const auto& msg : log_buffer) {
            log_file << msg << std::endl;
        }
        log_buffer.clear();
        log_file.flush();
    }

public:
    static void init() {
        std::lock_guard<std::mutex> lock(log_mutex);
        log_file.open("/data/adb/modules/AMMF/logs/process_manager.log", 
                     std::ios::out | std::ios::trunc);
    }

    static void shutdown() {
        std::lock_guard<std::mutex> lock(log_mutex);
        flushBuffer();
        if (log_file.is_open()) {
            log_file.close();
        }
    }

    static void info(const char* format, ...) {
        va_list args;
        va_start(args, format);
        log("[INFO] ", format, args);
        va_end(args);
    }

    static void error(const char* format, ...) {
        va_list args;
        va_start(args, format);
        log("[ERROR] ", format, args, stderr);
        va_end(args);
    }

    static void debug(const char* format, ...) {
        va_list args;
        va_start(args, format);
        log("[DEBUG] ", format, args);
        va_end(args);
    }

private:
    static void log(const char* prefix, const char* format, va_list args, bool is_error = false) {
        char buffer[1024];
        vsnprintf(buffer, sizeof(buffer), format, args);
        
        std::string message = prefix + std::string(buffer);
        
        std::lock_guard<std::mutex> lock(log_mutex);
        log_buffer.push_back(message);
        
        if (log_buffer.size() >= BUFFER_SIZE || is_error) {
            flushBuffer();
        }
    }
};

// 系统命令执行器
class CommandExecutor {
public:
    static std::string execute(const std::string& cmd) {
        std::string result;
        char buffer[512];
        
        std::unique_ptr<FILE, std::function<void(FILE*)>> pipe(
            popen(cmd.c_str(), "r"),
            [](FILE* p) { if(p) pclose(p); }
        );

        if (!pipe) {
            Logger::error("Failed to execute command: %s", cmd.c_str());
            return result;
        }

        while (fgets(buffer, sizeof(buffer), pipe.get()) != nullptr) {
            result += buffer;
        }

        return result;
    }
};

// 目标进程类
class Target {
public:
    Target(std::string pkg, std::vector<std::string> procs)
        : package_name(std::move(pkg))
        , process_names(std::move(procs))
        , is_foreground(true)
        , needs_check(true) {}

    std::string package_name;
    std::vector<std::string> process_names;
    bool is_foreground;
    std::chrono::steady_clock::time_point last_background_time;
    bool needs_check;
};

// 进程管理器类
class ProcessManager {
public:
    ProcessManager(std::vector<std::pair<std::string, std::vector<std::string>>> initial_targets) 
        : should_run(true)  // Initialize atomic bool
    {
        std::lock_guard<std::mutex> lock(mutex);
        targets.reserve(initial_targets.size());
        for (const auto& [pkg, procs] : initial_targets) {
            targets.emplace_back(pkg, procs);
        }
        Logger::info("Process Manager initialized with %zu targets", targets.size());
    }

    void start() {
        Logger::info("Starting process manager...");
        
        auto next_check_time = std::chrono::steady_clock::now();
        bool is_idle = false;

        while (should_run) {
            waitForNextCheck(next_check_time);
            if (!should_run) break;

            auto current_time = std::chrono::steady_clock::now();
            is_idle = processAllTargets(current_time);
            next_check_time = current_time + (is_idle ? IDLE_CHECK_INTERVAL : CHECK_INTERVAL);
        }
    }

    void stop() {
        {
            std::lock_guard<std::mutex> lock(mutex);
            should_run = false;
        }
        cv.notify_one();
    }

private:
    static constexpr auto BACKGROUND_THRESHOLD = std::chrono::seconds(10);
    static constexpr auto CHECK_INTERVAL = std::chrono::seconds(5);
    static constexpr auto IDLE_CHECK_INTERVAL = std::chrono::seconds(30);

    std::vector<Target> targets;
    std::atomic<bool> should_run;
    std::mutex mutex;
    std::condition_variable cv;

    void waitForNextCheck(const std::chrono::steady_clock::time_point& next_check_time) {
        std::unique_lock<std::mutex> lock(mutex);
        cv.wait_until(lock, next_check_time);
    }

    bool processAllTargets(const std::chrono::steady_clock::time_point& current_time) {
        bool has_active_targets = false;
        for (auto& target : targets) {
            if (target.needs_check) {
                has_active_targets = true;
                processTarget(target, current_time);
            }
        }
        return !has_active_targets;
    }

    void processTarget(Target& target, const std::chrono::steady_clock::time_point& current_time) {
        try {
            bool is_foreground = checkForegroundState(target.package_name);
            updateTargetState(target, is_foreground, current_time);
            handleBackgroundServices(target, current_time);
        } catch (const std::exception& e) {
            Logger::error("Error processing %s: %s", target.package_name.c_str(), e.what());
        }
    }

    bool checkForegroundState(const std::string& package_name) {
        // 检查前台活动
        std::string cmd = "dumpsys activity activities | grep -E 'mResumedActivity|mFocusedActivity' | grep " + package_name;
        if (!CommandExecutor::execute(cmd).empty()) {
            return true;
        }

        // 检查前台服务
        cmd = "dumpsys activity services " + package_name;
        return CommandExecutor::execute(cmd).find("isForeground=true") != std::string::npos;
    }

    void updateTargetState(Target& target, bool is_foreground, 
                          const std::chrono::steady_clock::time_point& current_time) {
        if (is_foreground == target.is_foreground) return;

        if (!is_foreground) {
            Logger::info("Package %s moved to background", target.package_name.c_str());
            target.is_foreground = false;
            target.last_background_time = current_time;
            target.needs_check = true;
        } else {
            Logger::info("Package %s returned to foreground", target.package_name.c_str());
            target.is_foreground = true;
            target.needs_check = false;
        }
    }

    // 在Logger类定义之后添加静态成员初始化
    std::ofstream Logger::log_file;
    std::deque<std::string> Logger::log_buffer;
    std::mutex Logger::log_mutex;

    // 修复handleBackgroundServices方法
    void handleBackgroundServices(Target& target, 
                                const std::chrono::steady_clock::time_point& current_time) {
        if (!target.is_foreground && target.needs_check) {
            auto background_duration = current_time - target.last_background_time;
            if (background_duration >= BACKGROUND_THRESHOLD) {
                for (const auto& process_name : target.process_names) {
                    if (isProcessRunning(process_name)) {
                        Logger::info("Killing background services for %s", target.package_name.c_str());
                        killBackgroundServices(target);
                        break;  // 只要有一个进程在运行就处理
                    }
                }
                target.needs_check = false;
            }
        }
    }

    bool isProcessRunning(const std::string& process_name) {
        std::string cmd = "ps -A | grep \"" + process_name + "\" | grep -v grep";
        return !CommandExecutor::execute(cmd).empty();
    }

    void killBackgroundServices(const Target& target) {
        for (const auto& process_name : target.process_names) {
            std::string cmd = "ps -ef | grep \"" + process_name + "\" | grep \":service\" | grep -v grep";
            std::string result = CommandExecutor::execute(cmd);
            
            std::istringstream iss(result);
            std::string line;
            
            while (std::getline(iss, line)) {
                size_t pos = line.find(' ');
                if (pos != std::string::npos) {
                    std::string pid = line.substr(0, pos);
                    if (!pid.empty()) {
                        std::string kill_cmd = "kill -9 " + pid;
                        if (system(kill_cmd.c_str()) == 0) {
                            Logger::info("Killed background service PID %s for %s", 
                                       pid.c_str(), process_name.c_str());
                        }
                    }
                }
            }
        }
    }
};

int main(int argc, char* argv[]) {
    if (argc < 3) {
        Logger::error("Usage: %s <package_name> <process_name_1> [<process_name_2> ...] [<package_name_2> <process_name_2_1> ...]", argv[0]);
        return 1;
    }

    std::vector<std::pair<std::string, std::vector<std::string>>> targets;
    int i = 1;
    while (i < argc) {
        std::string package_name = argv[i++];
        std::vector<std::string> process_names;
        
        // 收集所有进程名，直到遇到下一个包名或参数结束
        while (i < argc && strchr(argv[i], '.') == nullptr) {  // 假设包名中包含点号，进程名不包含
            process_names.push_back(argv[i++]);
        }
        
        if (process_names.empty()) {
            Logger::error("No process names specified for package %s", package_name.c_str());
            return 1;
        }
        
        targets.emplace_back(package_name, process_names);
        Logger::info("Added target: Package=%s with %zu processes", 
                    package_name.c_str(), process_names.size());
        for (const auto& proc : process_names) {
            Logger::info("  - Process: %s", proc.c_str());
        }
    }

    try {
        ProcessManager manager(std::move(targets));
        manager.start();
    } catch (const std::exception& e) {
        Logger::error("Fatal error: %s", e.what());
        return 1;
    } catch (...) {
        Logger::error("Unknown fatal error");
        return 1;
    }

    Logger::shutdown();  // 添加这行
    return 0;
}