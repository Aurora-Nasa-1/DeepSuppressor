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
#include <random>

class Logger {
public:
    enum class Level {
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
            "INFO", "WARN", "ERROR", "FATAL"
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

// 增强的应用统计结构
struct AppStats {
    int usage_count{ 0 };
    int total_foreground_time{ 0 };
    int total_background_time{ 0 };
    int switch_count{ 0 };
    double importance_weight{ 0.0 };
    int last_usage_hour{ -1 };
    double usage_pattern_score{ 0.0 };
    std::array<int, 24> hourly_usage{ 0 }; // 每小时使用情况
    std::chrono::steady_clock::time_point last_foreground_time;
    std::chrono::steady_clock::time_point last_background_time;
    int consecutive_days_used{ 0 }; // 连续使用天数
    int last_used_day{ -1 }; // 上次使用的日期

    void updateForegroundTime(int duration, int hour, int day) {
        total_foreground_time += duration;
        usage_count++;
        last_usage_hour = hour;
        hourly_usage[hour]++;
        
        // 更新连续使用天数
        if (last_used_day != day) {
            if (last_used_day != -1 && day - last_used_day == 1) {
                consecutive_days_used++;
            } else if (last_used_day != -1 && day - last_used_day > 1) {
                consecutive_days_used = 1;
            } else if (last_used_day == -1) {
                consecutive_days_used = 1;
            }
            last_used_day = day;
        }
        
        updateUsagePatternScore();
    }

    void updateBackgroundTime(int duration) {
        total_background_time += duration;
    }

    void updateUsagePatternScore() {
        // 计算使用频率分数
        double frequency_score = usage_count * 0.2;
        
        // 计算使用时长分数
        double duration_score = total_foreground_time / 3600.0 * 0.3;
        
        // 计算切换频率分数
        double switch_score = switch_count * 0.1;
        
        // 计算连续使用分数
        double consecutive_score = consecutive_days_used * 0.2;
        
        // 计算时间模式分数 - 检查是否有规律的使用时间
        double pattern_score = 0.0;
        int peak_hours = 0;
        for (int usage : hourly_usage) {
            if (usage > usage_count / 24) peak_hours++;
        }
        pattern_score = (peak_hours <= 8) ? 0.2 : 0.1; // 集中使用时间少于8小时给予更高分数
        
        usage_pattern_score = frequency_score + duration_score + switch_score + consecutive_score + pattern_score;
        usage_pattern_score = std::min(10.0, usage_pattern_score); // 限制最大值为10
    }

    void updateImportanceWeight() {
        // 重新设计权重计算，考虑更多因素
        double usage_weight = usage_pattern_score * 6.0; // 使用模式权重
        double time_weight = total_foreground_time / 3600.0 * 3.0; // 使用时长权重
        double recency_weight = (last_usage_hour >= 0) ? 1.0 : 0.0; // 最近使用权重
        
        importance_weight = usage_weight + time_weight + recency_weight;
        importance_weight = std::min(100.0, importance_weight);
    }
    
    // 获取应用的活跃时段
    std::vector<int> getActiveHours() const {
        std::vector<int> active_hours;
        int threshold = std::max(1, usage_count / 24);
        
        for (int i = 0; i < 24; i++) {
            if (hourly_usage[i] >= threshold) {
                active_hours.push_back(i);
            }
        }
        
        return active_hours;
    }
};

// 增强的时间模式结构
struct TimePattern {
    int hour{ 0 };
    double activity_level{ 0.0 };
    int check_frequency{ 0 };
    std::vector<std::string> active_apps; // 该时段活跃的应用

    void update(double activity, int freq, const std::string& app_name) {
        // 使用指数移动平均更新活动水平和检查频率
        activity_level = (activity_level * 0.8) + (activity * 0.2);
        check_frequency = static_cast<int>((check_frequency * 0.8) + (freq * 0.2));
        
        // 更新活跃应用列表
        if (!app_name.empty() && 
            std::find(active_apps.begin(), active_apps.end(), app_name) == active_apps.end() && 
            active_apps.size() < 5) { // 限制每个时段最多记录5个应用
            active_apps.push_back(app_name);
        }
    }
    
