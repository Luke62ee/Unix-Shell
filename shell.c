#include "shell.h"

int main(int argc, char **argv) {
  if (argc == 2 && equal(argv[1], "--interactive")) {
    return interactiveShell();
  } else {
    return runTests();
  }
}

// interactive shell to process commands
int interactiveShell() {
  bool should_run = true;
  char lastCommand[MAXLINE + 1];
  char rawLastCommand[MAXLINE + 1];  
  bool hasHistory = false;
  // To store the commands
  // Assigned memory to line
  char *line = calloc(1, MAXLINE);
  
  // while true, run the program
  while (should_run) {

    // PROMPT = "osh>"
    printf("\n%s", PROMPT);

    // Ensure that printf(PROMPT) will be print out immediately 
    fflush(stdout);
    
    // reads the command the user types and stores the length
    int n = fetchline(&line);

    int counter = 0;
    char *args[MAXLINE/2 + 1];

    // For the extra credit: $ ps auxf | cat | tac | cat | tac | grep `whoami`
    // Find the first and last occurance of backticks (`) i
    char *start = strchr(line, '`');
    char *end = strrchr(line, '`');

    if (start && end && start != end) {
        // Terminate the string at the closing backtick
        *end = '\0';  
        // Extract the command inside the backticks
        char *commandInside = start + 1;  

        // Execute the command inside backticks and get the output
        FILE *fp = popen(commandInside, "r");
        if (fp == NULL) {
            perror("Error executing command substitution");
            continue;
        }

        // Read the output of the command
        char result[MAXLINE];
        if (fgets(result, MAXLINE, fp) != NULL) {
            result[strcspn(result, "\n")] = 0;  
        }
        pclose(fp);

        // Replace the backtick expression with the actual command output in line
        snprintf(line, MAXLINE, "%.*s%s%s", (int)(start - line), line, result, end + 1);
    }


    // Tokenize the first word of the input command
    char *token = strtok(line, " ");

    // prints out what user types
    printf("read: %s (length = %d)\n", line, n);
    if (n == -1 || equal(line, "exit")) {
      should_run = false;
      continue;
    }

    // If the command is not "!!", store it in history
    if (!equal(line, "!!")) {  
      // Save the full command
      strncpy(rawLastCommand, line, MAXLINE); 
      rawLastCommand[MAXLINE] = '\0';  

      // Set flag indicating that history exists
      hasHistory = true;
    } 

    // Handle "!!" command (repeat the last command)
    if (equal(line, "!!")) {
      if (!hasHistory) {
        printf("No commands in history.\n");
        continue;
      }
      printf("DEBUG: !! replaced with: %s\n", rawLastCommand);

      // Copy the last command back into `line`
      strncpy(line, rawLastCommand, MAXLINE);
      line[MAXLINE] = '\0';
      
      // Tokenize the last command to parse it into `args`
      counter = 0;
      token = strtok(line, " ");
      while (token != NULL) {
        args[counter++] = token;
        token = strtok(NULL, " ");
      }
      args[counter] = NULL;
    }
    
    // Process command arguments
    while (token != NULL) {
      args[counter++] = token;
      token = strtok(NULL, " "); 
    }

    // Ensure argument list is null-terminated
    args[counter] = NULL;

    // If no valid command was entered, show an error and continue
    if(counter == 0) {
      printf("Error: No command entered\n");
      continue;
    }

    // Check if the command contains redirection (`>` for output, `<` for input)
    if (counter >= 2 && args[1] && (equal(args[1], ">") || equal(args[1], "<"))) {
      // Get the filename for redirection
      char *fileName = args[2];
      int redirectType;
      int openFlags;

      // Determine if it's output (`>`) or input (`<`) redirection
      if (equal(args[1], ">")) {
        redirectType = STDOUT_FILENO;
      } else {
        redirectType = STDIN_FILENO;
      }

      // Set file opening mode based on redirection type
      if (redirectType == STDOUT_FILENO) {
        // O_WRONLY = write mode to open file
        // O_CREAT = create file if file is not existing
        // O_TRUNC = clean file if the file is already existing
        openFlags = O_WRONLY | O_CREAT | O_TRUNC;
      } else {
        openFlags = O_RDONLY;
      }

      // Remove redirection operator from command arguments
      args[1] = NULL;
      pid_t pid = fork();
      if (pid < 0) {
        perror("Fork failed");
      } else if (pid == 0) {
        int fd = open(fileName, openFlags, 0644);
        if (fd < 0) {
          perror("Error opening file\n");
          exit(EXIT_FAILURE);
        }

        // Redirect standard input/output to the file
        dup2(fd, redirectType);
        close(fd);
        
         // Execute the command
        execvp(args[0], args);
        perror("Exec failed");
        exit(EXIT_FAILURE);
      }
      // Wait for the child process to finish
      wait(NULL);
      continue;
    }

    // Find the pipe (`|`) in the command
    int pipeIndex = -1;
    for (int i = 0; i < counter; i++) {
        if (equal(args[i], "|")) {
          pipeIndex = i;
          break;
        }
    }

    // If a pipe is found, process it
    if (pipeIndex != -1) {
      int pipeFD[2];
      pipe(pipeFD);
      pid_t pid1 = fork();

      if (pid1 == 0) { 
          close(pipeFD[0]); 

          // Redirect standard output to the pipe
          dup2(pipeFD[1], STDOUT_FILENO); 
          close(pipeFD[1]);

          // Ensure execvp() only runs the left command
          args[pipeIndex] = NULL; 
          execvp(args[0], args);
          perror("Exec failed for first command");
          exit(EXIT_FAILURE);
      }

      // Second child process (for the right command)
      pid_t pid2 = fork();
      if (pid2 == 0) { 
        close(pipeFD[1]);
        // Redirect standard input from the pipe
        dup2(pipeFD[0], STDIN_FILENO); 
        close(pipeFD[0]);
        execvp(args[pipeIndex + 1], &args[pipeIndex + 1]); 
        perror("Exec failed for second command");
        exit(EXIT_FAILURE);
      }
      // Close pipe in parent process
      close(pipeFD[0]);
      close(pipeFD[1]);
      wait(NULL);
      wait(NULL);
      continue; 
    }

    // Check if the command should run in the background 
    bool isBackground = false;
    int bgIndex = -1;
    for (int i = 0; i < counter; i++) {
      if(equal(args[i], "&")) {
        isBackground = true;

        // Store the index of &
        bgIndex = i;
        args[i] = NULL;
        break;
      }
    }

    // Fork a child process to execute the command
    pid_t childPid = fork();
    if(childPid < 0) {
      perror("Fork failed");
    } else if (childPid == 0) {
      execvp(args[0], args);
      perror("Exec failed");
      exit(EXIT_FAILURE);
    }

    // If there is another command after &, execute it
    if (isBackground && bgIndex + 1 < counter) {
      char *nextCommand[MAXLINE / 2 + 1];
      int nextCounter = 0;

      // Copy the next command after &
      for (int i = bgIndex + 1; i < counter; i++) {
        nextCommand[nextCounter++] = args[i];
      }
      nextCommand[nextCounter] = NULL;

      // Fork a second child process for the next command
      pid_t secPid = fork();
      if (secPid < 0) {
        perror("Fork failed");
      } else if (secPid == 0) {
        execvp(nextCommand[0], nextCommand);
        perror("Exec failed");
        exit(EXIT_FAILURE);
      }
      // Wait for the second child process to finish
      waitpid(secPid, NULL, 0);
    }
    // Parent process waits for the first command unless it's a background process
    if (!isBackground) {
      wait(NULL);
    }


    // Extra point for ascii and I made a spongebob :)
    if (equal(args[0], "ascii")) {
    printf("      .--..--..--..--..--..--.\n");
    printf("    .' \\  (`._   (_)     _   \\\n");
    printf("  .'    |  '._)         (_)  |\n");
    printf("  \\ _.')\\      .----..---.   /\n");
    printf("  |(_.'  |    /    .-\\-.  \\  |\n");
    printf("  \\     0|    |   ( O| O) | o|\n");
    printf("   |  _  |  .--.____.'._.-.  |\n");
    printf("   \\ (_) | o         -` .-`  |\n");
    printf("    |    \\   |`-._ _ _ _ _\\ /\n");
    printf("    \\    |   |  `. |_||_|   |\n");
    printf("    | o  |    \\_      \\     |     -.   .-.\n");
    printf("    |.-.  \\     `--..-'   O |     `.`-' .'\n");
    printf("  _.'  .' |     `-.-'      /-.__   ' .-'\n");
    printf(".' `-.` '.|='=.='=.='=.='=|._/_ `-'.'\n");
    printf("`-._  `.  |________/\\_____|    `-.'\n");
    printf("   .'   ).| '=' '='\\/ '=' |\n");
    printf("   `._.`  '---------------'\n");
    printf("           //___\\   //___\\\n");
    printf("             ||       ||\n");
    printf("             ||_.-.   ||_.-.\n");
    printf("            (_.--__) (_.--__)\n");
    continue;  // Skip `execvp()`
}

  }
  free(line);
  return 0;
}

