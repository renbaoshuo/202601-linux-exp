#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <sys/socket.h>

#define PORT 8888

int main(void) {
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    perror("socket");
    return 1;
  }

  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK); /* 127.0.0.1 */
  addr.sin_port = htons(PORT);
  if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    perror("connect（请先运行 ./server）");
    return 1;
  }

  const char *msg = "Hi, this is client.";
  /* TCP 可能只发送部分数据，循环直到整条消息发完。 */
  for (size_t sent = 0; sent < strlen(msg);) {
    ssize_t n = write(fd, msg + sent, strlen(msg) - sent);
    if (n <= 0) {
      perror("write socket");
      return 1;
    }
    sent += n;
  }
  if (shutdown(fd, SHUT_WR) < 0) { /* 用发送方向的 EOF 标记消息结束 */
    perror("shutdown");
    return 1;
  }
  printf("[客户端] 已发送消息：%s\n", msg);

  char buf[256] = {0};
  size_t used = 0;
  ssize_t n;
  while ((n = read(fd, buf + used, sizeof(buf) - 1 - used)) > 0) {
    used += n;
    if (used == sizeof(buf) - 1) {
      char extra;
      n = read(fd, &extra, 1);
      if (n > 0) {
        fprintf(stderr, "消息超过 255 字节\n");
        return 1;
      }
      break;
    }
  }
  if (n < 0 || used == 0) {
    fprintf(stderr, "接收失败或消息为空\n");
    return 1;
  }
  printf("[客户端] 收到服务器回复：%s\n", buf);

  FILE *fp = fopen("client_recv.txt", "w");
  if (!fp) {
    perror("fopen");
    return 1;
  }
  if (fprintf(fp, "%s", buf) < 0) {
    perror("fprintf");
    return 1;
  }
  if (fclose(fp) == EOF) {
    perror("fclose");
    return 1;
  }
  printf("[客户端] 回复已保存到 client_recv.txt\n");

  fp = fopen("client_recv.txt", "r");
  if (!fp) {
    perror("fopen");
    return 1;
  }
  char check[256] = {0};
  if (!fgets(check, sizeof(check), fp)) {
    fprintf(stderr, "文件读取失败\n");
    return 1;
  }
  if (fclose(fp) == EOF) {
    perror("fclose");
    return 1;
  }
  if (strcmp(buf, check) != 0) {
    fprintf(stderr, "文件内容验证失败\n");
    return 1;
  }
  printf("[客户端] 从文件读回：%s\n", check);

  close(fd);
  return 0;
}
