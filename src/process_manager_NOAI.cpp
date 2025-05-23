#include <iostream>
#include <string>
#include <vector>
#include <chrono>
#include <atomic>
#include <mutex>
#include <array>
#include <thread>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <format>
// 头文件: 杀人啦
class Logger {
public:
    enum class Level {
        DEBUG,
        INFO,
        WARN,
        ERROR,
        FATAL
    };

private:
    static constexpr size_t BUFFER_RESERVE_SIZE = 2048;
    static constexpr size_t MAX_LOG_SIZE = 2 * 1024 * 1024;
    static constexpr size_t MAX_LOG_FILES = 3;
    static constexpr auto FLUSH_INTERVAL = std::chrono::seconds(60);

    static int log_fd;
    static std::string log_buffer;
    static std::mutex buffer_mutex;
    static std::chrono::steady_clock::time_point last_flush;
    static size_t current_log_size;
    static char time_buffer[20];

    static const char* getLevelString(Level level) noexcept {
        static const char* const level_strings[] = {
            "DEBUG", "INFO", "WARN", "ERROR", "FATAL"
        };
        return level_strings[static_cast<size_t>(level)];
    }

    static void rotateLogFiles() noexcept {
        std::string base_path = "/data/adb/modules/DeepSuppressor/logs/process_manager";
        std::string oldest = base_path + "." + std::to_string(MAX_LOG_FILES - 1) + ".log";
        unlink(oldest.c_str());

        for (int i = MAX_LOG_FILES - 2; i >= 0; --i) {
            std::string current = i == 0 ? base_path + ".log" : 
                                 base_path + "." + std::to_string(i) + ".log";
            std::string next = base_path + "." + std::to_string(i + 1) + ".log";
            rename(current.c_str(), next.c_str());
        }
    }

    static void formatTime() noexcept {
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        strftime(time_buffer, sizeof(time_buffer), "%Y-%m-%d %H:%M:%S", localtime(&time));
    }

    static void writeToFile() noexcept {
        if (log_fd == -1 || log_buffer.empty()) return;

        size_t total_size = log_buffer.length();
        current_log_size += total_size;

        if (current_log_size > MAX_LOG_SIZE) {
            ::close(log_fd);
            rotateLogFiles();
            log_fd = open("/data/adb/modules/DeepSuppressor/logs/process_manager.log", 
                         O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (log_fd == -1) return;
            current_log_size = total_size;
        }

        write(log_fd, log_buffer.data(), total_size);
        log_buffer.clear();
        log_buffer.reserve(BUFFER_RESERVE_SIZE);
    }

public:
    static bool init() noexcept {
        log_buffer.reserve(BUFFER_RESERVE_SIZE);
        current_log_size = 0;

        log_fd = open("/data/adb/modules/DeepSuppressor/logs/process_manager.log", 
                     O_WRONLY | O_CREAT | O_APPEND, 0644);
        if (log_fd == -1) return false;

        struct stat st;
        if (fstat(log_fd, &st) == 0) {
            current_log_size = st.st_size;
        }

        last_flush = std::chrono::steady_clock::now();
        return true;
    }

    static void log(Level level, std::string_view message) noexcept {
        if (log_fd == -1) return;

        std::lock_guard<std::mutex> lock(buffer_mutex);
        formatTime();
        log_buffer.append(time_buffer)
                 .append(" [")
                 .append(getLevelString(level))
                 .append("] ")
                 .append(message)
                 .append("\n");

        auto now = std::chrono::steady_clock::now();
        if (level >= Level::ERROR || 
            log_buffer.length() >= BUFFER_RESERVE_SIZE ||
            now - last_flush >= FLUSH_INTERVAL) {
            writeToFile();
            last_flush = now;
        }
    }

    static void close() noexcept {
        std::lock_guard<std::mutex> lock(buffer_mutex);
        if (!log_buffer.empty()) {
            writeToFile();
        }
        if (log_fd != -1) {
            ::close(log_fd);
            log_fd = -1;
        }
    }
};

// 静态成员初始化
int Logger::log_fd = -1;
std::string Logger::log_buffer;
std::mutex Logger::buffer_mutex;
std::chrono::steady_clock::time_point Logger::last_flush;
size_t Logger::current_log_size = 0;
char Logger::time_buffer[20];

class ProcessManager {
private:
    static constexpr auto CHECK_INTERVAL = std::chrono::seconds(60);
    static constexpr auto SCREEN_CHECK_INTERVAL = std::chrono::seconds(60);
    static constexpr auto SCREEN_OFF_DEEP_SLEEP = std::chrono::minutes(30);
    static constexpr auto STARTUP_SCREEN_CHECK_DELAY = std::chrono::minutes(10);

    struct Target {
        std::string package_name;
        std::vector<std::string> process_names;
        bool is_foreground;
        std::chrono::steady_clock::time_point last_background_time;

        Target(std::string pkg, std::vector<std::string> procs)
            : package_name(std::move(pkg))
            , process_names(std::move(procs))
            , is_foreground(true)
            , last_background_time(std::chrono::steady_clock::now()) {}
    };

