#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>

int main()
{
  // init socket vars
  int socketfd;
  struct sockaddr_in serv_addr;

  /* SERVER STEPS */
  // 1. create socket
  socketfd = socket(PF_INET, SOCK_STREAM, 0);
  if (socketfd == -1)
  {
    printf("Error creating socket\n");
  }

  // 2. bind socket to ip and port
  memset(&serv_addr, 0, sizeof(serv_addr));
  int server = bind(socketfd, );

  // 3. listen for client connection
  // 4. send/recieve data w/ client
  // 5. close client socket when done

  return 0;
}
