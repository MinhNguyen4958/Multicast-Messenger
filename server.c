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
#include <pthread.h>


#define SENDPORT "30000"  // the port senders will be connecting to
#define RECVPORT "30001"  // the port receivers will be connecting to

#define BACKLOG 10   // how many pending connections queue will hold

#define MAXBUFLEN 1000

pthread_mutex_t bufferLock;


pthread_cond_t bufferCond;

pthread_attr_t detached;

int send_sockfd, new_sendfd;  // listen on send_sockfd, new connection on new_sendfd
int recv_sockfd, new_recvfd; // listen on recv_sockfd, new connection on new_recvfd

struct addrinfo send_hints, *send_servinfo, *send_p;
struct addrinfo recv_hints, *recv_servinfo, *recv_p;

struct sockaddr_storage recv_addr; // receiver's address information
struct sockaddr_storage send_addr; // sender's address information

socklen_t recv_size, send_size;

int yes=1;
char receiver[INET6_ADDRSTRLEN];
char sender[INET6_ADDRSTRLEN];
int rv;
int recv_rv;

char buffer[MAXBUFLEN];

int recv_fds[BACKLOG];

// get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa)
{
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in*)sa)->sin_addr);
    }

    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

void* senderRoutine(void* socket_fd) {


    int sender_fd = *(int*) socket_fd;
    
    int bytes; 
    while ((bytes = recv(sender_fd, buffer, MAXBUFLEN, MSG_NOSIGNAL)) > 0) {
        if(pthread_mutex_lock(&bufferLock) != 0) {
            perror("pthread_mutex_lock");
        }
        
        printf("server: received %s from sender\n", buffer);
        if (pthread_mutex_unlock(&bufferLock) != 0) {
           perror("pthread_mutex_unlock");
        }

        pthread_cond_broadcast(&bufferCond);

    }
    close(sender_fd); 
    return 0;
}

void* receiverRoutine(void* socket_fd) {
    if (pthread_mutex_lock(&bufferLock) != 0) {
        perror("pthread_mutex_lock");
    }
    int receiver_fd = *(int*) socket_fd; 


    int firstCount = 0;
    while(recv_fds[firstCount] != 0 && firstCount < BACKLOG) {
        firstCount++;
    }

    recv_fds[firstCount] = receiver_fd;
    while (1) {
        if (strlen(buffer) == 0) {
            pthread_cond_wait(&bufferCond, &bufferLock);

        } else {
            int counter = 0;
            while(recv_fds[counter] != 0 && counter < BACKLOG) {
             if (send(recv_fds[counter], buffer, MAXBUFLEN, MSG_NOSIGNAL) == -1) {
                if (errno == EPIPE) {
                    recv_fds[counter] = 0;
                    }
                }

                counter++;
            }            

            // after sending to the receiver, we clear the buffer
            memset(buffer,0,strlen(buffer));

            if (pthread_mutex_unlock(&bufferLock) != 0) {
                perror("pthread_mutex_unlock");
            }
            
        }
    }

    close(receiver_fd);
    return 0;
}


