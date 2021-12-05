#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <netdb.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <arpa/inet.h>

#define PORT "30000" // the port client will be connecting to 

ssize_t readline(char **lineptr, FILE *stream)
{
  size_t len = 0;  // Size of the buffer, ignored.

  ssize_t chars = getline(lineptr, &len, stream);

  if ((*lineptr)[chars - 1] == '\n') {
      (*lineptr)[chars - 1] = '\0';
      --chars;
      }

  return chars;
}
// get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa)
{
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in*)sa)->sin_addr);
    }

    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

int main(int argc, char *argv[])
{
    int sockfd, numbytes;  
    struct addrinfo hints, *servinfo, *p;
    int rv;
    char s[INET6_ADDRSTRLEN];

    if (argc != 2) {
        fprintf(stderr,"usage: sender hostname\n");
        exit(1);
    }

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    if ((rv = getaddrinfo(argv[1], PORT, &hints, &servinfo)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        return 1;
    }

    // loop through all the results and connect to the first we can
    for(p = servinfo; p != NULL; p = p->ai_next) {
        if ((sockfd = socket(p->ai_family, p->ai_socktype,
                p->ai_protocol)) == -1) {
            perror("sender: socket");
            continue;
        }

        if (connect(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
            close(sockfd);
            perror("sender: connect");
            continue;
        }

        break;
    }

    if (p == NULL) {
        fprintf(stderr, "sender: failed to connect\n");
        return 2;
    }

    inet_ntop(p->ai_family, get_in_addr((struct sockaddr *)p->ai_addr), s, sizeof s);
    printf("sender: connected to the server\n");

    freeaddrinfo(servinfo); // all done with this structure

    char *message = NULL;
    ssize_t bytes;

    while(1) {
        printf("Enter a message: ");
        if ( (bytes = readline(&message, stdin)) == -1) {
            perror("getline");
        }

        if ((numbytes = send(sockfd, message, strlen(message), 0)) == -1) {
            perror("send");
            exit(1);
        }
        printf("sender: sent \"%s\" to the server\n", message);
        free(message);
    }

    close(sockfd);

    return 0;
}