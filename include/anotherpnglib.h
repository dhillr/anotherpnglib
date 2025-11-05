#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <zlib.h>

#define HEX_LINE_CHARS 32

#define min(a, b) a < b ? a : b
#define max(a, b) a > b ? a : b
#define PIXEL_NONE (pixel){.p={0, 0, 0, 0}}

#define CHUNK_IHDR 0
#define CHUNK_PLTE 1
#define CHUNK_IDAT 2
#define CHUNK_IEND 3

#define COLTYPE_GRAYSCALE       0
#define COLTYPE_TRUECOLOR       2
#define COLTYPE_INDEXED         3
#define COLTYPE_GRAYSCALE_APLHA 4
#define COLTYPE_TRUECOLOR_APLHA 6

typedef union {
    struct { unsigned char r, g, b, a; };
    unsigned char p[4];
} pixel;

typedef union {
    struct { unsigned short r, g, b, a; };
    unsigned short p[4];
} pixel16;
typedef struct {
    unsigned int width, height;
    unsigned char bit_depth, bpp, color_type;
    bool interlace_mode;
    pixel* palette;
    pixel* pixels;
} image;

char* coltype_names[] = {"GRAYSCALE", "", "TRUECOLOR", "INDEXED", "GRAYSCALE_ALPHA", "", "TRUECOLOR_ALPHA"};

/*
    converts base256 to uint32 big-endian mode
    note: will only care about first four bytes after the ptr
*/
unsigned int atoi_big(unsigned char* str) {
    return (str[0] << 24) + (str[1] << 16) + (str[2] << 8) + str[3];
}

void print_hex(unsigned char* hex, size_t len) {
    for (int j = 0; j < len; j += HEX_LINE_CHARS) {
        if (j > len)
            break;

        for (int i = 0; i < HEX_LINE_CHARS; i++) {
            printf("%02x ", hex[j+i]);
            if (i + j >= len) {
                for (int k = i + j; k < j + HEX_LINE_CHARS - 1; k++) {
                    printf("   ");
                }
                break;
            }
        }

        for (int i = 0; i < HEX_LINE_CHARS; i++) {
            if (i + j >= len) {
                putchar('.');
            } else {
                if (hex[j+i] < 32)
                    putchar('.');
                else
                    putchar(hex[j+i]);
            }
        }

        printf("\n");
    }
}

void pixel_blend(pixel* a, pixel b) {
    a->r += b.r;
    a->g += b.g;
    a->b += b.b;
    a->a += b.a;
}

void pixel_difference(pixel* a, pixel b) {
    a->r -= b.r;
    a->g -= b.g;
    a->b -= b.b;
    a->a -= b.a;
}

void filter_pixel(pixel* x, pixel a, pixel b, pixel c, unsigned char bytes_per_pixel, int filter) {
    if (filter == 1)
        pixel_blend(x, a);

    if (filter == 2)
        pixel_blend(x, b);

    if (filter == 3) {
        x->r += (a.r + b.r) >> 1;
        x->g += (a.g + b.g) >> 1;
        x->b += (a.b + b.b) >> 1;
        x->a += (a.a + b.a) >> 1;
    }

    if (filter == 4) {
        for (int k = 0; k < bytes_per_pixel; k++) {
            unsigned char byte_a = a.p[k];
            unsigned char byte_b = b.p[k];
            unsigned char byte_c = c.p[k];

            int p = byte_a + byte_b - byte_c;

            int pa = abs(p - byte_a);
            int pb = abs(p - byte_b);
            int pc = abs(p - byte_c);

            if (pa <= pb && pa <= pc)
                x->p[k] += byte_a;
            else if (pb <= pc)
                x->p[k] += byte_b;
            else
                x->p[k] += byte_c;
        }
    }
}

long long chunk_index(char* contents, size_t len_contents, char* chunk_name, size_t start) {
    long long i = start;
    char chunk[5];
    memcpy(chunk, contents + i, 4);

    while (strcmp(chunk, chunk_name) != 0) {
        if (i > len_contents)
            return -1;

        i++;
        memcpy(chunk, contents + i, 4);
    }

    return i - 4;
}

