#include <limits.h>
#include <linux/limits.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define MAX_ARGS 64
#define MAX_CMDS 10
#define MAX_INP 1024

void exec_pipe_cmd(char *args[]) {
  int num_cmd = 0;
  char *commands[MAX_CMDS][MAX_ARGS];
  int arg_index = 0;

  for (int i = 0; args[i] != NULL; i++) {
    if (strcmp(args[i], "|") == 0) {
      commands[num_cmd][arg_index] = NULL;
      num_cmd++;
      arg_index = 0;
    } else {
      commands[num_cmd][arg_index++] = args[i];
    }
  }
  commands[num_cmd][arg_index] = NULL;
  num_cmd++;

  int prev_pipe[2];

  for (int i = 0; i < num_cmd; i++) {
    int curr_pipe[2];

    if (i < num_cmd - 1) {
      if (pipe(curr_pipe) == -1) {
        perror("pipe failed");
        exit(1);
      }
    }

    pid_t pid = fork();
    if (pid == 0) {
      // Child process
      if (i > 0) {
        dup2(prev_pipe[0], STDIN_FILENO);
        close(prev_pipe[0]);
        close(prev_pipe[1]);
      }
      if (i < num_cmd - 1) {
        dup2(curr_pipe[1], STDOUT_FILENO);
        close(curr_pipe[0]);
        close(curr_pipe[1]);
      }
      execvp(commands[i][0], commands[i]);
      perror("execvp failed");
      exit(1);
    } else if (pid > 0) {
      // Parent process
      if (i > 0) {
        close(prev_pipe[0]);
        close(prev_pipe[1]);
      }
      if (i < num_cmd - 1) {
        prev_pipe[0] = curr_pipe[0];
        prev_pipe[1] = curr_pipe[1];
      }
    } else {
      perror("fork failed");
      exit(1);
    }
  }

  for (int i = 0; i < num_cmd; i++) {
    wait(NULL);
  }
}

int main(int argc, char *argv[]) {
  char *args[MAX_ARGS];
  char inp[MAX_INP];
  char cwd[PATH_MAX];
  char hostname[HOST_NAME_MAX + 1];

  int res = gethostname(hostname, HOST_NAME_MAX);
  uid_t uid = getuid();
  struct passwd *pw = getpwuid(uid);

  printf("\x1b[2J\x1b[H");

  while (1) {
    if (getcwd(cwd, sizeof(cwd)) != NULL)
      printf("\x1b[92m[%s]\x1b[0m on \x1b[33m[%s]\x1b[0m >> \x1b[95m%s\x1b[0m "
             "\x1b[92m$ ",
             pw->pw_name, hostname, cwd);
    else
      perror("getcwd error");

    if (fgets(inp, MAX_INP, stdin) == NULL)
      break;

    inp[strcspn(inp, "\n")] = 0;

    int argc = 0;
    char *p = inp;
    while (*p != '\0' && argc < MAX_ARGS - 1) {
      while (*p == ' ')
        p++;
      if (*p == '\0')
        break;

      char *start;
      if (*p == '"' || *p == '\'') {
        char quote = *p++;
        start = p;
        while (*p && *p != quote)
          p++;
      } else {
        start = p;
        while (*p && *p != ' ')
          p++;
      }

      int len = p - start;
      args[argc] = malloc(len + 1);
      strncpy(args[argc], start, len);
      args[argc][len] = '\0';
      argc++;
      if (*p)
        p++;
    }
    args[argc] = NULL;

    if (args[0] == NULL)
      continue;

    if (strcmp(args[0], "exit") == 0)
      break;

    if (strcmp(args[0], "cd") == 0) {
      const char *path = args[1] ? args[1] : getenv("HOME");
      if (chdir(path) != 0) {
        perror("cd failed");
      }
      for (int i = 0; i < argc; i++)
        free(args[i]);
      continue;
    }

    int is_piped = 0;
    for (int i = 0; args[i] != NULL; i++) {
      if (strcmp(args[i], "|") == 0) {
        is_piped = 1;
        break;
      }
    }

    if (is_piped) {
      exec_pipe_cmd(args);
    } else {
      pid_t pid = fork();
      if (pid == 0) {
        execvp(args[0], args);
        perror("execvp failed");
        exit(1);
      } else if (pid > 0) {
        wait(NULL);
      } else {
        perror("fork failed");
      }
    }

    for (int i = 0; i < argc; i++)
      free(args[i]);
  }

  return 0;
}
