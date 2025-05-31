#include <string>
#include <vector>
#include <chrono>
#include <atomic>
#include <mutex>
#include <array>
#include <thread>
#include <nlohmann/json.hpp>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <map>
#include <memory>
#include <condition_variable>
#include <signal.h>
#include <dirent.h>
#include <fstream>

#define LOG_FORMAT(fmt, ...) (std::string(fmt) + __VA_ARGS__)

class Logger {
public:
    enum class Level { DEBUG, INFO, WARN, ERROR, FATAL };

private:
    static constexpr size_t BUFFER_RESERVE_SIZE = 8192;
    static constexpr size_t MAX_BUFFER_AGE_MS = 60000;
    static constexpr size_t MAX_LOG_SIZE = 2 * 1024 * 1024;
    static constexpr size_t MAX_LOG_FILES = 3;

    static int log_fd;
    static std::string log_buffer;
    static std::mutex buffer_mutex;
    static std::chrono::steady_clock::time_point last_flush;
    static size_t current_log_size;
    static char time_buffer[32];
    static std::atomic<bool> initialized;
    static Level log_level;

    static const char* getLevelString(Level level) noexcept {
        static const char* level_strings[] = {"DEBUG", "INFO", "WARN", "ERROR", "FATAL"};
        return level_strings[static_cast<size_t>(level)];
    }

    static void rotateLogFiles() {
        std::string base_path = "/data/adb/modules/DeepSuppressor/logs/process_manager";
        std::string oldest = base_path + "." + std::to_string(MAX_LOG_FILES - 1) + ".log";
        unlink(oldest.c_str());
        for (int i = MAX_LOG_FILES - 2; i >= 0; --i) {
            std::string current = i == 0 ? base_path + ".log" : base_path + "." + std::to_string(i) + ".log";
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
            log_fd = open("/data/adb/modules/DeepSuppressor/logs/process_manager.log", O_WRONLY | O_CREAT | O_TRUNC, 0644);
            current_log_size = total_size;
        }
        write(log_fd, log_buffer.data(), total_size);
        fsync(log_fd);
        log_buffer.clear();
        log_buffer.reserve(BUFFER_RESERVE_SIZE);
    }

public:
    static bool init(Level min_level = Level::INFO) noexcept {
        if (initialized) return true;
        log_buffer.reserve(BUFFER_RESERVE_SIZE);
        current_log_size = 0;
        log_level = min_level;
        mkdir("/data/adb/modules/DeepSuppressor/logs", 0755);
        log_fd = open("/data/adb/modules/DeepSuppressor/logs/process_manager.log", O_WRONLY | O_CREAT | O_APPEND, 0644);
        if (log_fd == -1) return false;
        struct stat st;
        if (fstat(log_fd, &st) == 0) current_log_size = st.st_size;
        last_flush = std::chrono::steady_clock::now();
        initialized = true;
        return true;
    }

    static void log(Level level, const std::string& message) noexcept {
        if (!initialized || level < log_level) return;
        std::lock_guard<std::mutex> lock(buffer_mutex);
        formatTime();
        log_buffer.append(time_buffer).append(" [").append(getLevelString(level)).append("] ").append(message).append("\n");
        auto now = std::chrono::steady_clock::now();
        if (level >= Level::ERROR || log_buffer.length() >= BUFFER_RESERVE_SIZE * 0.8 ||
            (now - last_flush) >= std::chrono::milliseconds(MAX_BUFFER_AGE_MS)) {
            writeToFile();
            last_flush = now;
        }
    }

    static void close() noexcept {
        std::lock_guard<std::mutex> lock(buffer_mutex);
        if (!log_buffer.empty()) writeToFile();
        if (log_fd != -1) ::close(log_fd);
        initialized = false;
    }
};

int Logger::log_fd = -1;
std::string Logger::log_buffer;
std::mutex Logger::buffer_mutex;
std::chrono::steady_clock::time_point Logger::last_flush;
size_t Logger::current_log_size = 0;
char Logger::time_buffer[32];
std::atomic<bool> Logger::initialized{false};
Logger::Level Logger::log_level = Logger::Level::INFO;