    std::atomic<bool> running{true};
    bool is_screen_on{true};
    std::vector<Target> targets;
    std::chrono::steady_clock::time_point last_screen_check;
    std::chrono::steady_clock::time_point startup_time;

    static std::string executeCommand(const std::string& cmd) noexcept {
        std::array<char, 128> buffer;
        std::string result;

        FILE* pipe = popen(cmd.c_str(), "r");
        if (!pipe) {
            Logger::log(Logger::Level::ERROR, "Failed to execute command: " + cmd);
            return "";
        }

        while (fgets(buffer.data(), buffer.size(), pipe)) {
            result.append(buffer.data());
        }

        pclose(pipe);
        return result;
    }

    bool isScreenOn() noexcept {
        std::string output = executeCommand("dumpsys display");
        
        // 查找包含 "mScreenState=" 的行
        const std::string target = "mScreenState=";
        size_t pos = output.find(target);
        if (pos == std::string::npos) {
            Logger::log(Logger::Level::WARN, "Failed to find screen state in dumpsys display output");
            return false;
        }
        
        // 获取状态值
        pos += target.length();
        size_t end = output.find('\n', pos);
        if (end == std::string::npos) {
            end = output.length();
        }
        
        // 提取并比较状态值
        std::string state = output.substr(pos, end - pos);
        bool screen_on = state.find("ON") != std::string::npos;
        Logger::log(Logger::Level::INFO, "Screen state detected: " + state + " (is_on=" + (screen_on ? "true" : "false") + ")");
        return screen_on;
    }

    bool isProcessForeground(const std::string& package_name) noexcept {
        std::string output = executeCommand("dumpsys window");
        Logger::log(Logger::Level::DEBUG, 
            "Executing dumpsys window command");
        
        // Log preview of raw output for debugging
        Logger::log(Logger::Level::DEBUG, 
            std::format("Dumpsys output preview: {}", 
            output.substr(0, std::min(size_t(100), output.length()))));

        bool found_focus = false;
        std::string current_pkg;
        
        size_t start = 0;
        size_t end = output.find('\n');

        while (end != std::string::npos) {
            std::string line = output.substr(start, end - start);
            
            // Check both mCurrentFocus and mFocusedWindow
            size_t current_focus_pos = line.find("mCurrentFocus");
            size_t focused_window_pos = line.find("mFocusedWindow");
            
            if (current_focus_pos != std::string::npos || focused_window_pos != std::string::npos) {
                Logger::log(Logger::Level::DEBUG, 
                    std::format("Found focus line: {}", line));
                
                // Look for the pattern "Window{...}" and extract package name from it
                size_t window_pos = line.find("Window{");
                if (window_pos != std::string::npos) {
                    // Find the content inside Window{...}
                    size_t content_start = window_pos + 7; // Skip "Window{"
                    size_t content_end = line.find('}', content_start);
                    
                    if (content_end != std::string::npos) {
                        std::string window_content = line.substr(content_start, content_end - content_start);
                        Logger::log(Logger::Level::DEBUG, 
                            std::format("Window content: {}", window_content));
                        
                        // Find package name pattern: look for the part that contains '/'
                        // Format is usually: "hash u0 package.name/activity.name"
                        size_t slash_pos = window_content.find('/');
                        if (slash_pos != std::string::npos) {
                            // Work backwards from slash to find the start of package name
                            size_t pkg_start = window_content.rfind(' ', slash_pos);
                            if (pkg_start != std::string::npos) {
                                pkg_start++; // Skip the space
                                current_pkg = window_content.substr(pkg_start, slash_pos - pkg_start);
                                found_focus = true;
                                
                                Logger::log(Logger::Level::DEBUG, 
                                    std::format("Extracted package name: {}", current_pkg));
                                    
                                // Return immediately if matching package found
                                if (current_pkg == package_name) {
                                    Logger::log(Logger::Level::DEBUG, 
                                        std::format("Found matching package: {} == {}", 
                                        current_pkg, package_name));
                                    return true;
                                }
                            }
                        }
                    }
                }
            }
            
            start = end + 1;
            end = output.find('\n', start);
        }

        // Log final check result
        Logger::log(Logger::Level::DEBUG, 
            std::format("Focus check result - Found focus: {}, Current package: {}, Target package: {}", 
            found_focus ? "true" : "false", 
            found_focus ? current_pkg : "none", 
            package_name));
            
        return false;
    }

    void killProcess(const std::string& process_name, const std::string& package_name) noexcept {
        std::string cmd = "pkill -9 " + process_name;
        if (system(cmd.c_str()) != 0) {
            cmd = "kill -9 $(pidof " + process_name + ")";
            system(cmd.c_str());
        }
        Logger::log(Logger::Level::INFO, "Killed process: " + process_name + " for package: " + package_name);
    }

