#include <stdio.h>

int main(int argc, char *argv[]) {
    printf("Hello, World!\n");
    char str[100];
    scanf("%99s", str);
    printf("You entered: %s\n", str);
    return 0;
}