    // 检查应用是否在此时段活跃
    bool isAppActive(const std::string& app_name) const {
        return std::find(active_apps.begin(), active_apps.end(), app_name) != active_apps.end();
    }
};

// 增强的用户习惯结构
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
    int last_active_day{ -1 }; // 上次活跃的日期
    std::array<double, 7> day_of_week_activity{ 0.0 }; // 每周各天的活跃度

    static constexpr int LEARNING_HOURS_TARGET = 72;

    void updateTimePattern(int hour, double activity, int freq, const std::string& active_app = "") {
        daily_patterns[hour].hour = hour;
        daily_patterns[hour].update(activity, freq, active_app);
    }

    void updateLearningProgress() {
        if (learning_complete) return;
        
        auto now = std::chrono::system_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::hours>(now - last_update);
        learning_hours += duration.count();
        last_update = now;
        
        // 根据学习进度动态调整学习权重
        double progress = static_cast<double>(learning_hours) / LEARNING_HOURS_TARGET;
        learning_weight = std::max(0.1, 0.7 - (progress * 0.7));
        
        if (learning_hours >= LEARNING_HOURS_TARGET) {
            learning_complete = true;
            learning_weight = 0.1; // 保留小部分学习能力以适应变化
        }
    }
    
    // 更新每周活跃度
    void updateDayActivity(int day_of_week, double activity) {
        day_of_week_activity[day_of_week] = (day_of_week_activity[day_of_week] * 0.7) + (activity * 0.3);
    }
    
    // 获取当前时段的活跃应用
    std::vector<std::string> getActiveAppsForHour(int hour) const {
        return daily_patterns[hour].active_apps;
    }
    
    // 判断当前是否是用户的活跃时段
    bool isActiveTime(int hour, int day_of_week) const {
        return daily_patterns[hour].activity_level > 0.3 && day_of_week_activity[day_of_week] > 0.3;
    }
};

// 增强的间隔管理器
class IntervalManager {
public:
    IntervalManager(const UserHabits& habits) : habits_(habits) {}

