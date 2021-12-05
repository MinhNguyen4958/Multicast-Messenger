#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>

#define SENDPORT "30000"  // the port senders will be connecting to
#define RECVPORT "30001"  // the port receivers will be connecting to
#define BACKLOG 10   // how many pending connections queue will hold

#define MAXBUFLEN 1000

void sigchld_handler(int s)
{
    // waitpid() might overwrite errno, so we save and restore it:
    int saved_errno = errno;

    while(waitpid(-1, NULL, WNOHANG) > 0);

    errno = saved_errno;
}


// get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa)
{
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in*)sa)->sin_addr);
    }

    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

int main(void) {
    printf("Port for sender clients: %s\nPort for receiver clients: %s\n", SENDPORT, RECVPORT);
    int recv_sockfd, new_recvfd, send_sockfd, new_sendfd;  // listen on sock_fd, new connection on new_fd
    struct addrinfo hints, *servinfo, *p;

    struct sockaddr_storage recv_addr; // receiver's address information
    struct sockaddr_storage send_addr; // sender's address information
    
    socklen_t recv_size, send_size;

    struct sigaction sa;
    int yes=1;
    char receiver[INET6_ADDRSTRLEN];
    char sender[INET6_ADDRSTRLEN];
    int rv;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE; // use my IP

    // get the receiver's IP address info
    if ((rv = getaddrinfo(NULL, RECVPORT, &hints, &servinfo)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        return 1;
    }

    // loop through all the results and bind to the first we can
    for(p = servinfo; p != NULL; p = p->ai_next) {
        if ((recv_sockfd = socket(p->ai_family, p->ai_socktype,
                p->ai_protocol)) == -1) {
            perror("server: socket");
            continue;
        }

        if (setsockopt(recv_sockfd, SOL_SOCKET, SO_REUSEADDR, &yes,
                sizeof(int)) == -1) {
            perror("setsockopt");
            exit(1);
        }

        if (bind(recv_sockfd, p->ai_addr, p->ai_addrlen) == -1) {
            close(recv_sockfd);
            perror("server: bind");
            continue;
        }

        break;
    }

    freeaddrinfo(servinfo); // all done with this structure

    if (p == NULL)  {
        fprintf(stderr, "server: failed to bind\n");
        exit(1);
    }

    if (listen(recv_sockfd, BACKLOG) == -1) {
        perror("listen");
        exit(1);
    }
    
    
    // get the sender's IP address info
    if ((rv = getaddrinfo(NULL, SENDPORT, &hints, &servinfo)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        return 1;
    }

    // loop through all the results and bind to the first we can
    for(p = servinfo; p != NULL; p = p->ai_next) {
        if ((send_sockfd = socket(p->ai_family, p->ai_socktype,
                p->ai_protocol)) == -1) {
            perror("server: socket");
            continue;
        }

        if (setsockopt(send_sockfd, SOL_SOCKET, SO_REUSEADDR, &yes,
                sizeof(int)) == -1) {
            perror("setsockopt");
            exit(1);
        }

        if (bind(send_sockfd, p->ai_addr, p->ai_addrlen) == -1) {
            close(send_sockfd);
            perror("server: bind");
            continue;
        }

        break;
    }

    freeaddrinfo(servinfo); // all done with this structure

    if (p == NULL)  {
        fprintf(stderr, "server: failed to bind\n");
        exit(1);
    }

    if (listen(send_sockfd, BACKLOG) == -1) {
        perror("listen");
        exit(1);
    }

    sa.sa_handler = sigchld_handler; // reap all dead processes
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    if (sigaction(SIGCHLD, &sa, NULL) == -1) {
        perror("sigaction");
        exit(1);
    }

    printf("server: waiting for connections...\n");

    while(1) {  // main accept() loop
        recv_size = sizeof recv_addr;
        send_size = sizeof send_addr;
        new_recvfd = accept(recv_sockfd, (struct sockaddr *)&recv_addr, &recv_size);
        new_sendfd = accept(send_sockfd, (struct sockaddr *)&send_addr, &send_size);

        if (new_recvfd == -1) {
            perror("accept");
            continue;
        }
        
        if (new_sendfd == -1) {
            perror("accept");
            continue;
        }

        inet_ntop(recv_addr.ss_family, get_in_addr((struct sockaddr *)&recv_addr), receiver, sizeof receiver);
        inet_ntop(send_addr.ss_family, get_in_addr((struct sockaddr *)&send_addr), sender, sizeof sender);

        printf("server: got connection from %s, Port: %s\n", sender, SENDPORT);
        printf("server: got connection from %s, Port: %s\n", receiver, RECVPORT);
        if (!fork()) { // this is the child process
            close(recv_sockfd); // child doesn't need the listener
            close(send_sockfd); // also for the sender
            char buf[MAXBUFLEN];
            int bytes;
            if ((bytes = recv(new_sendfd, buf, MAXBUFLEN - 1, 0)) == -1) {
                perror("recv");
            }
            // add null terminator to the string
            buf[bytes] = '\0';

            if (send(new_recvfd, buf, MAXBUFLEN - 1, 0) == -1) {
                perror("send");
            }

            close(new_recvfd);
            close(new_sendfd);
            exit(0);
        }
        close(new_recvfd);  // parent doesn't need this
        close(new_sendfd);
    }

    return 0;
}