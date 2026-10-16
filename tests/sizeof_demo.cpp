#include <cstdio>

void f(char* p) {
    printf("inside f, sizeof(p) = %zu\n", sizeof(p));
}

int main() {
    char name[16];
    printf("in main, sizeof(name) = %zu\n", sizeof(name));
    f(name);
    return 0;
}