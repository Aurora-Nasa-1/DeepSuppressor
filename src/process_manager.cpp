#include <iostream>
#include <string>
#include <vector>
#include <chrono>
#include <atomic>
#include <mutex>
#include <thread>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <android/log.h>
#include <map>
#include <algorithm>
#include <array>
#include <condition_variable>
#include <string.h>
#include <errno.h>
#include <sys/sysinfo.h>

#define LOG_TAG "DeepSuppressor"
#define LOG_INFO(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOG_ERROR(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOG_DEBUG(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)

/**
 * Lightweight Logger using Android logcat
 */
class Logger {
public:
    enum class Level {
        DEBUG,
        INFO,
        ERROR
    };

    static bool init(Level min_level = Level::INFO) noexcept {
        log_level = min_level;
        initialized = true;
        LOG_INFO("Logger initialized with level: %d", static_cast<int>(min_level));
        return true;
    }

    static void setLogLevel(Level level) noexcept {
        log_level = level;
    }

    static void log(Level level, const std::string& message) noexcept {
        if (!initialized || level < log_level) return;

        char time_buffer[32];
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        strftime(time_buffer, sizeof(time_buffer), "%Y-%m-%d %H:%M:%S", localtime(&time));

        const char* level_str = level == Level::DEBUG ? "DEBUG" : level == Level::INFO ? "INFO" : "ERROR";
        std::string log_message = std::string(time_buffer) + " [" + level_str + "] " + message;

        if (level == Level::ERROR) {
            __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, "%s", log_message.c_str());
        } else if (level == Level::DEBUG) {
            __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, "%s", log_message.c_str());
        } else {
            __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "%s", log_message.c_str());
        }
    }

    static void close() noexcept {
        initialized = false;
        LOG_INFO("Logger closed");
    }

private:
    static std::atomic<bool> initialized;
    static Level log_level;
};

std::atomic<bool> Logger::initialized{false};
Logger::Level Logger::log_level = Logger::Level::INFO;

/**
 * Simplified AppStats for reduced memory and computation
 */
struct AppStats {
    int usage_count{0};
    int total_foreground_time{0};
    double importance_weight{0.0};
    int last_usage_hour{-1};

    void updateForegroundTime(int duration, int hour) {
        total_foreground_time += duration;
        usage_count++;
        last_usage_hour = hour;
        updateImportanceWeight();
    }

    void updateImportanceWeight() {
        importance_weight = std::min(100.0, (usage_count * 0.4 + total_foreground_time / 3600.0 * 0.6));
    }
};

/**
 * Simplified UserHabits with minimal data
 */
struct UserHabits {
    std::map<std::string, AppStats> app_stats;
    std::array<double, 24> daily_activity_levels{0};
    bool learning_complete{false};
    int learning_hours{0};

    static constexpr int LEARNING_HOURS_TARGET = 72;

    void updateTimePattern(int hour, double activity) {
        if (hour >= 0 && hour < 24) {
            constexpr double alpha = 0.2;
            daily_activity_levels[hour] = daily_activity_levels[hour] * (1.0 - alpha) + activity * alpha;
        }
    }

    void updateLearningProgress() {
        if (learning_complete) return;
        learning_hours++;
        if (learning_hours >= LEARNING_HOURS_TARGET) {
            learning_complete = true;
        }
    }

    double getCurrentHourActivityLevel() const {
        auto now = std::chrono::system_clock::now();
        auto tt = std::chrono::system_clock::to_time_t(now);
        int hour = localtime(&tt)->tm_hour;
        return daily_activity_levels[hour];
    }
};

/**
 * Lightweight IntervalManager with static intervals
 */
class IntervalManager {
public:
    explicit IntervalManager(const UserHabits& habits) : habits_(habits) {}

    std::chrono::seconds getScreenCheckInterval() const {
        return habits_.learning_complete ? SCREEN_CHECK_INTERVAL : SCREEN_CHECK_INTERVAL / 2;
    }

    std::chrono::seconds getProcessCheckInterval(const std::string& package_name) const {
        auto it = habits_.app_stats.find(package_name);
        if (it == habits_.app_stats.end()) return PROCESS_CHECK_INTERVAL_DEFAULT;
        double importance = it->second.importance_weight;
        long long interval = PROCESS_CHECK_INTERVAL_MAX.count() -
                            (PROCESS_CHECK_INTERVAL_MAX - PROCESS_CHECK_INTERVAL_MIN).count() * (importance / 100.0);
        return std::chrono::seconds(std::max(interval, static_cast<long long>(PROCESS_CHECK_INTERVAL_MIN.count())));
    }

