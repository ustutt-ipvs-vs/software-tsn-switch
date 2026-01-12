#ifndef NESTEDATTRBUILDER_H
#define NESTEDATTRBUILDER_H
#include <optional>
#include <vector>
#include <linux/rtnetlink.h>

class NestedAttrBuilder {
private:
    struct attrBuilderItem {
        rtattr* attr{};
        std::optional<int> parentID;
    };

    std::vector<attrBuilderItem*> attrs;
    const int maxPayloadLength;
    nlmsghdr* nlh{};

    void propagateToParent(const int parentID, const int childLen);
    int insertAttr(attrBuilderItem* attr, int type, const void* data, int len);
    void clearAttrVector();

public:
    NestedAttrBuilder(int maxPayloadLength);
    int addAttribute(int attrBuilderItemID, int type, const void* data, int len);
    int addAttribute(nlmsghdr* nlh, int  type, const void* data, int len);
    ~NestedAttrBuilder();
};

#endif
