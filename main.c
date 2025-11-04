#include <anotherpnglib.h>
#include <math.h>

void print_image(image img) {
    for (int j = 0; j < img.height; j += 2) {
        for (int i = 0; i < img.width; i++) {
            pixel upper = img.pixels[i+j*img.width];
            pixel lower = img.pixels[i+(j+1)*img.width];

            if (upper.a > 0) {
                if (lower.a > 0) {
                    printf("\033[38;2;%d;%d;%dm\033[48;2;%d;%d;%dm\xdf\033[0m", upper.r, upper.g, upper.b, lower.r, lower.g, lower.b);
                } else {
                    printf("\033[38;2;%d;%d;%dm\xdf\033[0m", upper.r, upper.g, upper.b);
                }
            } else {
                if (lower.a > 0) {
                    printf("\033[38;2;%d;%d;%dm\xdc\033[0m", lower.r, lower.g, lower.b);
                } else {
                    printf(" ");
                }
            }
        }

        printf("\n");
    }
}

int main() {
    image img = ap_load("images/plotworld5logo.png");

    printf("dimensions: %ux%u\nbit depth: %d\ncolor type: %s\ninterlacing: %s\n", img.width, img.height, img.bit_depth, coltype_names[img.color_type], img.interlace_mode ? "Adam7" : "none");
    print_image(img);

    free(img.pixels);

    return 0;
}