#define _POSIX_C_SOURCE 200809L

#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stddef.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>
#include <err.h>
#include <ctype.h>
#include <strings.h>

#define MAXCHAR 2048
#define MAXARGS 513
#define MAXJOBS 100

struct sigaction SIGINT_action = {0}, SIGTSTP_action = {0};

const char *prompt = ": ";
static bool fg_only = false;

typedef struct {
    pid_t pid;
    char command[MAXCHAR];
    bool running;
} Job;

static Job jobs[MAXJOBS];
static int jobCount = 0;

void str_sub(char **input);
void SIGINT_allowed();
void SIGINT_ignored();
void SIGTSTP_register();
void toggle_fg_SIGTSTP(int signo);
void checkChildStatus();
void printStatus(int status);
void addJob(pid_t pid, const char *command);
void removeJob(pid_t pid);
void showJobs();
void bringToForeground(int jobIndex);
void continueInBackground(int jobIndex);
void printWelcome();
void suggestCommand(const char *cmd);

int main(void) {
    SIGTSTP_register();
    SIGINT_ignored();

    printWelcome();

    char *input = NULL;
    size_t input_size = 0;
    static int status = 0;

    while (1) {
        char *args[MAXARGS] = {0};
        char inputFile[MAXCHAR] = {0};
        char outputFile[MAXCHAR] = {0};
        int argCount = 0;
        bool background = false;
        bool acceptArgs = true;

        printf("%s", prompt);
        fflush(stdout);

        ssize_t ret = getline(&input, &input_size, stdin);
        if (ret < 0) {
            if (feof(stdin)) break;
            if (errno == EINTR) {
                clearerr(stdin);
                continue;
            } else {
                err(errno, "getline");
            }
        }

        if (input[0] == '\n' || input[0] == '#') continue;
        input[strcspn(input, "\n")] = '\0';

        str_sub(&input);

        char *token = strtok(input, " ");
        if (!token) continue;

        char command[MAXCHAR];
        strncpy(command, token, MAXCHAR);
        args[argCount++] = token;

        while ((token = strtok(NULL, " "))) {
            if (strcmp(token, "<") == 0) {
                token = strtok(NULL, " ");
                if (token) strncpy(inputFile, token, MAXCHAR);
                acceptArgs = false;
            } else if (strcmp(token, ">") == 0) {
                token = strtok(NULL, " ");
                if (token) strncpy(outputFile, token, MAXCHAR);
                acceptArgs = false;
            } else if (acceptArgs) {
                args[argCount++] = token;
            }
        }

        if (argCount > 0 && strcmp(args[argCount - 1], "&") == 0) {
            background = !fg_only;
            args[--argCount] = NULL;
        } else {
            args[argCount] = NULL;
        }

        if (strcmp(command, "exit") == 0) {
            free(input);
            for (int i = 0; i < jobCount; i++) {
                if (jobs[i].running) kill(jobs[i].pid, SIGTERM);
            }
            exit(0);
        } else if (strcmp(command, "cd") == 0) {
            if (!args[1]) chdir(getenv("HOME"));
            else if (chdir(args[1]) == -1) perror("cd");
        } else if (strcmp(command, "status") == 0) {
            printStatus(status);
        } else if (strcmp(command, "jobs") == 0) {
            showJobs();
        } else if (strcmp(command, "fg") == 0) {
            if (args[1]) bringToForeground(atoi(args[1]));
            else fprintf(stderr, "Usage: fg <job#>\n");
        } else if (strcmp(command, "bg") == 0) {
            if (args[1]) continueInBackground(atoi(args[1]));
            else fprintf(stderr, "Usage: bg <job#>\n");
        } else {
            pid_t spawnPid = fork();
            switch (spawnPid) {
                case -1:
                    perror("fork");
                    exit(1);
                case 0:
                    if (strlen(inputFile) > 0) {
                        int inFd = open(inputFile, O_RDONLY);
                        if (inFd == -1) {
                            fprintf(stderr, "cannot open %s for input\n", inputFile);
                            exit(1);
                        }
                        dup2(inFd, STDIN_FILENO);
                        close(inFd);
                    }
                    if (strlen(outputFile) > 0) {
                        int outFd = open(outputFile, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                        if (outFd == -1) {
                            fprintf(stderr, "cannot open %s for output\n", outputFile);
                            exit(1);
                        }
                        dup2(outFd, STDOUT_FILENO);
                        close(outFd);
                    }
                    if (!background || fg_only) SIGINT_allowed();
                    else SIGINT_ignored();
                    execvp(command, args);
                    perror("Command not found\n");
                    suggestCommand(command);
                    exit(1);
                default:
                    if (background && !fg_only) {
                        printf("background pid is %d\n", spawnPid);
                        addJob(spawnPid, command);
                        fflush(stdout);
                    } else {
                        waitpid(spawnPid, &status, 0);
                        if (WIFSIGNALED(status)) printStatus(status);
                    }
                    checkChildStatus();
                    break;
            }
        }
    }

    free(input);
    return 0;
}

void str_sub(char **input) {
    char *str = *input;
    size_t len = strlen(str);
    const char *needle = "$$";
    pid_t pid = getpid();
    char pid_str[20];
    snprintf(pid_str, sizeof(pid_str), "%d", pid);

    while ((str = strstr(str, needle))) {
        size_t new_len = len + strlen(pid_str) - 2;
        char *new_input = malloc(new_len + 1);
        size_t offset = str - *input;

        strncpy(new_input, *input, offset);
        strcpy(new_input + offset, pid_str);
        strcpy(new_input + offset + strlen(pid_str), str + 2);

        free(*input);
        *input = new_input;
        str = *input + offset + strlen(pid_str);
        len = strlen(*input);
    }
}

void SIGINT_allowed() {
    SIGINT_action.sa_handler = SIG_DFL;
    sigfillset(&SIGINT_action.sa_mask);
    SIGINT_action.sa_flags = 0;
    sigaction(SIGINT, &SIGINT_action, NULL);
}

void SIGINT_ignored() {
    SIGINT_action.sa_handler = SIG_IGN;
    sigfillset(&SIGINT_action.sa_mask);
    SIGINT_action.sa_flags = 0;
    sigaction(SIGINT, &SIGINT_action, NULL);
}

void SIGTSTP_register() {
    SIGTSTP_action.sa_handler = toggle_fg_SIGTSTP;
    sigfillset(&SIGTSTP_action.sa_mask);
    SIGTSTP_action.sa_flags = SA_RESTART;
    sigaction(SIGTSTP, &SIGTSTP_action, NULL);
}

void toggle_fg_SIGTSTP(int signo) {
    if (!fg_only) {
        write(STDOUT_FILENO, "\nEntering foreground-only mode (& is now ignored)\n: ", 52);
        fg_only = true;
    } else {
        write(STDOUT_FILENO, "\nExiting foreground-only mode\n: ", 32);
        fg_only = false;
    }
    fflush(stdout);
}

void addJob(pid_t pid, const char *command) {
    if (jobCount < MAXJOBS) {
        jobs[jobCount].pid = pid;
        strncpy(jobs[jobCount].command, command, MAXCHAR);
        jobs[jobCount].running = true;
        jobCount++;
    }
}

void removeJob(pid_t pid) {
    for (int i = 0; i < jobCount; i++) {
        if (jobs[i].pid == pid) {
            jobs[i].running = false;
            break;
        }
    }
}

void showJobs() {
    bool anyJobs = false;
    for (int i = 0; i < jobCount; i++) {
        if (jobs[i].running) {
            printf("[%d] %d %s\n", i, jobs[i].pid, jobs[i].command);
            anyJobs = true;
        }
    }
    if (!anyJobs) {
        printf("No background jobs running.\n");
    }
}

void bringToForeground(int jobIndex) {
    if (jobIndex < 0 || jobIndex >= jobCount || !jobs[jobIndex].running) {
        fprintf(stderr, "Invalid job\n");
        return;
    }
    int status;
    printf("Bringing [%d] %s to foreground\n", jobIndex, jobs[jobIndex].command);
    kill(jobs[jobIndex].pid, SIGCONT);
    waitpid(jobs[jobIndex].pid, &status, 0);
    jobs[jobIndex].running = false;
    printStatus(status);
}

void continueInBackground(int jobIndex) {
    if (jobIndex < 0 || jobIndex >= jobCount || !jobs[jobIndex].running) {
        fprintf(stderr, "Invalid job\n");
        return;
    }
    printf("Continuing [%d] %s in background\n", jobIndex, jobs[jobIndex].command);
    kill(jobs[jobIndex].pid, SIGCONT);
}

void checkChildStatus() {
    int checkStatus;
    pid_t pid;
    while ((pid = waitpid(-1, &checkStatus, WNOHANG)) > 0) {
        printf("background pid %d is done: ", pid);
        printStatus(checkStatus);
        removeJob(pid);
    }
}

void printStatus(int status) {
    if (WIFEXITED(status)) {
        printf("exit value %d\n", WEXITSTATUS(status));
    } else if (WIFSIGNALED(status)) {
        printf("terminated by signal %d\n", WTERMSIG(status));
    }
    fflush(stdout);
}

void printWelcome() {
    printf("\n==============================\n");
    printf("\n");
    printf("  Welcome to tinyshell ʕ•ᴥ•ʔ\n");
    printf("\n");
    printf("==============================\n\n");
}

void suggestCommand(const char *cmd) {
    const char *suggestions[] = {"ls", "cd", "pwd", "echo", "exit", "status", NULL};
    bool found = false;

    for (int i = 0; suggestions[i]; i++) {
        size_t cmdLen = strlen(cmd);
        size_t sugLen = strlen(suggestions[i]);

        if (strcasecmp(cmd, suggestions[i]) == 0) {                                         // exact match
            printf("Did you mean: %s?\n", suggestions[i]);
            return;
        }

        if (cmdLen < sugLen && strncasecmp(cmd, suggestions[i], cmdLen) == 0) {
            printf("Did you mean: %s?\n", suggestions[i]);
            return;
        }

        bool repeated = true;
        for (size_t j = 1; j < cmdLen; j++) {                                               // repeated chars (e.g. ll for 'ls')
            if (tolower(cmd[j]) != tolower(cmd[0])) {
                repeated = false;
                break;
            }
        }
        if (repeated && tolower(cmd[0]) == tolower(suggestions[i][0])) {
            printf("Did you mean: %s?\n", suggestions[i]);
            return;
        }
    }
}
