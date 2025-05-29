#include <fcntl.h>
#include <limits.h>
#include <linux/limits.h>
#include <pwd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <termio.h>
#include <termios.h>
#include <unistd.h>

#define MAX_JOBS 100
#define MAX_CMD_LEN 1024

typedef struct {
  int id;
  pid_t pid;
  char command[MAX_CMD_LEN];
  int running;
  int stopped;
} Job;

pid_t current_fg_pid = 0;
char history[50][1024];
int history_count = 0;
Job jobs[MAX_JOBS];
int job_count = 0;

void sigtstp_handler(int sig) {
  if (current_fg_pid > 0) {
    kill(current_fg_pid, SIGTSTP);
  }
}

void sigchld_handler(int sig) {
  int status;
  pid_t pid;

  while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
    for (int i = 0; i < job_count; i++) {
      if (jobs[i].pid == pid) {
        for (int j = i; j < job_count - 1; j++) {
          jobs[j] = jobs[j + 1];
        }
        job_count--;
        break;
      }
    }
  }
}

void add_job(pid_t pid, char *command, int running) {
  jobs[job_count].id = job_count + 1;
  jobs[job_count].pid = pid;
  strncpy(jobs[job_count].command, command, MAX_CMD_LEN);
  jobs[job_count].running = running;
  jobs[job_count].stopped = !running;

  job_count++;
}

void show_jobs() {
  for (int i = 0; i < job_count; i++) {
    const char *state = jobs[i].stopped ? "Stopped" : "Running";
    printf("\x1b[95m[%d] ID:\x1b[0m %d / \x1b[95mStatus:\x1b[0m %s / "
           "\x1b[95mCommand:\x1b[0m %s\n",
           jobs[i].id, jobs[i].pid, state, jobs[i].command);
  }
}

void continue_job(int id) {
  for (int i = 0; i < job_count; i++) {
    if (jobs[i].id == id) {
      kill(jobs[i].pid, SIGCONT);
      jobs[i].running = 1;
      jobs[i].stopped = 0;

      return;
    }
  }
}

void bring_fg(int id) {
  for (int i = 0; i < job_count; i++) {
    if (jobs[i].id == id) {
      current_fg_pid = jobs[i].pid;
      kill(current_fg_pid, SIGCONT);
      jobs[i].running = 1;
      jobs[i].stopped = 0;

      int status;
      waitpid(current_fg_pid, &status, WUNTRACED);
      if (WIFSTOPPED(status)) {
        jobs[i].stopped = 1;
        jobs[i].running = 0;
      } else {
        for (int j = i; j < job_count - 1; j++) {
          jobs[j] = jobs[j + 1];
        }
        job_count--;
      }
      current_fg_pid = 0;
      return;
    }
  }
}

void disable_echoctl() {
  struct termios term;
  tcgetattr(STDIN_FILENO, &term);
  term.c_lflag &= ~ECHOCTL;
  tcsetattr(STDIN_FILENO, TCSANOW, &term);
}

void restore_term_settings() {
  struct termios term;
  tcgetattr(STDIN_FILENO, &term);
  term.c_lflag |= ECHOCTL;
  tcsetattr(STDIN_FILENO, TCSANOW, &term);
}

void add_to_history(char *command) {
  if (strlen(command) > 0 && history_count < 50) {
    strcpy(history[history_count++], command);
  }
}

void parse_args(char *input, char **args, int *is_bg) {
  int argc = 0;
  *is_bg = 0;

  while (*input) {
    while (*input == ' ')
      input++;
    if (*input == '\0')
      break;

    char *start;
    if (*input == '"' || *input == '\'') {
      char quote = *input++;
      start = input;
      while (*input && *input != quote)
        input++;
    } else {
      start = input;
      while (*input && *input != ' ')
        input++;
    }

    int len = input - start;
    if (len == 1 && start[0] == '&') {
      *is_bg = 1;
    } else {
      args[argc] = malloc(len + 1);
      strncpy(args[argc], start, len);
      args[argc][len] = '\0';
      argc++;
    }
    if (*input)
      input++;
  }
  args[argc] = NULL;
}

