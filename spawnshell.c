/* $begin shellmain */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <spawn.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>

#define MAXARGS 128
#define MAXLINE 8192 /* Max text line length */

extern char **environ; /* Defined by libc */

/* Function prototypes */
void eval(char *cmdline);
int parseline(char *buf, char **argv, int *pipe_fds);
int builtin_command(char **argv, pid_t pid, int status);
int specialCharCheck(char *buf, char **argv, int *pipe_fds);

posix_spawn_file_actions_t actions;
posix_spawn_file_actions_t actions2;
posix_spawn_file_actions_t nullActions;

//char *argw[MAXARGS]; /* Used for second commands listed in "x ; y" */
int amp = 0;
int status;
int statusDefault;
int counter = 0;
int numWords[MAXARGS];
int numSep = 0;
int sepInd = 0;

int numPipes = 0;
int pipeIndex = 0;
int lastPID = -1;
//int pipe_fds[2];
//pipe(pipe_fds);

void unix_error(char *msg) /* Unix-style error */
{
  fprintf(stderr, "%s: %s\n", msg, strerror(errno));
  exit(EXIT_FAILURE);
}

static void signal_handler(int signal){
  if(signal == SIGINT){
    write(STDOUT_FILENO, "\ncaught sigint\nCS361 > ", 24);
  }
  else if(signal == SIGTSTP){
    write(STDIN_FILENO, "\ncaught sigstp\nCS361 > ", 24);
  }
}

int main() {
  char cmdline[MAXLINE]; /* Command line */
  numSep = 0;

  signal(SIGINT, signal_handler);
  signal(SIGTSTP, signal_handler);

  while (1) {
    char *result;
    /* Read */
    actions = nullActions;
    actions2 = nullActions;
    printf("CS361 > ");
    result = fgets(cmdline, MAXLINE, stdin);
    if (result == NULL && ferror(stdin)) {
      fprintf(stderr, "fatal fgets error\n");
      exit(EXIT_FAILURE);
    }
    if (feof(stdin)) exit(0);

    /* Evaluate */
    eval(cmdline);
  }
}
/* $end shellmain */

/* $begin eval */
/* eval - Evaluate a command line */
void eval(char *cmdline) {
  char *argv[MAXARGS]; /* Argument list execve() */
  char buf[MAXLINE];   /* Holds modified command line */
  int bg;              /* Should the job run in bg or fg? */
  pid_t pid;           /* Process id */
  pid_t pid2;

  int pipe_fds[2];    /* pipe[0] = read , pipe[1] = write */
  pipe(pipe_fds);

  posix_spawn_file_actions_init(&actions);
  posix_spawn_file_actions_init(&actions2);

  strcpy(buf, cmdline);
  bg = parseline(buf, argv, pipe_fds);

  // TO-DO: CHECK FOR '>' , '<' , '|'
  if (argv[0] == NULL) return; /* Ignore empty lines */

  if (!builtin_command(argv, pid, status)) {

    if(0 != posix_spawnp(&pid, argv[0], &actions, NULL, argv, environ) && (*argv[0] != '?' || amp != 1)){
      perror("spawn failed");
      exit(1);
    }

    if(numPipes != 0){
      if(0 != posix_spawnp(&pid2, argv[pipeIndex+1], &actions2, NULL, argv+pipeIndex+1, environ)){
        perror("spawn failed");
        exit(1);
      }
    }

    if(numPipes != 0){
      close(pipe_fds[0]);
      close(pipe_fds[1]);
      numPipes = 0;
    }
    
    /* Parent waits for foreground job to terminate */
    if (!bg) {
      status = statusDefault;
      int count = 0;
      int sum = numWords[0]+1;

      if ((waitpid(pid, &status, 0) < 0) && *argv[0] != '?'){
        unix_error("waitfg: waitpid error");
      }

      while(numSep != 0){
        //argv[sepInd1] = NULL;
        
        posix_spawnp(&pid, argv[sum], &actions, NULL, argv+sum, environ);
        if (waitpid(pid, &status, 0) < 0) unix_error("waitfg: waitpid error");

        numSep--;
        count++;
        sum += numWords[count]+1;
      }

    } 
    else if(amp != 1)
      printf("%d %s", pid, cmdline);
    else{
      printf("[1] %d\n", pid);
      waitpid(pid, &status, 0);
      cmdline[strlen(cmdline)-2] = '\0';
      printf("[1]+\tDone\t\t%s\n", cmdline);
    }
  }
  return;
}

