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

// 日志宏定义
#define LOGI(...) do { printf("[INFO] "); printf(__VA_ARGS__); printf("\n"); fflush(stdout); } while(0)
#define LOGE(...) do { fprintf(stderr, "[ERROR] "); fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n"); fflush(stderr); } while(0)
#define LOGD(...) do { printf("[DEBUG] "); printf(__VA_ARGS__); printf("\n"); fflush(stdout); } while(0)

class ProcessManager {
private:
    struct Target {
        std::string package_name;
        std::string process_name;
        bool is_foreground;
        std::chrono::steady_clock::time_point last_background_time;
        bool needs_check;

        Target(std::string pkg, std::string proc)
            : package_name(std::move(pkg))
            , process_name(std::move(proc))
            , is_foreground(true)
            , needs_check(true) {}
    };

    std::vector<Target> targets;
    std::atomic<bool> should_run{true};
    std::mutex mutex;
    std::condition_variable cv;

    // 配置常量
    static constexpr auto BACKGROUND_THRESHOLD = std::chrono::seconds(10);
    static constexpr auto CHECK_INTERVAL = std::chrono::seconds(5);
    static constexpr auto IDLE_CHECK_INTERVAL = std::chrono::seconds(30);

    std::string execute_command(const std::string& cmd) {
        std::string result;
        char buffer[512];
        FILE* pipe = popen(cmd.c_str(), "r");
        
        if (!pipe) {
            LOGE("Failed to execute command: %s", cmd.c_str());
            return result;
        }

        try {
            while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
                result += buffer;
            }
        } catch (...) {
            pclose(pipe);
            throw;
        }

        pclose(pipe);
        return result;
    }

    bool is_process_running(const std::string& process_name) {
        std::string cmd = "ps -A | grep \"" + process_name + "\" | grep -v grep";
        return !execute_command(cmd).empty();
    }

    bool check_foreground_state(const std::string& package_name) {
        // 检查是否在前台活动列表中
        std::string cmd = "dumpsys activity activities | grep -E 'mResumedActivity|mFocusedActivity' | grep " + package_name;
        if (!execute_command(cmd).empty()) {
            return true;
        }

        // 检查是否有活跃服务
        cmd = "dumpsys activity services " + package_name;
        std::string result = execute_command(cmd);
        return result.find("isForeground=true") != std::string::npos;
    }

    void kill_background_services(const Target& target) {
        std::string cmd = "ps -ef | grep \"" + target.process_name + "\" | grep \":service\" | grep -v grep";
        std::string result = execute_command(cmd);
        
        std::istringstream iss(result);
        std::string line;
        
        while (std::getline(iss, line)) {
            size_t pos = line.find(' ');
            if (pos != std::string::npos) {
                std::string pid = line.substr(0, pos);
                if (!pid.empty()) {
                    std::string kill_cmd = "kill -9 " + pid;
                    if (system(kill_cmd.c_str()) == 0) {
                        LOGI("Killed background service PID %s for %s", 
                             pid.c_str(), target.process_name.c_str());
                    }
                }
            }
        }
    }

    void process_target(Target& target, const std::chrono::steady_clock::time_point& current_time) {
        try {
            bool is_foreground = check_foreground_state(target.package_name);
            
            if (is_foreground != target.is_foreground) {
                if (!is_foreground) {
                    LOGI("Package %s moved to background", target.package_name.c_str());
                    target.is_foreground = false;
                    target.last_background_time = current_time;
                    target.needs_check = true;
                } else {
                    LOGI("Package %s returned to foreground", target.package_name.c_str());
                    target.is_foreground = true;
                    target.needs_check = false;
                }
            }

            if (!target.is_foreground && target.needs_check) {
                auto background_duration = current_time - target.last_background_time;
                if (background_duration >= BACKGROUND_THRESHOLD) {
                    if (is_process_running(target.process_name)) {
                        LOGI("Killing background services for %s", target.package_name.c_str());
                        kill_background_services(target);
                    }
                    target.needs_check = false;
                }
            }
        } catch (const std::exception& e) {
            LOGE("Error processing %s: %s", target.package_name.c_str(), e.what());
        }
    }

public:
    explicit ProcessManager(std::vector<std::pair<std::string, std::string>> initial_targets) {
        targets.reserve(initial_targets.size());
        for (const auto& [pkg, proc] : initial_targets) {
            targets.emplace_back(pkg, proc);
        }
        LOGI("Process Manager initialized with %zu targets", targets.size());
    }

    void start() {
        LOGI("Starting process manager...");
        
        auto next_check_time = std::chrono::steady_clock::now();
        bool is_idle = false;

        while (should_run) {
            {
                std::unique_lock<std::mutex> lock(mutex);
                cv.wait_until(lock, next_check_time);
                if (!should_run) break;
            }

            auto current_time = std::chrono::steady_clock::now();
            bool has_active_targets = false;

            for (auto& target : targets) {
                if (target.needs_check) {
                    has_active_targets = true;
                    process_target(target, current_time);
                }
            }

            is_idle = !has_active_targets;
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
};

int main(int argc, char* argv[]) {
    if (argc < 3 || argc % 2 == 0) {
        LOGE("Usage: %s <package_name_1> <process_name_1> [<package_name_2> <process_name_2> ...]", argv[0]);
        return 1;
    }

    std::vector<std::pair<std::string, std::string>> targets;
    for (int i = 1; i < argc; i += 2) {
        targets.emplace_back(argv[i], argv[i + 1]);
        LOGI("Added target: Package=%s, Process=%s", argv[i], argv[i + 1]);
    }

    try {
        ProcessManager manager(std::move(targets));
        manager.start();
    } catch (const std::exception& e) {
        LOGE("Fatal error: %s", e.what());
        return 1;
    } catch (...) {
        LOGE("Unknown fatal error");
        return 1;
    }

    return 0;
}