#ifndef NESTEDATTRBUILDER_H
#define NESTEDATTRBUILDER_H
#include <linux/rtnetlink.h>

#include <optional>
#include <vector>

/**
 * @brief Helper to build netlink messages, used to add attributes to a message and resize everything appropriately.
 */
class NestedAttrBuilder {
   public:
    NestedAttrBuilder(int maxPayloadLength);
    int addChildAttribute(int attrBuilderParentID, int type, const void* data, int len);
    int addAttribute(nlmsghdr* nlh, int type, const void* data, int len);
    void addAttrLength(int attrBuilderID, int length);
    ~NestedAttrBuilder();

   private:
    struct attrBuilderItem {
        rtattr* attr{};
        std::optional<int> parentID;
    };
    std::vector<attrBuilderItem*> attrs;
    const int maxPayloadLength;
    nlmsghdr* nlh{};

    int insertAttr(attrBuilderItem* item, int type, const void* data, int len);
    void clearAttrVector();
};

#endif
