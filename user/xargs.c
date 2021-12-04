#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/param.h"
#include "user/user.h"
#include "kernel/fs.h"

#define MAXLENGTH 2000

typedef struct {
  char str[MAXLENGTH];
  int len, last_pos;
}buf_t;

typedef struct {
  char *argv[MAXARG]; 
  int argc, remains;
  int exec_argc_init, n, is_ever_exec;
}parse_t;

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

void exec_wrap(parse_t *parse)
{
  parse->argv[parse->argc] = 0;
  // for(int i = 0; argv[i]; ++ i) {
  //   printf("args%d: %s\n", i, argv[i]);
  // }
  int pid = fork();
  if(pid < 0) {
    fprintf(2, "xargs: fork failed\n");
    exit(1);
  } else if(pid == 0){
    exec(parse->argv[0], parse->argv);
  } else {
    wait(0);
  }
}

parse_t parse;
buf_t buf;

void solve(buf_t *buf, parse_t *parse)
{
  while (1)
  {
    while(parse->remains > 0 && buf->last_pos < buf->len) {
      skip(buf->str, &buf->last_pos, 0);
      if (parse->argc >= MAXARG) {
        fprintf(2, "xargs: too many args, exceed 32\n");
        exit(1);
      }
      if(buf->last_pos >= buf->len) break;
      parse->argv[parse->argc ++] = buf->str + buf->last_pos;
      skip(buf->str, &buf->last_pos, 1);
      buf->str[buf->last_pos ++] = '\0';
      parse->remains --;
    }
    if (parse->remains > 0) break;
    exec_wrap(parse);
    parse->remains = parse->n;
    parse->argc = parse->exec_argc_init;
    parse->is_ever_exec = 1;
  }
} 

int
main(int argc, char *argv[])
{
  int has_n = 0, cmd_start = 1;
  parse.n = MAXARG;
  if (argc >= 3) {
    if(strcmp(argv[1], "-n") == 0) {
      has_n = 1;
      parse.n = atoi(argv[2]);
      cmd_start = 3;
    }
  }

  char **exec_argv = parse.argv;

  exec_argv[0] = "echo"; // default to `echo`
  for(int i = cmd_start; i < argc; ++ i) {
    if (i - cmd_start >= MAXARG) {
      fprintf(2, "xargs: too many args, exceed 32\n");
      exit(1);
    }
    exec_argv[i - cmd_start] = argv[i];
  }

  parse.exec_argc_init = argc > cmd_start ? argc - cmd_start : 1;
  parse.remains = parse.n;
  parse.is_ever_exec = 0;

  parse.argc = parse.exec_argc_init;
  int special_ch = 0;
  while (read(0, buf.str + buf.len, 1))
  {
    char ch = buf.str[buf.len];
    if(ch == '\"') continue;
    if(ch == '\\') {
      if(!special_ch) special_ch = 1;
      else {
        special_ch = 0;
        buf.str[buf.len] = '\\';
      }
    }
    if(ch == 'n' && special_ch) {
      special_ch = 0;
      buf.str[buf.len] = ch = '\n';
    }
    if(special_ch) continue;
    ++ buf.len;
    if (buf.len >= MAXLENGTH) {
      fprintf(2, "input too long!\n");
      exit(1);
    }

    if(ch == '\n' && has_n) {
      solve(&buf, &parse);
    }
  }
  
  if (parse.argc == parse.exec_argc_init && parse.is_ever_exec) {
    exit(0);
  }
  solve(&buf, &parse);
  if(parse.argc > 1) {
    exec_wrap(&parse);
  }
  exit(0);
}