struct AppStats {
    int usage_count{0};
    int total_foreground_time{0};
    int switch_count{0};
    double importance_weight{0.0};
    int last_usage_hour{-1};
    double usage_pattern_score{0.0};
    std::chrono::steady_clock::time_point last_foreground_time;

    void updateForegroundTime(int duration, int hour) {
        total_foreground_time += duration;
        usage_count++;
        last_usage_hour = hour;
        updateUsagePatternScore();
    }

    void updateUsagePatternScore() {
        usage_pattern_score = (usage_count * 0.2) + (total_foreground_time / 3600.0 * 0.6) + (switch_count * 0.2);
    }

    void updateImportanceWeight() {
        importance_weight = (usage_pattern_score * 0.8) + (total_foreground_time / 3600.0 * 0.2);
        importance_weight = std::min(100.0, importance_weight);
    }
};

struct TimePattern {
    int hour{0};
    double activity_level{0.0};

    void update(double activity) {
        const double alpha = 0.1;
        activity_level = activity_level * (1 - alpha) + activity * alpha;
    }
};

struct UserHabits {
    std::map<std::string, AppStats> app_stats;
    int habit_samples{0};
    std::chrono::system_clock::time_point last_update;
    double learning_weight{0.7};
    std::array<TimePattern, 24> daily_patterns;
    int learning_hours{0};
    bool learning_complete{false};

    static constexpr int LEARNING_HOURS_TARGET = 48;

    void updateTimePattern(int hour, double activity) {
        if (hour >= 0 && hour < 24) {
            daily_patterns[hour].hour = hour;
            daily_patterns[hour].update(activity);
        }
    }

    void updateLearningProgress() {
        if (learning_complete) return;
        auto now = std::chrono::system_clock::now();
        if (last_update == std::chrono::system_clock::time_point{}) {
            last_update = now;
            return;
        }
        auto duration = std::chrono::duration_cast<std::chrono::hours>(now - last_update);
        learning_hours += duration.count();
        last_update = now;
        if (learning_hours >= LEARNING_HOURS_TARGET) {
            learning_complete = true;
            learning_weight = 0.15;
        } else {
            double progress = static_cast<double>(learning_hours) / LEARNING_HOURS_TARGET;
            learning_weight = 0.7 - (0.55 * progress);
        }
    }

    double getCurrentHourActivityLevel() const {
        auto now = std::chrono::system_clock::now();
        auto tt = std::chrono::system_clock::to_time_t(now);
        int hour = localtime(&tt)->tm_hour;
        return daily_patterns[hour].activity_level;
    }
};

class IntervalManager {
public:
    IntervalManager(const UserHabits& habits) : habits_(habits) {}

    std::chrono::seconds getScreenCheckInterval() const {
        if (habits_.learning_complete) {
            double activity = habits_.getCurrentHourActivityLevel();
            double factor = activity / 100.0;
            auto interval = SCREEN_CHECK_INTERVAL_MIN + (SCREEN_CHECK_INTERVAL_MAX - SCREEN_CHECK_INTERVAL_MIN) * (1 - factor);
            return std::chrono::seconds(static_cast<int>(interval.count()));
        }
        double progress = static_cast<double>(habits_.learning_hours) / UserHabits::LEARNING_HOURS_TARGET;
        auto interval = SCREEN_CHECK_INTERVAL_MIN + (SCREEN_CHECK_INTERVAL_MAX - SCREEN_CHECK_INTERVAL_MIN) * (1 - progress);
        return std::chrono::seconds(static_cast<int>(interval.count()));
    }

    std::chrono::seconds getProcessCheckInterval(const std::string& package_name) const {
        auto it = habits_.app_stats.find(package_name);
        if (it == habits_.app_stats.end()) return PROCESS_CHECK_INTERVAL_DEFAULT;
        double factor = it->second.importance_weight / 100.0;
        auto interval = PROCESS_CHECK_INTERVAL_MAX - (PROCESS_CHECK_INTERVAL_MAX - PROCESS_CHECK_INTERVAL_MIN) * factor;
        return std::chrono::seconds(static_cast<long long>(interval.count()));
    }

