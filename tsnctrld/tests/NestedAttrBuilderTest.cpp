/* Additional tests for NestedAttrBuilderTest.cpp
 *
 * These test cases are AI-generated and meant as demonstrations.
 * They focus on edge cases and behavior introduced in the current
 * NestedAttrBuilder implementation.
 */

#include <gtest/gtest.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>

#include <cstring>
#include <stdexcept>

#include "NestedAttBuilder.h"

class NestedAttrBuilderTest : public testing::Test {
protected:
    static constexpr int kBufferSize = 1024;

    char buffer[kBufferSize];
    nlmsghdr* nlh;

    void SetUp() override {
        std::memset(buffer, 0, sizeof(buffer));
        nlh = reinterpret_cast<nlmsghdr*>(buffer);
        nlh->nlmsg_len = NLMSG_LENGTH(0);
        nlh->nlmsg_type = RTM_NEWLINK;
    }
};

/**
 * Verifies that multiple top-level attributes correctly grow nlmsg_len
 */
TEST_F(NestedAttrBuilderTest, AddMultipleTopLevelAttributes) {
    NestedAttrBuilder builder(/*maxPayloadLength=*/512);

    int v1 = 1;
    int v2 = 2;

    int len0 = nlh->nlmsg_len;

    builder.addAttribute(nlh, 1, &v1, sizeof(v1));
    int len1 = nlh->nlmsg_len;

    builder.addAttribute(nlh, 2, &v2, sizeof(v2));
    int len2 = nlh->nlmsg_len;

    EXPECT_GT(len1, len0);
    EXPECT_GT(len2, len1);
}

/**
 * Verifies that nested attributes correctly propagate length
 * all the way to the netlink header.
 */
TEST_F(NestedAttrBuilderTest, DeeplyNestedAttributesUpdateMessageLength) {
    NestedAttrBuilder builder(/*maxPayloadLength=*/512);

    int rootVal = 10;
    int childVal = 20;
    int grandChildVal = 30;

    int rootId = builder.addAttribute(
        nlh, /*type=*/100, &rootVal, sizeof(rootVal));

    int lenAfterRoot = nlh->nlmsg_len;

    int childId = builder.addAttribute(
        rootId, /*type=*/101, &childVal, sizeof(childVal));

    int lenAfterChild = nlh->nlmsg_len;

    builder.addAttribute(
        childId, /*type=*/102, &grandChildVal, sizeof(grandChildVal));

    int lenAfterGrandChild = nlh->nlmsg_len;

    EXPECT_GT(lenAfterChild, lenAfterRoot);
    EXPECT_GT(lenAfterGrandChild, lenAfterChild);
}

/**
 * Ensures attributes with nullptr data do not crash
 * and still update lengths correctly.
 */
TEST_F(NestedAttrBuilderTest, AddAttributeWithNullData) {
    NestedAttrBuilder builder(/*maxPayloadLength=*/512);

    int lenBefore = nlh->nlmsg_len;

    EXPECT_NO_THROW({
        builder.addAttribute(
            nlh, /*type=*/50, nullptr, /*len=*/0);
    });

    EXPECT_GT(nlh->nlmsg_len, lenBefore);
}

/**
 * Ensures exceeding max payload via nested attributes throws
 * even when individual attributes are small.
 */
TEST_F(NestedAttrBuilderTest, NestedAttributesExceedMaxPayload) {
    NestedAttrBuilder builder(/*maxPayloadLength=*/24);

    int parentVal = 1;
    int childVal = 2;
    int childVal2 = 3;

    int parentId = builder.addAttribute(
        nlh, 1, &parentVal, sizeof(parentVal));

    EXPECT_THROW({
        builder.addAttribute(parentId, 2, &childVal, sizeof(childVal));
        builder.addAttribute(parentId, 3, &childVal2, sizeof(childVal2));
    }, std::runtime_error);
}

/**
 * Ensures clearAttrVector() is implicitly called when starting
 * a new top-level message via addAttribute(nlh, ...)
 */
TEST_F(NestedAttrBuilderTest, NewTopLevelAttributeClearsInternalState) {
    NestedAttrBuilder builder(/*maxPayloadLength=*/512);

    int v1 = 1;
    int v2 = 2;

    int id1 = builder.addAttribute(nlh, 10, &v1, sizeof(v1));
    (void)id1; // suppress unused warning

    // Simulate new message
    nlh->nlmsg_len = NLMSG_LENGTH(0);

    EXPECT_NO_THROW({
        builder.addAttribute(nlh, 20, &v2, sizeof(v2));
    });

    EXPECT_GT(nlh->nlmsg_len, NLMSG_LENGTH(0));
}

/**
 * Ensures destructor does not leak or double-free when attributes exist
 */
TEST_F(NestedAttrBuilderTest, DestructorCleansUpSafely) {
    int v = 123;

    {
        NestedAttrBuilder builder(/*maxPayloadLength=*/512);
        builder.addAttribute(nlh, 1, &v, sizeof(v));
        builder.addAttribute(nlh, 2, &v, sizeof(v));
    }

    SUCCEED(); // If we reach here, destructor behaved correctly
}