    std::chrono::seconds getKillInterval(const std::string& package_name) const {
        auto it = habits_.app_stats.find(package_name);
        if (it == habits_.app_stats.end()) return KILL_INTERVAL_DEFAULT;
        double importance = it->second.importance_weight;
        long long interval = KILL_INTERVAL_MIN.count() +
                            (KILL_INTERVAL_MAX - KILL_INTERVAL_MIN).count() * (importance / 100.0);
        return std::chrono::seconds(std::min(interval, static_cast<long long>(KILL_INTERVAL_MAX.count())));
    }

private:
    const UserHabits& habits_;
    static constexpr auto SCREEN_CHECK_INTERVAL = std::chrono::seconds(60);
    static constexpr auto PROCESS_CHECK_INTERVAL_MIN = std::chrono::seconds(90);
    static constexpr auto PROCESS_CHECK_INTERVAL_MAX = std::chrono::minutes(3);
    static constexpr auto PROCESS_CHECK_INTERVAL_DEFAULT = std::chrono::minutes(2);
    static constexpr auto KILL_INTERVAL_MIN = std::chrono::minutes(10);
    static constexpr auto KILL_INTERVAL_MAX = std::chrono::minutes(30);
    static constexpr auto KILL_INTERVAL_DEFAULT = std::chrono::minutes(15);
};

/**
 * SystemMonitor using lightweight system calls
 */
class SystemMonitor {
public:
    static bool isMemoryPressureHigh() {
        struct sysinfo info;
        if (sysinfo(&info) != 0) return false;
        double available_percent = (double)info.freeram / info.totalram * 100.0;
        return available_percent < 15.0;
    }

    static bool isBatteryLow() {
        char buffer[128];
        int fd = open("/sys/class/power_supply/battery/capacity", O_RDONLY);
        if (fd < 0) return false;
        ssize_t n = read(fd, buffer, sizeof(buffer) - 1);
        close(fd);
        if (n <= 0) return false;
        buffer[n] = '\0';
        int level = atoi(buffer);
        return level < 20;
    }
};

/**
 * Optimized ProcessManager
 */
class ProcessManager {
private:
    std::atomic<bool> running{true};
    std::atomic<bool> is_screen_on{true};
    std::chrono::steady_clock::time_point last_screen_check;
    std::map<std::string, std::chrono::steady_clock::time_point> last_process_check_times;
    UserHabits habits;
    std::unique_ptr<IntervalManager> interval_manager;
    std::mutex manager_mutex;
    std::condition_variable cv;
    std::thread worker_thread;

    struct Target {
        std::string package_name;
        std::vector<std::string> process_names;
        std::atomic<bool> is_foreground{false};
        std::chrono::steady_clock::time_point last_background_time;
        std::mutex target_mutex;

        Target(std::string pkg, std::vector<std::string> procs)
            : package_name(std::move(pkg)), process_names(std::move(procs)) {}
    };

    std::vector<std::shared_ptr<Target>> targets;

    static std::string executeCommand(const std::string& cmd) {
        std::array<char, 256> buffer;
        std::string result;
        FILE* pipe = popen(cmd.c_str(), "r");
        if (!pipe) {
            LOG_ERROR("Failed to execute command: %s", cmd.c_str());
            return "";
        }
        while (fgets(buffer.data(), buffer.size(), pipe)) {
            result += buffer.data();
        }
        pclose(pipe);
        return result;
    }

    bool checkScreenState() {
        std::string output = executeCommand("getprop sys.boot_completed");
        if (output.find("1") == std::string::npos) return is_screen_on;
        output = executeCommand("dumpsys display | grep mScreenState=");
        bool screen_on = output.find("ON") != std::string::npos;
        if (screen_on != is_screen_on) {
            LOG_INFO("Screen state changed: %s", screen_on ? "ON" : "OFF");
        }
        return screen_on;
    }

    bool shouldCheckProcesses(const std::string& package_name) {
        auto now = std::chrono::steady_clock::now();
        auto it = last_process_check_times.find(package_name);
        if (it == last_process_check_times.end()) {
            last_process_check_times[package_name] = now;
            return true;
        }
        if (now - it->second >= interval_manager->getProcessCheckInterval(package_name)) {
            it->second = now;
            return true;
        }
        return false;
    }

    bool isProcessForeground(const std::string& package_name) {
        std::string output = executeCommand("dumpsys activity | grep mFocusedActivity");
        return output.find(package_name) != std::string::npos;
    }

    void killProcess(const std::string& process_name, const std::string& package_name) {
        std::string cmd = "pkill -9 " + process_name;
        if (system(cmd.c_str()) != 0) {
            cmd = "kill -9 $(pidof " + process_name + ")";
            system(cmd.c_str());
        }
        LOG_INFO("Killed process: %s for package: %s", process_name.c_str(), package_name.c_str());
    }

    void handleScreenOff() {
        LOG_INFO("Screen off, entering power saving mode");
        bool battery_low = SystemMonitor::isBatteryLow();
        for (auto& target : targets) {
            std::lock_guard<std::mutex> lock(target->target_mutex);
            if (!target->is_foreground) {
                bool should_kill = battery_low;
                if (!should_kill) {
                    auto now = std::chrono::steady_clock::now();
                    auto background_duration = now - target->last_background_time;
                    should_kill = background_duration >= interval_manager->getKillInterval(target->package_name) / 2;
                }
                if (should_kill) {
                    for (const auto& proc : target->process_names) {
                        killProcess(proc, target->package_name);
                    }
                }
            }
        }
    }