void exec_cmd(char *input) {
  char *args[64];
  int is_bg = 0;

  parse_args(input, args, &is_bg);

  if (args[0] == NULL)
    return;

  if (strcmp(args[0], "cd") == 0) {
    if (args[1] != NULL) {
      chdir(args[1]);
    } else {
      const char *home = getenv("HOME");
      if (!home) {
        struct passwd *pw = getpwuid(getuid());
        home = pw->pw_dir;
      }
      chdir(home);
    }
    add_to_history(input);
    return;

  } else if (strcmp(args[0], "exit") == 0) {
    printf("\x1b[2J\x1b[H");
    restore_term_settings();
    exit(0);

  } else if (strcmp(args[0], "jobs") == 0) {
    show_jobs();
    add_to_history(input);
    return;

  } else if (strcmp(args[0], "fg") == 0) {
    if (args[1]) {
      bring_fg(atoi(args[1]));
      add_to_history(input);
    }
    return;

  } else if (strcmp(args[0], "bg") == 0) {
    if (args[1]) {
      continue_job(atoi(args[1]));
      add_to_history(input);
    }
    return;

  } else if (strcmp(args[0], "history") == 0) {
    for (int i = 0; i < history_count; i++) {
      printf("%d: %s\n", i + 1, history[i]);
    }
    add_to_history(input);
    return;
  }

  int num_cmds = 0;
  char *cmds[10][64];
  int cmd_index = 0, arg_index = 0;

  for (int i = 0; args[i] != NULL; i++) {
    if (strcmp(args[i], "|") == 0) {
      cmds[cmd_index][arg_index] = NULL;
      cmd_index++;
      arg_index = 0;
    } else {
      cmds[cmd_index][arg_index++] = args[i];
    }
  }
  cmds[cmd_index][arg_index] = NULL;
  num_cmds = cmd_index + 1;

  if (num_cmds == 1) {
    pid_t pid = fork();
    if (pid == 0) {
      for (int i = 0; args[i] != NULL; i++) {
        if (strcmp(args[i], ">") == 0) {
          int fd = open(args[i + 1], O_WRONLY | O_CREAT | O_TRUNC, 0644);
          dup2(fd, STDOUT_FILENO);
          close(fd);
          args[i] = NULL;
          args[i + 1] = NULL;
        } else if (strcmp(args[i], ">>") == 0) {
          int fd = open(args[i + 1], O_WRONLY | O_CREAT | O_APPEND, 0644);
          dup2(fd, STDOUT_FILENO);
          close(fd);
          args[i] = NULL;
          args[i + 1] = NULL;
        } else if (strcmp(args[i], "<") == 0) {
          int fd = open(args[i + 1], O_RDONLY);
          dup2(fd, STDIN_FILENO);
          close(fd);
          args[i] = NULL;
          args[i + 1] = NULL;
        }
      }
      execvp(args[0], args);
      perror("exec failed");
      exit(1);
    } else {
      if (!is_bg) {
        current_fg_pid = pid;
        int status;
        waitpid(pid, &status, WUNTRACED);
        current_fg_pid = 0;
        if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
          add_to_history(input);
        }
      } else {
        add_job(pid, input, 1);
        add_to_history(input);
      }
    }
    return;
  }

  int pipefds[2 * (num_cmds - 1)];
  for (int i = 0; i < num_cmds - 1; i++)
    pipe(pipefds + i * 2);

  for (int i = 0; i < num_cmds; i++) {
    pid_t pid = fork();
    if (pid == 0) {
      if (i != 0)
        dup2(pipefds[(i - 1) * 2], 0);
      if (i != num_cmds - 1)
        dup2(pipefds[i * 2 + 1], 1);

      for (int j = 0; j < 2 * (num_cmds - 1); j++)
        close(pipefds[j]);

      execvp(cmds[i][0], cmds[i]);
      perror("exec failed");
      exit(1);
    }
  }

  for (int i = 0; i < 2 * (num_cmds - 1); i++)
    close(pipefds[i]);

  int all_success = 1;
  for (int i = 0; i < num_cmds; i++) {
    int status;
    wait(&status);
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
      all_success = 0;
    }
  }

  if (all_success) {
    add_to_history(input);
  }
}

int main() {
  char input[MAX_CMD_LEN];
  char cwd[PATH_MAX];
  char hostname[HOST_NAME_MAX + 1];

  int res = gethostname(hostname, HOST_NAME_MAX);
  uid_t uid = getuid();
  struct passwd *pw = getpwuid(uid);

  signal(SIGTSTP, sigtstp_handler);
  signal(SIGCHLD, sigchld_handler);

  printf("\x1b[2J\x1b[H");
  disable_echoctl();

  while (1) {
    if (getcwd(cwd, sizeof(cwd)) != NULL)
      printf("\x1b[92m[%s]\x1b[0m on \x1b[33m[%s]\x1b[0m >> \x1b[95m%s\x1b[0m "
             "\x1b[92m$\x1b[0m ",
             pw->pw_name, hostname, cwd);
    else
      perror("getcwd error");

    fflush(stdout);

    if (fgets(input, sizeof(input), stdin) == NULL)
      break;

    input[strcspn(input, "\n")] = '\0';

    if (strcmp(input, "!!") == 0) {
      if (history_count > 0) {
        strcpy(input, history[history_count - 1]);
        printf("%s\n", input);
      } else {
        printf("No previous command in history\n");
        continue;
      }
    } else if (input[0] == '!' && strlen(input) > 1) {
      int n = atoi(input + 1);
      if (n > 0 && n <= history_count) {
        strcpy(input, history[n - 1]);
        printf("%s\n", input);
      } else {
        printf("No such command in history\n");
        continue;
      }
    }

    if (strlen(input) > 0) {
      exec_cmd(input);
    }
  }
  return 0;
}