    std::chrono::seconds getScreenCheckInterval(int current_hour, int day_of_week) const {
        // 如果是用户活跃时段，缩短检查间隔
        if (habits_.isActiveTime(current_hour, day_of_week)) {
            return SCREEN_CHECK_INTERVAL_ACTIVE;
        }
        
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

    std::chrono::seconds getProcessCheckInterval(const std::string& package_name, int current_hour) const {
        auto it = habits_.app_stats.find(package_name);
        if (it == habits_.app_stats.end()) {
            return PROCESS_CHECK_INTERVAL_DEFAULT;
        }
        
        // 检查应用是否在当前时段活跃
        bool is_active_hour = false;
        auto active_hours = it->second.getActiveHours();
        if (std::find(active_hours.begin(), active_hours.end(), current_hour) != active_hours.end()) {
            is_active_hour = true;
        }
        
        double importance = it->second.importance_weight;
        
        // 对活跃时段的重要应用使用更短的检查间隔
        if (is_active_hour && importance > 50.0) {
            return PROCESS_CHECK_INTERVAL_PRIORITY;
        }
        
        auto interval = static_cast<long long>(
            PROCESS_CHECK_INTERVAL_MAX.count() -
            (PROCESS_CHECK_INTERVAL_MAX - PROCESS_CHECK_INTERVAL_MIN).count() * (importance / 100.0)
        );
        interval = std::max(interval, static_cast<long long>(PROCESS_CHECK_INTERVAL_MIN.count()));
        return std::chrono::seconds(interval);
    }

    std::chrono::seconds getKillInterval(const std::string& package_name, int current_hour) const {
        auto it = habits_.app_stats.find(package_name);
        if (it == habits_.app_stats.end()) {
            return KILL_INTERVAL_DEFAULT;
        }
        
        double importance = it->second.importance_weight;
        
        // 检查应用是否在当前时段活跃
        bool is_active_hour = false;
        auto active_hours = it->second.getActiveHours();
        if (std::find(active_hours.begin(), active_hours.end(), current_hour) != active_hours.end()) {
            is_active_hour = true;
        }
        
        // 对活跃时段的重要应用使用更长的杀进程间隔（更少杀进程）
        if (is_active_hour && importance > 50.0) {
            return KILL_INTERVAL_PRIORITY;
        }
        
        auto interval = static_cast<long long>(
            KILL_INTERVAL_MIN.count() +
            (KILL_INTERVAL_MAX - KILL_INTERVAL_MIN).count() * (importance / 100.0)
        );
        interval = std::min(interval, static_cast<long long>(KILL_INTERVAL_MAX.count()));
        return std::chrono::seconds(interval);
    }

    std::chrono::seconds getScreenOffSleepInterval(int current_hour) const {
        // 在用户不活跃的时段使用更长的休眠间隔
        if (habits_.daily_patterns[current_hour].activity_level < 0.2) {
            return SCREEN_OFF_SLEEP_INTERVAL_LONG;
        }
        return SCREEN_OFF_SLEEP_INTERVAL;
    }

private:
    const UserHabits& habits_;
    static constexpr auto SCREEN_CHECK_INTERVAL_MIN = std::chrono::seconds(30);
    static constexpr auto SCREEN_CHECK_INTERVAL_MAX = std::chrono::minutes(5);
    static constexpr auto SCREEN_CHECK_INTERVAL_ACTIVE = std::chrono::seconds(20); // 活跃时段更短间隔
    static constexpr auto PROCESS_CHECK_INTERVAL_MIN = std::chrono::seconds(45);
    static constexpr auto PROCESS_CHECK_INTERVAL_MAX = std::chrono::minutes(3);
    static constexpr auto PROCESS_CHECK_INTERVAL_DEFAULT = std::chrono::minutes(1);
    static constexpr auto PROCESS_CHECK_INTERVAL_PRIORITY = std::chrono::seconds(30); // 优先应用更短间隔
    static constexpr auto KILL_INTERVAL_MIN = std::chrono::minutes(5);
    static constexpr auto KILL_INTERVAL_MAX = std::chrono::minutes(30);
    static constexpr auto KILL_INTERVAL_DEFAULT = std::chrono::minutes(10);
    static constexpr auto KILL_INTERVAL_PRIORITY = std::chrono::minutes(40); // 优先应用更长间隔
    static constexpr auto SCREEN_OFF_SLEEP_INTERVAL = std::chrono::minutes(1);
    static constexpr auto SCREEN_OFF_SLEEP_INTERVAL_LONG = std::chrono::minutes(3); // 不活跃时段更长休眠
};

// 增强的用户习惯管理器
class UserHabitManager {
public:
    UserHabitManager() : last_save_time(std::chrono::steady_clock::now()) { 
        loadHabits(); 
    }
    
    ~UserHabitManager() { 
        saveHabits(); 
    }

    void updateAppStats(const std::string& package_name, bool is_foreground, int duration) {
        auto& stats = habits.app_stats[package_name];
        auto now = std::chrono::system_clock::now();
        auto tt = std::chrono::system_clock::to_time_t(now);
        auto tm = *localtime(&tt);
        int hour = tm.tm_hour;
        int day = tm.tm_mday;
        int day_of_week = tm.tm_wday;

        if (is_foreground) {
            stats.updateForegroundTime(duration, hour, day);
            // 更新时间模式，记录活跃应用
            habits.updateTimePattern(hour, 1.0, 1, package_name);
        } else {
            stats.updateBackgroundTime(duration);
        }
        
        stats.switch_count++;
        stats.updateImportanceWeight();
        habits.app_switch_frequency++;
        
        // 更新每周活跃度
        if (habits.last_active_day != day) {
            habits.last_active_day = day;
            habits.updateDayActivity(day_of_week, 1.0);
        }
        
        updateHabits();
        checkSaveHabits();
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
        checkSaveHabits();
    }

