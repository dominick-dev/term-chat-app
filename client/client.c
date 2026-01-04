#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

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

  // send a string to server
  printf("Sending message to server...\n");
  char clientMessage[] = "Hello from the client!\n";
  ssize_t sendRes = send(socketfd, clientMessage, sizeof(clientMessage), 0);
  if (sendRes == -1)
  {
    printf("Error sending message to server: %s\n", strerror(errno));
  }
  else
  {
    printf("Send completed w/ status of: %lo\n", sendRes);
  }

  // get message from server
  char buff[256] = {0};
  ssize_t recvRes = recv(socketfd, buff, 256, 0);
  if (recvRes < 0)
  {
    printf("Error receiving message from server: %s\n", strerror(errno));
  }
  else if (recvRes == 0)
  {
    printf("No messages available to be recieved and peer has performed an "
           "orderly shutdown.\n");
  }
  else
  {
    printf("Message from the server: %s\n", buff);
  }

  // close client socekt when done
  close(socketfd);
  return 0;
}
