#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

void
panic(char *s)
{
  fprintf(2, "%s\n", s);
  exit(1);
}

int
main(int argc, char *argv[])
{

  int fd1[2], fd2[2];

  if(pipe(fd1) < 0) {
    panic("pipe failed");
  }
  if(pipe(fd2) < 0) {
    panic("pipe failed");
  }

  int pid = fork();
  if(pid < 0) {
    panic("fork failed");
  } else if(pid == 0) {
    // child
    close(fd1[1]);
    close(fd2[0]);
    char ch;
    if(read(fd1[0], &ch, 1) != 1) {
      panic("read failed");
    }
    printf("%d: received ping\n", getpid());
    if(write(fd2[1], &ch, 1) != 1) {
      panic("write failed");
    }
  } else {
    // father
    close(fd1[0]);
    close(fd2[1]);
    char ch = 'a';
    if(write(fd1[1], &ch, 1) != 1) {
      panic("write failed");
    }
    if(read(fd2[0], &ch, 1) != 1) {
      panic("read failed");
    }
    printf("%d: received pong\n", getpid());
  }

  exit(0);
}
