#include <iostream>
#include <string>
#include <vector>
#include <chrono>
#include <atomic>
#include <mutex>
#include <array>
#include <format>
#include <thread>
// 头文件你们好啊，我要开始()了
#include <nlohmann/json.hpp>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
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
        static constexpr size_t BUFFER_RESERVE_SIZE = 4096;
        static constexpr size_t BUFFER_SIZE_THRESHOLD = 32;
        static constexpr size_t MAX_BUFFER_AGE_MS = 30000;  // 延长自动刷新间隔到30秒
        static constexpr size_t MAX_LOG_SIZE = 2 * 1024 * 1024;
        static constexpr size_t MAX_LOG_FILES = 3;  // 保留3个日志文件
        
        static int log_fd;
        static std::string log_buffer;
        static std::mutex buffer_mutex;
        static std::chrono::steady_clock::time_point last_flush;
        static size_t current_log_size;
        static char time_buffer[32];
        static std::atomic<unsigned> message_count;
    
        static const char* getLevelString(Level level) noexcept {
            static const char* const level_strings[] = {
                "DEBUG", "INFO", "WARN", "ERROR", "FATAL"
            };
            return level_strings[static_cast<size_t>(level)];
        }
    
        static void rotateLogFiles() {
            std::string base_path = "/data/adb/modules/DeepSuppressor/logs/process_manager";
            
            // 删除最老的日志文件
            std::string oldest = base_path + "." + std::to_string(MAX_LOG_FILES - 1) + ".log";
            unlink(oldest.c_str());
            
            // 重命名现有日志文件
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
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                now.time_since_epoch()).count() % 1000;
            
            strftime(time_buffer, sizeof(time_buffer), "%Y-%m-%d %H:%M:%S", localtime(&time));
            sprintf(time_buffer + 19, ".%03d", static_cast<int>(ms));
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
            if (message_count >= 50) {  // 增加fsync间隔到50条消息
                fsync(log_fd);
                message_count = 0;
            }
            log_buffer.clear();
            log_buffer.reserve(BUFFER_RESERVE_SIZE);
        }
    
    public:
        static bool init() noexcept {
            log_buffer.reserve(BUFFER_RESERVE_SIZE);
            message_count = 0;
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
            
            message_count++;
            
            auto now = std::chrono::steady_clock::now();
            bool should_flush = 
                level >= Level::ERROR || 
                log_buffer.length() >= BUFFER_RESERVE_SIZE ||
                (now - last_flush) >= std::chrono::milliseconds(MAX_BUFFER_AGE_MS);
    
            if (should_flush) {
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
    char Logger::time_buffer[32];
    std::atomic<unsigned> Logger::message_count{0};
class ProcessManager {
private:
    // 修改学习阈值
    static constexpr int INITIAL_HABIT_THRESHOLD = 30;    // 初始学习阈值
    static constexpr int HABIT_LEARNING_THRESHOLD = 200;  // 最终学习阈值

    // 优化时间常量定义
    static constexpr auto INITIAL_CHECK_INTERVAL = std::chrono::seconds(30);  // 提高初始检测频率
    static constexpr auto MIN_CHECK_INTERVAL = std::chrono::seconds(45);
    static constexpr auto MAX_CHECK_INTERVAL = std::chrono::minutes(15);      // 增加最大间隔
    static constexpr auto SCREEN_OFF_DEEP_SLEEP = std::chrono::minutes(20);   // 增加息屏休眠时间
    static constexpr auto SCREEN_CHECK_INTERVAL = std::chrono::minutes(3);    // 减少屏幕检查间隔
    static constexpr auto PROCESS_CHECK_INTERVAL = std::chrono::minutes(5);   // 减少进程检查间隔

    struct Target {
        std::string package_name;
        std::vector<std::string> process_names;
        bool is_foreground;
        std::chrono::steady_clock::time_point last_background_time;
        int switch_count{0};
        std::chrono::steady_clock::time_point last_switch_time;
        
        Target(std::string pkg, std::vector<std::string> procs)
            : package_name(std::move(pkg))
            , process_names(std::move(procs))
            , is_foreground(true) {}
    };

    // 添加缺失的成员变量
    std::atomic<bool> running{true};
    bool is_screen_on{true};
    std::chrono::seconds current_check_interval{INITIAL_CHECK_INTERVAL};
    std::chrono::steady_clock::time_point last_screen_check;
    std::chrono::steady_clock::time_point last_process_check_time;
    std::vector<Target> targets;
    
    struct UserHabits {
        int foreground_duration_avg{0};    
        int background_duration_avg{0};    
        int screen_on_duration_avg{0};     
        int app_switch_frequency{0};       
        int background_check_hits{0};      
        int habit_samples{0};              
        std::chrono::system_clock::time_point last_update;
        double learning_weight{0.7};       // 添加学习权重
    } habits;

    static std::string executeCommand(const std::string& cmd) {
        std::array<char, 128> buffer;
        std::string result;
        
        FILE* pipe = popen(cmd.c_str(), "r");
        if (!pipe) {
            Logger::log(Logger::Level::ERROR, "Failed to execute command: " + cmd);
            return "";
        }
        
        while (!feof(pipe)) {
            if (fgets(buffer.data(), buffer.size(), pipe) != nullptr) {
                result += buffer.data();
            }
        }
        
        pclose(pipe);
        return result;
    }

    bool isScreenOn() {
        std::string cmd = "dumpsys power | grep 'Display Power: state=' | grep -o 'state=.*' | cut -d'=' -f2";
        std::string result = executeCommand(cmd);
        return result.find("ON") != std::string::npos;
    }

    bool shouldCheckProcesses() {
        auto now = std::chrono::steady_clock::now();
        if (now - last_process_check_time >= PROCESS_CHECK_INTERVAL) {
            last_process_check_time = now;
            return true;
        }
        return false;
    }

    bool isProcessForeground(const std::string& package_name) {
        std::string cmd = "dumpsys activity top | grep ACTIVITY";
        std::string result = executeCommand(cmd);
        
        size_t start = result.find("(top: ");
        if (start != std::string::npos) {
            start += 6;
            size_t end = result.find(")", start);
            if (end != std::string::npos) {
                std::string foreground_pkg = result.substr(start, end - start);
                return foreground_pkg == package_name;
            }
        }
        return false;
    }

    // 删除 isProcessRunning 函数，直接修改 killProcess 函数
    void killProcess(const std::string& process_name, const std::string& package_name) {
        std::string cmd = "pkill -9 " + process_name;
        if (system(cmd.c_str()) != 0) {
            cmd = "kill -9 $(pidof " + process_name + ")";
            system(cmd.c_str());
        }
        Logger::log(Logger::Level::INFO, "Killed process: " + process_name + " for package: " + package_name);
    }

    void loadUserHabits() {
        const std::string config_path = "/data/adb/modules/DeepSuppressor/module_settings/user_habits.json";
        int fd = open(config_path.c_str(), O_RDONLY);
        if (fd != -1) {
            try {
                std::string content;
                char buffer[4096];
                ssize_t bytes_read;
                while ((bytes_read = read(fd, buffer, sizeof(buffer))) > 0) {
                    content.append(buffer, bytes_read);
                }
                ::close(fd);

                auto j = nlohmann::json::parse(content);
                habits.foreground_duration_avg = j["foreground_duration_avg"];
                habits.background_duration_avg = j["background_duration_avg"];
                habits.screen_on_duration_avg = j["screen_on_duration_avg"];
                habits.app_switch_frequency = j["app_switch_frequency"];
                habits.background_check_hits = j["background_check_hits"];
                habits.habit_samples = j["habit_samples"];
                habits.last_update = std::chrono::system_clock::from_time_t(j["last_update"]);
                
                updateCheckInterval();
            } catch (...) {
                Logger::log(Logger::Level::WARN, "Failed to load user habits, using defaults");
            }
        }
    }

    void saveUserHabits() {
        const std::string config_path = "/data/adb/modules/DeepSuppressor/module_settings/user_habits.json";
        int fd = open(config_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd != -1) {
            try {
                nlohmann::json j;
                j["foreground_duration_avg"] = habits.foreground_duration_avg;
                j["background_duration_avg"] = habits.background_duration_avg;
                j["screen_on_duration_avg"] = habits.screen_on_duration_avg;
                j["app_switch_frequency"] = habits.app_switch_frequency;
                j["background_check_hits"] = habits.background_check_hits;
                j["habit_samples"] = habits.habit_samples;
                j["last_update"] = std::chrono::system_clock::to_time_t(habits.last_update);
                
                std::string json_str = j.dump(4);
                write(fd, json_str.c_str(), json_str.length());
                fsync(fd);
            } catch (...) {
                Logger::log(Logger::Level::ERROR, "Failed to save user habits");
            }
            ::close(fd);
        } else {
            Logger::log(Logger::Level::ERROR, "Failed to open habits file for writing");
        }
    }

    void updateCheckInterval() {
        if (habits.habit_samples < INITIAL_HABIT_THRESHOLD) {
            current_check_interval = INITIAL_CHECK_INTERVAL;
            return;
        }

        double learning_progress = std::min(1.0, static_cast<double>(habits.habit_samples) / HABIT_LEARNING_THRESHOLD);
        double hit_rate = static_cast<double>(habits.background_check_hits) / habits.habit_samples;
        
        int base_interval = static_cast<int>(
            MIN_CHECK_INTERVAL.count() + 
            (MAX_CHECK_INTERVAL - MIN_CHECK_INTERVAL).count() * learning_progress
        );

        if (hit_rate < 0.3) {
            base_interval *= 1.5;  // 低命中率，增加间隔
        } else if (hit_rate > 0.7) {
            base_interval *= 0.8;  // 高命中率，减少间隔
        }

        if (!is_screen_on) {
            base_interval *= 4;  // 息屏时显著增加间隔
        }

        current_check_interval = std::chrono::seconds(
            std::min(std::max(base_interval, 
                            static_cast<int>(MIN_CHECK_INTERVAL.count())),
                    static_cast<int>(MAX_CHECK_INTERVAL.count()))
        );
    }

    void updateHabits(const Target& target, bool is_foreground, int duration_seconds) {
        // 动态调整学习权重
        if (habits.habit_samples >= INITIAL_HABIT_THRESHOLD) {
            habits.learning_weight = std::max(0.3, habits.learning_weight - 0.02);
        }
        
        if (is_foreground) {
            habits.foreground_duration_avg = static_cast<int>(
                habits.foreground_duration_avg * (1 - habits.learning_weight) + 
                duration_seconds * habits.learning_weight
            );
        } else {
            habits.background_duration_avg = static_cast<int>(
                habits.background_duration_avg * (1 - habits.learning_weight) + 
                duration_seconds * habits.learning_weight
            );
        }

        auto now = std::chrono::steady_clock::now();
        if (target.last_switch_time != std::chrono::steady_clock::time_point()) {
            auto hours = std::chrono::duration_cast<std::chrono::hours>(now - target.last_switch_time).count();
            if (hours > 0) {
                habits.app_switch_frequency = static_cast<int>(
                    habits.app_switch_frequency * (1 - habits.learning_weight) + 
                    (target.switch_count / hours) * habits.learning_weight
                );
            }
        }

        habits.habit_samples++;
        habits.last_update = std::chrono::system_clock::now();
        
        // 减少写入频率
        if (habits.habit_samples % 5 == 0 || habits.habit_samples < INITIAL_HABIT_THRESHOLD) {
            saveUserHabits();
        }
        updateCheckInterval();
    }

public:
    explicit ProcessManager(const std::vector<std::pair<std::string, std::vector<std::string>>>& initial_targets) {
        for (const auto& [pkg, procs] : initial_targets) {
            if (!pkg.empty() && !procs.empty()) {
                targets.emplace_back(pkg, procs);
                Logger::log(Logger::Level::INFO, std::format("Added target: {} with {} processes", 
                    pkg, procs.size()));
            }
        }
    }

    void start() {
        Logger::log(Logger::Level::INFO, std::format("Process manager started with {} targets", targets.size()));
        loadUserHabits();
        last_screen_check = std::chrono::steady_clock::now();
        last_process_check_time = std::chrono::steady_clock::now();
        
        while (running) {
            auto now = std::chrono::steady_clock::now();
            
            // 优化屏幕状态检查逻辑
            if (now - last_screen_check >= SCREEN_CHECK_INTERVAL) {
                bool previous_screen_state = is_screen_on;
                is_screen_on = isScreenOn();
                last_screen_check = now;
                
                if (!is_screen_on && previous_screen_state) {
                    Logger::log(Logger::Level::INFO, "Screen turned off, entering deep sleep mode");
                    // 屏幕关闭时立即清理所有后台进程
                    for (auto& target : targets) {
                        if (!target.is_foreground) {
                            for (const auto& proc : target.process_names) {
                                killProcess(proc, target.package_name);
                            }
                        }
                    }
                    std::this_thread::sleep_for(SCREEN_OFF_DEEP_SLEEP);
                    continue;
                }
            }

            if (!is_screen_on) {
                std::this_thread::sleep_for(SCREEN_CHECK_INTERVAL);
                continue;
            }

            bool any_active = false;
            bool check_processes = shouldCheckProcesses();
            
            for (auto& target : targets) {
                try {
                    if (check_processes || target.is_foreground) {
                        bool current_foreground = isProcessForeground(target.package_name);
                        
                        if (current_foreground) {
                            any_active = true;
                            if (!target.is_foreground) {
                                target.is_foreground = true;
                                target.last_background_time = now;
                                target.switch_count++;
                                target.last_switch_time = now;
                                updateHabits(target, true, 
                                    std::chrono::duration_cast<std::chrono::seconds>(
                                        now - target.last_background_time).count());
                                Logger::log(Logger::Level::INFO, "Package " + target.package_name + " moved to foreground");
                            }
                        } else if (target.is_foreground) {
                            target.is_foreground = false;
                            target.last_background_time = now;
                            target.switch_count++;
                            target.last_switch_time = now;
                            updateHabits(target, false, 
                                std::chrono::duration_cast<std::chrono::seconds>(
                                    now - target.last_background_time).count());
                            Logger::log(Logger::Level::INFO, "Package " + target.package_name + " moved to background");
                        }

                        if (!target.is_foreground && check_processes) {
                            auto background_duration = now - target.last_background_time;
                            if (background_duration >= std::chrono::minutes(3)) {
                                bool should_kill = true;
                                // 优化保活判断逻辑
                                if (habits.habit_samples >= HABIT_LEARNING_THRESHOLD) {
                                    auto avg_switch_time = std::chrono::seconds(habits.background_duration_avg);
                                    auto switch_frequency = habits.app_switch_frequency;
                                    
                                    // 如果应用切换频繁或者在平均后台时间内，不杀死进程
                                    if (background_duration < avg_switch_time * 1.5 || 
                                        switch_frequency > 10) {  // 每小时切换超过10次认为是频繁使用
                                        should_kill = false;
                                    }
                                }
                                
                                if (should_kill) {
                                    for (const auto& proc : target.process_names) {
                                        killProcess(proc, target.package_name);
                                    }
                                    habits.background_check_hits++;
                                    updateCheckInterval();
                                }
                            }
                        }
                    }
                } catch (const std::exception& e) {
                    Logger::log(Logger::Level::ERROR, "Error processing target " + target.package_name + ": " + std::string(e.what()));
                }
            }

            if (any_active) {
                std::this_thread::sleep_for(current_check_interval);
            } else {
                std::this_thread::sleep_for(SCREEN_CHECK_INTERVAL);
            }
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
        Logger::log(Logger::Level::INFO, "Process manager starting...");

        if (argc < 3) {
            Logger::log(Logger::Level::ERROR, std::format("Usage: {} [-d] <package_name> <process_name_1> [<process_name_2> ...]", 
                argv[0]));
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
            Logger::log(Logger::Level::ERROR, "No valid targets specified");
            return 1;
        }

        ProcessManager manager(targets);
        manager.start();

    } catch (const std::exception& e) {
        Logger::log(Logger::Level::ERROR, "Fatal error: " + std::string(e.what()));
        return 1;
    }

    Logger::log(Logger::Level::INFO, "Process manager shutting down");
    Logger::close();
    return 0;
}