    const UserHabits& getHabits() const { return habits; }

private:
    UserHabits habits;
    std::chrono::steady_clock::time_point last_save_time;
    static constexpr int SAVE_INTERVAL_SAMPLES = 5; // 样本数间隔
    static constexpr auto SAVE_INTERVAL_TIME = std::chrono::minutes(15); // 时间间隔

    void updateHabits() {
        habits.habit_samples++;
        habits.last_update = std::chrono::system_clock::now();
        habits.updateLearningProgress();

        auto now = std::chrono::system_clock::now();
        auto tt = std::chrono::system_clock::to_time_t(now);
        auto tm = *localtime(&tt);
        int hour = tm.tm_hour;
        int day_of_week = tm.tm_wday;
        
        double activity = calculateActivityLevel();
        habits.updateTimePattern(hour, activity, habits.app_switch_frequency);
        habits.updateDayActivity(day_of_week, activity);
    }

    double calculateActivityLevel() const {
        if (habits.app_stats.empty()) return 0.0;
        
        double total_activity = 0.0;
        int active_apps = 0;
        
        for (const auto& [pkg, stats] : habits.app_stats) {
            if (stats.importance_weight > 10.0) { // 只考虑重要性超过阈值的应用
                total_activity += stats.importance_weight;
                active_apps++;
            }
        }
        
        return active_apps > 0 ? total_activity / active_apps : 0.0;
    }
    
    void checkSaveHabits() {
        auto now = std::chrono::steady_clock::now();
        bool should_save = false;
        
        // 根据样本数量判断是否保存
        if (habits.habit_samples % SAVE_INTERVAL_SAMPLES == 0) {
            should_save = true;
        }
        
        // 根据时间间隔判断是否保存
        if (now - last_save_time >= SAVE_INTERVAL_TIME) {
            should_save = true;
        }
        
        if (should_save) {
            saveHabits();
            last_save_time = now;
        }
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
                stats.consecutive_days_used = stats_json.value("consecutive_days_used", 0);
                stats.last_used_day = stats_json.value("last_used_day", -1);
                
                // 加载每小时使用情况
                if (stats_json.contains("hourly_usage")) {
                    for (size_t i = 0; i < 24; ++i) {
                        stats.hourly_usage[i] = stats_json["hourly_usage"][i];
                    }
                }
                
                habits.app_stats[pkg] = stats;
            }
            
            habits.screen_on_duration_avg = j["screen_on_duration_avg"];
            habits.app_switch_frequency = j["app_switch_frequency"];
            habits.habit_samples = j["habit_samples"];
            habits.last_update = std::chrono::system_clock::from_time_t(j["last_update"]);
            habits.learning_weight = j["learning_weight"];
            habits.learning_hours = j["learning_hours"];
            habits.learning_complete = j["learning_complete"];
            habits.last_active_day = j.value("last_active_day", -1);
            
            // 加载每周活跃度
            if (j.contains("day_of_week_activity")) {
                for (size_t i = 0; i < 7; ++i) {
                    habits.day_of_week_activity[i] = j["day_of_week_activity"][i];
                }
            }
            
            // 加载 daily_patterns
            if (j.contains("daily_patterns")) {
                for (size_t i = 0; i < 24; ++i) {
                    auto& pattern = habits.daily_patterns[i];
                    pattern.hour = j["daily_patterns"][i]["hour"];
                    pattern.activity_level = j["daily_patterns"][i]["activity_level"];
                    pattern.check_frequency = j["daily_patterns"][i]["check_frequency"];
                    
                    // 加载活跃应用
                    if (j["daily_patterns"][i].contains("active_apps")) {
                        for (const auto& app : j["daily_patterns"][i]["active_apps"]) {
                            pattern.active_apps.push_back(app);
                        }
                    }
                }
            }
            
