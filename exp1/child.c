#include "child.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <sys/wait.h>

/* fork 子进程，子进程 exec 加载 hello，父进程等待 */
void create_child(void) {
  pid_t pid = fork();
  if (pid < 0) {
    perror("fork");
    exit(1);
  }

  if (pid == 0) {
    execl("./hello", "hello", NULL);
    perror("exec");
    exit(1);
  }

  wait(NULL);
  printf("子进程 %d 已结束\n", pid);
}
