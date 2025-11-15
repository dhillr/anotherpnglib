#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

typedef struct {
    char* data;
    unsigned int len;
} compression_out;

compression_out lz77(char* data, unsigned int len) {
    int i = 0;
    int window_len = 1;

    char** dict = malloc(sizeof(char*));
    int dict_len = 0;

    while (i < len) {
        bool is_new_word = true;

        char* sliding_window_str = calloc(window_len + 1, 1);
        memcpy(sliding_window_str, data + i, window_len);

        for (int j = 0; j < dict_len; j++) {
            if (strcmp(sliding_window_str, dict[j]) == 0)
                is_new_word = false;
        }

        while (!is_new_word && i + window_len <= len) {
            is_new_word = true;
            for (int j = 0; j < dict_len; j++) {
                if (strcmp(sliding_window_str, dict[j]) == 0)
                    is_new_word = false;
            }

            if (!is_new_word) {
                window_len++;
                sliding_window_str = realloc(sliding_window_str, window_len + 1);
                memcpy(sliding_window_str, data + i, window_len);
                sliding_window_str[window_len] = '\0';
            }
        }
        
        if (is_new_word) {
            dict_len++;
            dict = realloc(dict, dict_len * sizeof(char*));
            dict[dict_len-1] = sliding_window_str;
        }

        printf("%s\n", sliding_window_str);

        i += window_len;
        window_len = 1;
    }

    for (int i = 0; i < dict_len; i++) {
        // printf("%s\n", dict[i]);
        free(dict[i]);
    }

    free(dict);

    return (compression_out){NULL, 0};
}