            Logger::log(Logger::Level::INFO, "User habits loaded successfully");
        } catch (const std::exception& e) {
            Logger::log(Logger::Level::WARN, std::format("Failed to load user habits: {}", e.what()));
        }
        
        // 确保 last_update 总是有效
        if (habits.last_update == std::chrono::system_clock::time_point{}) {
            habits.last_update = std::chrono::system_clock::now();
        }
    }

    void saveHabits() {
        const std::string config_path = "/data/adb/modules/DeepSuppressor/module_settings/user_habits.json";
        const std::string temp_path = config_path + ".tmp";
        
        // 先写入临时文件，成功后再重命名，避免写入过程中崩溃导致文件损坏
        int fd = open(temp_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd == -1) {
            Logger::log(Logger::Level::ERROR, "Failed to open temporary habits file for writing");
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
                stats_json["consecutive_days_used"] = stats.consecutive_days_used;
                stats_json["last_used_day"] = stats.last_used_day;
                
                // 保存每小时使用情况
                nlohmann::json hourly_array = nlohmann::json::array();
                for (int usage : stats.hourly_usage) {
                    hourly_array.push_back(usage);
                }
                stats_json["hourly_usage"] = hourly_array;
                
                j["app_stats"][pkg] = stats_json;
            }
            
            j["screen_on_duration_avg"] = habits.screen_on_duration_avg;
            j["app_switch_frequency"] = habits.app_switch_frequency;
            j["habit_samples"] = habits.habit_samples;
            j["last_update"] = std::chrono::system_clock::to_time_t(habits.last_update);
            j["learning_weight"] = habits.learning_weight;
            j["learning_hours"] = habits.learning_hours;
            j["learning_complete"] = habits.learning_complete;
            j["last_active_day"] = habits.last_active_day;
            
            // 保存每周活跃度
            nlohmann::json day_activity_array = nlohmann::json::array();
            for (double activity : habits.day_of_week_activity) {
                day_activity_array.push_back(activity);
            }
            j["day_of_week_activity"] = day_activity_array;

            // 保存 daily_patterns
            nlohmann::json patterns_array = nlohmann::json::array();
            for (const auto& pattern : habits.daily_patterns) {
                nlohmann::json pattern_json;
                pattern_json["hour"] = pattern.hour;
                pattern_json["activity_level"] = pattern.activity_level;
                pattern_json["check_frequency"] = pattern.check_frequency;
                
                // 保存活跃应用
                nlohmann::json apps_array = nlohmann::json::array();
                for (const auto& app : pattern.active_apps) {
                    apps_array.push_back(app);
                }
                pattern_json["active_apps"] = apps_array;
                
                patterns_array.push_back(pattern_json);
            }
            j["daily_patterns"] = patterns_array;

            std::string json_str = j.dump(4);
            write(fd, json_str.c_str(), json_str.length());
            fsync(fd);
            ::close(fd);
            
            // 重命名临时文件为正式文件
            if (rename(temp_path.c_str(), config_path.c_str()) != 0) {
                Logger::log(Logger::Level::ERROR, "Failed to rename temporary habits file");
                unlink(temp_path.c_str()); // 删除临时文件
                return;
            }
            
            Logger::log(Logger::Level::INFO, "User habits saved successfully");
        } catch (const std::exception& e) {
            Logger::log(Logger::Level::ERROR, std::format("Failed to save user habits: {}", e.what()));
            ::close(fd);
            unlink(temp_path.c_str()); // 删除临时文件
        }
    }
};

// 优化的进程管理器
class ProcessManager {
private:
    static constexpr auto INITIAL_SCREEN_CHECK_DELAY = std::chrono::minutes(5); // 减少初始延迟
    std::chrono::steady_clock::time_point start_time;
    std::atomic<bool> running{ true };
    bool is_screen_on{ true };
    std::chrono::steady_clock::time_point last_screen_check;
    std::map<std::string, std::chrono::steady_clock::time_point> last_process_check_times;
    UserHabitManager habit_manager;
    IntervalManager interval_manager;
    std::chrono::steady_clock::time_point last_save_check;
    std::mt19937 random_engine{std::random_device{}()}; // 随机数生成器，用于抖动

