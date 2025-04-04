# tinyshell

A simple shell implemented in C. Supports running commands in foreground and background, handling signals, basic I/O redirection, and built-in commands.

## Features

- Execute system commands using `execvp()`
- Built-in commands:
  - `exit`: Terminates the shell and kills any remaining background processes.
  - `cd [path]`: Changes the current working directory. If no path is given, goes to `$HOME`.
  - `status`: Reports the exit status or termination signal of the last foreground process.
- Input/output redirection: `command < input.txt > output.txt`
- Background execution with `&`
- Foreground-only mode toggled with `Ctrl+Z` (`SIGTSTP`)
- `$$` is replaced with the shell’s PID (useful for creating unique filenames)
- Suggestion system: suggests similar built-in commands for unrecognized input (e.g., `ll` -> `ls`)

## Compile

```bash
gcc -o tinyshell tinyshell.c
```

## Run

```bash
./tinyshell
```

## Built-In Command Usage

| Command       | Description                                 | Example                     | Expected Output                     |
|---------------|---------------------------------------------|-----------------------------|--------------------------------------|
| `exit`        | Exits the shell                             | `exit`                      | Shell terminates                     |
| `cd`          | Changes to home directory                   | `cd`                        | No output, current dir is `$HOME`    |
| `cd [path]`   | Changes to specified directory              | `cd /tmp`                   | No output, current dir is `/tmp`     |
| `status`      | Shows exit status of last foreground cmd    | `status`                    | `exit value 0` or signal msg         |

## Other Supported Features

| Feature                  | Example                             | Expected Output                          |
|--------------------------|--------------------------------------|-------------------------------------------|
| Run command              | `ls`                                 | Lists current directory contents          |
| Run in background        | `sleep 5 &`                          | `background pid is [PID]`                 |
| Redirect input           | `sort < input.txt`                  | Sorts lines from `input.txt`              |
| Redirect output          | `ls > out.txt`                      | Output of `ls` saved in `out.txt`         |
| Redirect both            | `sort < in.txt > out.txt`           | Sorted `in.txt` saved to `out.txt`        |
| Variable expansion       | `echo mypid_$$`                     | Prints `mypid_[shell_pid]`                |
| Foreground-only toggle   | Press `Ctrl+Z`                      | Toggles fg-only mode and shows message    |
| Background process done  | After `sleep 2 &`                   | After 2s: `background pid [PID] is done:` |

## Notes

- Foreground-only mode disables `&` from launching background tasks.
- Background process completion is reported when done.
- Handles `SIGINT` (`Ctrl+C`) for foreground processes only.

## Running on Windows (with VSCode)

### Option 1: Using Windows Subsystem for Linux (WSL) — Recommended

1. **Install WSL**
   - Open PowerShell and run:
     ```powershell
     wsl --install
     ```
   - Restart your computer if prompted.

2. **Set Up Ubuntu**
   - Ubuntu will install automatically. If not, install it from the Microsoft Store.

3. **Install VSCode & WSL Extension**
   - Open VSCode and install the **Remote - WSL** extension.
   - Press `F1`, then choose `Remote-WSL: New Window`.

4. **Open Your Project in WSL**
   - Use the new WSL window to open your project folder.

5. **Compile & Run the Shell**
   ```bash
   gcc -o tinyshell tinyshell.c
   ./tinyshell
   ```

### Option 2: Using MinGW or Cygwin (Not Recommended for Full Compatibility)

- Install [MinGW](http://mingw.org/) or [Cygwin](https://www.cygwin.com/).
- Open a terminal in your install environment and try:
  ```bash
  gcc -o tinyshell tinyshell.c
  ./tinyshell
  ```
- Note: Some system headers and signals may not work as expected in native Windows environments.

## Extra Notes for Developers

- The command suggestion feature uses `strncasecmp`, which is declared in the header:

  ```c
  #include <strings.h>
  ```

  Make sure this is included if you're compiling on Linux

- Command suggestion compares the first 2 letters of your input with known built-ins like `cd`, `exit`, etc. You can customize this list inside `suggestCommand()`