void processLine(char *line) { 
    printf("Executing: %s\n", line);
    int ret = system(line); 
    if (ret == -1) {
        perror("Error executing command");
    }
}


// I added ascii here since it is not an official command
// You can see the result easily 
int runTests() {
  printf("*** Running basic tests ***\n");
  char lines[7][MAXLINE] = {
      "ls", "ls -al", "ls & whoami ;", "ls > junk.txt",
      "cat < junk.txt", "ls | wc", "ascii"};

  for (int i = 0; i < 7; i++) {
    printf("* %d. Testing %s *\n", i + 1, lines[i]);

    // **Handle 'ascii' separately instead of using system()**
    if (equal(lines[i], "ascii")) {
        printf("      .--..--..--..--..--..--.\n");
        printf("    .' \\  (`._   (_)     _   \\\n");
        printf("  .'    |  '._)         (_)  |\n");
        printf("  \\ _.')\\      .----..---.   /\n");
        printf("  |(_.'  |    /    .-\\-.  \\  |\n");
        printf("  \\     0|    |   ( O| O) | o|\n");
        printf("   |  _  |  .--.____.'._.-.  |\n");
        printf("   \\ (_) | o         -` .-`  |\n");
        printf("    |    \\   |`-._ _ _ _ _\\ /\n");
        printf("    \\    |   |  `. |_||_|   |\n");
        printf("    | o  |    \\_      \\     |     -.   .-.\n");
        printf("    |.-.  \\     `--..-'   O |     `.`-' .'\n");
        printf("  _.'  .' |     `-.-'      /-.__   ' .-'\n");
        printf(".' `-.` '.|='=.='=.='=.='=|._/_ `-'.'\n");
        printf("`-._  `.  |________/\\_____|    `-.'\n");
        printf("   .'   ).| '=' '='\\/ '=' |\n");
        printf("   `._.`  '---------------'\n");
        printf("           //___\\   //___\\\n");
        printf("             ||       ||\n");
        printf("             ||_.-.   ||_.-.\n");
        printf("            (_.--__) (_.--__)\n");
    } else {
        processLine(lines[i]); // Regular command execution
    }
  }
  return 0;
}


// return true if C-strings are equal
bool equal(char *a, char *b) { return (strcmp(a, b) == 0); }

// read a line from console
// return length of line read or -1 if failed to read
// removes the \n on the line read
int fetchline(char **line) {
  size_t len = 0;
  size_t n = getline(line, &len, stdin);
  if (n > 0) {
    (*line)[n - 1] = '\0';
  }
  return n;
}