    std::chrono::seconds getKillInterval(const std::string& package_name) const {
        auto it = habits_.app_stats.find(package_name);
        if (it == habits_.app_stats.end()) return KILL_INTERVAL_DEFAULT;
        double factor = it->second.importance_weight / 100.0;
        auto interval = KILL_INTERVAL_MIN + (KILL_INTERVAL_MAX - KILL_INTERVAL_MIN) * factor;
        return std::chrono::seconds(static_cast<long long>(interval.count()));
    }

    std::chrono::seconds getScreenOffSleepInterval() const { return SCREEN_OFF_SLEEP_INTERVAL; }
    std::chrono::seconds getMemoryPressureInterval() const { return MEMORY_PRESSURE_INTERVAL; }

private:
    const UserHabits& habits_;
    static constexpr auto SCREEN_CHECK_INTERVAL_MIN = std::chrono::seconds(30);
    static constexpr auto SCREEN_CHECK_INTERVAL_MAX = std::chrono::minutes(8);
    static constexpr auto PROCESS_CHECK_INTERVAL_MIN = std::chrono::seconds(45);
    static constexpr auto PROCESS_CHECK_INTERVAL_MAX = std::chrono::minutes(4);
    static constexpr auto PROCESS_CHECK_INTERVAL_DEFAULT = std::chrono::minutes(2);
    static constexpr auto KILL_INTERVAL_MIN = std::chrono::minutes(5);
    static constexpr auto KILL_INTERVAL_MAX = std::chrono::minutes(30);
    static constexpr auto KILL_INTERVAL_DEFAULT = std::chrono::minutes(10);
    static constexpr auto SCREEN_OFF_SLEEP_INTERVAL = std::chrono::minutes(3);
    static constexpr auto MEMORY_PRESSURE_INTERVAL = std::chrono::seconds(30);
};

class SystemMonitor {
public:
    static bool isMemoryPressureHigh() {
        std::ifstream meminfo("/proc/meminfo");
        if (!meminfo) return false;
        std::string line;
        long available_kb = 0, total_kb = 0;
        while (std::getline(meminfo, line)) {
            if (line.find("MemTotal:") == 0) std::istringstream(line.substr(9)) >> total_kb;
            else if (line.find("MemAvailable:") == 0) std::istringstream(line.substr(13)) >> available_kb;
            if (total_kb > 0 && available_kb > 0) break;
        }
        if (total_kb <= 0) return false;
        return (static_cast<double>(available_kb) / total_kb * 100.0) < 20.0;
    }

    static bool isCpuLoadHigh() {
        std::ifstream loadavg("/proc/loadavg");
        if (!loadavg) return false;
        double load1;
        loadavg >> load1;
        int cpu_count = std::thread::hardware_concurrency();
        if (cpu_count <= 0) cpu_count = 1;
        return load1 > (cpu_count * 0.7);
    }

    static bool isBatteryLow() {
        std::ifstream status_file("/sys/class/power_supply/battery/status");
        if (!status_file) return false;
        std::string status;
        std::getline(status_file, status);
        if (status == "Charging") return false;
        std::ifstream capacity_file("/sys/class/power_supply/battery/capacity");
        if (!capacity_file) return false;
        int capacity;
        capacity_file >> capacity;
        return capacity < 25;
    }
};

class UserHabitManager {
public:
    UserHabitManager() { loadHabits(); }
    ~UserHabitManager() { saveHabits(); }

    void updateAppStats(const std::string& package_name, bool is_foreground, int duration) {
        std::lock_guard<std::mutex> lock(habits_mutex);
        auto& stats = habits.app_stats[package_name];
        auto now = std::chrono::system_clock::now();
        auto tt = std::chrono::system_clock::to_time_t(now);
        int hour = localtime(&tt)->tm_hour;
        if (is_foreground) stats.updateForegroundTime(duration, hour);
        stats.switch_count++;
        stats.updateImportanceWeight();
        updateHabits();
    }

    void updateScreenStats(bool is_screen_on, int duration) {
        std::lock_guard<std::mutex> lock(habits_mutex);
        if (is_screen_on) {
            for (auto& [pkg, stats] : habits.app_stats) {
                stats.updateImportanceWeight();
            }
        }
        habits.habit_samples += duration / 60; // Use duration to increment habit samples (in minutes)
        updateHabits();
    }