    struct Target {
        std::string package_name;
        std::vector<std::string> process_names;
        bool is_foreground;
        std::chrono::steady_clock::time_point last_background_time;
        int switch_count{ 0 };
        std::chrono::steady_clock::time_point last_switch_time;
        int kill_attempts{ 0 }; // 记录杀进程尝试次数
        bool is_protected{ false }; // 标记是否为受保护应用

        Target(std::string pkg, std::vector<std::string> procs, bool protected_app = false)
            : package_name(std::move(pkg)), process_names(std::move(procs)), 
              is_foreground(true), is_protected(protected_app) {}
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
        Logger::log(Logger::Level::INFO, "Screen state: " + state + " (is_on=" + (screen_on ? "true" : "false") + ")");
        return screen_on;
    }

    bool shouldCheckProcesses(const std::string& package_name) {
        auto now = getCurrentTime();
        auto it = last_process_check_times.find(package_name);
        if (it == last_process_check_times.end()) {
            last_process_check_times[package_name] = now;
            return true;
        }
        
        // 获取当前时间信息
        auto tt = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        auto tm = *localtime(&tt);
        int current_hour = tm.tm_hour;
        
        // 获取检查间隔并添加随机抖动（±10%）以避免同步检查
        auto base_interval = interval_manager.getProcessCheckInterval(package_name, current_hour);
        std::uniform_int_distribution<> dist(-static_cast<int>(base_interval.count() * 0.1), 
                                           static_cast<int>(base_interval.count() * 0.1));
        auto jitter = std::chrono::seconds(dist(random_engine));
        auto interval = base_interval + jitter;
        
        if (now - it->second >= interval) {
            it->second = now;
            return true;
        }
        return false;
    }

    bool isProcessForeground(const std::string& package_name) noexcept {
        // 优化前台检测逻辑，减少日志输出
        std::string output = executeCommand("dumpsys window");
        
        // 使用更精确的正则表达式模式匹配
        const std::string current_focus_pattern = "mCurrentFocus";
        const std::string focused_window_pattern = "mFocusedWindow";
        
        size_t current_focus_pos = output.find(current_focus_pattern);
        size_t focused_window_pos = output.find(focused_window_pattern);
        
        // 检查两个关键位置
        std::vector<size_t> positions;
        if (current_focus_pos != std::string::npos) positions.push_back(current_focus_pos);
        if (focused_window_pos != std::string::npos) positions.push_back(focused_window_pos);
        
        for (size_t pos : positions) {
            // 找到行的结束位置
            size_t line_end = output.find('\n', pos);
            if (line_end == std::string::npos) line_end = output.length();
            
            // 提取整行
            std::string line = output.substr(pos, line_end - pos);
            
            // 查找 Window{...} 模式
            size_t window_start = line.find("Window{");
            if (window_start != std::string::npos) {
                size_t content_start = window_start + 7; // 跳过 "Window{"
                size_t content_end = line.find('}', content_start);
                
                if (content_end != std::string::npos) {
                    std::string window_content = line.substr(content_start, content_end - content_start);
                    
                    // 查找包含 '/' 的部分，这通常是包名/活动名格式
                    size_t slash_pos = window_content.find('/');
                    if (slash_pos != std::string::npos) {
                        // 从斜杠向前查找包名的开始位置
                        size_t pkg_start = window_content.rfind(' ', slash_pos);
                        if (pkg_start != std::string::npos) {
                            pkg_start++; // 跳过空格
                            std::string current_pkg = window_content.substr(pkg_start, slash_pos - pkg_start);
                            
                            // 如果找到匹配的包名，立即返回
                            if (current_pkg == package_name) {
                                return true;
                            }
                        }
                    }
                }
            }
        }
        
        return false;
    }

