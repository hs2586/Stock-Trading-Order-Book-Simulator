#include <cstdio>
#include <cstdlib>
#include <cstring>

// A deliberately buggy order-entry handler for testing the analyzer
void handle(char* input) {
    char name[16];
    char buf[64];

    snprintf(name, sizeof(name), "%s", input); /* AUTOFIX: dest must be an array */
    snprintf(buf, sizeof(buf), "ORDER %s", name);
    printf("%s", buf);
    fgets(name, sizeof(name), stdin) /* AUTOFIX: dest must be an array */;
    system("echo order received");
    int r = rand();

    // strcpy(name, input);           <- this is a comment, must be ignored
    /* gets(name);
       system("ls");                  <- inside a block comment, ignore */
    printf("done %d\n", r);           // safe: literal format string
}