    UserHabits getHabits() const { std::lock_guard<std::mutex> lock(habits_mutex); return habits; }
    void saveHabits() { std::lock_guard<std::mutex> lock(habits_mutex); saveHabitsInternal(); }

private:
    UserHabits habits;
    mutable std::mutex habits_mutex; // Added missing mutex
    std::string config_path = "/data/adb/modules/DeepSuppressor/module_settings/user_habits.json";

    void updateHabits() {
        habits.habit_samples++;
        habits.last_update = std::chrono::system_clock::now();
        habits.updateLearningProgress();
        auto tt = std::chrono::system_clock::to_time_t(habits.last_update);
        int hour = localtime(&tt)->tm_hour;
        double activity = calculateActivityLevel();
        habits.updateTimePattern(hour, activity);
    }

    double calculateActivityLevel() const {
        if (habits.app_stats.empty()) return 0.0;
        double total_activity = 0.0, weight_sum = 0.0;
        auto now = std::chrono::system_clock::now();
        auto tt = std::chrono::system_clock::to_time_t(now);
        int current_hour = localtime(&tt)->tm_hour;
        for (const auto& [pkg, stats] : habits.app_stats) {
            double time_weight = 1.0;
            if (stats.last_usage_hour >= 0) {
                int hour_diff = (current_hour >= stats.last_usage_hour) ?
                    current_hour - stats.last_usage_hour : current_hour + 24 - stats.last_usage_hour;
                time_weight = std::exp(-hour_diff / 12.0);
            }
            double weight = time_weight * (0.3 + 0.7 * (stats.importance_weight / 100.0));
            total_activity += stats.importance_weight * weight;
            weight_sum += weight;
        }
        return weight_sum > 0.0 ? total_activity / weight_sum : 0.0;
    }

    void loadHabits() {
        mkdir("/data/adb/modules/DeepSuppressor/module_settings", 0755);
        FILE* file = fopen(config_path.c_str(), "rb");
        if (!file) {
            Logger::log(Logger::Level::INFO, "No existing user habits found, starting fresh");
            habits.last_update = std::chrono::system_clock::now();
            return;
        }
        std::string content;
        char buffer[4096];
        size_t bytes_read;
        while ((bytes_read = fread(buffer, 1, sizeof(buffer), file)) > 0) content.append(buffer, bytes_read);
        fclose(file);
        try {
            auto j = nlohmann::json::parse(content);
            if (j.contains("app_stats")) {
                for (auto& [pkg, stats_json] : j["app_stats"].items()) {
                    AppStats stats;
                    stats.usage_count = stats_json.value("usage_count", 0);
                    stats.total_foreground_time = stats_json.value("total_foreground_time", 0);
                    stats.switch_count = stats_json.value("switch_count", 0);
                    stats.importance_weight = stats_json.value("importance_weight", 0.0);
                    stats.last_usage_hour = stats_json.value("last_usage_hour", -1);
                    stats.usage_pattern_score = stats_json.value("usage_pattern_score", 0.0);
                    habits.app_stats[pkg] = stats;
                }
            }
            habits.habit_samples = j.value("habit_samples", 0);
            habits.last_update = std::chrono::system_clock::from_time_t(j.value("last_update", std::chrono::system_clock::to_time_t(std::chrono::system_clock::now())));
            habits.learning_weight = j.value("learning_weight", 0.7);
            habits.learning_hours = j.value("learning_hours", 0);
            habits.learning_complete = j.value("learning_complete", false);
            if (j.contains("daily_patterns")) {
                size_t i = 0;
                for (const auto& pattern_json : j["daily_patterns"]) {
                    if (i < 24) {
                        habits.daily_patterns[i].hour = pattern_json.value("hour", static_cast<int>(i));
                        habits.daily_patterns[i].activity_level = pattern_json.value("activity_level", 0.0);
                        i++;
                    }
                }
            }
            Logger::log(Logger::Level::INFO, "Loaded user habits with " + std::to_string(habits.app_stats.size()) + " apps");
        } catch (const std::exception& e) {
            Logger::log(Logger::Level::WARN, "Failed to load user habits: " + std::string(e.what()));
            habits.last_update = std::chrono::system_clock::now();
        }
    }

