#include <iostream>
#include <string>
#include <vector>
#include <chrono>
#include <atomic>
#include <mutex>
#include <array>
#include <format>
#include <thread>
#include <nlohmann/json.hpp>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <cmath>
#include <map>
#include <algorithm>

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
    static constexpr size_t MAX_BUFFER_AGE_MS = 30000;
    static constexpr size_t MAX_LOG_SIZE = 2 * 1024 * 1024;
    static constexpr size_t MAX_LOG_FILES = 3;

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
        if (message_count >= 50) {
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

int Logger::log_fd = -1;
std::string Logger::log_buffer;
std::mutex Logger::buffer_mutex;
std::chrono::steady_clock::time_point Logger::last_flush;
size_t Logger::current_log_size = 0;
char Logger::time_buffer[32];
std::atomic<unsigned> Logger::message_count{ 0 };

struct AppStats {
    int usage_count{ 0 };
    int total_foreground_time{ 0 };
    int total_background_time{ 0 };
    int switch_count{ 0 };
    double importance_weight{ 0.0 };
    int last_usage_hour{ -1 };
    double usage_pattern_score{ 0.0 };
    std::chrono::steady_clock::time_point last_foreground_time;
    std::chrono::steady_clock::time_point last_background_time;

    void updateForegroundTime(int duration, int hour) {
        total_foreground_time += duration;
        usage_count++;
        last_usage_hour = hour;
        updateUsagePatternScore();
    }

    void updateBackgroundTime(int duration) {
        total_background_time += duration;
    }

    void updateUsagePatternScore() {
        usage_pattern_score = (usage_count * 0.3) + (total_foreground_time / 3600.0 * 0.4) + (switch_count * 0.3);
    }

    void updateImportanceWeight() {
        importance_weight = (usage_pattern_score * 0.6) + (total_foreground_time / 3600.0 * 0.4);
        importance_weight = std::min(100.0, importance_weight);
    }
};

struct TimePattern {
    int hour{ 0 };
    double activity_level{ 0.0 };
    int check_frequency{ 0 };

    void update(double activity, int freq) {
        activity_level = (activity_level * 0.8) + (activity * 0.2);
        check_frequency = (check_frequency * 0.8) + (freq * 0.2);
    }
};

struct UserHabits {
    std::map<std::string, AppStats> app_stats;
    int screen_on_duration_avg{ 0 };
    int app_switch_frequency{ 0 };
    int habit_samples{ 0 };
    std::chrono::system_clock::time_point last_update;
    double learning_weight{ 0.7 };
    std::array<TimePattern, 24> daily_patterns;
    int learning_hours{ 0 };
    bool learning_complete{ false };

    static constexpr int LEARNING_HOURS_TARGET = 72;

    void updateTimePattern(int hour, double activity, int freq) {
        daily_patterns[hour].hour = hour;
        daily_patterns[hour].update(activity, freq);
    }

    void updateLearningProgress() {
        if (learning_complete) return;
        auto now = std::chrono::system_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::hours>(now - last_update);
        learning_hours += duration.count();
        last_update = now;
        if (learning_hours >= LEARNING_HOURS_TARGET) {
            learning_complete = true;
            learning_weight = 0.0;
        }
    }
};

class IntervalManager {
public:
    IntervalManager(const UserHabits& habits) : habits_(habits) {}

    std::chrono::seconds getScreenCheckInterval() const {
        if (habits_.learning_complete) {
            return SCREEN_CHECK_INTERVAL_MAX;
        }
        double progress = static_cast<double>(habits_.learning_hours) / UserHabits::LEARNING_HOURS_TARGET;
        int interval = static_cast<int>(
            SCREEN_CHECK_INTERVAL_MIN.count() +
            (SCREEN_CHECK_INTERVAL_MAX - SCREEN_CHECK_INTERVAL_MIN).count() * (1.0 - progress)
        );
        return std::chrono::seconds(interval);
    }

    std::chrono::seconds getProcessCheckInterval(const std::string& package_name) const {
        auto it = habits_.app_stats.find(package_name);
        if (it == habits_.app_stats.end()) {
            return PROCESS_CHECK_INTERVAL_DEFAULT;
        }
        double importance = it->second.importance_weight;
        auto interval = static_cast<long long>(
            PROCESS_CHECK_INTERVAL_MAX.count() -
            (PROCESS_CHECK_INTERVAL_MAX - PROCESS_CHECK_INTERVAL_MIN).count() * (importance / 100.0)
        );
        interval = std::max(interval, static_cast<long long>(PROCESS_CHECK_INTERVAL_MIN.count()));
        return std::chrono::seconds(interval);
    }

    std::chrono::seconds getKillInterval(const std::string& package_name) const {
        auto it = habits_.app_stats.find(package_name);
        if (it == habits_.app_stats.end()) {
            return KILL_INTERVAL_DEFAULT;
        }
        double importance = it->second.importance_weight;
        auto interval = static_cast<long long>(
            KILL_INTERVAL_MIN.count() +
            (KILL_INTERVAL_MAX - KILL_INTERVAL_MIN).count() * (importance / 100.0)
        );
        interval = std::min(interval, static_cast<long long>(KILL_INTERVAL_MAX.count()));
        return std::chrono::seconds(interval);
    }

    std::chrono::seconds getScreenOffSleepInterval() const {
        return SCREEN_OFF_SLEEP_INTERVAL;
    }

private:
    const UserHabits& habits_;
    static constexpr auto SCREEN_CHECK_INTERVAL_MIN = std::chrono::seconds(30);
    static constexpr auto SCREEN_CHECK_INTERVAL_MAX = std::chrono::minutes(5);
    static constexpr auto PROCESS_CHECK_INTERVAL_MIN = std::chrono::seconds(45);
    static constexpr auto PROCESS_CHECK_INTERVAL_MAX = std::chrono::minutes(3);
    static constexpr auto PROCESS_CHECK_INTERVAL_DEFAULT = std::chrono::minutes(1);
    static constexpr auto KILL_INTERVAL_MIN = std::chrono::minutes(5);
    static constexpr auto KILL_INTERVAL_MAX = std::chrono::minutes(30);
    static constexpr auto KILL_INTERVAL_DEFAULT = std::chrono::minutes(10);
    static constexpr auto SCREEN_OFF_SLEEP_INTERVAL = std::chrono::minutes(1);
};

class UserHabitManager {
public:
    UserHabitManager() { loadHabits(); }
    ~UserHabitManager() { saveHabits(); }

    void updateAppStats(const std::string& package_name, bool is_foreground, int duration) {
        auto& stats = habits.app_stats[package_name];
        auto now = std::chrono::system_clock::now();
        auto tt = std::chrono::system_clock::to_time_t(now);
        int hour = localtime(&tt)->tm_hour;

        if (is_foreground) {
            stats.updateForegroundTime(duration, hour);
        } else {
            stats.updateBackgroundTime(duration);
        }
        stats.switch_count++;
        stats.updateImportanceWeight();
        habits.app_switch_frequency++;
        updateHabits();
    }

    void updateScreenStats(bool is_screen_on, int duration) {
        habits.screen_on_duration_avg = static_cast<int>(
            habits.screen_on_duration_avg * (1 - habits.learning_weight) +
            duration * habits.learning_weight
        );
        if (is_screen_on) {
            for (auto& [pkg, stats] : habits.app_stats) {
                stats.updateImportanceWeight();
            }
        }
        updateHabits();
    }

    const UserHabits& getHabits() const { return habits; }

private:
    UserHabits habits;
    static constexpr int SAVE_INTERVAL = 5;

    void updateHabits() {
        if (habits.learning_complete) return;
        habits.habit_samples++;
        habits.last_update = std::chrono::system_clock::now();
        habits.updateLearningProgress();

        auto now = std::chrono::system_clock::now();
        auto tt = std::chrono::system_clock::to_time_t(now);
        int hour = localtime(&tt)->tm_hour;
        double activity = calculateActivityLevel();
        habits.updateTimePattern(hour, activity, habits.app_switch_frequency);

        if (habits.habit_samples % SAVE_INTERVAL == 0) {
            saveHabits();
        }
    }

    double calculateActivityLevel() const {
        double total_activity = 0.0;
        for (const auto& [pkg, stats] : habits.app_stats) {
            total_activity += stats.importance_weight;
        }
        return habits.app_stats.empty() ? 0.0 : total_activity / habits.app_stats.size();
    }

    void loadHabits() {
        const std::string config_path = "/data/adb/modules/DeepSuppressor/module_settings/user_habits.json";
        int fd = open(config_path.c_str(), O_RDONLY);
        if (fd == -1) return;

        std::string content;
        char buffer[4096];
        ssize_t bytes_read;
        while ((bytes_read = read(fd, buffer, sizeof(buffer))) > 0) {
            content.append(buffer, bytes_read);
        }
        ::close(fd);

        try {
            auto j = nlohmann::json::parse(content);
            for (auto& [pkg, stats_json] : j["app_stats"].items()) {
                AppStats stats;
                stats.usage_count = stats_json["usage_count"];
                stats.total_foreground_time = stats_json["total_foreground_time"];
                stats.total_background_time = stats_json["total_background_time"];
                stats.switch_count = stats_json["switch_count"];
                stats.importance_weight = stats_json["importance_weight"];
                stats.last_usage_hour = stats_json["last_usage_hour"];
                stats.usage_pattern_score = stats_json["usage_pattern_score"];
                habits.app_stats[pkg] = stats;
            }
            habits.screen_on_duration_avg = j["screen_on_duration_avg"];
            habits.app_switch_frequency = j["app_switch_frequency"];
            habits.habit_samples = j["habit_samples"];
            habits.last_update = std::chrono::system_clock::from_time_t(j["last_update"]);
            habits.learning_weight = j["learning_weight"];
            habits.learning_hours = j["learning_hours"];
            habits.learning_complete = j["learning_complete"];
        } catch (const std::exception& e) {
            Logger::log(Logger::Level::WARN, std::format("Failed to load user habits: {}", e.what()));
        }
    }

    void saveHabits() {
        const std::string config_path = "/data/adb/modules/DeepSuppressor/module_settings/user_habits.json";
        int fd = open(config_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd == -1) {
            Logger::log(Logger::Level::ERROR, "Failed to open habits file for writing");
            return;
        }

        try {
            nlohmann::json j;
            for (const auto& [pkg, stats] : habits.app_stats) {
                nlohmann::json stats_json;
                stats_json["usage_count"] = stats.usage_count;
                stats_json["total_foreground_time"] = stats.total_foreground_time;
                stats_json["total_background_time"] = stats.total_background_time;
                stats_json["switch_count"] = stats.switch_count;
                stats_json["importance_weight"] = stats.importance_weight;
                stats_json["last_usage_hour"] = stats.last_usage_hour;
                stats_json["usage_pattern_score"] = stats.usage_pattern_score;
                j["app_stats"][pkg] = stats_json;
            }
            j["screen_on_duration_avg"] = habits.screen_on_duration_avg;
            j["app_switch_frequency"] = habits.app_switch_frequency;
            j["habit_samples"] = habits.habit_samples;
            j["last_update"] = std::chrono::system_clock::to_time_t(habits.last_update);
            j["learning_weight"] = habits.learning_weight;
            j["learning_hours"] = habits.learning_hours;
            j["learning_complete"] = habits.learning_complete;

            std::string json_str = j.dump(4);
            write(fd, json_str.c_str(), json_str.length());
            fsync(fd);
            Logger::log(Logger::Level::DEBUG, "User habits saved successfully");
        } catch (const std::exception& e) {
            Logger::log(Logger::Level::ERROR, std::format("Failed to save user habits: {}", e.what()));
        }
        ::close(fd);
    }
};

class ProcessManager {
private:
    static constexpr auto INITIAL_SCREEN_CHECK_DELAY = std::chrono::minutes(10);
    std::chrono::steady_clock::time_point start_time;
    std::atomic<bool> running{ true };
    bool is_screen_on{ true };
    std::chrono::steady_clock::time_point last_screen_check;
    std::map<std::string, std::chrono::steady_clock::time_point> last_process_check_times;
    UserHabitManager habit_manager;
    IntervalManager interval_manager;

    struct Target {
        std::string package_name;
        std::vector<std::string> process_names;
        bool is_foreground;
        std::chrono::steady_clock::time_point last_background_time;
        int switch_count{ 0 };
        std::chrono::steady_clock::time_point last_switch_time;

        Target(std::string pkg, std::vector<std::string> procs)
            : package_name(std::move(pkg)), process_names(std::move(procs)), is_foreground(true) {}
    };

    std::vector<Target> targets;

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

    bool shouldCheckProcesses(const std::string& package_name) {
        auto now = getCurrentTime();
        auto it = last_process_check_times.find(package_name);
        if (it == last_process_check_times.end()) {
            last_process_check_times[package_name] = now;
            return true;
        }
        if (now - it->second >= interval_manager.getProcessCheckInterval(package_name)) {
            it->second = now;
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

    void killProcess(const std::string& process_name, const std::string& package_name) {
        std::string cmd = "pkill -9 " + process_name;
        if (system(cmd.c_str()) != 0) {
            cmd = "kill -9 $(pidof " + process_name + ")";
            system(cmd.c_str());
        }
        Logger::log(Logger::Level::INFO, "Killed process: " + process_name + " for package: " + package_name);
    }

    std::chrono::steady_clock::time_point getCurrentTime() const {
        return std::chrono::steady_clock::now();
    }

    void checkScreenState() {
        auto now = getCurrentTime();
        if (now - start_time < INITIAL_SCREEN_CHECK_DELAY ||
            now - last_screen_check < interval_manager.getScreenCheckInterval()) {
            return;
        }

        bool previous_screen_state = is_screen_on;
        is_screen_on = isScreenOn();
        last_screen_check = now;

        int duration = std::chrono::duration_cast<std::chrono::seconds>(now - last_screen_check).count();
        habit_manager.updateScreenStats(is_screen_on, duration);

        if (!is_screen_on && previous_screen_state) {
            Logger::log(Logger::Level::INFO, "Screen turned off, entering deep sleep mode");
            handleScreenOff();
        }
    }

    void handleScreenOff() {
        for (auto& target : targets) {
            if (!target.is_foreground) {
                for (const auto& proc : target.process_names) {
                    killProcess(proc, target.package_name);
                }
            }
        }
        std::this_thread::sleep_for(interval_manager.getScreenOffSleepInterval());
    }

    void checkProcesses() {
        bool any_active = false;

        for (auto& target : targets) {
            try {
                bool check_processes = shouldCheckProcesses(target.package_name);
                if (check_processes || target.is_foreground) {
                    bool current_foreground = isProcessForeground(target.package_name);
                    bool should_kill = false;

                    auto now = getCurrentTime();
                    int duration = std::chrono::duration_cast<std::chrono::seconds>(
                        now - target.last_switch_time).count();

                    if (current_foreground != target.is_foreground) {
                        target.is_foreground = current_foreground;
                        target.last_background_time = now;
                        target.switch_count++;
                        target.last_switch_time = now;
                        habit_manager.updateAppStats(target.package_name, current_foreground, duration);
                        Logger::log(Logger::Level::INFO, "Package " + target.package_name +
                            (current_foreground ? " moved to foreground" : " moved to background"));
                    }

                    if (!target.is_foreground && check_processes) {
                        auto background_duration = now - target.last_background_time;
                        auto kill_interval = interval_manager.getKillInterval(target.package_name);
                        if (background_duration >= kill_interval) {
                            should_kill = true;
                        }
                    }

                    if (should_kill) {
                        for (const auto& proc : target.process_names) {
                            killProcess(proc, target.package_name);
                        }
                    }

                    if (current_foreground) any_active = true;
                }
            } catch (const std::exception& e) {
                Logger::log(Logger::Level::ERROR,
                    std::format("Error processing target {}: {}", target.package_name, e.what()));
            }
        }

        std::this_thread::sleep_for(any_active ?
            interval_manager.getProcessCheckInterval(targets[0].package_name) :
            interval_manager.getScreenCheckInterval());
    }

public:
    explicit ProcessManager(const std::vector<std::pair<std::string, std::vector<std::string>>>& initial_targets)
        : start_time(getCurrentTime()), interval_manager(habit_manager.getHabits()) {
        for (const auto& [pkg, procs] : initial_targets) {
            if (!pkg.empty() && !procs.empty()) {
                targets.emplace_back(pkg, procs);
                Logger::log(Logger::Level::INFO, std::format("Added target: {} with {} processes", pkg, procs.size()));
            }
        }
    }

    void start() {
        Logger::log(Logger::Level::INFO, std::format("Process manager started with {} targets", targets.size()));
        last_screen_check = getCurrentTime();

        while (running) {
            checkScreenState();
            if (is_screen_on) checkProcesses();
            else std::this_thread::sleep_for(interval_manager.getScreenCheckInterval());
        }
    }

    void stop() { running = false; }
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
                if (!current_package.empty()) current_processes.push_back(arg);
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
    try {
        Logger::init();
        Logger::log(Logger::Level::INFO, "Process manager starting...");

        if (argc < 3) {
 elegido Logger::log(Logger::Level::ERROR, std::format("Usage: {} [-d] <package_name> <process_name_1> [<process_name_2> ...]", argv[0]));
            return 1;
        }

        int arg_offset = 1;
        if (strcmp(argv[1], "-d") == 0) {
            arg_offset = 2;
            if (fork() > 0) return 0;
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