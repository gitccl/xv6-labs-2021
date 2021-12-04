#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

void
panic(char *s)
{
  fprintf(2, "%s\n", s);
  exit(1);
}

void run(int readfd) {
  int val, cnt;
  cnt = read(readfd, &val, 4);
  if(!cnt) {
    return;
  }else if(cnt != 4){
    panic("read failed");
  }

  printf("prime %d\n", val);

  int fd[2];
  if(pipe(fd) < 0) {
    panic("pipe failed");
  }

  int pid = fork();
  if (pid < 0) {
    panic("fork failed");
  } else if(pid == 0) {
    close(fd[1]);
    run(fd[0]);
  } else {
    close(fd[0]);
    int newval;
    while ((cnt = read(readfd, &newval, 4)))
    {
      if(cnt != 4){
        fprintf(2, "read failed\n");
        break;
      }
      if (newval % val == 0) continue;
      // put newval to pipe
      write(fd[1], &newval, 4);
    }
    close(readfd);
    close(fd[1]);
    wait(0);
  }
}

int
main(int argc, char *argv[])
{

  int fd[2];

  if(pipe(fd) < 0) {
    panic("pipe failed");
  }

  int pid = fork();
  if(pid < 0) {
    panic("fork failed");
  } else if(pid == 0) {
    // child
    close(fd[1]);
    run(fd[0]);
  } else {
    // father
    close(fd[0]);
    for (int i = 2; i <= 35; ++ i) {
      write(fd[1], &i, 4);
    }
    close(fd[1]);
    wait(0);
  }

  exit(0);
}