/* If first arg is a builtin command, run it and return true */
int builtin_command(char **argv, pid_t pid, int status) {
  if (!strcmp(argv[0], "exit")){  /* exit command */
    exit(0);
  }
  if (!strcmp(argv[0], "&")){   /* Ignore singleton & */
    return 1;
  }
  if (!strcmp(argv[0], "?")){   /* returns pid and status of last process & */
    printf("\npid:%d status:%d\n", pid, status);
  }
  return 0; /* Not a builtin command */
}
/* $end eval */

/* $begin parseline */
/* parseline - Parse the command line and build the argv array */
int parseline(char *buf, char **argv, int *pipe_fds) {
  char *delim; /* Points to first space delimiter */
  int argc;    /* Number of args */
  int bg;      /* Background job? */

  buf[strlen(buf) - 1] = ' ';   /* Replace trailing '\n' with space */
  while (*buf && (*buf == ' ')) /* Ignore leading spaces */
    buf++;

  /* Build the argv list */
  argc = 0;
  while ((delim = strchr(buf, ' '))) {
    argv[argc++] = buf;
    //printf("%s", argv[argc]);
    *delim = '\0';
    buf = delim + 1;
    while (*buf && (*buf == ' ')) /* Ignore spaces */
      buf++;
    //printf("%d\n", argc);
  }
  argv[argc] = NULL;

  if (argc == 0) /* Ignore blank line */
    return 1;

  /* Should the job run in the background? */
  if ((bg = (*argv[argc - 1] == '&')) != 0){
    argv[--argc] = NULL;
    amp = 1;
  } 

  specialCharCheck(buf, argv, pipe_fds);

  return bg;
}

int specialCharCheck(char *buf, char **argv, int *pipe_fds){

  int i = 0;
  int prev = 0;
  //printf("initialized\n");
  
  while(argv[i] != 0x0){

    if(*argv[i] == '>'){     // Output redirection
      posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, argv[i+1], O_WRONLY | O_CREAT, S_IRUSR | S_IWUSR);
      //printf("output redirection\n");
      argv[i] = NULL;
    }

    else if(*argv[i] == '<'){     // Input redirection
      argv[i] = NULL;
      posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, argv[i+1], O_RDONLY, S_IRUSR | S_IRGRP);
      //printf("input redirection\n");
    }

    else if(*argv[i] == '|'){     // Piping output   
      //printf("Piped output\n");
      numPipes++;
      argv[i] = NULL;

      pipeIndex = i;

      posix_spawn_file_actions_adddup2(&actions, pipe_fds[1], STDOUT_FILENO);
      posix_spawn_file_actions_addclose(&actions, pipe_fds[0]);
      posix_spawn_file_actions_adddup2(&actions2, pipe_fds[0], STDIN_FILENO);
      posix_spawn_file_actions_addclose(&actions2, pipe_fds[1]);
    }

    else if(*argv[i] == '&'){     // Piping output   
      //printf("Piped output\n");
      amp = 1;
      argv[i] = NULL;
    }

    else if(*argv[i] == ';'){      // Command separator
      numSep++;

      if (counter == 0)
        numWords[counter] = i;
      else
        numWords[counter] = i - prev;
      argv[i] = NULL;
      counter++;
      prev = i+1;
    }
   
    i++;
  }

  return 0;
}
/* $end parseline */
