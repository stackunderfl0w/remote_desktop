#include <stdio.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/XTest.h>
#include <X11/extensions/XShm.h>
#include <X11/extensions/Xrandr.h>

#include <stdlib.h>
#include <sys/shm.h>
#include <pthread.h>
#include <sys/time.h>

#include "net-wrapper.h"
#include "qoi.h"

typedef struct{
    Display* disp;
    int connectionSocket;
    ssize_t* ack_count;
    int* running;
} server_input_thread_args;

int handle_input_event(Display *disp, int connectionSocket, ssize_t *ack_count) {
    int len;
    int inputs_received=0;
    int count= socket_status(connectionSocket);
    while(count){
        if(count==-1)
            return -1;
        inputs_received++;
        char *m = get_message_with_header(connectionSocket, &len);
        if(m==NULL)
            return -1;
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
        count= socket_status(connectionSocket);
    }
    return inputs_received;
}

void* input_thread_main(server_input_thread_args* args){
    while(*(args->running)){
        int res=handle_input_event(args->disp,args->connectionSocket,args->ack_count);
        if(res<0){
            *(args->running)=0;
            break;
        }
        else if(res>0){
            XFlush(args->disp);
        }
        usleep(1);
    }
    return NULL;
}

//replace clock() which counts program execution time and freezes during sleep/usleep
clock_t clock_mono(){
    struct timeval current_time;
    gettimeofday(&current_time, NULL);
    return current_time.tv_sec*(int)1e6+current_time.tv_usec;
}


int main(int argc, char* argv[]) {
    Display* display = XOpenDisplay(NULL);
    int screen = DefaultScreen(display);
    Window root = RootWindow(display, screen);


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

//        pid_t pid = fork();
//        if (pid == -1)
//            err(1, "Hull Breach");
//        if(pid>0){//parent
//            close(connectionSocket);
//            waitpid(pid,NULL,0);
//            continue;
//        }
        //Initialize X11/Xlib after forking to prevent the child being able to mess up the parent's copy
        //Mainly an issue over the network
        XInitThreads();
        Display* disp = XOpenDisplay(NULL);
        if(!disp)
            err(1,"Error: Cannot open default x11 display\n");

        Window root = DefaultRootWindow(disp);
        if(!root)
            err(1,"Error: Cannot open default x11 root window\n");
        XRRScreenConfiguration *config = XRRGetScreenInfo(display, root);
        short rate = XRRConfigCurrentRate(config);
        XRRFreeScreenConfigInfo(config);

        printf("%d\n",rate);

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
        ssize_t max_frame_presend=3;
        int running=1;
        server_input_thread_args inputThreadArgs={disp, connectionSocket, &max_frame_presend, &running};
        pthread_t input_thread;
        pthread_create(&input_thread, NULL, (void *(*)(void *)) input_thread_main, &inputThreadArgs);

        long frametime=CLOCKS_PER_SEC/rate;
        long halfframe=frametime/2;
        clock_t next_start = clock_mono();


        while(running) {
            //printf("NS:%ld,CL:%ld\n",next_start,clock_mono());
            clock_t current_time=clock_mono();
            //running early, wait a bit to process the next frame
            if(current_time<next_start-halfframe){
                next_start+=frametime;
                //printf("%ld\n",((next_start-halfframe)-current_time));
                usleep((next_start-halfframe)-current_time);
            }
            //running late, don't wait up
            else if(current_time>next_start+halfframe){
                while(current_time>next_start+halfframe)
                    next_start+=frametime;
            }
            //running fine, just set the next frame time
            else{
                next_start+=frametime;
            }

            //wait until we get confirmation the client received frames
            while(running && max_frame_presend==0){
                usleep(1000);
            }

            //XLockDisplay(disp);
            if(xshm_available)
                XShmGetImage(disp, RootWindow(disp,0), image, 0, 0, AllPlanes);
            else
                image = XGetImage(disp, root, 0, 0, wa.width, wa.height, AllPlanes, ZPixmap);
            if(!image)
                err(1,"Invalid X11 image");
            //XUnlockDisplay(disp);

            int size;
            qoi_encode_diff(enc, image->data, prev_img, wa.width, wa.height, image->bits_per_pixel / 8, &size);
            memcpy(prev_img, image->data, wa.height * wa.width * 4);

            if(!running)
                break;
            if(send_message_with_header(connectionSocket, (char *) enc, size)==-1)
                break;
            if(!xshm_available)
                XDestroyImage(image);

            XFlush(disp);
            max_frame_presend--;
        }
        running = 0;
        close(connectionSocket);
        free(prev_img);
        printf("Server: Connection to client closed\n\n");
        XDestroyImage(image);
    }

    close(listenSocket);
    return 0;
}
