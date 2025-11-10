#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <zlib.h>

#define HEX_LINE_CHARS 32

#define min(a, b) a < b ? a : b
#define max(a, b) a > b ? a : b
#define PIXEL_NONE (pixel){.p={0, 0, 0, 0}}

// critical chunks
#define CHUNK_IHDR 0x80
#define CHUNK_PLTE 0x81
#define CHUNK_IDAT 0x82
#define CHUNK_IEND 0x83

// ancillary chunks
#define CHUNK_tRNS 0x84
#define CHUNK_sRGB 0x85
#define CHUNK_bkGD 0x86
#define CHUNK_tIME 0x87

#define COLTYPE_GRAYSCALE       0x00
#define COLTYPE_TRUECOLOR       0x02
#define COLTYPE_INDEXED         0x03
#define COLTYPE_GRAYSCALE_APLHA 0x04
#define COLTYPE_TRUECOLOR_APLHA 0x06

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
char* chunk_names[] = {"IHDR", "PLTE", "IDAT", "IEND", "tRNS", "sRGB", "bkGD", "tIME"};

char* label(unsigned char id) {
    if (id & 0b10000000)
        return chunk_names[id&0b00111111];
    return coltype_names[id&0b00111111];
}

/*
    converts base256 to uint32 big-endian mode
    note: will only care about first four bytes after the ptr
*/
unsigned int atoi_big(unsigned char* str) {
    return (str[0] << 24) + (str[1] << 16) + (str[2] << 8) + str[3];
}

/*
    converts uint32 to base256 big-endian mode
    note: result is allocated
*/
unsigned char* itoa_big(unsigned int num) {
    unsigned char* res = malloc(4);
    res[0] = num >> 24;
    res[1] = num >> 16;
    res[2] = num >> 8;
    res[3] = num;

    return res;
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

unsigned char get_bpp(unsigned char color_type, unsigned char bit_depth) {
    unsigned char res = bit_depth; // grayscale and indexed

    if (color_type & 0b010 && !(color_type & 0b001))
        res = 3 * bit_depth; // truecolor
    
    if (color_type & 0b100)
        res += bit_depth; // + alpha

    return res;
}

unsigned int get_crc(unsigned char* txt, unsigned int len) {
    return crc32(crc32(0, Z_NULL, 0), txt, len);
}

void append_chunk(unsigned char* file_data, unsigned int* file_data_len, char* chunk_name, unsigned char* chunk_data, unsigned int len) {
    unsigned char chunk[len+12];

    unsigned char* p1 = itoa_big(len);
    memcpy(chunk, p1, 4);
    memcpy(chunk + 4, chunk_name, 4);
    memcpy(chunk + 8, chunk_data, len);

    unsigned char* p2 = itoa_big(get_crc(chunk_data, len));
    memcpy(chunk + 8 + len, p2, 4);

    memcpy(file_data + *file_data_len, chunk, len + 12);

    *file_data_len += len + 12;

    free(p1);
    free(p2);
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

            if (!(img->color_type & 0b010)) {
                if (img->color_type & 0b100)
                    x.a = x.g;
                x.g = x.r;
                x.b = x.r;
            }

            if (img->color_type == COLTYPE_INDEXED)
                x = img->palette[x.r];

            img->pixels[j+y*img->width] = x;
        }

        y++;
    }
}

void parse_chunk(unsigned char* contents, unsigned char chunk_type, long long chunk_index, image* img) {
    if (chunk_index < 0)
        return;

    unsigned int len = atoi_big(contents + chunk_index);

    switch (chunk_type) {
        case CHUNK_IHDR:
            img->width = atoi_big(contents + chunk_index + 8);
            img->height = atoi_big(contents + chunk_index + 12);
            img->bit_depth = contents[chunk_index+16];
            img->color_type = contents[chunk_index+17];

            img->bpp = get_bpp(img->color_type, img->bit_depth);

            img->interlace_mode = contents[chunk_index+20];
            img->pixels = malloc(img->width * img->height * sizeof(pixel));

            break;
        case CHUNK_PLTE:
            img->palette = malloc(len / 3 * sizeof(pixel));

            for (int i = 0; i < len / 3; i++)
                img->palette[i] = (pixel){
                    .r=contents[chunk_index+i*3+8],
                    .g=contents[chunk_index+i*3+9],
                    .b=contents[chunk_index+i*3+10],
                    .a=255
                };

            break;
        // case CHUNK_tRNS:
        //     for (int i = 0; i < len; i++)
        //         img->palette[i].a = contents[chunk_index+i];
            
        //     break;
    }
}

