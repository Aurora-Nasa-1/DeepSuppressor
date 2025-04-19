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

class Logger {
private:
    static std::ofstream log_file;
    static void ensureDirectoryExists(const std::string& path) {
        std::filesystem::path dir = std::filesystem::path(path).parent_path();
        if (!std::filesystem::exists(dir)) {
            std::filesystem::create_directories(dir);
        }
    }

public:
    static void init() {
        const std::string log_path = "/data/adb/modules/DeepSuppressor/logs/process_manager.log";
        ensureDirectoryExists(log_path);
        log_file.open(log_path, std::ios::out | std::ios::app);
    }

    static void log(const std::string& level, const std::string& message) {
        if (!log_file.is_open()) return;
        
        time_t now = time(nullptr);
        std::string time_str = ctime(&now);
        time_str = time_str.substr(0, time_str.length() - 1); // 移除换行符
        
        log_file << time_str << " [" << level << "] " << message << std::endl;
        log_file.flush();
        
        // 同时输出到控制台
        std::cout << time_str << " [" << level << "] " << message << std::endl;
    }

    static void close() {
        if (log_file.is_open()) {
            log_file.close();
        }
    }
};

std::ofstream Logger::log_file;

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
    static constexpr auto BACKGROUND_THRESHOLD = std::chrono::seconds(10);
    static constexpr auto CHECK_INTERVAL = std::chrono::seconds(5);

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
        std::string cmd = "dumpsys activity activities | grep -E 'mResumedActivity|mFocusedActivity' | grep " + package_name;
        return !exec(cmd).empty();
    }

    bool isProcessRunning(const std::string& process_name) {
        std::string cmd = "ps -A | grep \"" + process_name + "\" | grep -v grep";
        return !exec(cmd).empty();
    }

    void killProcess(const std::string& process_name) {
        if (isProcessRunning(process_name)) {
            std::string cmd = "killall -9 " + process_name;
            if (system(cmd.c_str()) == 0) {
                Logger::log("INFO", "Successfully killed process: " + process_name);
            } else {
                Logger::log("ERROR", "Failed to kill process: " + process_name);
            }
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
                    
                    if (!current_foreground && target.is_foreground) {
                        target.is_foreground = false;
                        target.last_background_time = std::chrono::steady_clock::now();
                        Logger::log("INFO", "Package " + target.package_name + " moved to background");
                    } else if (current_foreground && !target.is_foreground) {
                        target.is_foreground = true;
                        Logger::log("INFO", "Package " + target.package_name + " moved to foreground");
                        continue;
                    }

                    if (!target.is_foreground) {
                        auto now = std::chrono::steady_clock::now();
                        if (now - target.last_background_time >= BACKGROUND_THRESHOLD) {
                            for (const auto& proc : target.process_names) {
                                killProcess(proc);
                            }
                        }
                    }
                } catch (const std::exception& e) {
                    Logger::log("ERROR", "Error processing target " + target.package_name + ": " + e.what());
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

        bool daemon_mode = false;
        int arg_offset = 1;

        if (strcmp(argv[1], "-d") == 0) {
            daemon_mode = true;
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