    void checkProcesses() {
        bool memory_pressure = SystemMonitor::isMemoryPressureHigh();
        for (auto& target : targets) {
            if (!shouldCheckProcesses(target->package_name) && !target->is_foreground) continue;
            bool current_foreground = isProcessForeground(target->package_name);
            std::lock_guard<std::mutex> lock(target->target_mutex);
            auto now = std::chrono::steady_clock::now();
            if (current_foreground != target->is_foreground) {
                int duration = std::chrono::duration_cast<std::chrono::seconds>(
                    now - target->last_background_time).count();
                target->is_foreground = current_foreground;
                target->last_background_time = now;
                auto hour = localtime(&std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()))->tm_hour;
                habits.app_stats[target->package_name].updateForegroundTime(duration, hour);
                LOG_INFO("Package %s %s", target->package_name.c_str(),
                         current_foreground ? "moved to foreground" : "moved to background");
            }
            if (!current_foreground && shouldCheckProcesses(target->package_name)) {
                auto background_duration = now - target->last_background_time;
                auto kill_interval = interval_manager->getKillInterval(target->package_name);
                if (memory_pressure) kill_interval /= 2;
                if (background_duration >= kill_interval) {
                    for (const auto& proc : target->process_names) {
                        killProcess(proc, target->package_name);
                    }
                }
            }
        }
        interval_manager = std::make_unique<IntervalManager>(habits);
    }

    void workerFunction() {
        LOG_INFO("Worker thread started with %zu targets", targets.size());
        last_screen_check = std::chrono::steady_clock::now();
        while (running) {
            auto now = std::chrono::steady_clock::now();
            if (now - last_screen_check >= interval_manager->getScreenCheckInterval()) {
                is_screen_on = checkScreenState();
                last_screen_check = now;
                if (!is_screen_on) handleScreenOff();
            }
            if (is_screen_on) checkProcesses();
            std::unique_lock<std::mutex> lock(manager_mutex);
            cv.wait_for(lock, interval_manager->getScreenCheckInterval(), [this] { return !running; });
        }
        LOG_INFO("Worker thread stopped");
    }

public:
    explicit ProcessManager(const std::vector<std::pair<std::string, std::vector<std::string>>>& initial_targets)
        : interval_manager(std::make_unique<IntervalManager>(habits)) {
        for (const auto& [pkg, procs] : initial_targets) {
            if (!pkg.empty() && !procs.empty()) {
                targets.push_back(std::make_shared<Target>(pkg, procs));
                LOG_INFO("Added target: %s with %zu processes", pkg.c_str(), procs.size());
            }
        }
    }

    ~ProcessManager() { stop(); }

    void start() {
        if (targets.empty()) {
            LOG_ERROR("No targets to monitor");
            return;
        }
        LOG_INFO("Starting with %zu targets", targets.size());
        running = true;
        worker_thread = std::thread(&ProcessManager::workerFunction, this);
    }

    void stop() {
        if (running) {
            LOG_INFO("Stopping process manager");
            running = false;
            cv.notify_all();
            if (worker_thread.joinable()) worker_thread.join();
        }
    }
};

/**
 * Signal Handler
 */
static std::atomic<bool> g_running{true};

void signalHandler(int signum) {
    LOG_INFO("Received signal %d, shutting down", signum);
    g_running = false;
    Logger::close();
    exit(0);
}

/**
 * Argument Parser
 */
class ArgumentParser {
public:
    static std::vector<std::pair<std::string, std::vector<std::string>>> parse(int argc, char* argv[], int start_index) {
        std::vector<std::pair<std::string, std::vector<std::string>>> result;
        std::string current_package;
        std::vector<std::string> current_processes;
        for (int i = start_index; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg.find(':') == std::string::npos) {
                if (!current_package.empty()) {
                    result.emplace_back(current_package, current_processes);
                    current_processes.clear();
                }
                current_package = arg;
            } else {
                if (!current_package.empty()) current_processes.push_back(arg);
            }
        }
        if (!current_package.empty() && !current_processes.empty()) {
            result.emplace_back(current_package, current_processes);
        }
        return result;
    }
};

/**
 * Main Function
 */
int main(int argc, char* argv[]) {
    Logger::init(Logger::Level::INFO);
    LOG_INFO("Process manager starting...");

    if (argc < 3) {
        LOG_ERROR("Usage: %s [-d] <package_name_1> <process_name_1> ...", argv[0]);
        return 1;
    }

    int arg_offset = 1;
    if (strcmp(argv[1], "-d") == 0) {
        arg_offset = 2;
        if (fork() > 0) return 0;
        setsid();
        close(STDIN_FILENO);
        close(STDOUT_FILENO);
        close(STDERR_FILENO);
    }

    struct sigaction sa;
    sa.sa_handler = signalHandler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGTERM, &sa, nullptr);
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGHUP, &sa, nullptr);

    auto targets = ArgumentParser::parse(argc, argv, arg_offset);
    if (targets.empty()) {
        LOG_ERROR("No valid targets specified");
        return 1;
    }

    ProcessManager manager(targets);
    manager.start();
    while (g_running) pause();

    LOG_INFO("Process manager shutting down");
    Logger::close();
    return 0;
}