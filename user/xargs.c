#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/param.h"
#include "user/user.h"
#include "kernel/fs.h"

#define MAXLENGTH 2000
char buf[MAXLENGTH];
char *exec_argv[MAXARG]; 


int is_whilespace(char ch) {
  return ch == ' ' || ch == '\t' || ch == '\n';
}

void skip(char *str, int *pos, int op) {
  if (op == 0) { // skip while space
    while ( str[*pos] && is_whilespace(str[*pos]) ) 
      (*pos) ++;
  } else {
    while ( str[*pos] && !is_whilespace(str[*pos]) ) 
      (*pos) ++;
  }
}

void exec_wrap(int argc, char *argv[])
{
  argv[argc] = 0;
  // for(int i = 0; argv[i]; ++ i) {
  //   printf("args%d: %s\n", i, argv[i]);
  // }
  int pid = fork();
  if(pid < 0) {
    fprintf(2, "xargs: fork failed\n");
    exit(1);
  } else if(pid == 0){
    exec(exec_argv[0], exec_argv);
  } else {
    wait(0);
  }
}

int
main(int argc, char *argv[])
{
  int has_n = 0, n = MAXARG, cmd_start = 1;
  if (argc >= 3) {
    if(strcmp(argv[1], "-n") == 0) {
      has_n = 1;
      n = atoi(argv[2]);
      cmd_start = 3;
    }
  }

  exec_argv[0] = "echo"; // default to `echo`
  for(int i = cmd_start; i < argc; ++ i) {
    if (i - cmd_start >= MAXARG) {
      fprintf(2, "xargs: too many args, exceed 32\n");
      exit(1);
    }
    exec_argv[i - cmd_start] = argv[i];
  }
  int exec_argc_init = argc > cmd_start ? argc - cmd_start : 1;

  int len = 0, last_pos = 0;
  int num = n, is_done = 0;
  int exec_argc = exec_argc_init;
  int special_ch = 0;
  while (read(0, buf + len, 1))
  {
    char ch = buf[len];
    if(ch == '\"') continue;
    if(ch == '\\') {
      if(!special_ch) special_ch = 1;
      else {
        special_ch = 0;
        buf[len] = '\\';
      }
    }
    if(ch == 'n' && special_ch) {
      special_ch = 0;
      buf[len] = ch = '\n';
    }
    if(special_ch) continue;
    ++ len;
    if (len >= MAXLENGTH) {
      fprintf(2, "input too long!\n");
      exit(1);
    }

    if(ch == '\n' && has_n) {
      while (1)
      {
        while(num > 0 && last_pos < len) {
          is_done = 0;
          skip(buf, &last_pos, 0);
          if (exec_argc >= MAXARG) {
            fprintf(2, "xargs: too many args, exceed 32\n");
            exit(1);
          }
          if(last_pos >= len) break;
          exec_argv[exec_argc ++] = buf + last_pos;
          skip(buf, &last_pos, 1);
          buf[last_pos ++] = '\0';
          num --;
        }
        if (num > 0) break;
        // exec
        exec_wrap(exec_argc, exec_argv);
        num = n;
        exec_argc = exec_argc_init;
        is_done = 1;
      }
    }
  }
  
  // merge has -n and hasn't -n
  if (exec_argc == exec_argc_init && is_done) {
    exit(0);
  }
  while (1)
  {
    while(num > 0 && last_pos < len) {
      skip(buf, &last_pos, 0);
      if (exec_argc >= MAXARG) {
        fprintf(2, "xargs: too many args, exceed 32\n");
        exit(1);
      }
      if(last_pos >= len) break;
      exec_argv[exec_argc ++] = buf + last_pos;
      skip(buf, &last_pos, 1);
      buf[last_pos ++] = '\0';
      num --;
    }
    if (num > 0) break;
    exec_wrap(exec_argc, exec_argv);
    num = n;
    exec_argc = exec_argc_init;
  }
  if(exec_argc > 1) {
    exec_wrap(exec_argc, exec_argv);
  }
  exit(0);
}
