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

void
poll_wait(int fd, int events)
{
    int n;
    struct pollfd pollfds[1];
    memset((char *) &pollfds, 0, sizeof(pollfds));

    pollfds[0].fd = fd;
    pollfds[0].events = events;

    n = poll(pollfds, 1, -1);
    if (n < 0) {
        perror("poll()");
        errx(1, "Poll failed");
    }
}


size_t
writenw(int fd, char *buf, size_t n){
    size_t pos = 0;
    ssize_t res;
    while (n > pos) {
        poll_wait(fd, POLLOUT | POLLERR);
        res = write (fd, buf + pos, n - pos);
        switch ((int)res) {
            case -1:
                if (errno == EINTR || errno == EAGAIN)
                    continue;
                return 0;
            case 0:
                errno = EPIPE;
                return pos;
            default:
                pos += (size_t)res;
        }
    }
    return (pos);

}

#define ACCEPT "ACC"
#define DECLINE "DEC"

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


//make sure input buf is 1 longer than expected message.
static inline int get_message(int listenSocket,char* buf,int expected_length){
    ssize_t charsRead=0;
    while (charsRead<expected_length){ // As long as we haven't found the terminal...
        size_t r = recv(listenSocket, buf+charsRead, expected_length-charsRead, 0); // Get the next chunk
        if (r == -1) {//socket open, no data recieved
            if (charsRead < 0){
                err(2,"ERROR reading from socket\n");
            }
            r=0;
        }
        else if(r==0){//socket closed
            printf("Server: connection timeout or closed by peer\n");
            return -1;
        }
        charsRead+=r;

    }
    //printf("%ld\n",charsRead);
    return 0;
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
void send_message(int socketFD, char* buffer, int len){
    ssize_t charsWritten = 0;
    while(charsWritten<len){
        charsWritten+=send(socketFD, buffer+charsWritten, len-charsWritten, 0);
    }
}
void send_message_with_header(int socketFD, char* buffer, int len){
    char tmp[9];
    sprintf(tmp,"%-8d",len);
    //send header
    //send_message(socketFD, tmp, 8);
    //send message
    //send_message(socketFD, buffer, len);

    //scatter gather i/o
    struct iovec iov[2];
    iov[0].iov_base = tmp;
    iov[0].iov_len = 8;
    iov[1].iov_base = buffer;
    iov[1].iov_len = len;

    int bytes_written = writev (socketFD, iov, 2);
}

void cork_socket(int socketfd){
    int state = 1;
    setsockopt(socketfd, IPPROTO_TCP, TCP_CORK, &state, sizeof(state));
}
void uncork_socket(int socketfd){
    int state = 0;
    setsockopt(socketfd, IPPROTO_TCP, TCP_CORK, &state, sizeof(state));
}
void enable_tcp_nodelay(int socketfd){
    int state = 1;
    int result = setsockopt(socketfd,
                            IPPROTO_TCP,
                            TCP_NODELAY,
                            (char *) &state,
                            sizeof(int));    // 1 - on, 0 - off

}

