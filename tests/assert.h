/* ASSERT macro — §0.1 of motion_streaming_tdd.md.

   Prints the file, line, and expression on failure, then returns a
   non-zero status from main.  No external test framework.
*/

#ifndef ASSERT_H
#define ASSERT_H

#include <stdio.h>
#include <stdlib.h>

#define ASSERT(cond) \
    do { \
        if (!(cond)) { \
            fprintf(stderr, "ASSERTION FAILED: %s:%d: %s\n", \
                    __FILE__, __LINE__, #cond); \
            return 1; \
        } \
    } while (0)

#endif /* ASSERT_H */