void parse_idat(image* img, unsigned char* compressed_img_data, size_t len) {
    unsigned char bytes_per_pixel = img->bpp >> 3;
    unsigned char buf[(img->width+1)*img->height*bytes_per_pixel];

    z_stream stream = {
        .zalloc=NULL,
        .zfree=NULL,
        .opaque=NULL,
        .avail_in=len,
        .next_in=compressed_img_data,
        .avail_out=(img->width + 1) * img->height * bytes_per_pixel,
        .next_out=buf,
    };
    
    inflateInit(&stream);
    inflate(&stream, Z_NO_FLUSH);
    inflateEnd(&stream);
    
    int y = 0;
    for (int i = 0; i < stream.total_out; i += img->width * bytes_per_pixel + 1) {
        int filter = buf[i];

        // printf("%d - \n", filter);

        for (int j = 0; j < img->width; j++) {
            pixel x = {
                .r=buf[i+1+j*bytes_per_pixel],
                .g=buf[i+2+j*bytes_per_pixel],
                .b=buf[i+3+j*bytes_per_pixel],
                .a=255
            };

            if (bytes_per_pixel > 3)
                x.a = buf[i+4+j*bytes_per_pixel];

            pixel a = PIXEL_NONE;
            pixel b = PIXEL_NONE;
            pixel c = PIXEL_NONE;
            
            if (j > 0) {
                a = img->pixels[j-1+y*img->width];
                if (y > 0)
                    c = img->pixels[j-1+(y-1)*img->width];
            }

            if (y > 0)
                b = img->pixels[j+(y-1)*img->width];

            filter_pixel(&x, a, b, c, bytes_per_pixel, filter);

            if (img->color_type == COLTYPE_INDEXED)
                x = img->palette[x.r];

            img->pixels[j+y*img->width] = x;
        }

        y++;
    }
}

void parse_chunk(unsigned char* contents, unsigned char chunk_type, long long chunk_index, image* img) {
    switch (chunk_type) {
        case CHUNK_IHDR:
            img->width = atoi_big(contents + chunk_index + 8);
            img->height = atoi_big(contents + chunk_index + 12);
            img->bit_depth = contents[chunk_index+16];
            img->color_type = contents[chunk_index+17];

            img->bpp = img->bit_depth; // grayscale
            if (img->color_type & 0b010)
                img->bpp = 3 * img->bit_depth; // truecolor
            
            if (img->color_type & 0b001)
                img->bpp = img->bit_depth; // indexed
            
            if (img->color_type & 0b100)
                img->bpp += img->bit_depth; // + alpha

            img->interlace_mode = contents[chunk_index+20];
            img->pixels = malloc(img->width * img->height * sizeof(pixel));

            break;
        case CHUNK_PLTE:
            unsigned int len = atoi_big(contents + chunk_index);

            img->palette = malloc(len / 3 * sizeof(pixel));

            for (int i = 0; i < len / 3; i++) {
                img->palette[i] = (pixel){
                    .r=contents[chunk_index+i*3+8],
                    .g=contents[chunk_index+i*3+9],
                    .b=contents[chunk_index+i*3+10],
                    .a=255
                };
            }

            break;
    }
}

image ap_load(char* filepath) {
    FILE* f = fopen(filepath, "rb");
    image res;

    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);

    unsigned char image_buf[len+1];
    fread(image_buf, 1, len, f);

    parse_chunk(image_buf, CHUNK_IHDR, chunk_index(image_buf, len, "IHDR", 0), &res);
    parse_chunk(image_buf, CHUNK_PLTE, chunk_index(image_buf, len, "PLTE", 0), &res);

    long long chunk_i = 0;
    unsigned char* img_data = malloc(1);
    size_t img_data_len = 0;

    while ((chunk_i = chunk_index(image_buf, len, "IDAT", chunk_i + 8)) >= 0) {
        img_data = realloc(img_data, img_data_len + atoi_big(image_buf + chunk_i));
        memcpy(img_data + img_data_len, image_buf + chunk_i + 8, atoi_big(image_buf + chunk_i));
        img_data_len += atoi_big(image_buf + chunk_i);
    }

    parse_idat(&res, img_data, img_data_len);

    fclose(f);
    return res;
}

unsigned char* ap_save(image img) {
    // ...
}