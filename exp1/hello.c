#include <stdio.h>

/* 被 exec 加载的子程序 */
int main(void) {
  printf("这是被 exec 加载的 hello 程序\n");
  return 0;
}
