#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

int main(int argc, char *argv[]) {
  int sockfd;
  int len;
  struct sockaddr_in address;
  int result;
  //   char ch = 'A';
  char ch[40] = "GET /\n";
  char rst[2000];
  u_short port = 4000;

  sockfd = socket(AF_INET, SOCK_STREAM, 0);
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = inet_addr("127.0.0.1");
  address.sin_port = htons(port);
  len = sizeof(address);
  result = connect(sockfd, (struct sockaddr *)&address, len);

  if (result == -1) {
    perror("oops: client1");
    exit(1);
  }
  printf("client1 running on port %d\n", port);
  send(sockfd, ch, strlen(ch), 0);
  recv(sockfd, rst, sizeof(rst), 0);
  //   printf("char from server = %c\n", ch);
  printf("result from server = %s\n", rst);
  close(sockfd);
  exit(0);
}
