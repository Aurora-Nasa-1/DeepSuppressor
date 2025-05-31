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
#include <optional>
#include <memory>
#include <condition_variable>
#include <filesystem>

/**
 * 高效的日志系统，使用缓冲区减少I/O操作
 */
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
    // 优化的缓冲区大小和刷新策略
    static constexpr size_t BUFFER_RESERVE_SIZE = 8192;
    static constexpr size_t MAX_BUFFER_AGE_MS = 60000;  // 增加到60秒，减少写入频率
    static constexpr size_t MAX_LOG_SIZE = 2 * 1024 * 1024;
    static constexpr size_t MAX_LOG_FILES = 3;
    static constexpr size_t MIN_FLUSH_MESSAGES = 100;  // 增加批量处理消息数

    static int log_fd;
    static std::string log_buffer;
    static std::mutex buffer_mutex;
    static std::chrono::steady_clock::time_point last_flush;
    static size_t current_log_size;
    static char time_buffer[32];
    static std::atomic<unsigned> message_count;
    static std::atomic<bool> initialized;
    static Level log_level;

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
        if (message_count >= MIN_FLUSH_MESSAGES) {
            fsync(log_fd);
            message_count = 0;
        }
        log_buffer.clear();
        log_buffer.reserve(BUFFER_RESERVE_SIZE);
    }

public:
    static bool init(Level min_level = Level::INFO) noexcept {
        if (initialized) return true;
        
        log_buffer.reserve(BUFFER_RESERVE_SIZE);
        message_count = 0;
        current_log_size = 0;
        log_level = min_level;

        // 确保日志目录存在
        std::filesystem::path log_dir = "/data/adb/modules/DeepSuppressor/logs";
        try {
            std::filesystem::create_directories(log_dir);
        } catch (...) {
            // 目录创建失败，但仍然尝试打开日志文件
        }

        log_fd = open("/data/adb/modules/DeepSuppressor/logs/process_manager.log",
            O_WRONLY | O_CREAT | O_APPEND, 0644);
        if (log_fd == -1) return false;

        struct stat st;
        if (fstat(log_fd, &st) == 0) {
            current_log_size = st.st_size;
        }

        last_flush = std::chrono::steady_clock::now();
        initialized = true;
        return true;
    }

    static void setLogLevel(Level level) noexcept {
        log_level = level;
    }

    static void log(Level level, std::string_view message) noexcept {
        if (!initialized || level < log_level) return;

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
            log_buffer.length() >= BUFFER_RESERVE_SIZE * 0.8 ||
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
        initialized = false;
    }
};

// 静态成员初始化
int Logger::log_fd = -1;
std::string Logger::log_buffer;
std::mutex Logger::buffer_mutex;
std::chrono::steady_clock::time_point Logger::last_flush;
size_t Logger::current_log_size = 0;
char Logger::time_buffer[32];
std::atomic<unsigned> Logger::message_count{ 0 };
std::atomic<bool> Logger::initialized{ false };
Logger::Level Logger::log_level = Logger::Level::INFO;

/**
 * 应用统计信息，记录应用使用模式
 */
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

    // 更新前台使用时间和相关统计
    void updateForegroundTime(int duration, int hour) {
        total_foreground_time += duration;
        usage_count++;
        last_usage_hour = hour;
        updateUsagePatternScore();
    }

    // 更新后台运行时间
    void updateBackgroundTime(int duration) {
        total_background_time += duration;
    }

    // 计算使用模式得分
    void updateUsagePatternScore() {
        // 优化权重分配，更好地反映应用重要性
        usage_pattern_score = (usage_count * 0.25) + 
                             (total_foreground_time / 3600.0 * 0.5) + 
                             (switch_count * 0.25);
    }

    // 更新应用重要性权重
    void updateImportanceWeight() {
        // 优化权重计算，更准确地反映应用重要性
        importance_weight = (usage_pattern_score * 0.7) + 
                           (total_foreground_time / 3600.0 * 0.3);
        importance_weight = std::min(100.0, importance_weight);
    }
};

/**
 * 时间模式，记录用户在不同时间段的活动模式
 */
struct TimePattern {
    int hour{ 0 };
    double activity_level{ 0.0 };
    int check_frequency{ 0 };