    void killProcess(const std::string& process_name, const std::string& package_name, Target& target) {
        // 增加智能杀进程逻辑
        target.kill_attempts++;
        
        // 如果是受保护应用或者杀进程尝试次数过多，跳过杀进程
        if (target.is_protected) {
            Logger::log(Logger::Level::INFO, "Skipping kill for protected app: " + package_name);
            return;
        }
        
        if (target.kill_attempts > 10) {
            // 如果多次尝试杀进程但失败，可能是系统应用或重要应用，标记为受保护
            target.is_protected = true;
            Logger::log(Logger::Level::INFO, "Marking app as protected due to multiple kill attempts: " + package_name);
            return;
        }
        
        // 执行杀进程操作
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
            now - last_screen_check < getScreenCheckInterval()) {
            return;
        }

        bool previous_screen_state = is_screen_on;
        is_screen_on = isScreenOn();
        last_screen_check = now;

        int duration = std::chrono::duration_cast<std::chrono::seconds>(now - last_screen_check).count();
        habit_manager.updateScreenStats(is_screen_on, duration);

        if (!is_screen_on && previous_screen_state) {
            Logger::log(Logger::Level::INFO, "Screen turned off, entering sleep mode");
            handleScreenOff();
        } else if (is_screen_on && !previous_screen_state) {
            Logger::log(Logger::Level::INFO, "Screen turned on, resuming normal operation");
        }
    }

    void handleScreenOff() {
        // 获取当前时间信息
        auto tt = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        auto tm = *localtime(&tt);
        int current_hour = tm.tm_hour;
        
        // 智能休眠：只杀死非重要应用
        for (auto& target : targets) {
            if (!target.is_foreground && !target.is_protected) {
                // 检查应用重要性
                auto it = habit_manager.getHabits().app_stats.find(target.package_name);
                if (it != habit_manager.getHabits().app_stats.end() && it->second.importance_weight < 50.0) {
                    for (const auto& proc : target.process_names) {
                        killProcess(proc, target.package_name, target);
                    }
                }
            }
        }
        
        // 使用基于当前时段的休眠间隔
        std::this_thread::sleep_for(interval_manager.getScreenOffSleepInterval(current_hour));
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

                    // 获取当前时间信息
                    auto tt = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
                    auto tm = *localtime(&tt);
                    int current_hour = tm.tm_hour;
                    
                    // 严格的前台保护：只有在确认不在前台时才考虑杀死进程
                    if (!current_foreground && !target.is_foreground && check_processes) {
                        auto background_duration = now - target.last_background_time;
                        auto kill_interval = interval_manager.getKillInterval(target.package_name, current_hour);
                        
                        // 检查应用是否在当前时段活跃
                        bool is_active_hour = false;
                        auto it = habit_manager.getHabits().app_stats.find(target.package_name);
                        if (it != habit_manager.getHabits().app_stats.end()) {
                            auto active_hours = it->second.getActiveHours();
                            if (std::find(active_hours.begin(), active_hours.end(), current_hour) != active_hours.end()) {
                                is_active_hour = true;
                            }
                        }
                        
                        // 在活跃时段给予更长的宽限期
                        if (is_active_hour) {
                            kill_interval *= 2; // 活跃时段杀进程间隔加倍
                        }
                        
                        if (background_duration >= kill_interval && !target.is_protected) {
                            should_kill = true;
                            Logger::log(Logger::Level::INFO, 
                                std::format("Marking {} for kill - background for {}s, threshold {}s", 
                                target.package_name, 
                                std::chrono::duration_cast<std::chrono::seconds>(background_duration).count(),
                                kill_interval.count()));
                        }
                    } else if (current_foreground) {
                        // 重置杀进程尝试计数
                        target.kill_attempts = 0;
                    }

                    if (should_kill) {
                        for (const auto& proc : target.process_names) {
                            killProcess(proc, target.package_name, target);
                        }
                    }

                    if (current_foreground) any_active = true;
                }
            } catch (const std::exception& e) {
                Logger::log(Logger::Level::ERROR,
                    std::format("Error processing target {}: {}", target.package_name, e.what()));
            }
        }

        // 检查是否需要保存用户习惯数据
        auto now = getCurrentTime();
        if (now - last_save_check >= std::chrono::minutes(5)) { // 每5分钟检查一次
            last_save_check = now;
            // 这里不直接调用saveHabits，而是通过UserHabitManager的机制来决定是否保存
            habit_manager.updateScreenStats(is_screen_on, 0); // 触发检查保存逻辑
        }

        // 根据是否有活跃应用调整休眠时间
        std::this_thread::sleep_for(any_active ?
            getProcessCheckInterval() :
            getScreenCheckInterval());
    }
    
    std::chrono::seconds getScreenCheckInterval() const {
        // 获取当前时间信息
        auto tt = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        auto tm = *localtime(&tt);
        int current_hour = tm.tm_hour;
        int day_of_week = tm.tm_wday;
        
        return interval_manager.getScreenCheckInterval(current_hour, day_of_week);
    }
    
    std::chrono::seconds getProcessCheckInterval() const {
        // 如果没有目标，使用默认间隔
        if (targets.empty()) {
            return std::chrono::minutes(1);
        }
        
        // 获取当前时间信息
        auto tt = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        auto tm = *localtime(&tt);
        int current_hour = tm.tm_hour;
        
        // 使用第一个前台应用的检查间隔
        for (const auto& target : targets) {
            if (target.is_foreground) {
                return interval_manager.getProcessCheckInterval(target.package_name, current_hour);
            }
        }
        
        // 如果没有前台应用，使用第一个目标的间隔
        return interval_manager.getProcessCheckInterval(targets[0].package_name, current_hour);
    }