    void saveHabitsInternal() {
        mkdir("/data/adb/modules/DeepSuppressor/module_settings", 0755);
        FILE* file = fopen(config_path.c_str(), "wb");
        if (!file) {
            Logger::log(Logger::Level::ERROR, "Failed to open habits file for writing: " + config_path);
            return;
        }
        try {
            nlohmann::json j;
            for (const auto& [pkg, stats] : habits.app_stats) {
                nlohmann::json stats_json;
                stats_json["usage_count"] = stats.usage_count;
                stats_json["total_foreground_time"] = stats.total_foreground_time;
                stats_json["switch_count"] = stats.switch_count;
                stats_json["importance_weight"] = stats.importance_weight;
                stats_json["last_usage_hour"] = stats.last_usage_hour;
                stats_json["usage_pattern_score"] = stats.usage_pattern_score;
                j["app_stats"][pkg] = stats_json;
            }
            j["habit_samples"] = habits.habit_samples;
            j["last_update"] = std::chrono::system_clock::to_time_t(habits.last_update);
            j["learning_weight"] = habits.learning_weight;
            j["learning_hours"] = habits.learning_hours;
            j["learning_complete"] = habits.learning_complete;
            nlohmann::json patterns_array = nlohmann::json::array();
            for (const auto& pattern : habits.daily_patterns) {
                nlohmann::json pattern_json;
                pattern_json["hour"] = pattern.hour;
                pattern_json["activity_level"] = pattern.activity_level;
                patterns_array.push_back(pattern_json);
            }
            j["daily_patterns"] = patterns_array;
            std::string json_str = j.dump();
            fwrite(json_str.c_str(), 1, json_str.length(), file);
            fflush(file);
        } catch (const std::exception& e) {
            Logger::log(Logger::Level::ERROR, "Failed to save user habits: " + std::string(e.what()));
        }
        fclose(file);
    }
};

class ProcessManager {
private:
    static constexpr auto INITIAL_SCREEN_CHECK_DELAY = std::chrono::minutes(2);
    std::chrono::steady_clock::time_point start_time;
    std::atomic<bool> running{true};
    std::atomic<bool> is_screen_on{true};
    std::atomic<bool> force_check{false};
    std::chrono::steady_clock::time_point last_screen_check;
    std::map<std::string, std::chrono::steady_clock::time_point> last_process_check_times;
    UserHabitManager habit_manager;
    std::unique_ptr<IntervalManager> interval_manager;
    std::mutex manager_mutex;
    std::condition_variable cv;
    std::thread worker_thread;

    struct Target {
        std::string package_name;
        std::vector<std::string> process_names;
        std::atomic<bool> is_foreground{false};
        std::chrono::steady_clock::time_point last_background_time;
        std::chrono::steady_clock::time_point last_switch_time;
        std::mutex target_mutex;

        Target(std::string pkg, std::vector<std::string> procs) : package_name(std::move(pkg)), process_names(std::move(procs)) {
            last_switch_time = last_background_time = std::chrono::steady_clock::now();
        }
    };

    std::vector<std::shared_ptr<Target>> targets;

    static std::string executeCommand(const std::string& cmd) {
        std::array<char, 512> buffer;
        std::string result;
        FILE* pipe = popen(cmd.c_str(), "r");
        if (!pipe) return "";
        while (!feof(pipe)) if (fgets(buffer.data(), buffer.size(), pipe)) result += buffer.data();
        pclose(pipe);
        return result;
    }

    bool checkScreenState() {
        FILE* file = fopen("/sys/class/leds/lcd-backlight/brightness", "r");
        if (file) {
            int brightness = 0;
            fscanf(file, "%d", &brightness);
            fclose(file);
            bool screen_on = brightness > 0;
            if (screen_on != is_screen_on) Logger::log(Logger::Level::INFO, "Screen state changed: " + std::string(screen_on ? "ON" : "OFF"));
            return screen_on;
        }
        std::string output = executeCommand("dumpsys display | grep mScreenState");
        bool screen_on = output.find("ON") != std::string::npos;
        if (screen_on != is_screen_on) Logger::log(Logger::Level::INFO, "Screen state changed: " + output);
        return screen_on;
    }

