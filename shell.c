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
#include <unistd.h>

#define MAX_JOBS 100
#define MAX_CMD_LEN 1024

struct job {
  int id;
  pid_t pid;
  char command[MAX_CMD_LEN];
  int running;
  int stopped;
};

struct job jobs[MAX_JOBS];
int job_count = 0;
pid_t current_fg_pid = 0;

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
    printf("[%d] %d %s %s\n", jobs[i].id, jobs[i].pid, state, jobs[i].command);
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

    return;
  } else if (strcmp(args[0], "exit") == 0) {
    exit(0);

  } else if (strcmp(args[0], "jobs") == 0) {
    show_jobs();
    return;

  } else if (strcmp(args[0], "fg") == 0) {
    if (args[1]) {
      bring_fg(atoi(args[1]));
    }
    return;

  } else if (strcmp(args[0], "bg") == 0) {
    if (args[1]) {
      continue_job(atoi(args[1]));
    }
    return;
  }

  pid_t pid = fork();
  if (pid == 0) {
    setpgid(0, 0);

    if (!is_bg) {
      signal(SIGTSTP, SIG_DFL);
    }
    execvp(args[0], args);
    perror("execvp failed");
    exit(1);
  } else if (pid > 0) {
    setpgid(pid, pid);

    if (is_bg) {
      add_job(pid, input, 1);
    } else {
      current_fg_pid = pid;
      signal(SIGTSTP, sigtstp_handler);

      int status;
      waitpid(pid, &status, WUNTRACED);
      if (WIFSTOPPED(status)) {
        add_job(pid, input, 0);
      }
      current_fg_pid = 0;
    }
  }

  for (int i = 0; args[i] != NULL; i++) {
    free(args[i]);
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
    exec_cmd(input);
  }
  return 0;
}
