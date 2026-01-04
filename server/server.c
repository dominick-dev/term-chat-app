#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define PORT 8080

int main()
{
  // init socket vars
  int socketfd;
  struct sockaddr_in client_addr;
  socklen_t servSocketLength = sizeof(socketfd);
  socklen_t client_addr_len = sizeof(client_addr);

  // test

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
  struct sockaddr_in serv_addr;
  memset(&serv_addr, 0, sizeof(serv_addr));
  serv_addr.sin_family = AF_INET;
  serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
  serv_addr.sin_port = htons(PORT);

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
      accept(socketfd, (struct sockaddr *)&serv_addr, &servSocketLength);
  if (new_socket == -1)
  {
    printf("Error connecting to new client socket: %s\n", strerror(errno));
  }
  else
  {
    printf("Server socket connected to new client!\n");
  }

  // 5. send/recieve data w/ client
  char buff[256] = {0};
  ssize_t recvRes = recv(new_socket, buff, 256, 0);
  if (recvRes < 0)
  {
    printf("Error receiving message from client: %s\n", strerror(errno));
  }
  else if (recvRes == 0)
  {
    printf("No messages available to be recieved and peer has performed an "
           "orderly shutdown.\n");
  }
  else
  {
    printf("Message from the client: %s\n", buff);
  }

  // send message back to client
  char serverMessage[] = "Hello from the server!\n";
  ssize_t sRes = send(new_socket, serverMessage, sizeof(serverMessage), 0);
  if (sRes == -1)
  {
    printf("Message send from Server failed: %s\n", strerror(errno));
  }

  // 6. close server socket when done
  close(socketfd);
  close(new_socket);
  return 0;
}
