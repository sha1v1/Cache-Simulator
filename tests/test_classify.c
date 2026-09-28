#include "../src/unity/unity.h"
#include "../include/classify.h"
#include <stdlib.h>

void setUp(void) {}
void tearDown(void) {}

void test_seen_set_grows_without_losing_blocks(void) {
    classifier_t classifier;
    TEST_ASSERT_EQUAL(0, classifier_init(&classifier, 8));

    miss_kind_t kind;
    for(unsigned long block = 0; block < 800; block++) {
        TEST_ASSERT_EQUAL(0, classifier_access(&classifier, block, true, &kind));
        TEST_ASSERT_EQUAL(MISS_COMPULSORY, kind);
    }

    TEST_ASSERT_EQUAL_UINT64(800, classifier.seen_count);
    TEST_ASSERT_EQUAL_UINT64(2048, classifier.seen_capacity);

    /* A block inserted before the growth is still known afterwards. */
    TEST_ASSERT_EQUAL(0, classifier_access(&classifier, 0, true, &kind));
    TEST_ASSERT_NOT_EQUAL(MISS_COMPULSORY, kind);
    TEST_ASSERT_EQUAL_UINT64(800, classifier.seen_count);

    classifier_free(&classifier);
}

void test_full_seen_set_fails_instead_of_probing_forever(void) {
    classifier_t classifier = {0};
    classifier.seen = malloc(sizeof(*classifier.seen));
    classifier.ways = malloc(sizeof(*classifier.ways));
    classifier.used = malloc(sizeof(*classifier.used));
    TEST_ASSERT_NOT_NULL(classifier.seen);
    TEST_ASSERT_NOT_NULL(classifier.ways);
    TEST_ASSERT_NOT_NULL(classifier.used);

    classifier.seen[0] = 0;
    classifier.seen_capacity = 1;
    classifier.seen_count = 1;
    classifier.ways[0] = ~0UL;
    classifier.used[0] = 0;
    classifier.way_count = 1;

    miss_kind_t kind = MISS_CAPACITY;
    TEST_ASSERT_EQUAL(-1, classifier_access(&classifier, 1, true, &kind));
    TEST_ASSERT_EQUAL_UINT64(1, classifier.seen_count);
    TEST_ASSERT_EQUAL_UINT64(0, classifier.clock);

    classifier_free(&classifier);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_seen_set_grows_without_losing_blocks);
    RUN_TEST(test_full_seen_set_fails_instead_of_probing_forever);
    return UNITY_END();
}
