#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{

  if(argc < 2){
    fprintf(2, "Usage: %s ticks...\n", argv[0]);
    exit(1);
  }

  if(sleep(atoi(argv[1])) < 0) {
    fprintf(2, "sleep failed\n");
  }

  exit(0);
}