    // 使用指数移动平均更新活动水平
    void update(double activity, int freq) {
        // 使用指数移动平均，更平滑地适应变化
        constexpr double alpha = 0.3; // 增加到0.3，更快地适应新模式
        activity_level = (activity_level * (1.0 - alpha)) + (activity * alpha);
        check_frequency = static_cast<int>(check_frequency * (1.0 - alpha) + freq * alpha);
    }
};

/**
 * 用户习惯数据结构，存储用户使用模式
 */
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

    // 学习目标小时数，72小时约等于3天
    static constexpr int LEARNING_HOURS_TARGET = 72;

    // 更新特定小时的时间模式
    void updateTimePattern(int hour, double activity, int freq) {
        if (hour >= 0 && hour < 24) {
            daily_patterns[hour].hour = hour;
            daily_patterns[hour].update(activity, freq);
        }
    }

    // 更新学习进度
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
        
        // 随着学习的进行，逐渐减小学习权重
        if (learning_hours >= LEARNING_HOURS_TARGET) {
            learning_complete = true;
            learning_weight = 0.1;  // 保留小的学习权重以适应长期变化
        } else {
            // 随着学习的进行，逐渐减小学习权重
            learning_weight = 0.7 - (0.6 * learning_hours / LEARNING_HOURS_TARGET);
        }
    }
    
    // 获取当前小时的活动水平
    double getCurrentHourActivityLevel() const {
        auto now = std::chrono::system_clock::now();
        auto tt = std::chrono::system_clock::to_time_t(now);
        int hour = localtime(&tt)->tm_hour;
        return daily_patterns[hour].activity_level;
    }
};

/**
 * 间隔管理器，根据用户习惯动态调整检查间隔
 */
class IntervalManager {
public:
    IntervalManager(const UserHabits& habits) : habits_(habits) {}

    // 获取屏幕状态检查间隔
    std::chrono::seconds getScreenCheckInterval() const {
        if (habits_.learning_complete) {
            // 学习完成后，根据当前小时的活动水平调整
            double activity = habits_.getCurrentHourActivityLevel();
            // 活动水平越高，检查间隔越短
            auto interval = static_cast<int>(
                SCREEN_CHECK_INTERVAL_MAX.count() - 
                (SCREEN_CHECK_INTERVAL_MAX - SCREEN_CHECK_INTERVAL_MIN).count() * 
                std::min(1.0, activity / 50.0)
            );
            return std::chrono::seconds(interval);
        }
        
        // 学习阶段，根据学习进度调整
        double progress = static_cast<double>(habits_.learning_hours) / UserHabits::LEARNING_HOURS_TARGET;
        int interval = static_cast<int>(
            SCREEN_CHECK_INTERVAL_MIN.count() +
            (SCREEN_CHECK_INTERVAL_MAX - SCREEN_CHECK_INTERVAL_MIN).count() * (1.0 - progress)
        );
        return std::chrono::seconds(interval);
    }

    // 获取进程检查间隔，根据应用重要性调整
    std::chrono::seconds getProcessCheckInterval(const std::string& package_name) const {
        auto it = habits_.app_stats.find(package_name);
        if (it == habits_.app_stats.end()) {
            return PROCESS_CHECK_INTERVAL_DEFAULT;
        }
        
        double importance = it->second.importance_weight;
        // 重要性越高，检查间隔越短
        auto interval = static_cast<long long>(
            PROCESS_CHECK_INTERVAL_MAX.count() -
            (PROCESS_CHECK_INTERVAL_MAX - PROCESS_CHECK_INTERVAL_MIN).count() * (importance / 100.0)
        );
        interval = std::max(interval, static_cast<long long>(PROCESS_CHECK_INTERVAL_MIN.count()));
        return std::chrono::seconds(interval);
    }

    // 获取进程终止间隔，根据应用重要性调整
    std::chrono::seconds getKillInterval(const std::string& package_name) const {
        auto it = habits_.app_stats.find(package_name);
        if (it == habits_.app_stats.end()) {
            return KILL_INTERVAL_DEFAULT;
        }
        
        double importance = it->second.importance_weight;
        // 重要性越高，终止间隔越长（更不容易被终止）
        auto interval = static_cast<long long>(
            KILL_INTERVAL_MIN.count() +
            (KILL_INTERVAL_MAX - KILL_INTERVAL_MIN).count() * (importance / 100.0)
        );
        interval = std::min(interval, static_cast<long long>(KILL_INTERVAL_MAX.count()));
        return std::chrono::seconds(interval);
    }

