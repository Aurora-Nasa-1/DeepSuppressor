#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include <vector>
#include <sstream>
#include <cstdio>    // For popen, pclose, fgets, printf, fprintf
#include <cstdlib>   // For system, atoi
#include <unistd.h>  // For sleep (though std::this_thread::sleep_for is used)
#include <vector>    // Include vector for storing targets
#include <utility>   // Include utility for std::pair and std::move
#include <map>       // Include map for tracking state per target

// 使用标准输出和标准错误替代Android日志
#define LOGI(...) do { printf("[INFO] "); printf(__VA_ARGS__); printf("\n"); fflush(stdout); } while(0)
#define LOGE(...) do { fprintf(stderr, "[ERROR] "); fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n"); fflush(stderr); } while(0)
#define LOGW(...) do { fprintf(stderr, "[WARN] "); fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n"); fflush(stderr); } while(0)

// Helper function to execute a command and get its output
std::string execute_command(const std::string& cmd) {
    std::string result = "";
    char buffer[256];
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
        LOGE("Failed to execute command: %s", cmd.c_str());
        return "";
    }
    try {
        while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
            result += buffer;
        }
    } catch (...) {
        pclose(pipe);
        LOGE("Exception while reading command output for: %s", cmd.c_str());
        throw;
    }
    int return_code = pclose(pipe);
    // popen/pclose return code handling can be complex, focusing on output for now
    // A non-zero exit status from grep when nothing is found is expected, not necessarily an error.
    return result;
}

// 结构体用于存储每个监控目标的信息和状态
struct TargetInfo {
    std::string package_name;
    std::string process_name;
    bool is_foreground;
    std::chrono::steady_clock::time_point background_transition_time;
    bool kill_pending;

    TargetInfo(std::string pkg, std::string proc)
        : package_name(std::move(pkg)),
          process_name(std::move(proc)),
          is_foreground(true), // 初始假设为前台
          kill_pending(false) {}
};


class ProcessManager {
private:
    std::vector<TargetInfo> targets; // 存储所有监控目标

    // Configuration constants using C++11 chrono literals
    // 这些常量现在是全局的，适用于所有目标
    static constexpr auto CHECK_FREQUENCY = std::chrono::seconds(30);
    static constexpr auto KILL_DELAY = std::chrono::seconds(2);
    // 注意：BACKGROUND_CHECK_INTERVAL 可能不再需要，因为我们总是以 CHECK_FREQUENCY 检查所有目标
    // 但保留它，以防将来需要不同的逻辑
    static constexpr auto BACKGROUND_CHECK_INTERVAL = std::chrono::seconds(60); // 或者可以移除或调整逻辑

    // Checks if the specified package is in the foreground
    // 修改为接受包名作为参数
    bool check_package_foreground_state(const std::string& package_name) {
        std::string cmd = "dumpsys activity activities | grep -E 'mResumedActivity|mFocusedActivity' | grep " + package_name;
        std::string result = execute_command(cmd);
        bool is_foreground = !result.empty();
        // 日志可以保持，或者根据需要调整详细程度
        // LOGI("Package %s foreground check: %s", package_name.c_str(), is_foreground ? "Yes" : "No");
        return is_foreground;
    }

    // Kills the target process using pkill
    // 修改为接受进程名作为参数
    void kill_target_process(const std::string& process_name) {
        std::string cmd = "pkill -9 -f \"" + process_name + "\""; // 为进程名加上引号以处理特殊字符
        LOGI("Attempting to kill process matching: %s", process_name.c_str());
        int result = system(cmd.c_str());
        if (result == 0) {
            LOGI("Successfully sent SIGKILL to process(es) matching: %s", process_name.c_str());
        } else {
            // pkill 在找不到进程时通常返回 1，这不一定是错误
             if (result != 1) { // 只记录非“未找到”的错误码
                 LOGW("pkill command for '%s' finished with exit code %d.", process_name.c_str(), result);
             } else {
                 LOGI("Process matching '%s' not found (or already killed).", process_name.c_str());
             }
        }
    }

public:
    // 构造函数接受一个 TargetInfo 的向量
    ProcessManager(std::vector<TargetInfo> initial_targets)
        : targets(std::move(initial_targets))
    {
        LOGI("Process Manager initialized with %zu targets.", targets.size());
        // 对每个目标执行初始状态检查
        for (auto& target : targets) {
            target.is_foreground = check_package_foreground_state(target.package_name);
            LOGI("Initial state for target (Package: %s, Process: %s): %s",
                 target.package_name.c_str(), target.process_name.c_str(),
                 target.is_foreground ? "Foreground" : "Background");
        }
    }

