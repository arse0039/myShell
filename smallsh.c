#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <stddef.h>
#include <unistd.h>
#include <sys/wait.h>
#include <errno.h>
#include <inttypes.h>
#include <fcntl.h>
/* pid_t pid*/

#define MAX_WORD 512

static int tokenizeInput(char *input, char *tokenArray[]);
static void expandTokensSearch(char *tokenArray[], int numberOfTokens, char *bg, char *fg);
static char *expandToken(char **haystack, char const *needle, char const *sub );
static struct ParsedInput parseTokens(char *tokenArray[], int numberOfTokens);
static void executeCommand(struct ParsedInput *tokenStruct, pid_t *backgroundPids, int numberofBGPids, char *bg, char *fg);
static int executeChangeDir(struct ParsedInput *tokenStruct);
static void executeExit(struct ParsedInput *tokenStruct, char *bg);
static void backgroundKiller(char *bg);
static void sigIntHandler(int signo);

struct ParsedInput {
  char *command;
  char *arguments;
  char *argArray[MAX_WORD];
  int argLength;
  char *infile;
  char *outfile;
  int background;
};


int main() {

  /*
   * Ignore SIGTSTP Block.
   */
  struct sigaction SIGTSTP_ignore = {0};
  SIGTSTP_ignore.sa_handler = SIG_IGN;
  sigfillset(&SIGTSTP_ignore.sa_mask);
  SIGTSTP_ignore.sa_flags = 0;
  sigaction(SIGTSTP, &SIGTSTP_ignore, NULL); 

  struct sigaction SIGINT_ignore = {0};
  SIGINT_ignore.sa_handler = SIG_IGN;
  sigfillset(&SIGINT_ignore.sa_mask);
  SIGINT_ignore.sa_flags = 0;
  sigaction(SIGINT, &SIGINT_ignore, NULL);

  char* inputPtr;
  size_t inputSize = 0;
  ssize_t read;
  char *prompt = getenv("PS1");
  char background[100] = "";
  char foreground[100] = "0";
  char *tokenArray[MAX_WORD];
  int numberOfTokens;
  pid_t backgroundPIDS[100];
  int numberOfBGPids = 0;
  int errorStatus;
 

   if (prompt == NULL) {
      prompt = "";
   }

  for(;;) {
    
    backgroundKiller(background);
    
    // Print the appropriate prompt to the screen.

    printf("%s ", prompt);

    // Get user input

    read = getline(&inputPtr, &inputSize, stdin); 
    if (read == -1) {
      if(feof(stdin)){
        exit(0);
      } else {
      clearerr(stdin);
    }}

    SIGINT_ignore.sa_handler = sigIntHandler;
    sigaction(SIGINT, &SIGINT_ignore, NULL);

    numberOfTokens = tokenizeInput(inputPtr, tokenArray);
    expandTokensSearch(tokenArray, numberOfTokens, background, foreground);    
    struct ParsedInput tokenStruct = parseTokens(tokenArray, numberOfTokens);

    if (tokenStruct.command == NULL) goto exit;
    
    if (strcmp(tokenStruct.command, "cd") == 0) {
        errorStatus = executeChangeDir(&tokenStruct);
    } else if ( strcmp(tokenStruct.command, "exit") == 0 ) {
        executeExit(&tokenStruct, background);
    } else {
        executeCommand(&tokenStruct, backgroundPIDS, numberOfBGPids, background, foreground);
      }

    if(errorStatus != 0) goto exit;

  exit:
    continue;

  }

  free(inputPtr);
  
  return 0;

}

int tokenizeInput(char* input, char* tokenArray[]){
  char *token, *tokenDup;
  char *delim = getenv("IFS");
  int i = 0;
  
  if (delim == NULL){
    delim = " \t\n";
  }
    
  token = strtok(input, delim);  

  while(token != NULL && i < MAX_WORD) {
    if (*token == '#') {
      break;
    }
    tokenDup = strdup(token);    
    tokenArray[i] = tokenDup;
    token = strtok(NULL, delim);
    i++;
  }
  return i;
}

void expandTokensSearch(char *tokenArray[], int numberOfTokens, char *bg, char *fg){
  
  char *homeVar = getenv("HOME");
  pid_t pidVal = getpid();
  char pidString[10];

  sprintf(pidString, "%d", pidVal);

  

  for(int i = 0; i < numberOfTokens; i++) {
    if (tokenArray[i][0] =='~' && tokenArray[i][1] == '/') {
      expandToken(&tokenArray[i], "~", homeVar);
    }

    expandToken(&tokenArray[i], "$$", pidString);
    expandToken(&tokenArray[i], "$?", fg);
    expandToken(&tokenArray[i], "$!", bg);
  }
}


