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
#define MAX_INP 1024

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

    if (strcmp(args[0], "exit") == 0)
      break;

    if (strcmp(args[0], "cd") == 0) {
      const char *path = args[1] ? args[1] : getenv("HOME");
      if (chdir(path) != 0) {
        perror("cd failed");
      }
      continue;
    }

    pid_t pid = fork();
    if (pid == 0) {
      execvp(args[0], args);
      perror("execvp failed");
      exit(1);
    } else if (pid > 0) {
      wait(NULL);
      for (int i = 0; i < argc; i++)
        free(args[i]);
    } else {
      perror("fork failed");
    }
  }
  return 0;
}
