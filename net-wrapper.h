#pragma once
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/wait.h>
#include <netdb.h>      // gethostbyname()
#include <fcntl.h>
#include <sys/poll.h>
#include <err.h>
#include <errno.h>
#include <sys/uio.h>

#include <netinet/tcp.h>

#include <poll.h>

int socket_status(int socket_fd) {
    struct pollfd fds[1];
    fds[0].fd = socket_fd;
    fds[0].events = POLLIN;

    // Timeout set to zero for non-blocking check
    return poll(fds, 1, 0);
}


#define MIN(a,b) (((a)<(b))?(a):(b))
#define MAX(a,b) (((a)>(b))?(a):(b))

void serverAddressStruct(struct sockaddr_in* address, int portNumber){

    // Clear out the address struct
    memset((char*) address, 0, sizeof(*address));

    // The address should be network capable
    address->sin_family = AF_INET;
    // Store the port number
    address->sin_port = htons(portNumber);
    // Allow a client at any address to connect to this server
    address->sin_addr.s_addr = INADDR_ANY;
}

void clientAddressStruct(struct sockaddr_in* address, int portNumber, char* hostname){

    // Clear out the address struct
    memset((char*) address, 0, sizeof(*address));

    // The address should be network capable
    address->sin_family = AF_INET;
    // Store the port number
    address->sin_port = htons(portNumber);

    // Get the DNS entry for this host name
    struct hostent* hostInfo = gethostbyname(hostname);
    if (hostInfo == NULL) {
        fprintf(stderr, "CLIENT: ERROR, no such host\n");
        exit(0);
    }
    // Copy the first IP address from the DNS entry to sin_addr.s_addr
    memcpy((char*) &address->sin_addr.s_addr,
           hostInfo->h_addr_list[0],
           hostInfo->h_length);
}


static inline ssize_t get_message(int listenSocket,char* buf,int expected_length){
    ssize_t charsRead=0;
    while (charsRead<expected_length){
        ssize_t r = recv(listenSocket, buf+charsRead, expected_length-charsRead, 0); // Get the next chunk
        if (r <= 0) {//socket open, no data recieved
            printf("Server: connection timeout or closed by peer\n");
            return -1;
        }
        charsRead+=r;
    }
    return charsRead;
}
static inline char* get_message_with_header(int listenSocket, int* len){
    //get 8 byte length header
    char tmp[9]={0};
    if(get_message(listenSocket,tmp,8)==-1){
        return NULL;//timeout
    }

    *len=atoi(tmp);
    char* ret_buf= calloc(*len+1,1);
    //get actual message of specified length
    if(get_message(listenSocket,ret_buf,*len)==-1){
        free(ret_buf);
        return NULL;
    }
    return ret_buf;
}
ssize_t send_message(int socketFD, char* buffer, int len){
    ssize_t charsWritten = 0;
    while (charsWritten<len){
        ssize_t r = send(socketFD, buffer+charsWritten, len-charsWritten, 0); // Send the next chunk
        if (r <= 0) {
            printf("Server: connection timeout or closed by peer\n");
            return -1;
        }
        charsWritten+=r;
    }
    return charsWritten;
}
ssize_t send_message_with_header(int socketFD, char* buffer, size_t len){
    char tmp[9];
    sprintf(tmp,"%-8d",len);
    //send header
//    if(send_message(socketFD, tmp, 8)==-1){
//        return -1;
//    }
//    //send message
//    ssize_t bytes_written = send_message(socketFD, buffer, len);

    //scatter gather i/o
    struct iovec iov[2];
    iov[0].iov_base = tmp;
    iov[0].iov_len = 8;
    iov[1].iov_base = buffer;
    iov[1].iov_len = len;

    ssize_t bytes_written = writev (socketFD, iov, 2);
    return bytes_written;
}

void cork_socket(int socketfd){
    int state = 1;
    setsockopt(socketfd, IPPROTO_TCP, TCP_CORK, &state, sizeof(state));
}
void uncork_socket(int socketfd){
    int state = 0;
    setsockopt(socketfd, IPPROTO_TCP, TCP_CORK, &state, sizeof(state));
}
int enable_tcp_nodelay(int socketfd){
    int state = 1;
    return setsockopt(socketfd,
                            IPPROTO_TCP,
                            TCP_NODELAY,
                            (char *) &state,
                            sizeof(int));    // 1 - on, 0 - off
}

