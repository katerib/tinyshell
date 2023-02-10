// Fall 2022

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

#define MAXCHAR 2048
#define MAXARGS 513

struct sigaction SIGINT_action = {0}, SIGTSTP_action = {0};

const char *prompt = ": ";
const char *comment = "#";
const char *sep = " ";
const char *newline = "\n";
static bool fg_only = false;  // when true, foreground-only mode
static int pidCount = 0; // background pids


void **str_sub(char *restrict *restrict input);
void SIGINT_allowed();
void SIGINT_ignored();
void SIGTSTP_register();
void SIGTSTP_ignored();
void toggle_fg_SIGTSTP(int signo);

void checkChildStatus();
void printStatus(int status);

int main(void) {
  SIGTSTP_ignored();   
  char *input = malloc(sizeof(char*) * MAXCHAR);
  size_t input_size = 0;
  pid_t pids[10];
  while(1){
    SIGINT_ignored();

    //char *input = malloc(sizeof(char*) * MAXCHAR);
    char *args[MAXARGS];
    //memset(args, 0, sizeof args);
    char inputFile[MAXCHAR] = {0};  // = {0} to clear each loop
    char outputFile[MAXCHAR] = {0};
    static int status = 0;
    int argCount = 0;
    //size_t input_size = 0;
    char *token;    

    bool background = false;
    bool acceptArgs = true;
        
    /* GET USER INPUT */
    printf("%s", prompt);
    fflush(stdout);
    SIGTSTP_register();
    //fgets(input, MAXCHAR + 1, stdin);
    ssize_t ret = getline(&input, &input_size, stdin);
    /* error handling ; specifically referenced prof's pseudocode shared
    in the class discord */ 
    if (ret < 0) {
      if (feof(stdin)) break;
      if (errno == EINTR) {  // interrupted by signal
        printf("Interrupted by a signal\n");
        clearerr(stdin);
        continue;
      } else {
        err(errno, "getline");
      }
    }
    SIGTSTP_ignored();
        
    // ignore blank lines and comments
    if (!input | (input[0] == '#') | (input[0] == '\n')) continue;

  	// remove newline
    input[strlen(input)-1] = '\0';

    /* PERFORM VARIABLE EXPANSION */
    str_sub(&input);
    // fflush(stdout);  // from testing print statements
        
    /* SPLIT INPUT INTO: command, args, input file, output file
    referenced example in manpages strtok(3) */
    token = strtok(input, sep);
    char command[strlen(token)];
    strcpy(command, token);
    args[argCount] = token;
        
    while (token != NULL) {
    /* use acceptArgs to keep track of command line order;
    stop accepting args once < or > found */
      if (strcmp(token, "<")==0) {
        acceptArgs = false; // arguments must come before I/O redir
        token = strtok(NULL, sep);
        if (token != NULL) {
          strcpy(inputFile, token);
          token = strtok(NULL, sep);
        }
      }
      if (token == NULL) break; // if only input redir and nothing after
      if (strcmp(token, ">")==0) {
        acceptArgs = false; // arguments must come before I/O redir
        token = strtok(NULL, sep);
        if (token != NULL) {
          strcpy(outputFile, token);
          token = strtok(NULL, sep);
        }
      }
      if (token != NULL && acceptArgs) {
        args[argCount++] = token;
        token = strtok(NULL, sep);
      } 
    }
        
    // check if background and null terminate
    if (strcmp(args[argCount-1], "&\0")==0) {
      if (fg_only == false) {
        background = true;
      } // remove '&' whether foreground-only mode active or not
      args[argCount-1] = NULL; // remove & from args passed to exec()
      argCount--;
    } else {
      args[argCount] = NULL;  // null terminate for exec
    }

    /* EXECUTE COMMANDS 
    built-in commands: exit, cd, status -> don't handle I/O redir */
    if (strcmp(command, "exit")==0) {
      free(input);
      // loop through all stored background pids, even if already terminated
      for (int p=0; p < pidCount; p++) {
        pid_t pidToKill = pids[p];
        kill(pidToKill, SIGTERM);
        pids[p] = '\0';  // clear, not really necessary
      }
      exit(0); 
    } else if (strcmp(command, "cd")==0) {
      if (args[1] == NULL) {
        chdir(getenv("HOME"));
      } else if (chdir(args[1]) == -1) {
        printf("error changing directory\n");
        fflush(stdout);
      }
    } else if (strcmp(command, "status")==0) { 
      printStatus(status);
    } else { 
      // referenced modules ; fork new process
      pid_t spawnPid = fork();
      pid_t childPid;

      switch(spawnPid) {
        case -1:
          perror("fork() failed\n");
          exit(2);
          break;
        case 0: // child process
          /* INPUT & OUTPUT REDIRECTION 
          performed before exec() in the child process ; referenced
          modules example- "redirecting both stdin and stdout" 
          modules- files & directories -> permissions */
          // input file opened for reading only
          if (strlen(inputFile) > 0) { 
            int inputSource = open(inputFile, O_RDONLY);
            // error message on fail
            if (inputSource == -1) {
              //perror("source open()");
              printf("cannot open %s for input\n", inputFile);
              fflush(stdout);
              status = 1; // set exit status 1 ; don't exit shell   
              exit(1);
            }
            int inResult = dup2(inputSource, 0);  // redirect stdin to source file
            if (inResult == -1) {
              //perror("source dup2() input");
              status = 1;
              exit(1);
            }
            fcntl(inputSource, F_SETFD, FD_CLOEXEC);
          }
          /* output file opened for writing only 
          truncate if exists or create if doesn't 
          permissions 0644: group & others only read, 
          owner read & write */
          if (strlen(outputFile) > 0) {
            int outputSource = open(outputFile, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            //error message on fail
            if (outputSource == -1) {
              printf("cannot open %s for output\n", outputFile);
              fflush(stdout);
              status = 1; // set exit status 1 ; don't exit shell
              exit(1);
            }
            int outResult = dup2(outputSource, 1);  // redirect stdout to target file
            if (outResult == -1) {
              //perror("target dup2() open");
              status = 1;
              exit(1);
            }
            fcntl(outputSource, F_SETFD, FD_CLOEXEC);
          }

          /* print statement format follows "Sample Program 
          Execution" syntax from assignment page */         
          if (background == 1 & fg_only == false) {
            // ignore SIGINT in bacgkround  
            SIGINT_ignored();
            pid_t pid_curr = getpid();
            fflush(stdout);
            printf("background pid is %u\n", getpid());
            printf("%s", prompt);
            fflush(stdout);
          } else {
            // foreground child terminate on receipt of SIGINT signal
            SIGINT_allowed();
          }

          // pass command, then args (arg[0] == command)
          execvp(command, (char* const*)args);
          // exec returns if error
          perror("execve");
          exit(1);
          break;
        default:
          // parent process ; wait for child if foreground
          if (background == 1 && fg_only == false) {
            pids[pidCount] = spawnPid;  // add all background pids to PIDS
            pidCount++;
            childPid = waitpid(spawnPid, &status, WNOHANG);
          } else {
            childPid = waitpid(spawnPid, &status, 0);
            if (WTERMSIG(status) != 0) {
              //printf("terminated by signal %d\n", WTERMSIG(status));
              printStatus(status);
            }
          }
          checkChildStatus();
          break;
      }
    }
  }
}

void **str_sub(char *restrict *restrict input) {
  /* referenced string substitution and replacement video */
  char *str = *input;
  size_t input_len = strlen(str);
    
  const char *needle = "$$";
  size_t const needle_len = strlen(needle);
    
  pid_t pid_int = getpid();
  char pid[600];
  sprintf(pid, "%u", pid_int);
  size_t const pid_len = strlen(pid);
    
  for (;;) {
    str = strstr(str, needle); // does needle (still) exist?
    if (!str) {
      return 0;
    }
        
    ptrdiff_t offset = str - *input;  // determine location of needle
        
    // reallocate memory if needed
    if (pid_len > needle_len) {
      /* calculate how many things to move:
      input_len + 1 -> size of input plus null term 
      then subtract length of needle + length of pid */
      str = realloc(*input, sizeof *input * (input_len + pid_len - needle_len + 1));
      if (!str) {
        return 0;
      }
      *input = str;
      str = *input + offset;
    }
        
    // copy memory
    memmove(str + pid_len, str + needle_len, input_len + 1 - offset - needle_len);
    memcpy(str, pid, pid_len);
    // update new input size
    input_len = input_len + pid_len - needle_len;
    // avoid searching location of substitution
    str += pid_len;
  }   
  str = *input;
  // if need to shrink size
  if (pid_len < needle_len) {
    str = realloc(*input, sizeof *input * (input_len + 1));
    if (!str) return 0;
    *input = str;
  }
}

/* from modules Signal API */
void SIGINT_allowed(){
	/* Register SIG_DFL as the signal handler
	specifying this value means that the signal type should be ignored */
	SIGINT_action.sa_handler = SIG_DFL;
	// Block all catchable signals while handle_SIGINT is running
	sigfillset(&SIGINT_action.sa_mask);
	// No flags set
	SIGINT_action.sa_flags = 0;
	sigaction(SIGINT, &SIGINT_action, NULL);
}

void SIGINT_ignored(){
	/* Register SIG_DFL as the signal handler
	specifying this value means that the signal type should be ignored */
	SIGINT_action.sa_handler = SIG_IGN;
	// Block all catchable signals while handle_SIGINT is running
	sigfillset(&SIGINT_action.sa_mask);
	// No flags set
	SIGINT_action.sa_flags = 0;
	sigaction(SIGINT, &SIGINT_action, NULL);
}

void SIGTSTP_register() {
  /* SIGTSTP referenced modules 
  also read: https://www.gnu.org/software/libc/manual/html_node/Basic-Signal-Handling.html */
  SIGTSTP_action.sa_handler = toggle_fg_SIGTSTP;
	// Block all catchable signals while toggle_fg_SIGTSTP is running
	sigfillset(&SIGTSTP_action.sa_mask);
	// Set flag to cause automatic restart after sig handler done
	SIGTSTP_action.sa_flags = SA_RESTART;
	sigaction(SIGTSTP, &SIGTSTP_action, NULL);
}

void SIGTSTP_ignored() {
  /* SIGTSTP referenced modules */
  SIGTSTP_action.sa_handler = SIG_IGN;
	// Block all catchable signals while toggle_fg_SIGTSTP is running
	sigfillset(&SIGTSTP_action.sa_mask);
	// No flags set
	SIGTSTP_action.sa_flags = 0;
	sigaction(SIGTSTP, &SIGTSTP_action, NULL);
}

void toggle_fg_SIGTSTP(int signo) {
  /* referenced modules > handle_SIGINT example */
  if (fg_only == false) {
    char* message = "\nEntering foreground-only mode (& is now ignored)\n: ";
    write(STDOUT_FILENO, message, 52);
    fflush(stdout);
    fg_only = true;
  } else {
    char* message = "\nExiting foreground-only mode\n: ";
    write(STDOUT_FILENO, message, 32);
    fflush(stdout);
    fg_only = false;
  }
}

void checkChildStatus() {
  int checkStatus;
  pid_t childPid = waitpid(-1, &checkStatus, WNOHANG);
    
  if (childPid > 0) {
    printf("background pid %u is done: ", childPid);
    fflush(stdout);
    printStatus(checkStatus);
  }
}

void printStatus(int status) {
  /* status printed twice in program (built-in status and termination
  of background process) so created function to minimize redundancy ; 
  referenced code from modules for syntax */
  if (WIFEXITED(status)) {
    printf("exit value %d\n", WEXITSTATUS(status));
    fflush(stdout);
  } else {
    printf("terminated by signal %d\n", WTERMSIG(status));
    fflush(stdout);
  }
}