    // 获取屏幕关闭后的休眠间隔
    std::chrono::seconds getScreenOffSleepInterval() const {
        return SCREEN_OFF_SLEEP_INTERVAL;
    }

    // 获取内存压力下的检查间隔
    std::chrono::seconds getMemoryPressureInterval() const {
        return MEMORY_PRESSURE_INTERVAL;
    }

private:
    const UserHabits& habits_;
    
    // 优化的时间间隔常量
    static constexpr auto SCREEN_CHECK_INTERVAL_MIN = std::chrono::seconds(45);  // 增加到45秒
    static constexpr auto SCREEN_CHECK_INTERVAL_MAX = std::chrono::minutes(10);  // 增加到10分钟
    static constexpr auto PROCESS_CHECK_INTERVAL_MIN = std::chrono::seconds(60); // 增加到60秒
    static constexpr auto PROCESS_CHECK_INTERVAL_MAX = std::chrono::minutes(5);  // 增加到5分钟
    static constexpr auto PROCESS_CHECK_INTERVAL_DEFAULT = std::chrono::minutes(2); // 增加到2分钟
    static constexpr auto KILL_INTERVAL_MIN = std::chrono::minutes(8);  // 增加到8分钟
    static constexpr auto KILL_INTERVAL_MAX = std::chrono::minutes(45); // 增加到45分钟
    static constexpr auto KILL_INTERVAL_DEFAULT = std::chrono::minutes(15); // 增加到15分钟
    static constexpr auto SCREEN_OFF_SLEEP_INTERVAL = std::chrono::minutes(2); // 增加到2分钟
    static constexpr auto MEMORY_PRESSURE_INTERVAL = std::chrono::minutes(1); // 内存压力下的检查间隔
};

/**
 * 用户习惯管理器，负责加载、更新和保存用户习惯
 */
class UserHabitManager {
public:
    UserHabitManager() { loadHabits(); }
    ~UserHabitManager() { saveHabits(); }

