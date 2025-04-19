#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include <atomic>
#include <cstdio>
#include <unistd.h>

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

    static std::string exec(const char* cmd) {
        char buffer[128];
        std::string result;
        FILE* pipe = popen(cmd, "r");
        if (!pipe) return "";
        
        while (!feof(pipe)) {
            if (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
                result += buffer;
            }
        }
        pclose(pipe);
        return result;
    }

    bool isProcessForeground(const std::string& package_name) {
        std::string cmd = "dumpsys activity activities | grep -E 'mResumedActivity|mFocusedActivity' | grep " + package_name;
        return !exec(cmd.c_str()).empty();
    }

    void killProcess(const std::string& process_name) {
        std::string cmd = "pkill -f " + process_name;
        system(cmd.c_str());
    }

public:
    ProcessManager(std::vector<std::pair<std::string, std::vector<std::string>>> initial_targets) {
        targets.reserve(initial_targets.size());
        for (const auto& [pkg, procs] : initial_targets) {
            targets.emplace_back(pkg, procs);
        }
    }

    void start() {
        while (running) {
            for (auto& target : targets) {
                bool current_foreground = isProcessForeground(target.package_name);
                
                if (!current_foreground && target.is_foreground) {
                    target.is_foreground = false;
                    target.last_background_time = std::chrono::steady_clock::now();
                } else if (current_foreground && !target.is_foreground) {
                    target.is_foreground = true;
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
            }
            std::this_thread::sleep_for(CHECK_INTERVAL);
        }
    }

    void stop() {
        running = false;
    }
};

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " [-d] <package_name> <process_name_1> [<process_name_2> ...]\n";
        return 1;
    }

    bool daemon_mode = false;
    int arg_offset = 1;

    if (strcmp(argv[1], "-d") == 0) {
        daemon_mode = true;
        arg_offset = 2;
        
        if (daemon_mode && fork() > 0) {
            return 0;
        }
    }

    std::vector<std::pair<std::string, std::vector<std::string>>> targets;
    int i = arg_offset;
    
    while (i < argc) {
        std::string package_name = argv[i++];
        std::vector<std::string> process_names;
        
        while (i < argc && strchr(argv[i], '.') == nullptr) {
            process_names.push_back(argv[i++]);
        }
        
        if (!process_names.empty()) {
            targets.emplace_back(package_name, process_names);
        }
    }

    if (targets.empty()) {
        std::cerr << "No valid targets specified\n";
        return 1;
    }

    ProcessManager manager(std::move(targets));
    manager.start();

    return 0;
}