char *expandToken(char **haystack, char const *needle, char const *sub ) 
{
  char *str = *haystack;
  size_t haystack_len = strlen(str);
  size_t const needle_len = strlen(needle),
         sub_len = strlen(sub);

  for(; (str = strstr(str, needle));){
    ptrdiff_t off = str - *haystack;
    if (sub_len > needle_len) {
      str = realloc(*haystack, sizeof **haystack * (haystack_len + sub_len - needle_len + 1));
      if (!str) goto exit;
      *haystack = str;
      str = *haystack + off;
    }

    memmove(str + sub_len, str + needle_len, haystack_len + 1 - off - needle_len);
    memcpy(str, sub, sub_len);
    haystack_len = haystack_len + sub_len - needle_len;
    str += sub_len;
  }
  str = *haystack;
  if (sub_len < needle_len) {
    str = realloc(*haystack, sizeof **haystack * (haystack_len + 1));
    if (!str) goto exit;
    *haystack = str;
  }
  exit:
     return str;

}

/*
 * This function accepts the array of tokenized inputs and the
 * total number of tokens. It iterates through the token list and
 * parses them into a ParsedInput struct.
 *
 * Returns a ParsedInput struct to be used for command execution.
 */
struct ParsedInput parseTokens(char *tokenArray[], int numberOfTokens) {
  struct ParsedInput parsedLine = {0};
  parsedLine.command = NULL;
  parsedLine.arguments = NULL;
  parsedLine.argLength = 0;
  parsedLine.infile = NULL;
  parsedLine.outfile = NULL;
  parsedLine.background = 0;

  int counter = numberOfTokens;
  char argString[MAX_WORD] = "";

  for(int i = 0; i < counter; i++) {
    // Check if the last item is an & and adjust the loop if so.
    if ( strcmp(tokenArray[counter - 1], "&") == 0 ) {
        parsedLine.background = 1;
        counter--;
        }

    if (i == 0) {
      parsedLine.command = tokenArray[i];
      parsedLine.argArray[parsedLine.argLength] = tokenArray[i];
      continue;
    }

    if (strcmp(tokenArray[i],"<") == 0 && ((i + 1) < numberOfTokens)) {
      if ((strcmp(tokenArray[i+1], "<") != 0) && (strcmp(tokenArray[i+1], ">") != 0) && (strcmp(tokenArray[i+1], "&") != 0)) {
          parsedLine.infile = tokenArray[i+1];
          i++;
          continue;
          }
    }

    if (strcmp(tokenArray[i],">") == 0 && ((i + 1) < numberOfTokens)) {
      if ((strcmp(tokenArray[i+1], "<") != 0) && (strcmp(tokenArray[i+1], ">") != 0) && (strcmp(tokenArray[i+1], "&") != 0)) {
          parsedLine.outfile = tokenArray[i+1];
          i++;
          continue;
          }
    }

    parsedLine.argArray[parsedLine.argLength + 1] = tokenArray[i];
    strcat(argString, tokenArray[i]);  
    strcat(argString, " "); 
    parsedLine.argLength++;
  }
  parsedLine.arguments = argString;

  // Remove the trailing " " from the argument string.
  if(parsedLine.argLength > 0) {
    parsedLine.arguments[strlen(argString) - 1] = '\0';
  }

  parsedLine.argArray[parsedLine.argLength + 1] = NULL;

  
  return parsedLine;
}

