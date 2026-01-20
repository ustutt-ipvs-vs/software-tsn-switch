/* Tests for NestedAttrBuilderTest.cpp
 *
 * These test cases have been AI generated and should only serve as a
 * demonstration. Feel free to discard them when they are no longer useful,
 * ideally replacing them with better test cases of course.
 */

#include <gtest/gtest.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>

#include <cstring>

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

TEST_F(NestedAttrBuilderTest, AddSingleTopLevelAttribute) {
    NestedAttrBuilder builder(/*maxPayloadLength=*/512);

    int value = 42;
    EXPECT_NO_THROW({
        builder.addAttribute(nlh, /*type=*/1, &value, sizeof(value));
    });

    EXPECT_GT(nlh->nlmsg_len, NLMSG_LENGTH(0));
}

TEST_F(NestedAttrBuilderTest, AddNestedAttribute) {
    NestedAttrBuilder builder(/*maxPayloadLength=*/512);

    int parentValue = 1;
    int childValue = 2;

    int parentId = builder.addAttribute(
        nlh, /*type=*/10, &parentValue, sizeof(parentValue));

    int lenAfterParent = nlh->nlmsg_len;

    EXPECT_NO_THROW({
        builder.addAttribute(
            parentId, /*type=*/11, &childValue, sizeof(childValue));
    });

    EXPECT_GT(nlh->nlmsg_len, lenAfterParent);
}

TEST_F(NestedAttrBuilderTest, ThrowsWhenMaxPayloadExceeded) {
    // Intentionally small limit
    NestedAttrBuilder builder(/*maxPayloadLength=*/32);

    int largeData[16]; // ~64 bytes
    std::memset(largeData, 0, sizeof(largeData));

    EXPECT_THROW({
        builder.addAttribute(
            nlh, /*type=*/1, largeData, sizeof(largeData));
    }, std::runtime_error);
}

TEST_F(NestedAttrBuilderTest, ClearAttributesOnNewMessage) {
    NestedAttrBuilder builder(/*maxPayloadLength=*/512);

    int value = 5;
    builder.addAttribute(nlh, 1, &value, sizeof(value));

    // Reset message header to simulate a new message
    nlh->nlmsg_len = NLMSG_LENGTH(0);

    EXPECT_NO_THROW({
        builder.addAttribute(nlh, 2, &value, sizeof(value));
    });

    EXPECT_GT(nlh->nlmsg_len, NLMSG_LENGTH(0));
}
