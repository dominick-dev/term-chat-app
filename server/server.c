#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>

int main()
{
  // init socket vars, set server ip and port
  int socketfd;
  struct sockaddr_in serv_addr;
  serv_addr.sin_family = AF_INET;
  serv_addr.sin_addr.s_addr = INADDR_ANY;
  serv_addr.sin_port = 8080;

  struct sockaddr_in client_addr;
  socklen_t client_addr_len = sizeof(client_addr);

  /* SERVER STEPS */
  // 1. create socket
  socketfd = socket(AF_INET, SOCK_STREAM, 0);
  if (socketfd == -1)
  {
    printf("Error creating server socket: %s\n", strerror(errno));
  }
  else
  {
    printf("Server Socket created\n");
  }

  // 2. bind socket to ip and port
  memset(&serv_addr, 0, sizeof(serv_addr));
  int server = bind(socketfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr));
  if (server == -1)
  {
    printf("Error binding server socket: %s\n", strerror(errno));
  }
  else
  {
    printf("Server socket bound to ip and port\n");
  }

  // 3. listen for client connection
  if (listen(socketfd, 5) != 0)
  {
    printf("Error listening on socket server: %s\n", strerror(errno));
  }
  else
  {
    printf("Server socket listening for new connections...\n");
  }

  // 4. accept new client connection
  int new_socket =
      accept(socketfd, (struct sockaddr *)&client_addr, &client_addr_len);
  if (new_socket != 0)
  {
    printf("Error connecting to new client socket: %s\n", strerror(errno));
  }
  else
  {
    printf("Server socket connected to new client\n");
  }

  // 5. send/recieve data w/ client
  // 6. close client socket when done

  return 0;
}
