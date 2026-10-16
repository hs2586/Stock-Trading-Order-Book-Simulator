#include <cstring>

void f(char* p) {
    snprintf(p, sizeof(p), "%s", "hello world"); /* AUTOFIX: dest must be an array */
}
