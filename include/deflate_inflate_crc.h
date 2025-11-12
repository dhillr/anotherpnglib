#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

typedef struct {
    char* data;
    unsigned int len;
} compression_out;

compression_out lz77(char* data, unsigned int len) {
    char chars[256];
    int indices[256];

    int chars_len = 0;

    for (int i = 0; i < len; i++) {
        bool is_new_char = true;
        for (int j = 0; j < chars_len; j++) {
            if (data[i] == chars[j]) {
                is_new_char = false;
                break;
            }
        }

        if (is_new_char) {
            chars[chars_len] = data[i];
            indices[chars_len] = i; 
            chars_len++;
        }
    }

    for (int i = 0; i < chars_len; i++)
        printf("%c - %d\n", chars[i], indices[i]);

    return (compression_out){NULL, 0};
}