void executeCommand(struct ParsedInput *tokenStruct, pid_t *backgroundPIDS, int numberofBGPids, char *bg, char *fg){


   int inputFile = -1;
   int outFile = -1;
   int childStatus;
 
   pid_t spawnPid = fork();

   switch(spawnPid) {
      case -1:
        fprintf(stderr, "fork() error\n");
        fflush(stderr);
        exit(1);
        break;
      
      case 0:
        if(tokenStruct->infile != NULL) {
          inputFile = open(tokenStruct->infile, O_RDONLY);
          if(inputFile == -1) {
            fprintf(stderr, "Invalid input file.\n");
            fflush(stderr);
            exit(1);
          }
          dup2(inputFile, STDIN_FILENO);
          close(inputFile);
        }

        if(tokenStruct->outfile != NULL) {
          outFile = open(tokenStruct->outfile, O_WRONLY | O_CREAT | O_TRUNC, 0777);
          if(outFile == -1) {
            fprintf(stderr, "Invalid output file.\n");
            fflush(stderr);
            exit(1);
          }
          dup2(outFile, STDOUT_FILENO);
          close(outFile);
        }

        execvp(tokenStruct->command, tokenStruct->argArray);
        fprintf(stderr, "Could not execute: execv error.\n");
        fflush(stderr);
        exit(2);
        break;
      
      default:
        if (tokenStruct->background == 1) {
          sprintf(bg, "%d", spawnPid);
          //waitpid(spawnPid, &childStatus, WNOHANG);
          backgroundPIDS[numberofBGPids] = spawnPid;
          numberofBGPids++;
        } else {

          spawnPid = waitpid(spawnPid, &childStatus, 0);
          if (spawnPid == -1) {
            perror("waitpid Error");
            exit(1);
          }
        
          if(WIFEXITED(childStatus)){
            sprintf(fg, "%d", WEXITSTATUS(childStatus));
          } 

          else if(WIFSIGNALED(childStatus)) {
            sprintf(fg, "%d", WTERMSIG(childStatus)+128);
          }

          if(WIFSTOPPED(childStatus)) {
            kill(spawnPid, SIGCONT);
            fprintf(stderr, "Child process %jd stopped. Continuing.\n",(intmax_t) spawnPid);
            fflush(stderr);
            sprintf(bg, "%d", WSTOPSIG(childStatus));
          }
      }
  }
}

int executeChangeDir(struct ParsedInput *tokenStruct) {

  
  int errorNumber = 0;
  char *dirPath = getenv("HOME");

  if(tokenStruct->argLength > 1) {
    fprintf(stderr, "Error: Too many arguments provided. \n");
    fflush(stderr);
    errorNumber = -1;
    goto exit;
  } 

  if (tokenStruct->argLength == 1) {
    dirPath = tokenStruct->arguments;
  }

  errorNumber = chdir(dirPath);
  
  if (errorNumber == -1) {
      fprintf(stderr, "Error: Invalid Path provided. \n");
      fflush(stderr);
      goto exit;
    }
  
exit:
  return errorNumber;
}

void executeExit(struct ParsedInput *tokenStruct, char *bg) {
   
  char *exitStatus;
  int convertedExitNum;

  if(tokenStruct->argLength == 0) {
    exitStatus = bg;
    goto processExit;
  } 

  if(tokenStruct->argLength == 1) {
    convertedExitNum = atoi(tokenStruct->argArray[1]);
    if(convertedExitNum == 0) {
      fprintf(stderr, "Provided argument is not an integer.\n");
      fflush(stderr);
      goto exit;
     }
    exitStatus = tokenStruct->argArray[1];
    goto processExit;
  }

  if(tokenStruct->argLength > 1) {
     fprintf(stderr, "Too many arguments provided to exit call.\n");
     fflush(stderr);
     goto exit;
  };

processExit:
  convertedExitNum = atoi(exitStatus);
  fprintf(stderr, "\n exit\n");
  kill(0, SIGINT);
  exit(convertedExitNum);

exit:
  return; 
}

void backgroundKiller(char *bg) {
  int childStatus;
  
  pid_t bgPID;
  
  while((bgPID = waitpid(-1, &childStatus, WNOHANG | WUNTRACED)) > 0) {

    if(WIFEXITED(childStatus)){
       fprintf(stderr, "Child process %jd done. Exit status %d.\n", (intmax_t) bgPID, WEXITSTATUS(childStatus));
       fflush(stdout);
    } 
    else if(WIFSIGNALED(childStatus))  {
       fprintf(stderr, "Child process %jd done. Signaled %d.\n",(intmax_t) bgPID, WTERMSIG(childStatus));
       fflush(stdout);
    }
    else if(WIFSTOPPED(childStatus)) {
      fprintf(stderr, "Child process %jd stopped. Continuing.\n",(intmax_t) bgPID);
      fflush(stderr);
      kill(bgPID, SIGCONT);
    }
  }

  if(bgPID == -1 && errno != ECHILD) {
    perror("waitpid Error");
    exit(1);
  }

}
  

void sigIntHandler(int signo)
{
  return;
}