image ap_image(unsigned int width, unsigned int height, unsigned char bit_depth, unsigned char color_type, bool interlace_mode) {
    return (image){
        .width=width, .height=height,
        .color_type=color_type,
        .bit_depth=bit_depth, .bpp=get_bpp(color_type, bit_depth),
        .interlace_mode=interlace_mode,
        .pixels=malloc(width * height * sizeof(pixel))
    };
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

    if (res.color_type == COLTYPE_INDEXED) {
        parse_chunk(image_buf, CHUNK_PLTE, chunk_index(image_buf, len, "PLTE", 0), &res);
        parse_chunk(image_buf, CHUNK_tRNS, chunk_index(image_buf, len, "tRNS", 0), &res);
    }

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

unsigned char* ap_save(image img, int* len) {
    unsigned char bytes_per_pixel = img.bpp >> 3;

    unsigned int avail_in = (img.width * bytes_per_pixel + 1) * img.height * bytes_per_pixel;

    unsigned char* res = calloc(33, 1);
    *len = 33;
    memcpy(res, "\x89\x50\x4e\x47\x0d\x0a\x1a\x0a\0\0\0\x0dIHDR", 16);

    unsigned char ihdr_chunk[17];
    memset(ihdr_chunk, 0, 17);

    unsigned char* p1 = itoa_big(img.width);
    unsigned char* p2 = itoa_big(img.height);
    memcpy(ihdr_chunk, p1, 4);
    memcpy(ihdr_chunk + 4, p2, 4);
    
    free(p1);
    free(p2);

    ihdr_chunk[8] = img.bit_depth;
    ihdr_chunk[9] = img.color_type;
    ihdr_chunk[12] = img.interlace_mode;

    unsigned char* p3 = itoa_big(get_crc(res + 12, 17));
    memcpy(ihdr_chunk + 13, p3, 4);

    free(p3);

    memcpy(res + 16, ihdr_chunk, 17);

    unsigned char pixel_data[avail_in];

    for (int j = 0; j < img.height; j++) {
        pixel_data[j*(img.width*bytes_per_pixel+1)] = '\0'; // no filtering for now
        for (int i = 0; i < img.width; i++) {
            for (int k = 0; k < bytes_per_pixel; k++)
                pixel_data[i*bytes_per_pixel+1+j*(img.width*bytes_per_pixel+1)+k] = img.pixels[i+j*img.width].p[k];
        }
    }

    unsigned int avail_out = compressBound(avail_in);
    unsigned char buf[avail_out];

    z_stream stream = {
        .zalloc=NULL,
        .zfree=NULL,
        .opaque=NULL,
        .avail_in=avail_in,
        .next_in=pixel_data,
        .avail_out=avail_out,
        .next_out=buf,
    };

    deflateInit(&stream, Z_DEFAULT_COMPRESSION);
    deflate(&stream, Z_FINISH);
    deflateEnd(&stream);

    unsigned char idat_chunk[stream.total_out+12];
    res = realloc(res, 33 + stream.total_out + 12 + 12);

    unsigned char* p4 = itoa_big(stream.total_out);

    memcpy(idat_chunk, p4, 4);
    memcpy(idat_chunk + 4, "IDAT", 4);
    memcpy(idat_chunk + 8, buf, stream.total_out);
    
    unsigned char* p5 = itoa_big(get_crc(idat_chunk + 4, stream.total_out + 4));
    memcpy(idat_chunk + stream.total_out + 8, p5, 4);

    memcpy(res + 33, idat_chunk, stream.total_out + 12);
    memcpy(res + 33 + stream.total_out + 12, "\0\0\0\0IEND\xae\x42\x60\x82", 12);

    // free(p4);
    // free(p5);

    *len = 33 + stream.total_out + 12 + 12;

    return res;
}

void* ap_free_img(image img) {
    if (img.palette)
        free(img.palette);
    
    free(img.pixels);
}