#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include <cstdlib>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>

// Only include Android logging on Android builds
#ifdef __ANDROID__
#include <android/log.h>
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "ProcessManager", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "ProcessManager", __VA_ARGS__)
#else
#define LOGI(...) printf("INFO: " __VA_ARGS__); printf("\n")
#define LOGE(...) fprintf(stderr, "ERROR: " __VA_ARGS__); fprintf(stderr, "\n")
#endif

class ProcessManager {
private:
    std::string target_process;
    bool is_foreground;
    
    // 固定配置参数
    const int CHECK_FREQUENCY = 30;        // 检查频率（秒）
    const int KILL_DELAY_SEC = 2;         // 进入后台后的终止延迟（秒）
    const int BACKGROUND_CHECK_INTERVAL = 60;  // 后台状态下的检查间隔（秒）

    bool check_foreground_state(const std::string& package_name) {
        std::string cmd = "dumpsys activity activities | grep -E 'mResumedActivity|mFocusedActivity' | grep " + package_name;
        FILE* pipe = popen(cmd.c_str(), "r");
        if (!pipe) {
            LOGE("Failed to execute dumpsys command");
            return false;
        }

        char buffer[256];
        std::string result = "";
        while (!feof(pipe)) {
            if (fgets(buffer, sizeof(buffer), pipe) != NULL)
                result += buffer;
        }
        pclose(pipe);
        
        bool is_foreground = !result.empty();
        LOGI("Package %s is %s", package_name.c_str(), is_foreground ? "in foreground" : "in background");
        return is_foreground;
    }

    void kill_target_process(const std::string& process_name) {
        // 转义进程名中的特殊字符
        std::string escaped_name = process_name;
        // 替换冒号为转义后的格式
        size_t pos = 0;
        while ((pos = escaped_name.find(':', pos)) != std::string::npos) {
            escaped_name.replace(pos, 1, "\\:");
            pos += 2;
        }
        
        std::string cmd = "pidof " + escaped_name;
        FILE* pipe = popen(cmd.c_str(), "r");
        if (!pipe) {
            LOGE("Failed to get PID for process: %s", escaped_name.c_str());
            return;
        }
    
        char buffer[128];
        while (fgets(buffer, sizeof(buffer), pipe)) {
            buffer[strcspn(buffer, "\n")] = 0;
            char* token = strtok(buffer, " ");
            while (token != NULL) {
                int pid = atoi(token);
                if (pid > 0) {
                    if (kill(pid, SIGKILL) == 0) {
                        LOGI("Killed process: %s (PID: %d)", escaped_name.c_str(), pid);
                    } else {
                        if (errno != ESRCH) {
                            LOGE("Failed to kill process: %s (PID: %d), errno: %d", 
                                 escaped_name.c_str(), pid, errno);
                        }
                    }
                }
                token = strtok(NULL, " ");
            }
        }
        pclose(pipe);
    }

public:
    ProcessManager(const std::string& process_name)
        : target_process(process_name),
          is_foreground(true)  // 默认假设为前台状态
    {
        LOGI("Process Manager initialized for target: %s", process_name.c_str());
    }

    void monitor_and_control(const std::string& package_name) {
        LOGI("Starting process manager for package: %s", package_name.c_str());

        while (true) {
            try {
                bool current_foreground = check_foreground_state(package_name);

                if (current_foreground != is_foreground) {
                    if (!current_foreground) { // 应用进入后台
                        LOGI("App %s went to background. Waiting %d seconds before killing target process...", 
                             package_name.c_str(), KILL_DELAY_SEC);
                        
                        std::this_thread::sleep_for(std::chrono::seconds(KILL_DELAY_SEC));
                        
                        if (!check_foreground_state(package_name)) {
                            kill_target_process(target_process);
                            is_foreground = false;
                        }
                    } else { // 应用回到前台
                        LOGI("App %s came to foreground", package_name.c_str());
                        is_foreground = true;
                    }
                }

                // 根据前台/后台状态决定检查间隔
                int sleep_duration = is_foreground ? CHECK_FREQUENCY : BACKGROUND_CHECK_INTERVAL;
                std::this_thread::sleep_for(std::chrono::seconds(sleep_duration));

            } catch (const std::exception& e) {
                LOGE("Error in monitor loop: %s", e.what());
                std::this_thread::sleep_for(std::chrono::seconds(CHECK_FREQUENCY));
            } catch (...) {
                LOGE("Unknown error in monitor loop");
                std::this_thread::sleep_for(std::chrono::seconds(CHECK_FREQUENCY));
            }
        }
    }
};

int main(int argc, char* argv[]) {
    if (argc != 3) {
        std::cerr << "Usage: " << argv[0] << " <package_name> <target_process>" << std::endl;
        LOGE("Invalid arguments. Usage: %s <package_name> <target_process>", argv[0]);
        return 1;
    }

    std::string package_name = argv[1];
    std::string target_process = argv[2];

    ProcessManager manager(target_process);
    manager.monitor_and_control(package_name);

    return 0;
}