# shell

A Unix shell written from scratch in C. Supports pipes, I/O redirection, background jobs, job control, and command history.

## Build and Run

```bash
gcc shell.c -o shell
./shell

```

or visit https://m-olive.fly.dev to run in browser

## Built-in Commands

| Command | Description |
|---------|-------------|
| `cd [path]` | Change directory (defaults to `$HOME`) |
| `exit` | Exit the shell |
| `jobs` | List background and stopped jobs |
| `fg [id]` | Bring job to foreground (defaults to last job) |
| `bg <id>` | Resume stopped job in background |
| `history` | Show last 50 commands |
| `!!` | Repeat last command |
| `!n` | Repeat command `n` from history |

## Features

**Pipes** — Commands separated by `|` are connected with `pipe()`. The parser builds a `pipefds` array sized for all inter-process connections, forks a child per command, and wires stdin/stdout accordingly.

**I/O Redirection** — `>` truncates, `>>` appends, `<` reads. Redirection tokens are stripped from `argv` before `exec`.

**Background Jobs** — Trailing `&` puts a command in the background. The process is added to the job list and the shell returns immediately.

**Job Control** — `SIGTSTP` is caught and forwarded to the current foreground process. `SIGCHLD` is handled asynchronously with `waitpid(..., WNOHANG)` to reap children without blocking. `fg` waits with `WUNTRACED` to detect stops.

**Quoted Strings** — Single and double quotes preserve spaces within arguments.

**Prompt** — Colored `[user] on [host] >> [cwd] $` using ANSI escape codes.

## Job Struct

```c
typedef struct {
    int id;
    pid_t pid;
    char command[1024];
    int running;
} Job;
```

Up to 100 jobs tracked in a static array. History stores the last 50 commands.

## Notes

- Terminal raw mode (`ICANON`, `ECHO`, `ECHOCTL` all disabled) is set on startup for per-keystroke input handling. Original settings are restored on exit.
- In the portfolio Docker container, the shell runs in a sandboxed filesystem at `/home/user/filesystem` via node-pty.

