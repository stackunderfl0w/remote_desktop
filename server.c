#include <stdio.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/XTest.h>
#include <X11/extensions/XShm.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/shm.h>

#include "net-wrapper.h"
#include "qoi.h"

int handle_input_event(Display *disp, int connectionSocket, ssize_t *ack_count) {
    int len;
    int count;
    ioctl(connectionSocket, FIONREAD, &count);
    while(count){
        char *m = get_message_with_header(connectionSocket, &len);
        if(m==NULL)
            return 1;
        //check if socket has messages
        switch (m[0]) {
            case 'A'://frame ack
                (*ack_count)++;
                break;
            case 'M':{//mouse
                //printf("mouse-event\n");
                int x,y,mb=0,click=0;
                int dir=0;
                int scroll=0;
                switch (m[1]){
                    case 'C'://Click
                        click=m[2]=='D'?True:False;
                        sscanf(m+4,"%d %d %d",&mb,&x,&y);
                        break;
                    case 'S'://Scroll
                        scroll=1;
                        dir=m[2]=='D';
                    case 'V'://Mouse moved/dragged
                        sscanf(m+4,"%d%d",&x,&y);
                        break;

                }
                //printf("%d %d %d\n",mb,x,y);
                //move mouse
                XTestFakeMotionEvent(disp,0,x,y,CurrentTime);
                //click
                if(mb)
                    XTestFakeButtonEvent(disp, mb, click, CurrentTime);
                //scroll
                if(scroll){
                    //button 4 up, button 5 down
                    XTestFakeButtonEvent(disp, Button4+dir, True, CurrentTime);
                    XTestFakeButtonEvent(disp, Button4+dir, False, CurrentTime);
                }
                break;
            }
            case 'K':{
                //printf("key-event\n");
                //printf("(%s)\n",m);
                int kc=0,press;
                press=m[1]=='D'?True:False;
                sscanf(m+3,"%d",&kc);
                XTestFakeKeyEvent(disp,XKeysymToKeycode(disp,kc),press,CurrentTime);
                break;
            }
        }
        free(m);
        ioctl(connectionSocket, FIONREAD, &count);
    }
    return 0;
}


int main(int argc, char* argv[]) {
    int portNumber=6001;
    if(argc>1){
        portNumber= (int)strtol(argv[1],NULL,10);
        if(!portNumber){
            err(EXIT_FAILURE,"Invalid port number \'%s\'",argv[1]);
        }
    }

    int listenSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (listenSocket < 0)
        err(2,"ERROR opening socket");

    int connectionSocket;
    struct sockaddr_in serverAddress, clientAddress;
    socklen_t sizeOfClientInfo = sizeof(clientAddress);

    // Set up the address struct for the server socket
    serverAddressStruct(&serverAddress, portNumber);

    //set socket timeout
    struct timeval tv;
    tv.tv_sec = 5; tv.tv_usec = 0;

    // Associate the socket to the port
    if (bind(listenSocket,(struct sockaddr *)&serverAddress,sizeof(serverAddress)) < 0)
        err(3,"ERROR on binding\n");

    // Start listening for connetions.
    listen(listenSocket, 1);

    while(1){
        printf("Server: Waiting for connection\n");
        connectionSocket = accept(listenSocket,(struct sockaddr *)&clientAddress,&sizeOfClientInfo);
        if (setsockopt (connectionSocket, SOL_SOCKET, SO_RCVTIMEO, &tv,sizeof tv) < 0) perror("setsockopt failed\n");
        //enable_tcp_nodelay(connectionSocket);

        pid_t pid = fork();
        if (pid == -1)
            err(1, "Hull Breach");
        if(pid>0){//parent
            close(connectionSocket);
            waitpid(pid,NULL,0);
            continue;
        }
        //Initialize X11/Xlib after forking to prevent the child being able to mess up the parent's copy
        //Mainly an issue over the network
        Display* disp = XOpenDisplay(NULL);
        if(!disp)
            err(1,"Error: Cannot open default x11 display\n");

        Window root = DefaultRootWindow(disp);
        if(!root)
            err(1,"Error: Cannot open default x11 root window\n");

        int xshm_available=XShmQueryExtension(disp);
        if(!xshm_available)
            printf( "Server: X11 SHM extension support not found, falling back\n" );

        XWindowAttributes wa;
        XGetWindowAttributes(disp, root, &wa);

        XImage *image=NULL;
        XShmSegmentInfo shminfo;
        if(xshm_available){
            image = XShmCreateImage(disp, wa.visual, wa.depth, ZPixmap, NULL, &shminfo, wa.width, wa.height);
            shminfo.shmid = shmget(IPC_PRIVATE, image->bytes_per_line * image->height, IPC_CREAT | 0777);
            shminfo.shmaddr = image->data = shmat(shminfo.shmid, 0, 0);
            shminfo.readOnly = False;//needed?
            XShmAttach(disp, &shminfo);
        }

        uint8_t* prev_img= malloc(wa.width*wa.height*4);
        uint8_t* enc= malloc(wa.width*wa.height*4);

        for (int i = 0; i < wa.width*wa.height; ++i) {
            ((uint32_t*)prev_img)[i]=0xff000000;
        }
        ssize_t max_frame_presend=5;
        while(1) {
            if(handle_input_event(disp, connectionSocket, &max_frame_presend)){
                break;
            }
            XFlush(disp);

            //wait until we get confirmation the client recieved frames
            while(max_frame_presend==0){
                if(handle_input_event(disp, connectionSocket, &max_frame_presend)){
                    break;
                }
            }

            if(xshm_available)
                XShmGetImage(disp, RootWindow(disp,0), image, 0, 0, AllPlanes);
            else
                image = XGetImage(disp, root, 0, 0, wa.width, wa.height, AllPlanes, ZPixmap);
            if(!image)
                err(1,"Invalid X11 image");

            int size;
            qoi_encode_diff(enc, image->data, prev_img, wa.width, wa.height, image->bits_per_pixel / 8, &size);
            memcpy(prev_img, image->data, wa.height * wa.width * 4);

            if(send_message_with_header(connectionSocket, (char *) enc, size)==-1){
                break;
            }
            if(!xshm_available){
                XDestroyImage(image);
            }
            XFlush(disp);
            max_frame_presend--;
        }
        close(connectionSocket);
        free(prev_img);
        printf("Server: Connection to client closed\n\n");
        XDestroyImage(image);
    }

    close(listenSocket);
    return 0;
}
