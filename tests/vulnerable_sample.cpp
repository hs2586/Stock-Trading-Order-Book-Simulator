#include <cstdio>
#include <cstdlib>
#include <cstring>

// A deliberately buggy order-entry handler for testing the analyzer
void handle(char* input) {
    char name[16];
    char buf[64];

    strcpy(name, input);
    sprintf(buf, "ORDER %s", name);
    printf(buf);
    gets(name);
    system("echo order received");
    int r = rand();

    // strcpy(name, input);           <- this is a comment, must be ignored
    /* gets(name);
       system("ls");                  <- inside a block comment, ignore */
    printf("done %d\n", r);           // safe: literal format string
}