#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "shm.h"

int main(void) {
  int id = shmget(SHM_KEY, sizeof(struct shm_data), IPC_CREAT | 0666);
  if (id < 0) {
    perror("shmget");
    return 1;
  }

  struct shm_data *p = shmat(id, NULL, 0);
  if (p == (void *)-1) {
    perror("shmat");
    return 1;
  }
  memset(p, 0, sizeof(struct shm_data));

  strcpy(p->server_msg, "Hello from server!");
  p->server_ready = 1;
  printf("[服务器] 已写入消息，等待客户端...\n");

  /* 等待客户端回复 */
  while (!p->client_ready) {
    sleep(1);
  }

  printf("[服务器] 收到客户端回复：%s\n", p->client_msg);

  shmdt(p);
  shmctl(id, IPC_RMID, NULL); /* 删除共享内存 */
  printf("[服务器] 共享内存已删除，退出\n");
  return 0;
}
