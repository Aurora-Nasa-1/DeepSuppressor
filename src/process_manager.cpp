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
#include <set>

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

// 获取当前日期（天数）
inline int getCurrentDay() {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    auto* tm_info = localtime(&time);
    return tm_info->tm_yday; // 一年中的第几天
}

// 获取当前小时
inline int getCurrentHour() {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    auto* tm_info = localtime(&time);
    return tm_info->tm_hour;
}

struct AppStats {
    int usage_count{ 0 };
    int total_foreground_time{ 0 };
    int total_background_time{ 0 };
    int switch_count{ 0 };
    double importance_weight{ 0.0 };
    int last_usage_hour{ -1 };
    int last_used_day{ -1 };
    int consecutive_days_used{ 0 };
    double usage_pattern_score{ 0.0 };
    std::array<int, 24> hourly_usage{ 0 }; // 每小时使用情况
    std::chrono::steady_clock::time_point last_foreground_time;
    std::chrono::steady_clock::time_point last_background_time;

    void updateForegroundTime(int duration, int hour) {
        total_foreground_time += duration;
        usage_count++;
        
        // 更新小时使用情况
        hourly_usage[hour]++;
        
        // 更新连续使用天数
        int current_day = getCurrentDay();
        if (last_used_day != current_day) {
            if (last_used_day != -1 && (current_day == (last_used_day + 1) || 
                (current_day == 0 && last_used_day == 364))) { // 处理跨年
                consecutive_days_used++;
            } else if (last_used_day != -1) {
                consecutive_days_used = 1; // 不连续，重置为1
            } else {
                consecutive_days_used = 1; // 首次使用
            }
            last_used_day = current_day;
        }
        
        last_usage_hour = hour;
        updateUsagePatternScore();
    }

    void updateBackgroundTime(int duration) {
        total_background_time += duration;
    }

    void updateUsagePatternScore() {
        // 更复杂的使用模式评分，考虑连续使用天数和小时分布
        double hourly_diversity = 0.0;
        int active_hours = 0;
        
        for (int usage : hourly_usage) {
            if (usage > 0) {
                active_hours++;
                hourly_diversity += std::log(usage + 1.0); // 使用对数避免单一时段过高权重
            }
        }
        
        // 归一化小时多样性
        if (active_hours > 0) {
            hourly_diversity /= active_hours;
        }
        
        // 计算最终分数，考虑使用次数、前台时间、切换次数、连续使用天数和小时多样性
        usage_pattern_score = 
            (usage_count * 0.2) + 
            (total_foreground_time / 3600.0 * 0.3) + 
            (switch_count * 0.1) + 
            (consecutive_days_used * 0.2) + 
            (hourly_diversity * 0.2);
    }

    void updateImportanceWeight() {
        // 更智能的重要性权重计算
        double recency_factor = last_usage_hour >= 0 ? 1.0 : 0.5; // 最近使用过的应用权重更高
        double consistency_factor = std::min(1.0, consecutive_days_used / 7.0); // 连续使用天数影响
        
        importance_weight = 
            (usage_pattern_score * 0.4) + 
            (total_foreground_time / 3600.0 * 0.3) + 
            (recency_factor * 0.1) + 
            (consistency_factor * 0.2);
            
        importance_weight = std::min(100.0, importance_weight);
    }
};

struct TimePattern {
    int hour{ 0 };
    double activity_level{ 0.0 };
    int check_frequency{ 0 };
    std::vector<std::string> active_apps; // 该时段活跃的应用

    void update(double activity, int freq, const std::vector<std::string>& apps) {
        // 指数移动平均，更新活动水平和检查频率
        activity_level = (activity_level * 0.8) + (activity * 0.2);
        check_frequency = static_cast<int>(check_frequency * 0.8 + freq * 0.2);
        
        // 更新活跃应用列表
        std::set<std::string> current_apps(apps.begin(), apps.end());
        std::set<std::string> existing_apps(active_apps.begin(), active_apps.end());
        
        // 合并应用列表，保留最活跃的应用（最多10个）
        for (const auto& app : current_apps) {
            if (std::find(active_apps.begin(), active_apps.end(), app) == active_apps.end()) {
                active_apps.push_back(app);
            }
        }
        
        // 如果应用过多，保留最近添加的应用
        if (active_apps.size() > 10) {
            active_apps.erase(active_apps.begin(), active_apps.begin() + (active_apps.size() - 10));
        }
    }
};
class UserHabitManager {
public:
    UserHabitManager() { 
        loadHabits(); 
        habits.last_save = std::chrono::system_clock::now();
        habits.last_full_save = habits.last_save;
        init_learning_phase();
    }
        enum class LearningIntensity {
        HIGH,    // 高频学习（前24小时）
        MEDIUM,  // 中频学习（24-48小时）
        LOW,     // 低频学习（48-72小时）
        STABLE   // 稳定阶段（72小时后）
    };
    ~UserHabitManager() { saveHabits(true); }

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
        
        // 标记此应用为已修改
        habits.markAppModified(package_name);
        
        // 检查是否应该保存习惯数据
        checkAndSaveHabits();
    }

    void updateScreenStats(bool is_screen_on, int duration) {
        habits.screen_on_duration_avg = static_cast<int>(
            habits.screen_on_duration_avg * (1 - habits.learning_weight) +
            duration * habits.learning_weight
        );
        if (is_screen_on) {
            for (auto& [pkg, stats] : habits.app_stats) {
                stats.updateImportanceWeight();
                habits.markAppModified(pkg);
            }
        }
        updateHabits();
        
        // 检查是否应该保存习惯数据
        checkAndSaveHabits();
    }
    
    void captureAdditionalData() {
        // 捕获系统状态、内存使用情况等附加数据
        try {
            captureBatteryStats();
            captureMemoryStats();
            captureNetworkActivity();
        } catch (const std::exception& e) {
            Logger::log(Logger::Level::WARN, std::format("Error capturing additional data: {}", e.what()));
        }
    }

    const UserHabits& getHabits() const { return habits; }
    
    // 学习阶段调整
    void adjustLearningIntensity() {
        auto now = std::chrono::system_clock::now();
        auto learning_elapsed = std::chrono::duration_cast<std::chrono::hours>(now - learning_start).count();
        
        // 根据学习进度调整检查频率
        if (learning_elapsed < 24) { // 第一天，高频学习
            learning_intensity = LearningIntensity::HIGH;
        } else if (learning_elapsed < 48) { // 第二天，中频学习
            learning_intensity = LearningIntensity::MEDIUM;
        } else if (learning_elapsed < 72) { // 第三天，低频学习
            learning_intensity = LearningIntensity::LOW;
        } else { // 学习阶段完成，进入稳定阶段
            learning_intensity = LearningIntensity::STABLE;
        }
        
        // 记录学习阶段变化
        if (learning_intensity != last_learning_intensity) {
            Logger::log(Logger::Level::INFO, std::format("Learning intensity changed from {} to {}", 
                        static_cast<int>(last_learning_intensity), static_cast<int>(learning_intensity)));
            last_learning_intensity = learning_intensity;
            habits.needs_full_save = true;
        }
    }
    
    LearningIntensity getLearningIntensity() const {
        return learning_intensity;
    }

