#ifndef NESTEDATTRBUILDER_H
#define NESTEDATTRBUILDER_H
#include <optional>
#include <vector>
#include <linux/rtnetlink.h>

class NestedAttrBuilder {
private:
    struct attr_builder_item {
        rtattr* attr{};
        std::optional<int> parent_id;
    }

    std::vector<attr_builder_item*> attrs;
    const int max_payload_length;
    nlmssghdr* nlh{};

    void add_attr_length(int attr_builder_id, int length);
    int insert_attr(attr_builder_item* attr, int type, const void* data, int len);
    void clear_attr_vector();

public:
    NestedAttrBuilder(int max_payload_length);
    int add_attribute(int attr_builder_parent_id, int type, const void* data, int len);
    int add_attribute(nlmssghdr* nlh, int  type, const void* data, int len);
    ~NestedAttrBuilder();
};

##endif