void* connectSender() {

    memset(&send_hints, 0, sizeof send_hints);
    send_hints.ai_family = AF_UNSPEC;
    send_hints.ai_socktype = SOCK_STREAM;
    send_hints.ai_flags = AI_PASSIVE; // use my IP

    // get the sender's IP address info
    if ((rv = getaddrinfo(NULL, SENDPORT, &send_hints, &send_servinfo)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        return 0;
    }

    // loop through all the results and bind to the first we can
    for(send_p = send_servinfo; send_p != NULL; send_p = send_p->ai_next) {
        if ((send_sockfd = socket(send_p->ai_family, send_p->ai_socktype,
                send_p->ai_protocol)) == -1) {
            perror("server: socket");
            continue;
        }

        if (setsockopt(send_sockfd, SOL_SOCKET, SO_REUSEADDR, &yes,
                sizeof(int)) == -1) {
            perror("setsockopt");
            exit(1);
        }

        if (bind(send_sockfd, send_p->ai_addr, send_p->ai_addrlen) == -1) {
            close(send_sockfd);
            perror("server: bind");
            continue;
        }

        break;
    }

    freeaddrinfo(send_servinfo); // all done with this structure

    if (send_p == NULL)  {
        fprintf(stderr, "server: failed to bind\n");
        exit(1);
    }

    if (listen(send_sockfd, BACKLOG) == -1) {
        perror("listen");
        exit(1);
    }

    send_size = sizeof send_addr;
    while ((new_sendfd = accept(send_sockfd, (struct sockaddr *)&send_addr, &send_size))) {
        
        inet_ntop(send_addr.ss_family, get_in_addr((struct sockaddr *)&send_addr), sender, sizeof sender);
        printf("server: got sender connection from %s, Port: %s\n", sender, SENDPORT);
        
        if (new_sendfd == -1) {
            perror("accept");
            continue;
        }

        // turn this variable passable to the pthread_create
        int *sock_fd = malloc(sizeof(int));
        *sock_fd = new_sendfd;
        pthread_t newSender;
        
        if (pthread_create(&newSender, &detached, &senderRoutine, sock_fd) != 0) {
            perror("Failed to create detached sender thread");
        }
    }

    close(send_sockfd);

    return EXIT_SUCCESS;
}

void* ConnectReceiver() {

    memset(&recv_hints, 0, sizeof recv_hints);
    recv_hints.ai_family = AF_UNSPEC;
    recv_hints.ai_socktype = SOCK_STREAM;
    recv_hints.ai_flags = AI_PASSIVE; // use my IP

    // get the receiver's IP address info
    if ((recv_rv = getaddrinfo(NULL, RECVPORT, &recv_hints, &recv_servinfo)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(recv_rv));
        return 0;
    }

    // loop through all the results and bind to the first we can
    for(recv_p = recv_servinfo; recv_p != NULL; recv_p = recv_p->ai_next) {
        if ((recv_sockfd = socket(recv_p->ai_family, recv_p->ai_socktype,
                recv_p->ai_protocol)) == -1) {
            perror("server: socket");
            continue;
        }

        if (setsockopt(recv_sockfd, SOL_SOCKET, SO_REUSEADDR, &yes,
                sizeof(int)) == -1) {
            perror("setsockopt");
            exit(1);
        }

        if (bind(recv_sockfd, recv_p->ai_addr, recv_p->ai_addrlen) == -1) {
            close(recv_sockfd);
            perror("server: bind");
            continue;
        }

        break;
    }

    freeaddrinfo(recv_servinfo); // all done with this structure

    if (recv_p == NULL)  {
        fprintf(stderr, "server: failed to bind\n");
        exit(1);
    }

    if (listen(recv_sockfd, BACKLOG) == -1) {
        perror("listen");
        exit(1);
    }

    recv_size = sizeof recv_addr;
    while ((new_recvfd = accept(recv_sockfd, (struct sockaddr *)&recv_addr, &recv_size))) {

        inet_ntop(recv_addr.ss_family, get_in_addr((struct sockaddr *)&recv_addr), receiver, sizeof receiver);
        printf("server: got connection from %s, Port: %s\n", receiver, RECVPORT);

        if (new_recvfd == -1) {
            perror("accept");
            continue;
        }

        int *sock_fd = malloc(sizeof(int));
        *sock_fd = new_recvfd;
        pthread_t newReceiver;
        if (pthread_create(&newReceiver, &detached, &receiverRoutine, sock_fd) != 0) {
            perror("Failed to make a receiver connection thread");
        }
    }

    close(recv_sockfd);

    return EXIT_SUCCESS;
}


int main(void) {

    if (pthread_mutex_init(&bufferLock,NULL) != 0) {
        perror("pthread_mutex_init");
        return -1;
    }


    if (pthread_cond_init(&bufferCond, NULL) != 0) {
        perror("pthread_cond_init");
        return -1;
    }

    if (pthread_attr_init(&detached) != 0) {
        perror("pthread_attr_init");
        return -1;
    }

    if (pthread_attr_setdetachstate(&detached, PTHREAD_CREATE_DETACHED) != 0) {
        perror("pthread_attr_setdetachstate");
        return -1;
    }

    printf("Port for sender clients: %s\nPort for receiver clients: %s\n", SENDPORT, RECVPORT);

    memset(&recv_fds, 0, sizeof(recv_fds));

    pthread_t senderConnection, receiverConnection;

    if (pthread_create(&senderConnection, NULL, &connectSender, NULL) != 0) {
        perror("Failed to create sender connection thread");
    }

    if (pthread_create(&receiverConnection, NULL, &ConnectReceiver, NULL) != 0) {
        perror("Failed to create receiver connection thread");
    }


    printf("server: waiting for connections...\n");

    if (pthread_join(senderConnection, NULL) != 0) {
        perror("Failed to join sender connection thread\n");
    }
    
    if (pthread_join(receiverConnection, NULL) != 0) {
        perror("Failed to join receiver connectoin thread\n");
    }

    return 0;
}