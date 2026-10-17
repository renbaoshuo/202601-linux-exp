#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <sys/socket.h>

#define PORT 8888

int main(void) {
  int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (listen_fd < 0) {
    perror("socket");
    return 1;
  }

  int on = 1; /* 允许重启后重新绑定端口 */
  if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) < 0) {
    perror("setsockopt");
    return 1;
  }

  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY); /* 本机任意地址 */
  addr.sin_port = htons(PORT);
  if (bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    perror("bind");
    return 1;
  }
  if (listen(listen_fd, 1) < 0) {
    perror("listen");
    return 1;
  }
  printf("[服务器] 监听端口 %d，等待客户端...\n", PORT);

  fflush(stdout);

  int conn_fd = accept(listen_fd, NULL, NULL);
  if (conn_fd < 0) {
    perror("accept");
    return 1;
  }

  char buf[256] = {0};
  size_t used = 0;
  ssize_t n;
  while ((n = read(conn_fd, buf + used, sizeof(buf) - 1 - used)) > 0) {
    used += n;
    if (used == sizeof(buf) - 1) {
      char extra;
      n = read(conn_fd, &extra, 1);
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
  printf("[服务器] 收到客户端消息：%s\n", buf);

  int file_fd = open("server_recv.txt", O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (file_fd < 0) {
    perror("open");
    return 1;
  }
  if (write(file_fd, buf, used) != (ssize_t)used) {
    perror("write");
    return 1;
  }
  if (close(file_fd) < 0) {
    perror("close file");
    return 1;
  }
  printf("[服务器] 消息已保存到 server_recv.txt\n");

  char file_buf[256] = {0};
  file_fd = open("server_recv.txt", O_RDONLY);
  if (file_fd < 0) {
    perror("open");
    return 1;
  }
  if (read(file_fd, file_buf, sizeof(file_buf) - 1) < 0) {
    perror("read");
    return 1;
  }
  if (close(file_fd) < 0) {
    perror("close file");
    return 1;
  }
  if (strcmp(buf, file_buf) != 0) {
    fprintf(stderr, "文件内容验证失败\n");
    return 1;
  }
  printf("[服务器] 从文件读回：%s\n", file_buf);

  const char *reply = "Hello from server!";
  /* TCP 可能只发送部分数据，循环直到整条消息发完。 */
  for (size_t sent = 0; sent < strlen(reply);) {
    ssize_t n = write(conn_fd, reply + sent, strlen(reply) - sent);
    if (n <= 0) {
      perror("write socket");
      return 1;
    }
    sent += n;
  }
  if (shutdown(conn_fd, SHUT_WR) < 0) { /* 用发送方向的 EOF 标记消息结束 */
    perror("shutdown");
    return 1;
  }
  printf("[服务器] 已发送回复：%s\n", reply);

  close(conn_fd);
  close(listen_fd);
  return 0;
}