public:
    explicit ProcessManager(const std::vector<std::pair<std::string, std::vector<std::string>>>& initial_targets)
        : start_time(getCurrentTime()), 
          interval_manager(habit_manager.getHabits()),
          last_save_check(getCurrentTime()) {
        
        for (const auto& [pkg, procs] : initial_targets) {
            if (!pkg.empty() && !procs.empty()) {
                // 检查是否为系统应用（简单判断）
                bool is_system_app = pkg.find("android") != std::string::npos || 
                                    pkg.find("google") != std::string::npos || 
                                    pkg.find("system") != std::string::npos;
                
                targets.emplace_back(pkg, procs, is_system_app);
                Logger::log(Logger::Level::INFO, std::format("Added target: {} with {} processes{}", 
                    pkg, procs.size(), is_system_app ? " (protected)" : ""));
            }
        }
    }

    void start() {
        Logger::log(Logger::Level::INFO, std::format("Process manager started with {} targets", targets.size()));
        last_screen_check = getCurrentTime();

        while (running) {
            try {
                checkScreenState();
                if (is_screen_on) checkProcesses();
                else std::this_thread::sleep_for(getScreenCheckInterval());
            } catch (const std::exception& e) {
                Logger::log(Logger::Level::ERROR, std::format("Error in main loop: {}", e.what()));
                // 添加短暂休眠以避免在错误情况下CPU使用率过高
                std::this_thread::sleep_for(std::chrono::seconds(5));
            }
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
            if (arg.find(':') == std::string::npos) {
                // 如果是package_name，先保存前一个package的信息
                if (!current_package.empty()) {
                    result.emplace_back(current_package, current_processes);
                    current_processes.clear();
                }
                current_package = arg;
            } else {
                // 如果是process_name，添加到当前package的process列表
                if (!current_package.empty()) {
                    current_processes.push_back(arg);
                }
            }
        }
        
        // 添加最后一个package的信息
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
            Logger::log(Logger::Level::ERROR, std::format("Usage: {} [-d] <package_name_1> <process_name_1> [<package_name_2> <process_name_1> ...]", argv[0]));
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