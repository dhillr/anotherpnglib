#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

typedef struct {
    char* data;
    unsigned int len;
} compression_out;

compression_out lz77(char* data, unsigned int len) {
    char* sliding_window = data;
    int window_len = 1;

    char** dict = malloc(sizeof(char*));
    int dict_len = 0;

    for (int i = 0; i < len; i++) {
        bool is_new_word = true;
        for (int j = 0; j < dict_len; j++) {
            
        }
    }

    fre(dict);

    return (compression_out){NULL, 0};
}