    // Main monitoring loop - 现在迭代处理所有目标
    void monitor_and_control() {
        LOGI("Starting combined monitoring loop...");
        using namespace std::chrono_literals;
        auto next_check_time = std::chrono::steady_clock::now();

        while (true) {
            auto current_time = std::chrono::steady_clock::now();
            // 等待直到下一个检查时间点
            if (current_time < next_check_time) {
                std::this_thread::sleep_until(next_check_time);
                current_time = std::chrono::steady_clock::now(); // 更新当前时间
            }

            // 设置下一次检查的时间
            next_check_time = current_time + CHECK_FREQUENCY;

            LOGI("Performing periodic check..."); // 添加一个周期性检查的日志

            for (auto& target : targets) {
                try {
                    bool current_foreground = check_package_foreground_state(target.package_name);

                    if (current_foreground != target.is_foreground) {
                        // 状态发生变化
                        if (!current_foreground) { // 应用转到后台
                            LOGI("Package %s moved to background. Scheduling kill for process '%s' in %lld seconds.",
                                 target.package_name.c_str(), target.process_name.c_str(),
                                 std::chrono::duration_cast<std::chrono::seconds>(KILL_DELAY).count());
                            target.is_foreground = false;
                            target.kill_pending = true;
                            target.background_transition_time = current_time; // 记录转换时间
                        } else { // 应用回到前台
                            LOGI("Package %s returned to foreground.", target.package_name.c_str());
                            target.is_foreground = true;
                            if (target.kill_pending) {
                                LOGI("Kill pending for process '%s' cancelled.", target.process_name.c_str());
                                target.kill_pending = false; // 取消待处理的 kill
                            }
                        }
                    }

                    // 检查是否有待处理的 kill 并且延迟时间已到
                    if (target.kill_pending && !target.is_foreground) {
                        auto time_since_background = current_time - target.background_transition_time;
                        if (time_since_background >= KILL_DELAY) {
                            LOGI("Kill delay elapsed for package %s. Killing process '%s'.",
                                 target.package_name.c_str(), target.process_name.c_str());
                            // 在杀死之前最后检查一次，以防万一用户在检查间隔内快速切换回来
                            if (!check_package_foreground_state(target.package_name)) {
                                kill_target_process(target.process_name);
                            } else {
                                LOGI("Package %s returned to foreground just before kill. Aborting kill.", target.package_name.c_str());
                                target.is_foreground = true; // 更新状态
                            }
                            target.kill_pending = false; // 重置 kill 标志
                        }
                        // 如果延迟未到，更新下一次检查时间，确保能在延迟结束后尽快检查
                        else {
                             auto potential_next_check = target.background_transition_time + KILL_DELAY;
                             if (potential_next_check < next_check_time) {
                                 next_check_time = potential_next_check;
                             }
                        }
                    }

                } catch (const std::exception& e) {
                    LOGE("Exception processing target (Package: %s, Process: %s): %s",
                         target.package_name.c_str(), target.process_name.c_str(), e.what());
                    // 发生异常时，可以考虑重置该目标的状态或跳过本次检查
                    target.kill_pending = false; // 避免异常导致意外杀死
                } catch (...) {
                    LOGE("Unknown exception processing target (Package: %s, Process: %s)",
                         target.package_name.c_str(), target.process_name.c_str());
                    target.kill_pending = false;
                }
            } // end for loop iterating through targets

            // 主循环不再需要根据前后台状态决定睡眠时间，统一由 next_check_time 控制
            // LOGI("Sleeping until next check..."); // 日志已移到循环开始处

        } // end while(true)
    } // end monitor_and_control
};

int main(int argc, char* argv[]) {
    // 参数格式: ./process_manager <pkg1> <proc1> <pkg2> <proc2> ...
    // 参数数量必须是奇数 (程序名 + N*2 个目标参数) 且至少为 3 (程序名 + 1 对目标)
    if (argc < 3 || argc % 2 == 0) {
        LOGE("Usage: %s <package_name_1> <target_process_name_1> [<package_name_2> <target_process_name_2> ...]", argv[0]);
        fprintf(stderr, "Usage: %s <package_name_1> <target_process_name_1> [<package_name_2> <target_process_name_2> ...]\n", argv[0]);
        return 1;
    }

    std::vector<TargetInfo> targets_to_monitor;
    LOGI("Parsing command line arguments for targets...");
    for (int i = 1; i < argc; i += 2) {
        std::string package_name = argv[i];
        std::string target_process = argv[i+1];
        LOGI("Adding target: Package=%s, Process=%s", package_name.c_str(), target_process.c_str());
        targets_to_monitor.emplace_back(package_name, target_process);
    }

    LOGI("Process Manager starting with %zu targets.", targets_to_monitor.size());

    try {
        ProcessManager manager(std::move(targets_to_monitor));
        manager.monitor_and_control(); // This runs indefinitely
    } catch (const std::exception& e) {
        LOGE("Fatal error during initialization or monitoring: %s", e.what());
        return 1;
    } catch (...) {
        LOGE("Unknown fatal error during initialization or monitoring.");
        return 1;
    }

    // 正常情况下不应到达这里
    LOGI("Process Manager exiting unexpectedly.");
    return 0;
}