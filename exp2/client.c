#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "shm.h"

int main(void) {
  int id = shmget(SHM_KEY, sizeof(struct shm_data), 0666);
  if (id < 0) {
    perror("shmget（请先运行 ./server）");
    return 1;
  }

  struct shm_data *p = shmat(id, NULL, 0);
  if (p == (void *)-1) {
    perror("shmat");
    return 1;
  }

  /* 等待服务器消息 */
  while (!p->server_ready) {
    sleep(1);
  }

  printf("[客户端] 收到服务器消息：%s\n", p->server_msg);

  strcpy(p->client_msg, "Hi, this is client.");
  p->client_ready = 1;
  printf("[客户端] 已写入回复，退出\n");

  shmdt(p);
  return 0;
}
