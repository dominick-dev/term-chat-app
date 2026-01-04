#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>

#define PORT 8080

int main()
{
  int socketfd;
  struct sockaddr_in serv_addr;
  memset(&serv_addr, 0, sizeof(serv_addr));
  serv_addr.sin_family = AF_INET;
  serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
  serv_addr.sin_port = htons(PORT);

  // create client socket
  socketfd = socket(AF_INET, SOCK_STREAM, 0);
  if (socketfd == -1)
  {
    printf("Error creating client socket: %s\n", strerror(errno));
    return -1;
  }
  else
  {
    printf("Client socket created successfully\n");
  }

  // attempt to connect to server socket
  int conn_res =
      connect(socketfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr));
  if (conn_res != 0)
  {
    printf("Error connecting to server socket w/ error: %s\n", strerror(errno));
    return -1;
  }
  else
  {
    printf("Connected to server socket!\n");
  }

  return 0;
}
