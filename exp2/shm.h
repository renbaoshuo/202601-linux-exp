#ifndef SHM_H
#define SHM_H

#include <sys/ipc.h>
#include <sys/shm.h>

#define SHM_KEY 1234

struct shm_data {
  int server_ready; /* 服务器已写入消息 */
  int client_ready; /* 客户端已写入回复 */
  char server_msg[128];
  char client_msg[128];
};

#endif
