/* test_list.cpp — main() and the registry. */

#include "test_list.h"
#include "assert.h"

#include <stddef.h>

static const int MAX_TESTS = 512;
static Test g_tests[MAX_TESTS];
static int g_count = 0;

int add_test(const char *name, int (*fn)(void)) {
    if (g_count < MAX_TESTS) {
        g_tests[g_count].name = name;
        g_tests[g_count].fn = fn;
    }
    ++g_count;
    return 1;
}

int main(void) {
    for (int i = 0; i < g_count && i < MAX_TESTS; ++i) {
        int rc = g_tests[i].fn();
        if (rc != 0) {
            fprintf(stderr, "FAILED TEST: %s\n", g_tests[i].name);
            return 1;
        }
    }
    if (g_count > MAX_TESTS) {
        fprintf(stderr, "registry overflow: %d > %d\n", g_count, MAX_TESTS);
        return 1;
    }
    printf("all %d tests passed\n", g_count);
    return 0;
}
