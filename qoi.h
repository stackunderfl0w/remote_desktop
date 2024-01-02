#pragma once
#include <stdint.h>
#include <stdlib.h>


#define QOI_OP_RGB      0b11111110
#define QOI_OP_RGBA     0b11111111
#define QOI_OP_INDEX    0b00
#define QOI_OP_DIFF     0b01
#define QOI_OP_LUMA     0b10
#define QOI_OP_RUN      0b11


struct qoi_header{
    char magic[4];      // magic bytes "qoif"
    uint32_t width;     // image width in pixels (BE)
    uint32_t height;    // image height in pixels (BE)
    uint8_t channels;   // 3 = RGB, 4 = RGBA
    uint8_t colorspace; // 0 = sRGB with linear alpha
    // 1 = all channels linear
} __attribute__((__packed__));

typedef struct __attribute__((__packed__)){
    union{
        struct{uint8_t r,g,b,a;};
        uint32_t raw;
    };

} pixel ;

static inline __attribute__((always_inline)) int qoi_hash(pixel p){
    return (p.r*3+p.g*5+p.b*7+p.a*11)%64;
}

void* qoi_encode(uint8_t* d, int width, int height, int channels, int* size){
    uint8_t* out=malloc(width*height*channels);
    *((struct qoi_header*)out)=(struct qoi_header){{"qoif"}, __bswap_constant_32(width), __bswap_constant_32(height),channels,0};
    uint8_t* o_idx=out+ sizeof(struct qoi_header);

    pixel recent[64]={0};
    pixel prev={{{0,0,0,255}}};
    pixel px=prev;

    int run=0;

    for (int i = 0; i < width*height; ++i) {
        px.r=*d++;
        px.g=*d++;
        px.b=*d++;
        if (channels == 4)
            px.a=*d++;
        if(px.raw==prev.raw){
            run++;
            if(run==62||i==width*height-1){
                *o_idx++=(QOI_OP_RUN<<6)|(run-1);
                run=0;
            }
        }
        else{
            if(run){
                *o_idx++=(QOI_OP_RUN<<6)|(run-1);
                run=0;
            }
            int index= qoi_hash(px);
            //if pixel exists in recent, save reference
            if(recent[index].raw == px.raw){
                *o_idx++=index;
            }
            else{
                recent[index]=px;
                if(px.a==prev.a){
                    int8_t dr=px.r-prev.r,dg=px.g-prev.g,db=px.b-prev.b;
                    uint8_t dr_g = dr-dg, db_g=db-dg;

                    if((uint8_t)(dr+2)<=3 && (uint8_t)(dg+2)<=3 && (uint8_t)(db+2)<=3){
                        *o_idx++=(QOI_OP_DIFF<<6)|(dr+2)<<4|(dg+2)<<2|(db+2);
                    }
                    else if((uint8_t)(dr_g+8)<=15 && (uint8_t)(dg+32)<=63 && (uint8_t)(db_g+8)<=15){
                        *o_idx++=(QOI_OP_LUMA<<6)|(dg+32);
                        *o_idx++=(dr_g+8)<<4|(db_g+8);
                    }
                    else{
                        *o_idx++=QOI_OP_RGB;
                        *o_idx++=px.r;
                        *o_idx++=px.g;
                        *o_idx++=px.b;
                    }
                } else{
                    *o_idx++=QOI_OP_RGBA;
                    *(pixel *) o_idx=px;
                    o_idx += 4;
                }
            }
            prev=px;
        }
    }
    //mark end stream;
    for (int i = 0; i < 7; ++i) {
        *o_idx++=0;
    }
    *o_idx++=1;
    *size=(o_idx - out);
    return out;
}
///todo investigate loop unrolling
void* qoi_encode_diff(uint8_t* out, uint8_t* d,uint8_t* o, int width, int height, int channels, int* size){
    *((struct qoi_header*)out)=(struct qoi_header){{"qoif"}, __bswap_constant_32(width), __bswap_constant_32(height),channels,0};
    uint8_t* o_idx=out+ sizeof(struct qoi_header);

    pixel recent[64]={0};
    pixel prev={{{0,0,0,255}}};
    pixel px=prev;

    int run=0;

    for (int i = 0; i < width*height; ++i) {
        px.r=*d++-*o++;
        px.g=*d++-*o++;
        px.b=*d++-*o++;
        if (channels == 4)
            px.a=*d++-*o++;
        if(px.raw==prev.raw){
            run++;
            if(run==62||i==width*height-1){
                *o_idx++=(QOI_OP_RUN<<6)|(run-1);
                run=0;
            }
        }
        else{
            if(run){
                *o_idx++=(QOI_OP_RUN<<6)|(run-1);
                run=0;
            }
            int index= qoi_hash(px);
            //if pixel exists in recent, save reference
            if(recent[index].raw == px.raw){
                *o_idx++=index;
            }
            else{
                recent[index]=px;
                if(px.a==prev.a){
                    int8_t dr=px.r-prev.r,dg=px.g-prev.g,db=px.b-prev.b;
                    uint8_t dr_g = dr-dg, db_g=db-dg;

                    if((uint8_t)(dr+2)<=3 && (uint8_t)(dg+2)<=3 && (uint8_t)(db+2)<=3){
                        *o_idx++=(QOI_OP_DIFF<<6)|(dr+2)<<4|(dg+2)<<2|(db+2);
                    }
                    else if((uint8_t)(dr_g+8)<=15 && (uint8_t)(dg+32)<=63 && (uint8_t)(db_g+8)<=15){
                        *o_idx++=(QOI_OP_LUMA<<6)|(dg+32);
                        *o_idx++=(dr_g+8)<<4|(db_g+8);
                    }
                    else{
                        *o_idx++=QOI_OP_RGB;
                        *o_idx++=px.r;
                        *o_idx++=px.g;
                        *o_idx++=px.b;
                    }
                } else{
                    *o_idx++=QOI_OP_RGBA;
                    *(pixel *) o_idx=px;
                    o_idx += 4;
                }
            }
            prev=px;
        }
    }
    //mark end stream;
    for (int i = 0; i < 7; ++i) {
        *o_idx++=0;
    }
    *o_idx++=1;
    *size=(o_idx - out);
    return out;
}

