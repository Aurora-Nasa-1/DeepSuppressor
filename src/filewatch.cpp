#include <iostream>
#include <string>
#include <cstdlib>
#include <unistd.h>
#include <cstring>
#include <cerrno>
#include <csignal>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/inotify.h>
#include <fcntl.h>
#include <ctime>
#include <poll.h>

#define EVENT_SIZE (sizeof(struct inotify_event))
#define BUF_LEN (1024 * (EVENT_SIZE + 16))

static int fd, wd;
static int running = 1;
static std::string target_file;
static std::string script_path;
static std::string shell_command;
static int daemon_mode = 0;
static int verbose = 0;
static int check_interval = 1;
static int low_power_mode = 0;

void handle_signal(int sig) {
    (void)sig;
    running = 0;
}

void daemonize() {
    pid_t pid;
    
    pid = fork();
    if (pid < 0) {
        exit(EXIT_FAILURE);
    }
    
    if (pid > 0) {
        exit(EXIT_SUCCESS);
    }
    
    if (setsid() < 0) {
        exit(EXIT_FAILURE);
    }
    
    signal(SIGHUP, SIG_IGN);
    
    pid = fork();
    if (pid < 0) {
        exit(EXIT_FAILURE);
    }
    
    if (pid > 0) {
        exit(EXIT_SUCCESS);
    }
    
    chdir("/");
    
    for (int i = 0; i < 1024; i++) {
        close(i);
    }
    
    open("/dev/null", O_RDWR);
    dup(0);
    dup(0);
}

void execute_script() {
    if (verbose) {
        if (!shell_command.empty()) {
            std::cout << "File " << target_file << " changed, executing shell command" << std::endl;
        } else {
            std::cout << "File " << target_file << " changed, executing script " << script_path << std::endl;
        }
    }
    
    int ret;
    if (!shell_command.empty()) {
        ret = system(shell_command.c_str());
    } else {
        ret = system(script_path.c_str());
    }
    
    if (ret != 0) {
        if (verbose) {
            std::cerr << "Script execution failed, return code: " << ret << std::endl;
        }
    } else if (verbose) {
        std::cout << "Script executed successfully" << std::endl;
    }
}

void print_usage(const char *prog_name) {
    std::cout << "Usage: " << prog_name << " [options] <file_to_monitor> <script_to_execute>" << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout << "  -d            Run in daemon mode" << std::endl;
    std::cout << "  -v            Enable verbose logging" << std::endl;
    std::cout << "  -i <seconds>  Set check interval (default 1 second)" << std::endl;
    std::cout << "  -c <command>  Execute shell command instead of script file" << std::endl;
    std::cout << "  -l            Enable low power mode (increases latency but reduces CPU usage)" << std::endl;
    std::cout << "  -h            Display this help information" << std::endl;
    std::cout << "\nExamples:" << std::endl;
    std::cout << "  " << prog_name << " -d -v /data/adb/modules/mymodule/config.txt /data/adb/modules/mymodule/update.sh" << std::endl;
    std::cout << "  " << prog_name << " -c \"echo 'File changed' >> /data/log.txt\" /data/config.txt" << std::endl;
    std::cout << "  " << prog_name << " -l -i 5 /data/config.txt /data/update.sh" << std::endl;
}

int main(int argc, char *argv[]) {
    int opt;
    
    while ((opt = getopt(argc, argv, "dvi:c:lh")) != -1) {
        switch (opt) {
            case 'd':
                daemon_mode = 1;
                break;
            case 'v':
                verbose = 1;
                break;
            case 'i':
                check_interval = atoi(optarg);
                if (check_interval < 1) check_interval = 1;
                break;
            case 'c':
                shell_command = optarg;
                break;
            case 'l':
                low_power_mode = 1;
                break;
            case 'h':
                print_usage(argv[0]);
                return EXIT_SUCCESS;
            default:
                print_usage(argv[0]);
                return EXIT_FAILURE;
        }
    }
    
    if (optind >= argc) {
        std::cerr << "Error: Missing file path to monitor" << std::endl;
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }
    
    target_file = argv[optind];
    
    if (shell_command.empty() && optind + 1 >= argc) {
        std::cerr << "Error: No shell command (-c) or script path provided" << std::endl;
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }
    
    if (shell_command.empty()) {
        script_path = argv[optind + 1];
    }
    
    struct stat st;
    if (stat(target_file.c_str(), &st) == -1) {
        std::cerr << "Error: Cannot access monitored file " << target_file << ": " << strerror(errno) << std::endl;
        return EXIT_FAILURE;
    }
    
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);
    
    if (daemon_mode) {
        daemonize();
    }
    
    fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (fd < 0) {
        std::cerr << "inotify initialization failed: " << strerror(errno) << std::endl;
        return EXIT_FAILURE;
    }
    
    wd = inotify_add_watch(fd, target_file.c_str(), IN_MODIFY | IN_ATTRIB);
    if (wd < 0) {
        std::cerr << "Cannot monitor file " << target_file << ": " << strerror(errno) << std::endl;
        close(fd);
        return EXIT_FAILURE;
    }
    
    if (verbose && !daemon_mode) {
        std::cout << "Started monitoring file " << target_file << std::endl;
        if (low_power_mode) {
            std::cout << "Running in low power mode with " << check_interval << " second interval" << std::endl;
        }
    }
    
    struct pollfd fds[1];
    fds[0].fd = fd;
    fds[0].events = POLLIN;
    
    char buffer[BUF_LEN];
    while (running) {
        int poll_ret = poll(fds, 1, check_interval * 1000);
        
        if (poll_ret < 0) {
            if (errno == EINTR) continue;
            std::cerr << "poll error: " << strerror(errno) << std::endl;
            break;
        }
        
        if (poll_ret == 0) {
            if (low_power_mode) {
                usleep(100000);
            }
            continue;
        }
        
        if (fds[0].revents & POLLIN) {
            int length = read(fd, buffer, BUF_LEN);
            if (length < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    continue;
                }
                if (errno == EINTR) continue;
                std::cerr << "Error reading events: " << strerror(errno) << std::endl;
                break;
            }
            
            int i = 0;
            while (i < length) {
                struct inotify_event *event = (struct inotify_event*)&buffer[i];
                
                if (event->mask & (IN_MODIFY | IN_ATTRIB)) {
                    execute_script();
                    
                    if (low_power_mode) {
                        sleep(1);
                    }
                }
                
                i += EVENT_SIZE + event->len;
            }
        }
    }
    
    inotify_rm_watch(fd, wd);
    close(fd);
    
    if (verbose && !daemon_mode) {
        std::cout << "Stopped monitoring file " << target_file << std::endl;
    }
    
    return EXIT_SUCCESS;
}