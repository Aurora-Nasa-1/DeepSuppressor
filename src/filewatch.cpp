#include <string>
#include <unistd.h>
#include <cerrno>
#include <csignal>
#include <sys/inotify.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/resource.h>

#define EVENT_SIZE (sizeof(struct inotify_event))
#define BUF_LEN (512 * (EVENT_SIZE + 16))  // 减小缓冲区大小以节省内存

static int fd, wd;
static volatile sig_atomic_t running = 1;  // 使用 sig_atomic_t 确保原子操作
static std::string target_file;
static std::string script_path;
static std::string shell_command;
static int daemon_mode = 0;
static int verbose = 0;
static int check_interval = 30;  // 默认监听间隔改为30秒
static int low_power_mode = 1;   // 默认开启低功耗模式

// 智能休眠时间控制
static struct {
    unsigned int base_interval;   // 基础休眠时间
    unsigned int max_interval;    // 最大休眠时间
    unsigned int current;         // 当前休眠时间
} sleep_control = {500000, 5000000, 500000};

void handle_signal(int sig) {
    (void)sig;
    running = 0;
}

void optimize_process_priority() {
    // 设置进程优先级为低优先级，减少CPU使用
    setpriority(PRIO_PROCESS, 0, 19);
    
    // 设置进程资源限制
    struct rlimit rlim;
    rlim.rlim_cur = rlim.rlim_max = BUF_LEN;
    setrlimit(RLIMIT_AS, &rlim);  // 限制内存使用
}

void daemonize() {
    pid_t pid = fork();
    if (pid < 0) exit(EXIT_FAILURE);
    if (pid > 0) exit(EXIT_SUCCESS);
    
    if (setsid() < 0) exit(EXIT_FAILURE);
    signal(SIGHUP, SIG_IGN);
    
    pid = fork();
    if (pid < 0) exit(EXIT_FAILURE);
    if (pid > 0) exit(EXIT_SUCCESS);
    
    chdir("/");
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);
    
    open("/dev/null", O_RDWR);
    dup(0);
    dup(0);
}

void execute_script() {
    if (!shell_command.empty()) {
        system(shell_command.c_str());
    } else {
        system(script_path.c_str());
    }
}

void print_usage(const char *prog_name) {
    write(STDOUT_FILENO, "Usage: ", 7);
    write(STDOUT_FILENO, prog_name, strlen(prog_name));
    write(STDOUT_FILENO, " [options] <file_to_monitor> <script_to_execute>\n", 47);
    write(STDOUT_FILENO, "Options:\n", 9);
    write(STDOUT_FILENO, "  -d            Run in daemon mode\n", 34);
    write(STDOUT_FILENO, "  -v            Enable verbose logging\n", 38);
    write(STDOUT_FILENO, "  -i <seconds>  Set check interval (default 30 seconds)\n", 53);
    write(STDOUT_FILENO, "  -c <command>  Execute shell command instead of script file\n", 59);
    write(STDOUT_FILENO, "  -l            Enable low power mode (default: enabled)\n", 54);
    write(STDOUT_FILENO, "  -h            Display this help information\n", 44);
}

void adjust_sleep_interval(bool file_changed) {
    if (file_changed) {
        // 如果文件发生变化，重置为基础间隔
        sleep_control.current = sleep_control.base_interval;
    } else {
        // 如果文件没有变化，逐渐增加休眠时间
        sleep_control.current = std::min(sleep_control.current * 2, sleep_control.max_interval);
    }
}

int main(int argc, char *argv[]) {
    int opt;
    while ((opt = getopt(argc, argv, "dvi:c:lh")) != -1) {
        switch (opt) {
            case 'd': daemon_mode = 1; break;
            case 'v': verbose = 1; break;
            case 'i': 
                check_interval = atoi(optarg);
                if (check_interval < 1) check_interval = 30;
                break;
            case 'c': shell_command = optarg; break;
            case 'l': low_power_mode = 1; break;
            case 'h': print_usage(argv[0]); return EXIT_SUCCESS;
            default: print_usage(argv[0]); return EXIT_FAILURE;
        }
    }
    
    if (optind >= argc) {
        write(STDERR_FILENO, "Error: Missing file path to monitor\n", 35);
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }
    
    target_file = argv[optind];
    
    if (shell_command.empty() && optind + 1 >= argc) {
        write(STDERR_FILENO, "Error: No shell command (-c) or script path provided\n", 51);
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }
    
    if (shell_command.empty()) {
        script_path = argv[optind + 1];
    }
    
    if (access(target_file.c_str(), F_OK) == -1) {
        write(STDERR_FILENO, "Error: Cannot access monitored file\n", 35);
        return EXIT_FAILURE;
    }
    
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);
    
    if (daemon_mode) {
        daemonize();
    }
    
    optimize_process_priority();  // 优化进程优先级
    
    fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (fd < 0) return EXIT_FAILURE;
    
    wd = inotify_add_watch(fd, target_file.c_str(), IN_MODIFY | IN_ATTRIB);
    if (wd < 0) {
        close(fd);
        return EXIT_FAILURE;
    }
    
    struct pollfd fds[1];
    fds[0].fd = fd;
    fds[0].events = POLLIN;
    
    char buffer[BUF_LEN] __attribute__((aligned(8)));  // 内存对齐优化
    bool file_changed = false;
    
    while (running) {
        int poll_ret = poll(fds, 1, check_interval * 1000);
        
        if (poll_ret < 0) {
            if (errno == EINTR) continue;
            break;
        }
        
        if (poll_ret == 0) {
            if (low_power_mode) {
                adjust_sleep_interval(false);
                usleep(sleep_control.current);
            }
            continue;
        }
        
        if (fds[0].revents & POLLIN) {
            int length = read(fd, buffer, BUF_LEN);
            if (length < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) continue;
                break;
            }
            
            int i = 0;
            file_changed = false;
            while (i < length) {
                struct inotify_event *event = (struct inotify_event*)&buffer[i];
                if (event->mask & (IN_MODIFY | IN_ATTRIB)) {
                    file_changed = true;
                    execute_script();
                    if (low_power_mode) {
                        adjust_sleep_interval(true);
                        sleep(3);  // 执行后休眠3秒，避免频繁执行
                    }
                }
                i += EVENT_SIZE + event->len;
            }
        }
    }
    
    inotify_rm_watch(fd, wd);
    close(fd);
    
    return EXIT_SUCCESS;
}