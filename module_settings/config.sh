#!/system/bin/sh
action_id="DeepSuppressor"
action_name="Deep Suppressor"
action_author="AuroraNasa & AI"
action_description="Background Process Suppressor"

print_languages="en"                   # Default language for printing

# About Install
magisk_min_version="25400"            # Minimum required version of Magisk
ksu_min_version="11300"               # Minimum compatible version of KernelSU
ksu_min_kernel_version="11300"        # Minimum compatible kernel version of KernelSU
apatch_min_version="10657"            # Minimum compatible version of APatch
ANDROID_API="26"                      # Minimum required Android API level

# Process Manager Configuration
# 格式说明:
# suppress_[包名，点号替换为下划线]=true/false          # 是否启用该应用的进程压制
# suppress_config_[包名，点号替换为下划线]_[序号]=进程名 # 要压制的进程名，序号从1开始
# 示例:
# suppress_com_example_app=true
# suppress_config_com_example_app_1=com.example.process1
# suppress_config_com_example_app_2=com.example.process2
# suppress_config_com_example_app_3=com.example.process3

# 应用1配置（压制多个进程）
suppress_APP_com_tencent_mm=true
suppress_config_com_tencent_mm_1="com.tencent.mm:appbrand0"
suppress_config_com_tencent_mm_2="com.tencent.mm:appbrand1"
#suppress_config_com_tencent_mm_3="com.tencent.mm:push"

# 应用2配置
suppress_APP_com_tencent_tim=true
suppress_config_com_tencent_tim_1="com.tencent.tim:appbrand0"
suppress_config_com_tencent_tim_2="com.tencent.tim:video"
#suppress_config_com_tencent_tim_3="com.tencent.tim:MSF"


