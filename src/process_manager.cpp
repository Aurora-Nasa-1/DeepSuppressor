#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include <vector>
#include <sstream>
#include <cstdio>    // For popen, pclose, fgets, printf, fprintf
#include <cstdlib>   // For system, atoi
#include <unistd.h>  // For sleep (though std::this_thread::sleep_for is used)
// #include <android/log.h> // Removed Android logging

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

class ProcessManager {
private:
    std::string target_process_name;
    std::string associated_package_name;
    bool is_package_foreground;

    // Configuration constants using C++11 chrono literals
    static constexpr auto CHECK_FREQUENCY = std::chrono::seconds(30);
    static constexpr auto KILL_DELAY = std::chrono::seconds(2);
    static constexpr auto BACKGROUND_CHECK_INTERVAL = std::chrono::seconds(60);

    // Checks if the associated package is in the foreground
    bool check_package_foreground_state() {
        // Using dumpsys activity activities is a common way, but output format might vary.
        // Grep for mResumedActivity or mFocusedActivity associated with the package name.
        std::string cmd = "dumpsys activity activities | grep -E 'mResumedActivity|mFocusedActivity' | grep " + associated_package_name;
        std::string result = execute_command(cmd);

        bool is_foreground = !result.empty();
        LOGI("Package %s foreground check: %s", associated_package_name.c_str(), is_foreground ? "Yes" : "No");
        return is_foreground;
    }

    // Kills the target process using pkill
    void kill_target_process() {
        // Use pkill -f to match the full process name/command line.
        // This avoids issues with special characters like ':' and handles multiple instances.
        // SIGKILL is forceful. Consider SIGTERM first if graceful shutdown is desired,
        // but SIGKILL is often used for this type of cleanup.
        std::string cmd = "pkill -9 -f " + target_process_name; // -9 is SIGKILL
        LOGI("Attempting to kill process matching: %s", target_process_name.c_str());

        int result = system(cmd.c_str());

        if (result == 0) {
            LOGI("Successfully sent SIGKILL to process(es) matching: %s", target_process_name.c_str());
        } else {
            LOGW("pkill command for '%s' finished with exit code %d. (Might mean process not found or another issue)", target_process_name.c_str(), result);
        }
    }

public:
    ProcessManager(std::string target_process, std::string package_name)
        : target_process_name(std::move(target_process)),
          associated_package_name(std::move(package_name)),
          is_package_foreground(true) // Assume foreground initially
    {
        LOGI("Process Manager initialized for target process '%s' associated with package '%s'",
             target_process_name.c_str(), associated_package_name.c_str());
        // Perform an initial check to set the correct state
        is_package_foreground = check_package_foreground_state();
        LOGI("Initial state: Package %s is %s",
             associated_package_name.c_str(), is_package_foreground ? "in foreground" : "in background");
    }

    // Main monitoring loop
    void monitor_and_control() {
        LOGI("Starting monitoring loop for package: %s", associated_package_name.c_str());

        using namespace std::chrono_literals; // For s literal

        while (true) {
            try {
                bool current_foreground = check_package_foreground_state();

                if (current_foreground != is_package_foreground) {
                    if (!current_foreground) { // Package went to background
                        LOGI("Package %s moved to background. Waiting %lld seconds before killing '%s'...",
                             associated_package_name.c_str(),
                             std::chrono::duration_cast<std::chrono::seconds>(KILL_DELAY).count(),
                             target_process_name.c_str());

                        std::this_thread::sleep_for(KILL_DELAY);

                        // Re-check state after delay, in case user quickly switched back
                        if (!check_package_foreground_state()) {
                            LOGI("Package %s still in background after delay. Killing process '%s'.",
                                 associated_package_name.c_str(), target_process_name.c_str());
                            kill_target_process();
                            is_package_foreground = false; // Update state only after confirmed kill attempt
                        } else {
                            LOGI("Package %s returned to foreground during kill delay. Aborting kill.", associated_package_name.c_str());
                            is_package_foreground = true; // Update state
                        }
                    } else { // Package came back to foreground
                        LOGI("Package %s returned to foreground.", associated_package_name.c_str());
                        is_package_foreground = true;
                    }
                }

                // Determine sleep duration based on current state
                auto sleep_duration = is_package_foreground ? CHECK_FREQUENCY : BACKGROUND_CHECK_INTERVAL;
                LOGI("Sleeping for %lld seconds...", std::chrono::duration_cast<std::chrono::seconds>(sleep_duration).count());
                std::this_thread::sleep_for(sleep_duration);

            } catch (const std::exception& e) {
                LOGE("Exception in monitor loop: %s. Sleeping before retry.", e.what());
                std::this_thread::sleep_for(CHECK_FREQUENCY); // Default sleep on error
            } catch (...) {
                LOGE("Unknown exception in monitor loop. Sleeping before retry.");
                std::this_thread::sleep_for(CHECK_FREQUENCY); // Default sleep on error
            }
        } // end while(true)
    } // end monitor_and_control
};

int main(int argc, char* argv[]) {
    // Redirect stderr to /dev/null to prevent cluttering Magisk logs if needed,
    // but keep it for now for potential errors from system() or other low-level issues.
    // freopen("/dev/null", "w", stderr);

    if (argc != 3) {
        // Use LOGE for errors that should go to stderr/log file
        LOGE("Usage: %s <package_name> <target_process_name>", argv[0]);
        // Also print to stderr for immediate feedback if run manually from shell
        fprintf(stderr, "Usage: %s <package_name> <target_process_name>\n", argv[0]);
        return 1;
    }

    std::string package_name = argv[1];
    std::string target_process = argv[2];

    LOGI("Process Manager starting. Package: %s, Target Process: %s", package_name.c_str(), target_process.c_str());

    try {
        ProcessManager manager(target_process, package_name);
        manager.monitor_and_control(); // This runs indefinitely
    } catch (const std::exception& e) {
        LOGE("Fatal error during initialization or monitoring: %s", e.what());
        return 1;
    } catch (...) {
        LOGE("Unknown fatal error during initialization or monitoring.");
        return 1;
    }

    LOGI("Process Manager exiting unexpectedly.");
    return 0;
}