void* qoi_decode(uint8_t* data, int* width, int* height, int* channels){
    struct qoi_header* qh= (struct qoi_header *) data;
    *width= __bswap_constant_32(qh->width);
    *height= __bswap_constant_32(qh->height);
    *channels=qh->channels;

    int size=(*width) * (*height);

    uint8_t * final_bitmap= malloc(size*qh->channels);
    uint8_t * img_p=final_bitmap;

    uint8_t* d=data+sizeof (struct qoi_header);

    pixel recent[64]={0};
    pixel px={{{0, 0, 0, 255}}};
    int run=0;
    for (int i=0;i<size;i++) {
        if(run){
            run--;
        }
        else {
            uint8_t chunk = *d++;
            if (chunk == QOI_OP_RGB) {
                px = (pixel) {{{d[0], d[1], d[2], px.a}}};
                d += 3;
            } else if (chunk == QOI_OP_RGBA) {
                px = *(pixel *) d;
                d += 4;
            } else if (chunk >> 6 == QOI_OP_INDEX) {
                px = recent[chunk];
            } else if (chunk >> 6 == QOI_OP_DIFF) {
                px.r += ((chunk >> 4) & 0x3) - 2;
                px.g += ((chunk >> 2) & 0x3) - 2;
                px.b += (chunk & 0x3) - 2;
            } else if (chunk >> 6 == QOI_OP_LUMA) {
                uint8_t dg = (chunk & 0x3f) - 32;
                uint8_t b2 = *d++;
                px.r += ((b2 >> 4) & 0xf) - 8 + dg;
                px.g += dg;
                px.b += (b2 & 0xf) - 8 + dg;
            } else if (chunk >> 6 == QOI_OP_RUN) {
                run=chunk&0x3f;
            }
            recent[qoi_hash(px)] = px;
        }
        *img_p++ = px.r;
        *img_p++ = px.g;
        *img_p++ = px.b;
        if (qh->channels == 4)
            *img_p++ = px.a;
    }

    //stbi_write_png("test.png", qh->width, qh->height, qh->channels, final_bitmap, qh->width*qh->channels);
    //printf("%d\n",final_bitmap[0]);
    //free(final_bitmap);
    return final_bitmap;
}

void* qoi_decode_diff(uint8_t* final_bitmap, uint8_t* data, int* width, int* height, int* channels){
    struct qoi_header* qh= (struct qoi_header *) data;
    *width= __bswap_constant_32(qh->width);
    *height= __bswap_constant_32(qh->height);
    *channels=qh->channels;

    int size=(*width) * (*height);

    uint8_t * img_p=final_bitmap;

    uint8_t* d=data+sizeof (struct qoi_header);

    pixel recent[64]={0};
    pixel px={{{0, 0, 0, 255}}};
    int run=0;
    for (int i=0;i<size;i++) {
        if(run){
            run--;
        }
        else {
            uint8_t chunk = *d++;
            if (chunk == QOI_OP_RGB) {
                px = (pixel) {{{d[0], d[1], d[2], px.a}}};
                d += 3;
            } else if (chunk == QOI_OP_RGBA) {
                px = *(pixel *) d;
                d += 4;
            } else if (chunk >> 6 == QOI_OP_INDEX) {
                px = recent[chunk];
            } else if (chunk >> 6 == QOI_OP_DIFF) {
                px.r += ((chunk >> 4) & 0x3) - 2;
                px.g += ((chunk >> 2) & 0x3) - 2;
                px.b += (chunk & 0x3) - 2;
            } else if (chunk >> 6 == QOI_OP_LUMA) {
                uint8_t dg = (chunk & 0x3f) - 32;
                uint8_t b2 = *d++;
                px.r += ((b2 >> 4) & 0xf) - 8 + dg;
                px.g += dg;
                px.b += (b2 & 0xf) - 8 + dg;
            } else if (chunk >> 6 == QOI_OP_RUN) {
                run=chunk&0x3f;
            }
            recent[qoi_hash(px)] = px;
        }
        *img_p++ += px.r;
        *img_p++ += px.g;
        *img_p++ += px.b;
        if (qh->channels == 4)
            *img_p++ += px.a;
    }
    return final_bitmap;
}