    // 更新应用统计信息
    void updateAppStats(const std::string& package_name, bool is_foreground, int duration) {
        std::lock_guard<std::mutex> lock(habits_mutex);
        
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

    // 更新屏幕统计信息
    void updateScreenStats(bool is_screen_on, int duration) {
        std::lock_guard<std::mutex> lock(habits_mutex);
        
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

    // 获取用户习惯
    UserHabits getHabits() const { 
        std::lock_guard<std::mutex> lock(habits_mutex);
        return habits; 
    }

private:
    UserHabits habits;
    mutable std::mutex habits_mutex;
    static constexpr int SAVE_INTERVAL = 10;  // 增加到10，减少写入频率
    std::string config_path = "/data/adb/modules/DeepSuppressor/module_settings/user_habits.json";

    // 更新用户习惯
    void updateHabits() {
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

    // 计算当前活动水平
    double calculateActivityLevel() const {
        if (habits.app_stats.empty()) return 0.0;
        
        double total_activity = 0.0;
        size_t count = 0;
        
        // 只考虑最近使用过的应用
        auto now = std::chrono::system_clock::now();
        auto tt = std::chrono::system_clock::to_time_t(now);
        int current_hour = localtime(&tt)->tm_hour;
        
        for (const auto& [pkg, stats] : habits.app_stats) {
            // 如果应用在过去24小时内使用过，或者重要性权重较高
            if ((stats.last_usage_hour >= 0 && 
                ((current_hour >= stats.last_usage_hour && 
                  current_hour - stats.last_usage_hour < 24) || 
                 (current_hour < stats.last_usage_hour && 
                  current_hour + 24 - stats.last_usage_hour < 24))) || 
                stats.importance_weight > 30.0) {
                
                total_activity += stats.importance_weight;
                count++;
            }
        }
        
        return count > 0 ? total_activity / count : 0.0;
    }

    // 加载用户习惯
    void loadHabits() {
        int fd = open(config_path.c_str(), O_RDONLY);
        if (fd == -1) {
            Logger::log(Logger::Level::INFO, "No existing user habits found, starting fresh");
            habits.last_update = std::chrono::system_clock::now();
            return;
        }

        std::string content;
        char buffer[4096];
        ssize_t bytes_read;
        while ((bytes_read = read(fd, buffer, sizeof(buffer))) > 0) {
            content.append(buffer, bytes_read);
        }
        ::close(fd);

        try {
            auto j = nlohmann::json::parse(content);
            
            // 加载应用统计信息
            if (j.contains("app_stats") && j["app_stats"].is_object()) {
                for (auto& [pkg, stats_json] : j["app_stats"].items()) {
                    AppStats stats;
                    stats.usage_count = stats_json.value("usage_count", 0);
                    stats.total_foreground_time = stats_json.value("total_foreground_time", 0);
                    stats.total_background_time = stats_json.value("total_background_time", 0);
                    stats.switch_count = stats_json.value("switch_count", 0);
                    stats.importance_weight = stats_json.value("importance_weight", 0.0);
                    stats.last_usage_hour = stats_json.value("last_usage_hour", -1);
                    stats.usage_pattern_score = stats_json.value("usage_pattern_score", 0.0);
                    habits.app_stats[pkg] = stats;
                }
            }
            
            // 加载其他习惯数据
            habits.screen_on_duration_avg = j.value("screen_on_duration_avg", 0);
            habits.app_switch_frequency = j.value("app_switch_frequency", 0);
            habits.habit_samples = j.value("habit_samples", 0);
            habits.last_update = std::chrono::system_clock::from_time_t(j.value("last_update", std::chrono::system_clock::to_time_t(std::chrono::system_clock::now())));
            habits.learning_weight = j.value("learning_weight", 0.7);
            habits.learning_hours = j.value("learning_hours", 0);
            habits.learning_complete = j.value("learning_complete", false);
            
            // 加载每日时间模式
            if (j.contains("daily_patterns") && j["daily_patterns"].is_array()) {
                size_t i = 0;
                for (const auto& pattern_json : j["daily_patterns"]) {
                    if (i < 24) {
                        auto& pattern = habits.daily_patterns[i];
                        pattern.hour = pattern_json.value("hour", static_cast<int>(i));
                        pattern.activity_level = pattern_json.value("activity_level", 0.0);
                        pattern.check_frequency = pattern_json.value("check_frequency", 0);
                        i++;
                    }
                }
            }
            
            Logger::log(Logger::Level::INFO, std::format("Loaded user habits with {} apps", habits.app_stats.size()));
        } catch (const std::exception& e) {
            Logger::log(Logger::Level::WARN, std::format("Failed to load user habits: {}", e.what()));
            // 确保 last_update 总是有效
            habits.last_update = std::chrono::system_clock::now();
        }
    }

    // 保存用户习惯
    void saveHabits() {
        // 确保目录存在
        std::filesystem::path config_dir = "/data/adb/modules/DeepSuppressor/module_settings";
        try {
            std::filesystem::create_directories(config_dir);
        } catch (...) {
            Logger::log(Logger::Level::ERROR, "Failed to create config directory");
        }

        int fd = open(config_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd == -1) {
            Logger::log(Logger::Level::ERROR, "Failed to open habits file for writing");
            return;
        }

        try {
            nlohmann::json j;
            
            // 保存应用统计信息
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
            
            // 保存其他习惯数据
            j["screen_on_duration_avg"] = habits.screen_on_duration_avg;
            j["app_switch_frequency"] = habits.app_switch_frequency;
            j["habit_samples"] = habits.habit_samples;
            j["last_update"] = std::chrono::system_clock::to_time_t(habits.last_update);
            j["learning_weight"] = habits.learning_weight;
            j["learning_hours"] = habits.learning_hours;
            j["learning_complete"] = habits.learning_complete;

            // 保存每日时间模式
            nlohmann::json patterns_array = nlohmann::json::array();
            for (const auto& pattern : habits.daily_patterns) {
                nlohmann::json pattern_json;
                pattern_json["hour"] = pattern.hour;
                pattern_json["activity_level"] = pattern.activity_level;
                pattern_json["check_frequency"] = pattern.check_frequency;
                patterns_array.push_back(pattern_json);
            }
            j["daily_patterns"] = patterns_array;

            // 使用紧凑格式减少存储空间
            std::string json_str = j.dump();
            write(fd, json_str.c_str(), json_str.length());
            fsync(fd);
            Logger::log(Logger::Level::DEBUG, "User habits saved successfully");
        } catch (const std::exception& e) {
            Logger::log(Logger::Level::ERROR, std::format("Failed to save user habits: {}", e.what()));
        }
        ::close(fd);
    }
};

/**
 * 系统状态监控器，检测系统资源状态
 */
class SystemMonitor {
public:
    // 检查内存压力
    static bool isMemoryPressureHigh() {
        try {
            std::string output = executeCommand("cat /proc/meminfo");
            size_t available_pos = output.find("MemAvailable:");
            if (available_pos == std::string::npos) return false;
            
            // 提取可用内存数值
            size_t value_start = output.find_first_not_of(" \t", available_pos + 13);
            size_t value_end = output.find_first_of(" \t\n", value_start);
            if (value_start == std::string::npos || value_end == std::string::npos) return false;
            
            std::string value_str = output.substr(value_start, value_end - value_start);
            long available_kb = std::stol(value_str);
            
            // 如果可用内存小于总内存的15%，认为内存压力高
            size_t total_pos = output.find("MemTotal:");
            if (total_pos == std::string::npos) return false;
            
            value_start = output.find_first_not_of(" \t", total_pos + 10);
            value_end = output.find_first_of(" \t\n", value_start);
            if (value_start == std::string::npos || value_end == std::string::npos) return false;
            
            value_str = output.substr(value_start, value_end - value_start);
            long total_kb = std::stol(value_str);
            
            double available_percent = (double)available_kb / total_kb * 100.0;
            return available_percent < 15.0;
        } catch (...) {
            return false;
        }
    }
    
    // 检查CPU负载
    static bool isCpuLoadHigh() {
        try {
            std::string output = executeCommand("cat /proc/loadavg");
            size_t space_pos = output.find(' ');
            if (space_pos == std::string::npos) return false;
            
            double load1 = std::stod(output.substr(0, space_pos));
            
            // 获取CPU核心数
            std::string cpu_count_output = executeCommand("nproc");
            int cpu_count = std::stoi(cpu_count_output);
            if (cpu_count <= 0) cpu_count = 1;
            
            // 如果负载超过核心数的80%，认为CPU负载高
            return load1 > (cpu_count * 0.8);
        } catch (...) {
            return false;
        }
    }
    
    // 检查电池状态
    static bool isBatteryLow() {
        try {
            std::string output = executeCommand("dumpsys battery");
            
            // 检查是否在充电
            size_t ac_pos = output.find("AC powered:");
            size_t usb_pos = output.find("USB powered:");
            size_t wireless_pos = output.find("Wireless powered:");
            
            if (ac_pos != std::string::npos && output.find("true", ac_pos) < output.find("\n", ac_pos)) {
                return false;  // AC充电中，不认为电量低
            }
            if (usb_pos != std::string::npos && output.find("true", usb_pos) < output.find("\n", usb_pos)) {
                return false;  // USB充电中，不认为电量低
            }
            if (wireless_pos != std::string::npos && output.find("true", wireless_pos) < output.find("\n", wireless_pos)) {
                return false;  // 无线充电中，不认为电量低
            }
            
            // 检查电量百分比
            size_t level_pos = output.find("level:");
            if (level_pos == std::string::npos) return false;
            
            size_t value_start = output.find_first_not_of(" \t", level_pos + 6);
            size_t value_end = output.find_first_of(" \t\n", value_start);
            if (value_start == std::string::npos || value_end == std::string::npos) return false;
            
            int battery_level = std::stoi(output.substr(value_start, value_end - value_start));
            return battery_level < 20;  // 电量低于20%认为电量低
        } catch (...) {
            return false;
        }
    }

private:
    // 执行命令并获取输出
    static std::string executeCommand(const std::string& cmd) {
        std::array<char, 128> buffer;
        std::string result;
        FILE* pipe = popen(cmd.c_str(), "r");
        if (!pipe) return "";
        
        while (!feof(pipe)) {
            if (fgets(buffer.data(), buffer.size(), pipe) != nullptr) {
                result += buffer.data();
            }
        }
        pclose(pipe);
        return result;
    }
};

/**
 * 进程管理器，负责监控和管理进程
 */
class ProcessManager {
private:
    static constexpr auto INITIAL_SCREEN_CHECK_DELAY = std::chrono::minutes(5);
    std::chrono::steady_clock::time_point start_time;
    std::atomic<bool> running{ true };
    std::atomic<bool> is_screen_on{ true };
    std::chrono::steady_clock::time_point last_screen_check;
    std::map<std::string, std::chrono::steady_clock::time_point> last_process_check_times;
    UserHabitManager habit_manager;
    std::unique_ptr<IntervalManager> interval_manager;
    std::mutex manager_mutex;
    std::condition_variable cv;
    std::thread worker_thread;

    // 目标应用结构
    struct Target {
        std::string package_name;
        std::vector<std::string> process_names;
        std::atomic<bool> is_foreground{false};
        std::chrono::steady_clock::time_point last_background_time;
        std::atomic<int> switch_count{0};
        std::chrono::steady_clock::time_point last_switch_time;
        std::mutex target_mutex;

        Target(std::string pkg, std::vector<std::string> procs)
            : package_name(std::move(pkg)), process_names(std::move(procs)) {
            last_switch_time = std::chrono::steady_clock::now();
        }
    };

    std::vector<std::shared_ptr<Target>> targets;

    // 执行命令并获取输出
    static std::string executeCommand(const std::string& cmd) {
        std::array<char, 256> buffer;  // 增加缓冲区大小
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

    // 检查屏幕状态
    bool checkScreenState() {
        try {
            std::string output = executeCommand("dumpsys display");
            
            // 查找包含 "mScreenState=" 的行
            const std::string target = "mScreenState=";
            size_t pos = output.find(target);
            if (pos == std::string::npos) {
                Logger::log(Logger::Level::WARN, "Failed to find screen state in dumpsys display output");
                return is_screen_on;  // 保持当前状态
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
            
            if (screen_on != is_screen_on) {
                Logger::log(Logger::Level::INFO, "Screen state changed: " + state + " (is_on=" + (screen_on ? "true" : "false") + ")");
            }
            
            return screen_on;
        } catch (const std::exception& e) {
            Logger::log(Logger::Level::ERROR, std::format("Error checking screen state: {}", e.what()));
            return is_screen_on;  // 出错时保持当前状态
        }
    }

    // 判断是否应该检查进程
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

    // 检查进程是否在前台
    bool isProcessForeground(const std::string& package_name) {
        try {
            std::string output = executeCommand("dumpsys window");
            
            // 优化：只检查关键部分
            size_t current_focus_pos = output.find("mCurrentFocus");
            size_t focused_window_pos = output.find("mFocusedWindow");
            
            // 如果找不到这两个关键字，返回false
            if (current_focus_pos == std::string::npos && focused_window_pos == std::string::npos) {
                return false;
            }
            
            // 检查mCurrentFocus
            if (current_focus_pos != std::string::npos) {
                size_t line_end = output.find('\n', current_focus_pos);
                if (line_end != std::string::npos) {
                    std::string line = output.substr(current_focus_pos, line_end - current_focus_pos);
                    if (line.find(package_name) != std::string::npos) {
                        return true;
                    }
                }
            }
            
            // 检查mFocusedWindow
            if (focused_window_pos != std::string::npos) {
                size_t line_end = output.find('\n', focused_window_pos);
                if (line_end != std::string::npos) {
                    std::string line = output.substr(focused_window_pos, line_end - focused_window_pos);
                    if (line.find(package_name) != std::string::npos) {
                        return true;
                    }
                }
            }
            
            return false;
        } catch (const std::exception& e) {
            Logger::log(Logger::Level::ERROR, std::format("Error checking foreground status: {}", e.what()));
            return false;
        }
    }

    // 终止进程
    void killProcess(const std::string& process_name, const std::string& package_name) {
        try {
            // 首先尝试使用pkill
            std::string cmd = "pkill -9 " + process_name;
            int result = system(cmd.c_str());
            
            // 如果pkill失败，尝试使用kill
            if (result != 0) {
                cmd = "kill -9 $(pidof " + process_name + ")";
                system(cmd.c_str());
            }
            
            Logger::log(Logger::Level::INFO, "Killed process: " + process_name + " for package: " + package_name);
        } catch (const std::exception& e) {
            Logger::log(Logger::Level::ERROR, std::format("Error killing process {}: {}", process_name, e.what()));
        }
    }

    // 处理屏幕关闭事件
    void handleScreenOff() {
        Logger::log(Logger::Level::INFO, "Screen turned off, entering power saving mode");
        
        // 检查是否处于低电量状态
        bool battery_low = SystemMonitor::isBatteryLow();
        
        for (auto& target : targets) {
            try {
                std::lock_guard<std::mutex> lock(target->target_mutex);
                
                // 如果不在前台，且满足终止条件，则终止进程
                if (!target->is_foreground) {
                    // 在低电量状态下，更积极地终止后台进程
                    bool should_kill = battery_low;
                    
                    if (!should_kill) {
                        auto now = std::chrono::steady_clock::now();
                        auto background_duration = now - target->last_background_time;
                        auto kill_interval = interval_manager->getKillInterval(target->package_name);
                        
                        // 屏幕关闭时使用更短的终止间隔
                        should_kill = background_duration >= (kill_interval / 2);
                    }
                    
                    if (should_kill) {
                        for (const auto& proc : target->process_names) {
                            killProcess(proc, target->package_name);
                        }
                    }
                }
            } catch (const std::exception& e) {
                Logger::log(Logger::Level::ERROR, 
                    std::format("Error handling screen off for {}: {}", target->package_name, e.what()));
            }
        }
    }

    // 检查并管理进程
    void checkProcesses() {
        bool any_active = false;
        bool memory_pressure = SystemMonitor::isMemoryPressureHigh();
        
        for (auto& target : targets) {
            try {
                bool check_processes = shouldCheckProcesses(target->package_name);
                bool current_foreground = false;
                
                // 只有在需要检查时才执行前台检测
                if (check_processes || target->is_foreground) {
                    current_foreground = isProcessForeground(target->package_name);
                    
                    std::lock_guard<std::mutex> lock(target->target_mutex);
                    auto now = std::chrono::steady_clock::now();
                    
                    // 如果状态发生变化
                    if (current_foreground != target->is_foreground) {
                        int duration = std::chrono::duration_cast<std::chrono::seconds>(
                            now - target->last_switch_time).count();
                        
                        target->is_foreground = current_foreground;
                        target->last_background_time = now;
                        target->switch_count++;
                        target->last_switch_time = now;
                        
                        habit_manager.updateAppStats(target->package_name, current_foreground, duration);
                        Logger::log(Logger::Level::INFO, "Package " + target->package_name +
                            (current_foreground ? " moved to foreground" : " moved to background"));
                    }
                    
                    // 更新interval_manager
                    interval_manager = std::make_unique<IntervalManager>(habit_manager.getHabits());
                    
                    // 检查是否应该终止进程
                    bool should_kill = false;
                    
                    // 只有确认不在前台时才考虑终止
                    if (!current_foreground && !target->is_foreground && check_processes) {
                        auto background_duration = now - target->last_background_time;
                        auto kill_interval = interval_manager->getKillInterval(target->package_name);
                        
                        // 在内存压力高时使用更短的终止间隔
                        if (memory_pressure) {
                            kill_interval = kill_interval / 2;
                        }
                        
                        should_kill = background_duration >= kill_interval;
                        
                        if (should_kill) {
                            Logger::log(Logger::Level::DEBUG, 
                                std::format("Marking {} for kill - background for {}s, threshold {}s", 
                                target->package_name, 
                                std::chrono::duration_cast<std::chrono::seconds>(background_duration).count(),
                                std::chrono::duration_cast<std::chrono::seconds>(kill_interval).count()));
                        }
                    }
                    
                    if (should_kill) {
                        for (const auto& proc : target->process_names) {
                            killProcess(proc, target->package_name);
                        }
                    }
                    
                    if (current_foreground) any_active = true;
                } else if (target->is_foreground) {
                    any_active = true;
                }
            } catch (const std::exception& e) {
                Logger::log(Logger::Level::ERROR,
                    std::format("Error processing target {}: {}", target->package_name, e.what()));
            }
        }
        
        // 根据系统状态和活动情况调整休眠时间
        std::chrono::seconds sleep_time;
        if (memory_pressure) {
            sleep_time = interval_manager->getMemoryPressureInterval();
        } else if (any_active) {
            sleep_time = interval_manager->getProcessCheckInterval(targets[0]->package_name);
        } else {
            sleep_time = interval_manager->getScreenCheckInterval();
        }
        
        // 使用条件变量等待，允许提前唤醒
        std::unique_lock<std::mutex> lock(manager_mutex);
        cv.wait_for(lock, sleep_time, [this] { return !running; });
    }

    // 工作线程函数
    void workerFunction() {
        Logger::log(Logger::Level::INFO, std::format("Process manager worker thread started with {} targets", targets.size()));
        last_screen_check = std::chrono::steady_clock::now();

        while (running) {
            try {
                auto now = std::chrono::steady_clock::now();
                
                // 检查屏幕状态
                if (now - start_time >= INITIAL_SCREEN_CHECK_DELAY &&
                    now - last_screen_check >= interval_manager->getScreenCheckInterval()) {
                    
                    bool previous_screen_state = is_screen_on;
                    is_screen_on = checkScreenState();
                    last_screen_check = now;

                    int duration = std::chrono::duration_cast<std::chrono::seconds>(now - last_screen_check).count();
                    habit_manager.updateScreenStats(is_screen_on, duration);

                    if (!is_screen_on && previous_screen_state) {
                        handleScreenOff();
                    }
                }
                
                // 根据屏幕状态决定是否检查进程
                if (is_screen_on) {
                    checkProcesses();
                } else {
                    // 屏幕关闭时使用更长的休眠时间
                    std::unique_lock<std::mutex> lock(manager_mutex);
                    cv.wait_for(lock, interval_manager->getScreenOffSleepInterval(), 
                                [this] { return !running; });
                }
            } catch (const std::exception& e) {
                Logger::log(Logger::Level::ERROR, std::format("Worker thread error: {}", e.what()));
                // 出错时短暂休眠后继续
                std::this_thread::sleep_for(std::chrono::seconds(5));
            }
        }
        
        Logger::log(Logger::Level::INFO, "Process manager worker thread stopped");
    }

public:
    // 构造函数
    explicit ProcessManager(const std::vector<std::pair<std::string, std::vector<std::string>>>& initial_targets)
        : start_time(std::chrono::steady_clock::now()) {
        
        // 初始化间隔管理器
        interval_manager = std::make_unique<IntervalManager>(habit_manager.getHabits());
        
        // 初始化目标列表
        for (const auto& [pkg, procs] : initial_targets) {
            if (!pkg.empty() && !procs.empty()) {
                targets.push_back(std::make_shared<Target>(pkg, procs));
                Logger::log(Logger::Level::INFO, std::format("Added target: {} with {} processes", pkg, procs.size()));
            }
        }
    }

    // 析构函数
    ~ProcessManager() {
        stop();
    }

    // 启动进程管理器
    void start() {
        if (targets.empty()) {
            Logger::log(Logger::Level::ERROR, "No targets to monitor, cannot start");
            return;
        }
        
        Logger::log(Logger::Level::INFO, std::format("Process manager starting with {} targets", targets.size()));
        running = true;
        
        // 启动工作线程
        worker_thread = std::thread(&ProcessManager::workerFunction, this);
    }

    // 停止进程管理器
    void stop() { 
        if (running) {
            Logger::log(Logger::Level::INFO, "Stopping process manager");
            running = false;
            
            // 通知工作线程退出
            cv.notify_all();
            
            // 等待工作线程结束
            if (worker_thread.joinable()) {
                worker_thread.join();
            }
        }
    }
};

/**
 * 命令行参数解析器
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

/**
 * 主函数
 */
int main(int argc, char* argv[]) {
    try {
        // 初始化日志系统
        #ifdef DEBUG
            Logger::init(Logger::Level::DEBUG);
        #else
            Logger::init(Logger::Level::INFO);
        #endif
        
        Logger::log(Logger::Level::INFO, "Process manager starting...");

        if (argc < 3) {
            Logger::log(Logger::Level::ERROR, std::format("Usage: {} [-d] <package_name_1> <process_name_1> [<package_name_2> <process_name_1> ...]", argv[0]));
            return 1;
        }

        int arg_offset = 1;
        if (strcmp(argv[1], "-d") == 0) {
            arg_offset = 2;
            if (fork() > 0) return 0;  // 父进程退出
            setsid();  // 创建新会话
            
            // 关闭标准输入输出
            close(STDIN_FILENO);
            close(STDOUT_FILENO);
            close(STDERR_FILENO);
        }

        // 解析目标列表
        auto targets = ArgumentParser::parse(argc, argv, arg_offset);
        if (targets.empty()) {
            Logger::log(Logger::Level::ERROR, "No valid targets specified");
            return 1;
        }

        // 创建并启动进程管理器
        ProcessManager manager(targets);
        manager.start();
        
        // 设置信号处理
        signal(SIGTERM, [](int) {
            Logger::log(Logger::Level::INFO, "Received SIGTERM, shutting down");
            Logger::close();
            exit(0);
        });
        
        signal(SIGINT, [](int) {
            Logger::log(Logger::Level::INFO, "Received SIGINT, shutting down");
            Logger::close();
            exit(0);
        });
        
        // 主线程等待
        pause();
    } catch (const std::exception& e) {
        Logger::log(Logger::Level::ERROR, "Fatal error: " + std::string(e.what()));
        return 1;
    }

    Logger::log(Logger::Level::INFO, "Process manager shutting down");
    Logger::close();
    return 0;
}