    void handleScreenOff() {
        for (auto& target : targets) {
            if (!target.is_foreground) {
                for (const auto& proc : target.process_names) {
                    killProcess(proc, target.package_name);
                }
            }
        }
        std::this_thread::sleep_for(SCREEN_OFF_DEEP_SLEEP);
    }

    void checkProcesses() {
        bool any_active = false;

        for (auto& target : targets) {
            try {
                bool current_foreground = isProcessForeground(target.package_name);
                bool should_kill = false;
                auto now = std::chrono::steady_clock::now();

                if (current_foreground != target.is_foreground) {
                    target.is_foreground = current_foreground;
                    if (!current_foreground) {
                        target.last_background_time = now;
                    }
                    Logger::log(Logger::Level::INFO, "Package " + target.package_name +
                        (current_foreground ? " moved to foreground" : " moved to background"));
                }

                // 添加严格的前台保护：只有在确认不在前台时才考虑杀死进程
                if (!current_foreground && !target.is_foreground) {
                    auto background_duration = now - target.last_background_time;
                    auto kill_threshold = std::chrono::minutes(10); // 默认10分钟后台阈值
                    
                    if (background_duration >= kill_threshold) {
                        should_kill = true;
                        Logger::log(Logger::Level::DEBUG, 
                            std::format("Marking {} for kill - background for {}s, threshold {}s", 
                            target.package_name, 
                            std::chrono::duration_cast<std::chrono::seconds>(background_duration).count(),
                            std::chrono::duration_cast<std::chrono::seconds>(kill_threshold).count()));
                    }
                } else if (current_foreground) {
                    Logger::log(Logger::Level::DEBUG, 
                        std::format("Protecting foreground app: {}", target.package_name));
                }

                if (should_kill) {
                    for (const auto& proc : target.process_names) {
                        killProcess(proc, target.package_name);
                    }
                }

                if (current_foreground) any_active = true;
            } catch (const std::exception& e) {
                Logger::log(Logger::Level::ERROR,
                    std::format("Error processing target {}: {}", target.package_name, e.what()));
            }
        }

        std::this_thread::sleep_for(any_active ? CHECK_INTERVAL : SCREEN_CHECK_INTERVAL);
    }

public:
    explicit ProcessManager(const std::vector<std::pair<std::string, std::vector<std::string>>>& initial_targets) {
        for (const auto& [pkg, procs] : initial_targets) {
            if (!pkg.empty() && !procs.empty()) {
                targets.emplace_back(pkg, procs);
                Logger::log(Logger::Level::INFO, "Added target: " + pkg + " with " + 
                    std::to_string(procs.size()) + " processes");
            }
        }
        startup_time = std::chrono::steady_clock::now();
    }

    void start() {
        Logger::log(Logger::Level::INFO, "Process manager started with " + 
            std::to_string(targets.size()) + " targets");
        last_screen_check = std::chrono::steady_clock::now();

        while (running) {
            auto now = std::chrono::steady_clock::now();

            // 检查屏幕状态（启动后10分钟内禁用）
            bool check_screen = now - startup_time >= STARTUP_SCREEN_CHECK_DELAY;
            if (check_screen && now - last_screen_check >= SCREEN_CHECK_INTERVAL) {
                is_screen_on = isScreenOn();
                last_screen_check = now;

                if (!is_screen_on) {
                    Logger::log(Logger::Level::INFO, "Screen off, entering deep sleep");
                    handleScreenOff();
                    continue;
                }
            }

            if (!is_screen_on && check_screen) {
                std::this_thread::sleep_for(SCREEN_CHECK_INTERVAL);
                continue;
            }

            checkProcesses();
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
            if (arg.find(':') != std::string::npos) {
                if (!current_package.empty()) {
                    current_processes.push_back(arg);
                }
            } else {
                if (!current_package.empty() && !current_processes.empty()) {
                    result.emplace_back(current_package, current_processes);
                    current_processes.clear();
                }
                current_package = arg;
            }
        }

        if (!current_package.empty() && !current_processes.empty()) {
            result.emplace_back(current_package, current_processes);
        }

        return result;
    }
};

int main(int argc, char* argv[]) {
    Logger::init();
    Logger::log(Logger::Level::INFO, "Starting process manager");

    if (argc < 3) {
        Logger::log(Logger::Level::ERROR, "Usage: " + std::string(argv[0]) + 
            " [-d] <package_name> <process_name_1> [<process_name_2> ...]");
        Logger::close();
        return 1;
    }

    int arg_offset = 1;
    if (argc > 1 && std::string(argv[1]) == "-d") {
        arg_offset = 2;
        if (fork() > 0) {
            Logger::close();
            return 0;
        }
        setsid();
    }

    auto targets = ArgumentParser::parse(argc, argv, arg_offset);
    if (targets.empty()) {
        Logger::log(Logger::Level::ERROR, "No valid targets specified");
        Logger::close();
        return 1;
    }

    ProcessManager manager(targets);
    manager.start();

    Logger::log(Logger::Level::INFO, "Shutting down process manager");
    Logger::close();
    return 0;
}