private:
    UserHabits habits;
    std::chrono::system_clock::time_point learning_start;
    LearningIntensity learning_intensity{LearningIntensity::HIGH};
    LearningIntensity last_learning_intensity{LearningIntensity::HIGH};
    
    // 系统资源使用统计
    struct SystemStats {
        int battery_level{0};
        double avg_cpu_usage{0.0};
        long total_memory{0};
        long avg_free_memory{0};
        double network_tx_rate{0.0};
        double network_rx_rate{0.0};
        int samples{0};
    } system_stats;
    
    void init_learning_phase() {
        learning_start = std::chrono::system_clock::now();
        learning_intensity = LearningIntensity::HIGH;
        last_learning_intensity = learning_intensity;
        Logger::log(Logger::Level::INFO, "Started learning phase with HIGH intensity");
    }

    void updateHabits() {
        habits.habit_samples++;
        habits.last_update = std::chrono::system_clock::now();
        habits.updateLearningProgress();

        int hour = getCurrentHour();
        double activity = calculateActivityLevel();
        
        // 收集当前活跃的应用
        std::vector<std::string> active_apps;
        for (const auto& [pkg, stats] : habits.app_stats) {
            if (stats.importance_weight > 20.0 || stats.last_usage_hour == hour) {
                active_apps.push_back(pkg);
            }
        }
        
        habits.updateTimePattern(hour, activity, habits.app_switch_frequency, active_apps);
        
        // 定期调整学习强度
        adjustLearningIntensity();
    }

    double calculateActivityLevel() const {
        if (habits.app_stats.empty()) return 0.0;
        
        double total_activity = 0.0;
        int active_apps = 0;
        
        for (const auto& [pkg, stats] : habits.app_stats) {
            if (stats.importance_weight > 0) {
                total_activity += stats.importance_weight;
                active_apps++;
            }
        }
        
        return active_apps > 0 ? total_activity / active_apps : 0.0;
    }
    
    void checkAndSaveHabits() {
        bool should_save = habits.shouldSaveNow();
        bool should_full_save = habits.shouldFullSaveNow();
        
        if (should_full_save) {
            saveHabits(true);
            habits.updateLastFullSaveTime();
            Logger::log(Logger::Level::INFO, "Performed full save of user habits");
        } else if (should_save) {
            saveHabits(false);
            habits.updateLastSaveTime();
            Logger::log(Logger::Level::INFO, "Performed incremental save of user habits");
        }
    }

    void loadHabits() {
        const std::string config_path = "/data/adb/modules/DeepSuppressor/module_settings/user_habits.json";
        int fd = open(config_path.c_str(), O_RDONLY);
        if (fd == -1) {
            Logger::log(Logger::Level::INFO, "No existing habits file found, starting fresh");
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
            
            // 保存版本号
            habits.save_version = j.value("save_version", 0);
            
            // 加载应用统计信息
            if (j.contains("app_stats") && j["app_stats"].is_object()) {
                for (auto& [pkg, stats_json] : j["app_stats"].items()) {
                    AppStats stats;
                    
                    // 基本统计信息
                    stats.usage_count = stats_json.value("usage_count", 0);
                    stats.total_foreground_time = stats_json.value("total_foreground_time", 0);
                    stats.total_background_time = stats_json.value("total_background_time", 0);
                    stats.switch_count = stats_json.value("switch_count", 0);
                    stats.importance_weight = stats_json.value("importance_weight", 0.0);
                    stats.last_usage_hour = stats_json.value("last_usage_hour", -1);
                    stats.usage_pattern_score = stats_json.value("usage_pattern_score", 0.0);
                    
                    // 新增字段
                    stats.last_used_day = stats_json.value("last_used_day", -1);
                    stats.consecutive_days_used = stats_json.value("consecutive_days_used", 0);
                    
                    // 小时使用情况
                    if (stats_json.contains("hourly_usage") && stats_json["hourly_usage"].is_array()) {
                        auto& hourly_array = stats_json["hourly_usage"];
                        for (size_t i = 0; i < std::min(size_t(24), hourly_array.size()); ++i) {
                            stats.hourly_usage[i] = hourly_array[i];
                        }
                    }
                    
                    habits.app_stats[pkg] = stats;
                }
            }
            
            // 基本习惯信息
            habits.screen_on_duration_avg = j.value("screen_on_duration_avg", 0);
            habits.app_switch_frequency = j.value("app_switch_frequency", 0);
            habits.habit_samples = j.value("habit_samples", 0);
            habits.learning_weight = j.value("learning_weight", 0.7);
            habits.learning_hours = j.value("learning_hours", 0);
            habits.learning_complete = j.value("learning_complete", false);
            
            // 时间戳转换
            if (j.contains("last_update")) {
                habits.last_update = std::chrono::system_clock::from_time_t(j["last_update"]);
            } else {
                habits.last_update = std::chrono::system_clock::now();
            }
            
            if (j.contains("last_full_save")) {
                habits.last_full_save = std::chrono::system_clock::from_time_t(j["last_full_save"]);
            } else {
                habits.last_full_save = habits.last_update;
            }
            
            // 加载每日模式
            if (j.contains("daily_patterns") && j["daily_patterns"].is_array()) {
                auto& patterns_array = j["daily_patterns"];
                for (size_t i = 0; i < std::min(size_t(24), patterns_array.size()); ++i) {
                    auto& pattern_json = patterns_array[i];
                    auto& pattern = habits.daily_patterns[i];
                    
                    pattern.hour = pattern_json.value("hour", static_cast<int>(i));
                    pattern.activity_level = pattern_json.value("activity_level", 0.0);
                    pattern.check_frequency = pattern_json.value("check_frequency", 0);
                    
                    // 加载活跃应用列表
                    if (pattern_json.contains("active_apps") && pattern_json["active_apps"].is_array()) {
                        for (const auto& app : pattern_json["active_apps"]) {
                            if (app.is_string()) {
                                pattern.active_apps.push_back(app);
                            }
                        }
                    }
                }
            }
            
            // 如果已经是学习完成状态，则跳过高强度学习阶段
            if (habits.learning_complete) {
                learning_intensity = LearningIntensity::STABLE;
                last_learning_intensity = learning_intensity;
            }
            
            Logger::log(Logger::Level::INFO, "User habits loaded successfully");
        } catch (const std::exception& e) {
            Logger::log(Logger::Level::WARN, std::format("Failed to load user habits: {}", e.what()));
        }
    }

    void saveHabits(bool full_save = false) {
        const std::string config_path = "/data/adb/modules/DeepSuppressor/module_settings/user_habits.json";
        // 创建备份文件
        if (full_save) {
            std::string backup_path = config_path + ".bak";
            unlink(backup_path.c_str());
            rename(config_path.c_str(), backup_path.c_str());
        }
        
        int fd = open(config_path.c_str(), 
                     full_save ? (O_WRONLY | O_CREAT | O_TRUNC) : (O_WRONLY | O_CREAT | O_APPEND), 
                     0644);
        if (fd == -1) {
            Logger::log(Logger::Level::ERROR, "Failed to open habits file for writing");
            return;
        }

        try {
            nlohmann::json j;
            
            if (full_save) {
                // 完整保存 - 保存所有数据
                habits.save_version++;
                j["save_version"] = habits.save_version;
                
                // 保存应用统计信息
                for (const auto& [pkg, stats] : habits.app_stats) {
                    nlohmann::json stats_json;
                    
                    // 基本统计信息
                    stats_json["usage_count"] = stats.usage_count;
                    stats_json["total_foreground_time"] = stats.total_foreground_time;
                    stats_json["total_background_time"] = stats.total_background_time;
                    stats_json["switch_count"] = stats.switch_count;
                    stats_json["importance_weight"] = stats.importance_weight;
                    stats_json["last_usage_hour"] = stats.last_usage_hour;
                    stats_json["usage_pattern_score"] = stats.usage_pattern_score;
                    
                    // 新增字段
                    stats_json["last_used_day"] = stats.last_used_day;
                    stats_json["consecutive_days_used"] = stats.consecutive_days_used;
                    
                    // 小时使用情况
                    nlohmann::json hourly_array = nlohmann::json::array();
                    for (int usage : stats.hourly_usage) {
                        hourly_array.push_back(usage);
                    }
                    stats_json["hourly_usage"] = hourly_array;
                    
                    j["app_stats"][pkg] = stats_json;
                }
                
                // 基本习惯信息
                j["screen_on_duration_avg"] = habits.screen_on_duration_avg;
                j["app_switch_frequency"] = habits.app_switch_frequency;
                j["habit_samples"] = habits.habit_samples;
                j["last_update"] = std::chrono::system_clock::to_time_t(habits.last_update);
                j["last_full_save"] = std::chrono::system_clock::to_time_t(habits.last_full_save);
                j["learning_weight"] = habits.learning_weight;
                j["learning_hours"] = habits.learning_hours;
                j["learning_complete"] = habits.learning_complete;
                j["learning_intensity"] = static_cast<int>(learning_intensity);

                // 保存每日模式
                nlohmann::json patterns_array = nlohmann::json::array();
                for (const auto& pattern : habits.daily_patterns) {
                    nlohmann::json pattern_json;
                    pattern_json["hour"] = pattern.hour;
                    pattern_json["activity_level"] = pattern.activity_level;
                    pattern_json["check_frequency"] = pattern.check_frequency;
                    
                    // 保存活跃应用列表
                    nlohmann::json active_apps_array = nlohmann::json::array();
                    for (const auto& app : pattern.active_apps) {
                        active_apps_array.push_back(app);
                    }
                    pattern_json["active_apps"] = active_apps_array;
                    
                    patterns_array.push_back(pattern_json);
                }
                j["daily_patterns"] = patterns_array;
                
                // 清除修改记录
                habits.clearModifiedApps();
            } else {
                // 增量保存 - 只保存修改的数据
                j["save_version"] = habits.save_version;  // 使用相同的版本号
                j["incremental"] = true;
                j["timestamp"] = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
                
                // 保存修改的应用统计
                for (const auto& [pkg, stats] : habits.modified_stats) {
                    nlohmann::json stats_json;
                    
                    stats_json["usage_count"] = stats.usage_count;
                    stats_json["total_foreground_time"] = stats.total_foreground_time;
                    stats_json["total_background_time"] = stats.total_background_time;
                    stats_json["switch_count"] = stats.switch_count;
                    stats_json["importance_weight"] = stats.importance_weight;
                    stats_json["last_usage_hour"] = stats.last_usage_hour;
                    stats_json["last_used_day"] = stats.last_used_day;
                    stats_json["consecutive_days_used"] = stats.consecutive_days_used;
                    stats_json["usage_pattern_score"] = stats.usage_pattern_score;
                    
                    j["modified_apps"][pkg] = stats_json;
                }
                
                // 更新基本信息
                j["screen_on_duration_avg"] = habits.screen_on_duration_avg;
                j["app_switch_frequency"] = habits.app_switch_frequency;
                j["habit_samples"] = habits.habit_samples;
                j["learning_hours"] = habits.learning_hours;
                j["learning_weight"] = habits.learning_weight;
                j["learning_complete"] = habits.learning_complete;
                
                // 清除当前修改记录
                habits.clearModifiedApps();
            }

            std::string json_str = j.dump(4);
            write(fd, json_str.c_str(), json_str.length());
            fsync(fd);
            Logger::log(Logger::Level::INFO, full_save ? 
                "User habits saved successfully (full save)" : 
                "User habits updated incrementally");
        } catch (const std::exception& e) {
            Logger::log(Logger::Level::ERROR, std::format("Failed to save user habits: {}", e.what()));
        }
        ::close(fd);
    }
    
    // 采集电池状态信息
    void captureBatteryStats() {
        std::string output = executeCommand("dumpsys battery");
        try {
            // 解析电池电量
            size_t level_pos = output.find("level:");
            if (level_pos != std::string::npos) {
                std::string level_str = output.substr(level_pos + 6);
                system_stats.battery_level = std::stoi(level_str);
            }
            
            // 记录电池温度、充电状态等其他信息可以在这里添加
        } catch (const std::exception& e) {
            Logger::log(Logger::Level::WARN, "Failed to parse battery stats");
        }
    }
    
    // 采集内存使用状况
    void captureMemoryStats() {
        std::string output = executeCommand("dumpsys meminfo");
        try {
            // 解析总内存和可用内存
            size_t total_pos = output.find("Total RAM:");
            size_t free_pos = output.find("Free RAM:");
            
            if (total_pos != std::string::npos && free_pos != std::string::npos) {
                std::string total_str = output.substr(total_pos + 10, output.find("K", total_pos) - total_pos - 10);
                std::string free_str = output.substr(free_pos + 9, output.find("K", free_pos) - free_pos - 9);
                
                long total_ram = std::stol(total_str);
                long free_ram = std::stol(free_str);
                
                // 更新系统统计
                if (system_stats.samples == 0) {
                    system_stats.total_memory = total_ram;
                    system_stats.avg_free_memory = free_ram;
                } else {
                    // 使用指数移动平均
                    system_stats.avg_free_memory = static_cast<long>(
                        system_stats.avg_free_memory * 0.8 + free_ram * 0.2);
                }
            }
        } catch (const std::exception& e) {
            Logger::log(Logger::Level::WARN, "Failed to parse memory stats");
        }
    }
    
    // 采集网络活动信息
    void captureNetworkActivity() {
        std::string output = executeCommand("cat /proc/net/dev");
        try {
            // 提取网络接口统计信息
            // 这里简化处理，实际应用中可能需要更复杂的解析
            // 并跟踪不同时间点的数据以计算速率
            system_stats.samples++;
        } catch (const std::exception& e) {
            Logger::log(Logger::Level::WARN, "Failed to parse network stats");
        }
    }
    
    static std::string executeCommand(const std::string& cmd) {
        std::array<char, 1024> buffer;
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
struct UserHabits {
    std::map<std::string, AppStats> app_stats;
    int screen_on_duration_avg{ 0 };
    int app_switch_frequency{ 0 };
    int habit_samples{ 0 };
    std::chrono::system_clock::time_point last_update;
    std::chrono::system_clock::time_point last_save;
    std::chrono::system_clock::time_point last_full_save;
    double learning_weight{ 0.7 };
    std::array<TimePattern, 24> daily_patterns;
    int learning_hours{ 0 };
    bool learning_complete{ false };
    std::map<std::string, AppStats> modified_stats; // 用于增量保存的修改记录
    int save_version{ 0 }; // 保存版本号，用于检测文件变化
    bool needs_full_save{ false }; // 标记是否需要完整保存

    static constexpr int LEARNING_HOURS_TARGET = 72;
    static constexpr int SAVE_INTERVAL_MINUTES = 30; // 每30分钟保存一次
    static constexpr int FULL_SAVE_INTERVAL_HOURS = 12; // 每12小时完整保存一次

    void updateTimePattern(int hour, double activity, int freq, const std::vector<std::string>& active_apps) {
        daily_patterns[hour].hour = hour;
        daily_patterns[hour].update(activity, freq, active_apps);
    }

    void updateLearningProgress() {
        if (learning_complete) return;
        
        auto now = std::chrono::system_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::hours>(now - last_update);
        learning_hours += duration.count();
        last_update = now;
        
        // 随着学习时间增加，学习权重逐渐降低
        learning_weight = std::max(0.0, 0.7 - (static_cast<double>(learning_hours) / LEARNING_HOURS_TARGET * 0.7));
        
        if (learning_hours >= LEARNING_HOURS_TARGET) {
            learning_complete = true;
            learning_weight = 0.0;
            needs_full_save = true;
        }
    }
    
    bool shouldSaveNow() const {
        auto now = std::chrono::system_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::minutes>(now - last_save);
        return duration.count() >= SAVE_INTERVAL_MINUTES;
    }
    
    bool shouldFullSaveNow() const {
        auto now = std::chrono::system_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::hours>(now - last_full_save);
        return duration.count() >= FULL_SAVE_INTERVAL_HOURS || needs_full_save;
    }
    
    void updateLastSaveTime() {
        last_save = std::chrono::system_clock::now();
    }
    
    void updateLastFullSaveTime() {
        last_full_save = std::chrono::system_clock::now();
        last_save = last_full_save;
        needs_full_save = false;
    }
    
    void markAppModified(const std::string& package_name) {
        auto it = app_stats.find(package_name);
        if (it != app_stats.end()) {
            modified_stats[package_name] = it->second;
        }
    }
    
    void clearModifiedApps() {
        modified_stats.clear();
    }
};

class IntervalManager {
public:
    IntervalManager(const UserHabits& habits, const UserHabitManager& habit_manager) 
        : habits_(habits), habit_manager_(habit_manager) {}

    std::chrono::seconds getScreenCheckInterval() const {
        // 根据学习阶段调整检查间隔
        auto intensity = habit_manager_.getLearningIntensity();
        
        // 根据当前时间段的活动水平调整屏幕检查间隔
        int current_hour = getCurrentHour();
        const auto& pattern = habits_.daily_patterns[current_hour];
        
        // 基于学习阶段设置基础间隔
        std::chrono::seconds base_interval;
        switch (intensity) {
            case UserHabitManager::LearningIntensity::HIGH:
                base_interval = SCREEN_CHECK_INTERVAL_LEARNING_HIGH;
                break;
            case UserHabitManager::LearningIntensity::MEDIUM:
                base_interval = SCREEN_CHECK_INTERVAL_LEARNING_MEDIUM;
                break;
            case UserHabitManager::LearningIntensity::LOW:
                base_interval = SCREEN_CHECK_INTERVAL_LEARNING_LOW;
                break;
            case UserHabitManager::LearningIntensity::STABLE:
                // 学习完成后，根据当前时段活动水平动态调整
                double activity_factor = std::min(1.0, pattern.activity_level);
                auto dynamic_interval = static_cast<int>(
                    SCREEN_CHECK_INTERVAL_MAX.count() - 
                    (SCREEN_CHECK_INTERVAL_MAX - SCREEN_CHECK_INTERVAL_MIN).count() * activity_factor
                );
                return std::chrono::seconds(dynamic_interval);
        }
        
        // 在学习阶段，适当考虑时间模式，但以学习强度为主
        double activity_factor = std::min(0.5, pattern.activity_level / 2);
        auto adjusted_interval = static_cast<int>(
            base_interval.count() * (1.0 - activity_factor)
        );
        
        return std::chrono::seconds(adjusted_interval);
    }

    std::chrono::seconds getProcessCheckInterval(const std::string& package_name) const {
        auto intensity = habit_manager_.getLearningIntensity();
        
        // 学习阶段使用更短的固定间隔
        if (intensity != UserHabitManager::LearningIntensity::STABLE) {
            switch (intensity) {
                case UserHabitManager::LearningIntensity::HIGH:
                    return PROCESS_CHECK_INTERVAL_LEARNING_HIGH;
                case UserHabitManager::LearningIntensity::MEDIUM:
                    return PROCESS_CHECK_INTERVAL_LEARNING_MEDIUM;
                case UserHabitManager::LearningIntensity::LOW:
                    return PROCESS_CHECK_INTERVAL_LEARNING_LOW;
                default:
                    return PROCESS_CHECK_INTERVAL_DEFAULT;
            }
        }
        
        // 稳定阶段，基于学习到的用户习惯调整
        auto it = habits_.app_stats.find(package_name);
        if (it == habits_.app_stats.end()) {
            return PROCESS_CHECK_INTERVAL_DEFAULT;
        }
        
        // 考虑应用重要性和当前时段
        double importance = it->second.importance_weight;
        int current_hour = getCurrentHour();
        const auto& pattern = habits_.daily_patterns[current_hour];
        
        // 检查应用是否在当前时段活跃
        bool is_active_in_hour = std::find(pattern.active_apps.begin(), 
                                         pattern.active_apps.end(), 
                                         package_name) != pattern.active_apps.end();
        
        // 活跃应用在其活跃时段检查更频繁
        double time_factor = is_active_in_hour ? 0.7 : 1.0;
        
        auto interval = static_cast<long long>(
            PROCESS_CHECK_INTERVAL_MAX.count() -
            (PROCESS_CHECK_INTERVAL_MAX - PROCESS_CHECK_INTERVAL_MIN).count() * 
            (importance / 100.0) * time_factor
        );
        
        interval = std::max(interval, static_cast<long long>(PROCESS_CHECK_INTERVAL_MIN.count()));
        return std::chrono::seconds(interval);
    }

    std::chrono::seconds getKillInterval(const std::string& package_name) const {
        auto intensity = habit_manager_.getLearningIntensity();
        
        // 学习阶段使用更短的固定间隔，但不会太短以避免误杀常用应用
        if (intensity != UserHabitManager::LearningIntensity::STABLE) {
            // 即使在学习阶段，也要考虑应用重要性
            auto it = habits_.app_stats.find(package_name);
            if (it != habits_.app_stats.end() && it->second.importance_weight > 50.0) {
                // 重要应用即使在学习阶段也应该有更长的存活时间
                return KILL_INTERVAL_IMPORTANT_APP;
            }
            
            switch (intensity) {
                case UserHabitManager::LearningIntensity::HIGH:
                    return KILL_INTERVAL_LEARNING_HIGH;
                case UserHabitManager::LearningIntensity::MEDIUM:
                    return KILL_INTERVAL_LEARNING_MEDIUM;
                case UserHabitManager::LearningIntensity::LOW:
                    return KILL_INTERVAL_LEARNING_LOW;
                default:
                    return KILL_INTERVAL_DEFAULT;
            }
        }
        
        // 稳定阶段，基于学习到的用户习惯调整
        auto it = habits_.app_stats.find(package_name);
        if (it == habits_.app_stats.end()) {
            return KILL_INTERVAL_DEFAULT;
        }
        
        // 重要应用有更长的后台存活时间
        double importance = it->second.importance_weight;
        int current_hour = getCurrentHour();
        
        // 检查应用是否在当前时段活跃
        const auto& pattern = habits_.daily_patterns[current_hour];
        bool is_active_in_hour = std::find(pattern.active_apps.begin(), 
                                         pattern.active_apps.end(), 
                                         package_name) != pattern.active_apps.end();
        
        // 在活跃时段，即使在后台也给予更长的存活时间
        double time_factor = is_active_in_hour ? 1.3 : 1.0;
        
        auto interval = static_cast<long long>(
            KILL_INTERVAL_MIN.count() +
            (KILL_INTERVAL_MAX - KILL_INTERVAL_MIN).count() * 
            (importance / 100.0) * time_factor
        );
        
        interval = std::min(interval, static_cast<long long>(KILL_INTERVAL_MAX.count()));
        return std::chrono::seconds(interval);
    }

    std::chrono::seconds getScreenOffSleepInterval() const {
        auto intensity = habit_manager_.getLearningIntensity();
        
        // 学习阶段使用更短的休眠间隔
        if (intensity != UserHabitManager::LearningIntensity::STABLE) {
            switch (intensity) {
                case UserHabitManager::LearningIntensity::HIGH:
                    return SCREEN_OFF_SLEEP_INTERVAL_LEARNING_HIGH;
                case UserHabitManager::LearningIntensity::MEDIUM:
                    return SCREEN_OFF_SLEEP_INTERVAL_LEARNING_MEDIUM;
                case UserHabitManager::LearningIntensity::LOW:
                    return SCREEN_OFF_SLEEP_INTERVAL_LEARNING_LOW;
                default:
                    return SCREEN_OFF_SLEEP_INTERVAL;
            }
        }
        
        return SCREEN_OFF_SLEEP_INTERVAL;
    }

private:
    const UserHabits& habits_;
    const UserHabitManager& habit_manager_;
    
    // 常规阶段间隔
    static constexpr auto SCREEN_CHECK_INTERVAL_MIN = std::chrono::seconds(30);
    static constexpr auto SCREEN_CHECK_INTERVAL_MAX = std::chrono::minutes(5);
    static constexpr auto PROCESS_CHECK_INTERVAL_MIN = std::chrono::seconds(45);
    static constexpr auto PROCESS_CHECK_INTERVAL_MAX = std::chrono::minutes(3);
    static constexpr auto PROCESS_CHECK_INTERVAL_DEFAULT = std::chrono::minutes(1);
    static constexpr auto KILL_INTERVAL_MIN = std::chrono::minutes(5);
    static constexpr auto KILL_INTERVAL_MAX = std::chrono::minutes(30);
    static constexpr auto KILL_INTERVAL_DEFAULT = std::chrono::minutes(10);
    static constexpr auto SCREEN_OFF_SLEEP_INTERVAL = std::chrono::minutes(1);
    
    // 学习阶段间隔
    static constexpr auto SCREEN_CHECK_INTERVAL_LEARNING_HIGH = std::chrono::seconds(15);
    static constexpr auto SCREEN_CHECK_INTERVAL_LEARNING_MEDIUM = std::chrono::seconds(30);
    static constexpr auto SCREEN_CHECK_INTERVAL_LEARNING_LOW = std::chrono::seconds(45);
    
    static constexpr auto PROCESS_CHECK_INTERVAL_LEARNING_HIGH = std::chrono::seconds(20);
    static constexpr auto PROCESS_CHECK_INTERVAL_LEARNING_MEDIUM = std::chrono::seconds(30);
    static constexpr auto PROCESS_CHECK_INTERVAL_LEARNING_LOW = std::chrono::seconds(40);
    
    static constexpr auto KILL_INTERVAL_LEARNING_HIGH = std::chrono::minutes(3);
    static constexpr auto KILL_INTERVAL_LEARNING_MEDIUM = std::chrono::minutes(5);
    static constexpr auto KILL_INTERVAL_LEARNING_LOW = std::chrono::minutes(7);
    static constexpr auto KILL_INTERVAL_IMPORTANT_APP = std::chrono::minutes(15);
    
    static constexpr auto SCREEN_OFF_SLEEP_INTERVAL_LEARNING_HIGH = std::chrono::seconds(30);
    static constexpr auto SCREEN_OFF_SLEEP_INTERVAL_LEARNING_MEDIUM = std::chrono::seconds(40);
    static constexpr auto SCREEN_OFF_SLEEP_INTERVAL_LEARNING_LOW = std::chrono::seconds(50);
};

class ProcessManager {
private:
    static constexpr auto INITIAL_SCREEN_CHECK_DELAY = std::chrono::minutes(5); // 减少初始延迟
    std::chrono::steady_clock::time_point start_time;
    std::atomic<bool> running{ true };
    bool is_screen_on{ true };
    std::chrono::steady_clock::time_point last_screen_check;
    std::chrono::steady_clock::time_point last_additional_data_capture;
    std::map<std::string, std::chrono::steady_clock::time_point> last_process_check_times;
    UserHabitManager habit_manager;
    IntervalManager interval_manager;
    
    // 优先级管理
    struct ProcessPriority {
        int32_t oom_adj_score{0};    // OOM调整分数
        int32_t scheduling_group{0}; // 调度组
        int32_t nice_value{0};       // nice值
    };
    std::map<std::string, ProcessPriority> process_priorities;
    
    // 统计数据
    struct Statistics {
        int total_processes_managed{0};
        int total_processes_killed{0};
        std::map<std::string, int> killed_count_by_package;
        std::chrono::steady_clock::time_point start_time;
        int total_check_cycles{0};
        double avg_check_duration_ms{0.0};
    } stats;

    struct Target {
        std::string package_name;
        std::vector<std::string> process_names;
        bool is_foreground;
        std::chrono::steady_clock::time_point last_background_time;
        int switch_count{ 0 };
        std::chrono::steady_clock::time_point last_switch_time;
        int cpu_usage_percent{0};
        int memory_usage_kb{0};
        bool is_sticky{false};  // 表示应该避免被杀死的应用
        int last_priority{0};
        std::chrono::steady_clock::time_point last_resource_check;

        Target(std::string pkg, std::vector<std::string> procs)
            : package_name(std::move(pkg)), process_names(std::move(procs)), is_foreground(false),
              last_switch_time(std::chrono::steady_clock::now()),
              last_resource_check(std::chrono::steady_clock::now()) {}
    };

    std::vector<Target> targets;

    static std::string executeCommand(const std::string& cmd) {
        std::array<char, 256> buffer; // 增加缓冲区大小
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
            return true; // 默认屏幕开启，避免误杀进程
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
        Logger::log(Logger::Level::INFO, "Screen state: " + state + " (" + (screen_on ? "on" : "off") + ")");
        return screen_on;
    }

    bool shouldCheckProcesses(const std::string& package_name) {
        auto now = std::chrono::steady_clock::now();
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

    bool isProcessForeground(const std::string& package_name) noexcept {
        std::string output = executeCommand("dumpsys window");
        
        // 优化：只检查关键部分
        const std::string focus_markers[] = {"mCurrentFocus", "mFocusedWindow"};
        bool found_focus = false;
        std::string current_pkg;
        
        for (const auto& marker : focus_markers) {
            size_t pos = output.find(marker);
            if (pos != std::string::npos) {
                // 找到行的结束位置
                size_t line_end = output.find('\n', pos);
                if (line_end == std::string::npos) line_end = output.length();
                
                // 提取这一行
                std::string line = output.substr(pos, line_end - pos);
                
                // 查找 Window{...} 模式
                size_t window_pos = line.find("Window{");
                if (window_pos != std::string::npos) {
                    // 找到 Window{...} 内容
                    size_t content_start = window_pos + 7; // 跳过 "Window{"
                    size_t content_end = line.find('}', content_start);
                    
                    if (content_end != std::string::npos) {
                        std::string window_content = line.substr(content_start, content_end - content_start);
                        
                        // 查找包含 '/' 的部分，通常格式为 "hash u0 package.name/activity.name"
                        size_t slash_pos = window_content.find('/');
                        if (slash_pos != std::string::npos) {
                            // 从斜杠向前查找包名的起始位置
                            size_t pkg_start = window_content.rfind(' ', slash_pos);
                            if (pkg_start != std::string::npos) {
                                pkg_start++; // 跳过空格
                                current_pkg = window_content.substr(pkg_start, slash_pos - pkg_start);
                                found_focus = true;
                                
                                // 如果找到匹配的包名，立即返回
                                if (current_pkg == package_name) {
                                    return true;
                                }
                            }
                        }
                    }
                }
            }
        }
        
        return false;
    }

    void killProcess(const std::string& process_name, const std::string& package_name) {
        // 使用更可靠的方法杀死进程
        std::string cmd = "am force-stop " + package_name;
        system(cmd.c_str());
        
        // 记录操作
        Logger::log(Logger::Level::INFO, "Killed processes for package: " + package_name);
        stats.total_processes_killed++;
        stats.killed_count_by_package[package_name]++;
    }
    
    void adjustProcessPriority(Target& target) {
        if (!target.is_foreground) {
            // 后台进程，根据重要性设置优先级
            auto it = habit_manager.getHabits().app_stats.find(target.package_name);
            if (it != habit_manager.getHabits().app_stats.end()) {
                double importance = it->second.importance_weight;
                
                // 设置OOM调整分数 - 对重要应用更友好
                int oom_adj = static_cast<int>(900 - importance * 8);  // 范围约为100-900
                oom_adj = std::max(100, std::min(900, oom_adj));
                
                // 设置nice值 - 重要应用获得更好的CPU优先级
                int nice_value = static_cast<int>(10 + (100 - importance) / 10);  // 范围约为10-20
                nice_value = std::max(10, std::min(19, nice_value));
                
                // 记录优先级值
                ProcessPriority priority;
                priority.oom_adj_score = oom_adj;
                priority.nice_value = nice_value;
                process_priorities[target.package_name] = priority;
                
                // 应用优先级设置
                std::string pid_cmd = "pidof " + target.process_names[0];
                std::string pid_output = executeCommand(pid_cmd);
                if (!pid_output.empty()) {
                    try {
                        int pid = std::stoi(pid_output);
                        
                        // 设置OOM调整值
                        std::string oom_cmd = "echo " + std::to_string(oom_adj) + 
                                            " > /proc/" + std::to_string(pid) + "/oom_score_adj";
                        system(oom_cmd.c_str());
                        
                        // 设置nice值
                        std::string nice_cmd = "renice -n " + std::to_string(nice_value) + 
                                            " -p " + std::to_string(pid);
                        system(nice_cmd.c_str());
                        
                        Logger::log(Logger::Level::INFO, 
                            std::format("Adjusted priority for {}: OOM={}, nice={}", 
                                      target.package_name, oom_adj, nice_value));
                    } catch (const std::exception& e) {
                        // PID解析失败
                    }
                }
            }
        } else {
            // 前台进程应当获得较高优先级
            ProcessPriority priority;
            priority.oom_adj_score = 0;  // 最低的OOM分数
            priority.nice_value = 0;     // 最高的CPU优先级
            process_priorities[target.package_name] = priority;
            
            // 查找PID并应用优先级
            std::string pid_cmd = "pidof " + target.process_names[0];
            std::string pid_output = executeCommand(pid_cmd);
            if (!pid_output.empty()) {
                try {
                    int pid = std::stoi(pid_output);
                    
                    // 设置最佳优先级
                    std::string oom_cmd = "echo 0 > /proc/" + std::to_string(pid) + "/oom_score_adj";
                    system(oom_cmd.c_str());
                    
                    std::string nice_cmd = "renice -n 0 -p " + std::to_string(pid);
                    system(nice_cmd.c_str());
                    
                    Logger::log(Logger::Level::INFO, 
                        std::format("Set high priority for foreground app {}", target.package_name));
                } catch (const std::exception& e) {
                    // PID解析失败
                }
            }
        }
    }
    
    void collectProcessResourceUsage(Target& target) {
        auto now = std::chrono::steady_clock::now();
        if (now - target.last_resource_check < std::chrono::seconds(30)) {
            return;  // 避免频繁检查
        }
        
        // 收集内存使用情况
        std::string mem_cmd = "dumpsys meminfo " + target.package_name + " | grep TOTAL";
        std::string mem_output = executeCommand(mem_cmd);
        
        if (!mem_output.empty()) {
            try {
                // 从输出中提取内存使用值（KB）
                size_t pos = mem_output.find_first_of("0123456789");
                if (pos != std::string::npos) {
                    std::string mem_str = mem_output.substr(pos);
                    target.memory_usage_kb = std::stoi(mem_str);
                }
            } catch (const std::exception& e) {
                // 解析失败
            }
        }
        
        // 收集CPU使用情况
        std::string cpu_cmd = "dumpsys cpuinfo | grep " + target.package_name;
        std::string cpu_output = executeCommand(cpu_cmd);
        
        if (!cpu_output.empty()) {
            try {
                // 从输出中提取CPU使用百分比
                size_t percent_pos = cpu_output.find('%');
                if (percent_pos != std::string::npos && percent_pos > 0) {
                    size_t start_pos = cpu_output.rfind(' ', percent_pos - 1);
                    if (start_pos != std::string::npos) {
                        std::string cpu_str = cpu_output.substr(start_pos + 1, percent_pos - start_pos - 1);
                        target.cpu_usage_percent = static_cast<int>(std::stof(cpu_str));
                    }
                }
            } catch (const std::exception& e) {
                // 解析失败
            }
        }
        
        target.last_resource_check = now;
    }

    void checkScreenState() {
        auto now = std::chrono::steady_clock::now();
        if (now - start_time < INITIAL_SCREEN_CHECK_DELAY ||
            now - last_screen_check < interval_manager.getScreenCheckInterval()) {
            return;
        }

        bool previous_screen_state = is_screen_on;
        is_screen_on = isScreenOn();
        
        // 计算自上次检查以来的时间
        int duration = std::chrono::duration_cast<std::chrono::seconds>(now - last_screen_check).count();
        last_screen_check = now;
        
        // 更新屏幕统计信息
        habit_manager.updateScreenStats(previous_screen_state, duration);

        // 屏幕关闭时处理
        if (!is_screen_on && previous_screen_state) {
            Logger::log(Logger::Level::INFO, "Screen turned off, entering deep sleep mode");
            handleScreenOff();
        }
        
        // 收集额外系统数据
        if (now - last_additional_data_capture > std::chrono::minutes(15)) {
            habit_manager.captureAdditionalData();
            last_additional_data_capture = now;
        }
    }

    void handleScreenOff() {
        // 屏幕关闭时，根据学习阶段和应用重要性智能清理
        auto intensity = habit_manager.getLearningIntensity();
        
        // 仅在稳定阶段或低学习强度时执行智能清理
        if (intensity == UserHabitManager::LearningIntensity::STABLE ||
            intensity == UserHabitManager::LearningIntensity::LOW) {
            for (auto& target : targets) {
                if (!target.is_foreground) {
                    // 检查应用重要性
                    auto it = habit_manager.getHabits().app_stats.find(target.package_name);
                    if (it != habit_manager.getHabits().app_stats.end()) {
                        double importance = it->second.importance_weight;
                        if (importance < 30.0) {  // 只杀死不太重要的应用
                            killProcess(target.process_names[0], target.package_name);
                        }
                    } else {
                        // 未知应用，默认杀死
                        killProcess(target.process_names[0], target.package_name);
                    }
                }
            }
        } else {
            // 学习阶段，更保守的清理策略
            for (auto& target : targets) {
                if (!target.is_foreground && !target.is_sticky) {
                    killProcess(target.process_names[0], target.package_name);
                }
            }
        }
        
        std::this_thread::sleep_for(interval_manager.getScreenOffSleepInterval());
    }

    void checkProcesses() {
        bool any_active = false;
        std::vector<std::string> active_apps;
        auto check_start_time = std::chrono::steady_clock::now();

        for (auto& target : targets) {
            try {
                bool should_check = shouldCheckProcesses(target.package_name);
                if (should_check || target.is_foreground) {
                    bool current_foreground = isProcessForeground(target.package_name);
                    bool should_kill = false;

                    auto now = std::chrono::steady_clock::now();
                    int duration = std::chrono::duration_cast<std::chrono::seconds>(
                        now - target.last_switch_time).count();

                    // 收集资源使用情况
                    collectProcessResourceUsage(target);
                    
                    // 状态变化处理
                    if (current_foreground != target.is_foreground) {
                        // 更新状态
                        target.is_foreground = current_foreground;
                        target.last_switch_time = now;
                        
                        if (!current_foreground) {
                            // 从前台切换到后台
                            target.last_background_time = now;
                        }
                        
                        target.switch_count++;
                        habit_manager.updateAppStats(target.package_name, current_foreground, duration);
                        
                        Logger::log(Logger::Level::INFO, "Package " + target.package_name +
                            (current_foreground ? " moved to foreground" : " moved to background"));
                    }

                    // 确定是否应该杀死进程
                    if (!current_foreground && !target.is_foreground && should_check && !target.is_sticky) {
                        auto background_duration = now - target.last_background_time;
                        auto kill_interval = interval_manager.getKillInterval(target.package_name);
                        
                        // 检查内存和CPU使用情况
                        bool resource_heavy = target.memory_usage_kb > 150000 || target.cpu_usage_percent > 5;
                        
                        // 根据资源使用情况调整kill间隔
                        if (resource_heavy) {
                            kill_interval = std::chrono::duration_cast<std::chrono::seconds>(
                                kill_interval * 0.7); // 对资源占用高的应用更积极清理
                        }
                        
                        if (background_duration >= kill_interval) {
                            should_kill = true;
                            Logger::log(Logger::Level::INFO, 
                                std::format("Killing {} - background for {}s, memory: {}KB, CPU: {}%", 
                                target.package_name, 
                                std::chrono::duration_cast<std::chrono::seconds>(background_duration).count(),
                                target.memory_usage_kb,
                                target.cpu_usage_percent));
                        }
                    }

                    // 执行杀死进程
                    if (should_kill) {
                        killProcess(target.process_names[0], target.package_name);
                    } else if (should_check) {
                        // 调整进程优先级
                        adjustProcessPriority(target);
                    }

                    // 记录活跃应用
                    if (current_foreground) {
                        any_active = true;
                        active_apps.push_back(target.package_name);
                    }
                }
            } catch (const std::exception& e) {
                Logger::log(Logger::Level::ERROR,
                    std::format("Error processing target {}: {}", target.package_name, e.what()));
            }
        }

        // 更新检查时长统计
        auto check_duration = std::chrono::steady_clock::now() - check_start_time;
        double duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(check_duration).count();
        stats.total_check_cycles++;
        stats.avg_check_duration_ms = (stats.avg_check_duration_ms * (stats.total_check_cycles - 1) + duration_ms) / stats.total_check_cycles;
        
        // 根据当前活跃状态和检查耗时决定下次检查间隔
        auto sleep_time = any_active ?
            interval_manager.getProcessCheckInterval(active_apps.empty() ? "" : active_apps[0]) :
            interval_manager.getScreenCheckInterval();
            
        // 如果检查耗时过长，适当增加休眠时间以减少CPU占用
        if (duration_ms > 200) {
            sleep_time = std::chrono::duration_cast<std::chrono::seconds>(
                sleep_time * 1.2); // 增加20%的休眠时间
        }
            
        std::this_thread::sleep_for(sleep_time);
    }
    
    void dumpStatistics() {
        auto now = std::chrono::steady_clock::now();
        auto total_runtime = std::chrono::duration_cast<std::chrono::hours>(now - stats.start_time).count();
        
        Logger::log(Logger::Level::INFO, std::format(
            "--- Statistics after {} hours ---", total_runtime));
        Logger::log(Logger::Level::INFO, std::format(
            "Total processes managed: {}", stats.total_processes_managed));
        Logger::log(Logger::Level::INFO, std::format(
            "Total processes killed: {}", stats.total_processes_killed));
        Logger::log(Logger::Level::INFO, std::format(
            "Average check duration: {:.2f}ms", stats.avg_check_duration_ms));
        
        // 输出每个应用的杀死次数
        std::string kill_stats = "Kill counts by package: ";
        for (const auto& [pkg, count] : stats.killed_count_by_package) {
            kill_stats += pkg + "(" + std::to_string(count) + ") ";
        }
        Logger::log(Logger::Level::INFO, kill_stats);
        
        // 输出学习状态
        Logger::log(Logger::Level::INFO, std::format(
            "Learning hours: {}, Learning intensity: {}", 
            habit_manager.getHabits().learning_hours, 
            static_cast<int>(habit_manager.getLearningIntensity())));
    }

public:
    explicit ProcessManager(const std::vector<std::pair<std::string, std::vector<std::string>>>& initial_targets)
        : start_time(std::chrono::steady_clock::now()), 
          interval_manager(habit_manager.getHabits(), habit_manager),
          last_additional_data_capture(std::chrono::steady_clock::now()) {
        for (const auto& [pkg, procs] : initial_targets) {
            if (!pkg.empty() && !procs.empty()) {
                targets.emplace_back(pkg, procs);
                Logger::log(Logger::Level::INFO, std::format("Added target: {} with {} processes", pkg, procs.size()));
                stats.total_processes_managed++;
            }
        }
        
        stats.start_time = start_time;
    }

    void start() {
        Logger::log(Logger::Level::INFO, std::format("Process manager started with {} targets", targets.size()));
        last_screen_check = std::chrono::steady_clock::now();
        std::chrono::steady_clock::time_point last_stats_dump = start_time;

        while (running) {
            try {
                // 检查屏幕状态
                checkScreenState();
                
                // 根据屏幕状态决定行为
                if (is_screen_on) {
                    checkProcesses();
                } else {
                    // 屏幕关闭时使用较长的休眠时间
                    std::this_thread::sleep_for(interval_manager.getScreenOffSleepInterval());
                }
                
                // 定期输出统计信息
                auto now = std::chrono::steady_clock::now();
                if (now - last_stats_dump > std::chrono::hours(6)) {
                    dumpStatistics();
                    last_stats_dump = now;
                }
                
            } catch (const std::exception& e) {
                Logger::log(Logger::Level::ERROR, std::format("Error in main loop: {}", e.what()));
                std::this_thread::sleep_for(std::chrono::seconds(10)); // 错误恢复延迟
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