    bool shouldCheckProcesses(const std::string& package_name) {
        if (force_check) return true;
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
        std::string cmd = "dumpsys window | grep -E 'mCurrentFocus|mFocusedApp'";
        std::string output = executeCommand(cmd);
        return output.find(package_name) != std::string::npos;
    }

    void handleScreenOff() {
        Logger::log(Logger::Level::INFO, "Screen turned off, entering power saving mode");
        bool battery_low = SystemMonitor::isBatteryLow();
        bool memory_pressure = SystemMonitor::isMemoryPressureHigh();
        for (auto& target : targets) {
            std::lock_guard<std::mutex> lock(target->target_mutex);
            if (!target->is_foreground) {
                bool should_kill = battery_low || memory_pressure;
                if (!should_kill) {
                    auto now = std::chrono::steady_clock::now();
                    auto background_duration = now - target->last_background_time;
                    auto kill_interval = interval_manager->getKillInterval(target->package_name);
                    should_kill = background_duration >= (kill_interval / 3);
                }
                if (should_kill) {
                    std::string cmd = "am force-stop " + target->package_name;
                    system(cmd.c_str());
                    Logger::log(Logger::Level::INFO, "Force stopped package: " + target->package_name);
                }
            }
        }
        habit_manager.saveHabits();
    }

    void checkProcesses() {
        bool any_active = false;
        bool memory_pressure = SystemMonitor::isMemoryPressureHigh();
        for (auto& target : targets) {
            bool check_processes = shouldCheckProcesses(target->package_name);
            bool current_foreground = false;
            if (check_processes || target->is_foreground) {
                current_foreground = isProcessForeground(target->package_name);
                std::lock_guard<std::mutex> lock(target->target_mutex);
                auto now = std::chrono::steady_clock::now();
                if (current_foreground != target->is_foreground) {
                    int duration = std::chrono::duration_cast<std::chrono::seconds>(now - target->last_switch_time).count();
                    target->is_foreground = current_foreground;
                    if (!current_foreground) target->last_background_time = now;
                    target->last_switch_time = now;
                    habit_manager.updateAppStats(target->package_name, current_foreground, duration);
                }
                interval_manager = std::make_unique<IntervalManager>(habit_manager.getHabits());
                if (!current_foreground && check_processes) {
                    auto background_duration = now - target->last_background_time;
                    auto kill_interval = interval_manager->getKillInterval(target->package_name);
                    if (memory_pressure) kill_interval /= 3;
                    if (background_duration >= kill_interval) {
                        std::string cmd = "am force-stop " + target->package_name;
                        system(cmd.c_str());
                        Logger::log(Logger::Level::INFO, "Force stopped package: " + target->package_name);
                    }
                }
                if (current_foreground) any_active = true;
            } else if (target->is_foreground) any_active = true;
        }
        force_check = false;
        std::chrono::seconds sleep_time = memory_pressure ? interval_manager->getMemoryPressureInterval() :
            any_active ? interval_manager->getProcessCheckInterval(targets[0]->package_name) : interval_manager->getScreenCheckInterval();
        std::unique_lock<std::mutex> lock(manager_mutex);
        cv.wait_for(lock, sleep_time, [this] { return !running || force_check; });
    }

