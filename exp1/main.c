#include <signal.h>
#include <stdio.h>

#include "child.h"

/* SIGINT 处理函数 */
void on_sigint(int sig) {
  printf("\n捕获到信号 SIGINT(%d)\n", sig);
}

/* 主函数：注册信号处理，创建子进程 */
int main(void) {
  signal(SIGINT, on_sigint);

  create_child();

  printf("程序结束\n");
  return 0;
}
