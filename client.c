#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>

#include <SDL2/SDL.h>
#include <X11/keysym.h>

#include "net-wrapper.h"
#include "qoi.h"
#include <time.h>


int main(int argc, char *argv[]) {
    int portNumber=6001;
    char* hostname="127.0.0.1";
    if(argc>1){
        hostname=argv[1];
    }
    if(argc>2){
        portNumber= (int)strtol(argv[2],NULL,10);
        if(!portNumber){
            err(EXIT_FAILURE,"Invalid port number \'%s\'",argv[2]);
        }
    }

    int socketFD;
    struct sockaddr_in serverAddress;

    socketFD = socket(AF_INET, SOCK_STREAM, 0);
    if (socketFD < 0){
        err(1,"CLIENT: ERROR opening socket");
    }
    // Set up the server address struct
    clientAddressStruct(&serverAddress, portNumber, hostname);

    // Connect to server
    if (connect(socketFD, (struct sockaddr*)&serverAddress, sizeof(serverAddress)) < 0){
        err(2,"CLIENT: ERROR connecting\n");
    }
    //don't buffer packets unnecessarily
    enable_tcp_nodelay(socketFD);

    SDL_Init(SDL_INIT_EVERYTHING);
    SDL_Window *window = SDL_CreateWindow("remote-desktop", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 1280, 720, SDL_WINDOW_OPENGL|SDL_WINDOW_RESIZABLE);
    SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);

    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
    SDL_RenderClear(renderer);
    SDL_RenderPresent(renderer);

    struct timespec timestamps[60];
    int time_idx=0;
    int running=1;
    char *ms = "Client connected\n";
    send_message_with_header(socketFD, ms, strlen(ms));
    int window_w=1280,window_h=720;
    int host_w=1920,host_h=1080,host_c=4;
    uint8_t* current_image=calloc(host_w*host_h*host_c,1);

    while(running){
        clock_gettime(CLOCK_MONOTONIC,&timestamps[time_idx%60]);
        struct timespec cur=timestamps[time_idx%60],p=timestamps[(time_idx+1)%60];
        double dtime=(double)(cur.tv_sec-p.tv_sec)+(double)(cur.tv_nsec-p.tv_nsec)/1000000000.0;
        double fps=60/dtime;
        if(!time_idx)
            printf("fps:%f\n",fps);
        time_idx=(time_idx+1)%60;
        SDL_Event event;
        //bundle packets
        cork_socket(socketFD);
        send_message_with_header(socketFD, ms, strlen(ms));




        while(SDL_PollEvent(&event)){
            char message[256]={0};
            switch(event.type){
                case SDL_QUIT:
                    running = 0; break;
                case SDL_WINDOWEVENT:
                    if (event.window.event == SDL_WINDOWEVENT_RESIZED){
                        window_w = event.window.data1;
                        window_h = event.window.data2;
                        printf("%d %d\n",window_w,window_h);
                    }
                    break;
                case SDL_MOUSEBUTTONDOWN: case SDL_MOUSEBUTTONUP:{
                    int w=event.button.x*host_w/window_w;
                    int h=event.button.y*host_h/window_h;
                    int mb_down=event.type==SDL_MOUSEBUTTONDOWN;
                    sprintf(message,"MC%c %d %d %d",mb_down?'D':'U',event.button.button,w,h);
                    send_message_with_header(socketFD,message,strlen(message));
                    break;
                }
                case SDL_MOUSEMOTION:{
                        int w=event.button.x*host_w/window_w;
                        int h=event.button.y*host_h/window_h;
                        sprintf(message,"MV- %d %d",w,h);
                        send_message_with_header(socketFD,message,strlen(message));
                    }
                    break;
                case SDL_MOUSEWHEEL: {
                    int w = event.wheel.mouseX * host_w / window_w;
                    int h = event.wheel.mouseY * host_h / window_h;
                    sprintf(message, "MS%C %d %d", event.wheel.preciseY < 0 ? 'U' : 'D', w,h);
                    //printf("MS:%s\n", message);
                    send_message_with_header(socketFD, message, strlen(message));
                    break;
                }
//                case SDL_FINGERMOTION:
//                    printf("%f %f\n",event.tfinger.dx,event.tfinger.dx);
//                    break;
                case SDL_KEYDOWN: case SDL_KEYUP:{
                    //so text keys genetate an instant keyup but modifiers like shift wait until you actually release the key
                    //printf("keyp\n");
                    int kc=event.key.keysym.sym;
                    int press=event.type==SDL_KEYDOWN;
                    //ascii control codes are prefixed with 0xff00 X11
                    switch (kc) {
                        //printable ascii characters are mapped normally ( note: glibc isprint crashes on special keys)
                        case ' '...'~': break;
                        case SDLK_BACKSPACE: case SDLK_TAB: case SDLK_RETURN: case SDLK_ESCAPE:
                            kc|=0xff00;
                            break;
                        case SDLK_LCTRL:kc=XK_Control_L;break;
                        case SDLK_RCTRL:kc=XK_Control_R;break;
                        case SDLK_LSHIFT:kc=XK_Shift_L;break;
                        case SDLK_RSHIFT:kc=XK_Shift_R;break;
                        case SDLK_LALT:kc=XK_Alt_L;break;
                        case SDLK_RALT:kc=XK_Alt_R;break;
                        case SDLK_LGUI:kc=XK_Meta_L;break;
                        case SDLK_RGUI:kc=XK_Meta_R;break;

                        case SDLK_RIGHT:kc=XK_Right;break;
                        case SDLK_LEFT:kc=XK_Left;break;
                        case SDLK_DOWN:kc=XK_Down;break;
                        case SDLK_UP:kc=XK_Up;break;
                        case SDLK_DELETE:kc=XK_Delete;break;


                        case SDLK_F1...SDLK_F12: kc=kc-SDLK_F1+XK_F1; break;

                        //case SDLK_CAPSLOCK:kc=XK_Caps_Lock;break;

                        default:printf("Unmapped key %d\n",kc);continue;
                    }
                    printf("%d\n",kc);
                    sprintf(message,"K%c %8d",press?'D':'U',kc);
                    send_message_with_header(socketFD,message,strlen(message));
                    break;
                default:
                    printf("type: %d\n",event.type);
                }
            }
        }
        uncork_socket(socketFD);
        int len;
        uint8_t *received = (uint8_t*)get_message_with_header(socketFD, &len);
        send_message_with_header(socketFD,"ACK",3);
        if(!received){
            err(1,"Connection failed");
        }
        //printf("Frame recieved size:%d\n",len);
        char* dec= qoi_decode(received,&host_w,&host_h,&host_c);
        for (int i = 0; i < host_w*host_h*host_c; ++i) {
            current_image[i]+=dec[i];
        }
        SDL_Surface* surface =
                SDL_CreateRGBSurfaceFrom(
                        current_image,       // dest_buffer from CopyTo
                        host_w,        // in pixels
                        host_h,       // in pixels
                        host_c*8,        // in bits, so should be dest_depth * 8
                        host_w*host_c,        // dest_row_span from CopyTo
                        0x00ff0000,        // RGBA masks, see docs
                        0x0000ff00,
                        0x000000ff,
                        0x00000000
                );
        //SDL_Surface * surface = SDL_CreateRGBSurfaceWithFormatFrom(dec,w,h,c*8,w*c,SDL_PIXELFORMAT_BGR24);
        //SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");
        SDL_Texture* text= SDL_CreateTextureFromSurface(renderer,surface);
        SDL_FreeSurface(surface);
        free(received);
        free(dec);
        SDL_RenderCopy(renderer,text,NULL,NULL);
        SDL_DestroyTexture(text);
        SDL_RenderPresent(renderer);
    }
}