    void workerFunction() {
        Logger::log(Logger::Level::INFO, "Process manager worker thread started with " + std::to_string(targets.size()) + " targets");
        last_screen_check = std::chrono::steady_clock::now();
        while (running) {
            try {
                auto now = std::chrono::steady_clock::now();
                if (now - start_time >= INITIAL_SCREEN_CHECK_DELAY && now - last_screen_check >= interval_manager->getScreenCheckInterval()) {
                    bool previous_screen_state = is_screen_on;
                    is_screen_on = checkScreenState();
                    last_screen_check = now;
                    int duration = std::chrono::duration_cast<std::chrono::seconds>(now - last_screen_check).count();
                    habit_manager.updateScreenStats(is_screen_on, duration);
                    if (!is_screen_on && previous_screen_state) handleScreenOff();
                    else if (is_screen_on && !previous_screen_state) force_check = true;
                }
                if (is_screen_on) checkProcesses();
                else {
                    std::unique_lock<std::mutex> lock(manager_mutex);
                    cv.wait_for(lock, interval_manager->getScreenOffSleepInterval(), [this] { return !running || force_check; });
                }
            } catch (const std::exception& e) {
                Logger::log(Logger::Level::ERROR, "Worker thread error: " + std::string(e.what()));
                std::this_thread::sleep_for(std::chrono::seconds(5));
            }
        }
        habit_manager.saveHabits();
        Logger::log(Logger::Level::INFO, "Process manager worker thread stopped");
    }

public:
    explicit ProcessManager(const std::vector<std::pair<std::string, std::vector<std::string>>>& initial_targets) : start_time(std::chrono::steady_clock::now()) {
        interval_manager = std::make_unique<IntervalManager>(habit_manager.getHabits());
        for (const auto& [pkg, procs] : initial_targets) {
            if (!pkg.empty() && !procs.empty()) targets.push_back(std::make_shared<Target>(pkg, procs));
        }
    }

    ~ProcessManager() { stop(); }

    void start() {
        if (targets.empty()) {
            Logger::log(Logger::Level::ERROR, "No targets to monitor, cannot start");
            return;
        }
        running = true;
        worker_thread = std::thread(&ProcessManager::workerFunction, this);
    }

    void stop() {
        if (running) {
            running = false;
            cv.notify_all();
            if (worker_thread.joinable()) worker_thread.join();
        }
    }

    void forceCheck() { force_check = true; cv.notify_all(); }
};

class ArgumentParser {
public:
    static std::vector<std::pair<std::string, std::vector<std::string>>> parse(int argc, char* argv[], int start_index) {
        std::vector<std::pair<std::string, std::vector<std::string>>> result;
        std::string current_package;
        std::vector<std::string> current_processes;
        for (int i = start_index; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg.find('.') != std::string::npos) {
                if (!current_package.empty()) result.push_back({current_package, current_processes});
                current_package = arg;
                current_processes.clear();
            } else {
                if (current_package.empty()) continue;
                current_processes.push_back(arg);
            }
        }
        if (!current_package.empty()) result.push_back({current_package, current_processes});
        return result;
    }
};

int main(int argc, char* argv[]) {
    Logger::init(Logger::Level::INFO);
    if (argc < 3) {
        Logger::log(Logger::Level::ERROR, "Usage: " + std::string(argv[0]) + " [-d] <package_name_1> <process_name_1> ...");
        return 1;
    }
    int arg_offset = 1;
    if (argc > 1 && strcmp(argv[1], "-d") == 0) {
        arg_offset = 2;
        if (fork() > 0) return 0;
        setsid();
        close(STDIN_FILENO);
        close(STDOUT_FILENO);
        close(STDERR_FILENO);
    }
    auto targets = ArgumentParser::parse(argc, argv, arg_offset);
    if (targets.empty()) {
        Logger::log(Logger::Level::ERROR, "No valid targets specified");
        return 1;
    }
    ProcessManager manager(targets);
    manager.start();
    struct sigaction sa_term, sa_int, sa_usr1;
    sa_term.sa_handler = [](int) { Logger::log(Logger::Level::INFO, "Received SIGTERM, shutting down"); Logger::close(); exit(0); };
    sa_int.sa_handler = [](int) { Logger::log(Logger::Level::INFO, "Received SIGINT, shutting down"); Logger::close(); exit(0); };
    static ProcessManager* manager_ptr = &manager;
    sa_usr1.sa_handler = [](int) { Logger::log(Logger::Level::INFO, "Received SIGUSR1, forcing process check"); manager_ptr->forceCheck(); };
    sigemptyset(&sa_term.sa_mask);
    sigemptyset(&sa_int.sa_mask);
    sigemptyset(&sa_usr1.sa_mask);
    sa_term.sa_flags = sa_int.sa_flags = sa_usr1.sa_flags = 0;
    sigaction(SIGTERM, &sa_term, nullptr);
    sigaction(SIGINT, &sa_int, nullptr);
    sigaction(SIGUSR1, &sa_usr1, nullptr);
    pause();
    Logger::log(Logger::Level::INFO, "Process manager shutting down");
    Logger::close();
    return 0;
}