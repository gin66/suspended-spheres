/* test_list.h — the tiny registration point for every test file.
 *
 * Each phase keeps its own .cpp under tests/ and registers its tests via
 * static initialisers; test_list.cpp holds main() and runs them in
 * registration order.  A failing ASSERT returns non-zero and main stops
 * there, matching §0.1 of motion_streaming_tdd.md.
 */

#ifndef TEST_LIST_H
#define TEST_LIST_H

struct Test {
    const char *name;
    int (*fn)(void);
};

/* Register one test.  Returns 1 so it can be called from a static
 * initialiser.  Name is printed on failure. */
int add_test(const char *name, int (*fn)(void));

#endif /* TEST_LIST_H */
