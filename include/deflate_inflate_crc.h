#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

typedef struct {
    char* data;
    unsigned int len;
} compression_out;

compression_out lz77(char* data, unsigned int len) {
    char* sliding_window = malloc(1);

    int chars_len = 0;

    for (int i = 0; i < len; i++) {
        
    }

    free(sliding_window);

    return (compression_out){NULL, 0};
}