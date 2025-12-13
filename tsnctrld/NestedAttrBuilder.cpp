#include "NestedAttrBuilder.h"

#include <cstring>
#include <NetlinkSocket.h>
#include <stdexcept>

NestedAttrBuilder::NestedAttrBuilder(const int max_payload_length) : max_payload_length(max_payload_length) {}

void NestedAttrBuilder::add_attr_length(const int attr_builder_id, const int length) {
    attr_builder_item* item = this->attrs.at(attr_builder_id);

    item->attr->rta_len += RTA_ALIGN(length);

    if(item->parent_id.has_value()) {
        this->add_attr_length(item->parent_id.value(), length);
    } else {
        const int nlh_len = NLMSG_ALIGN(this->nlh->nlmsg_len) + item->attr->rta_len;

        if(nlh_len > this->max_payload_length) {
            throw std::runtime_error("Message exceeds maximum length");
        }

        this->nlh->nlmsg_len = nlh_len;
    }
}

int NestedAttrBuilder::add_attribute(nlmsghdr *nlh, const int type, const void *data, const int len) {
    this->clear_attr_vector();

    attr* attr = NLMSG_TAIL(nlh);
    this->nlh = nlh;
    return this->insert_attr(new attr_builder_item{.attr = attr}, type, data, len);
}

int NestedAttrBuilder::add_attribute(const int attr_builder_parent_id, const int type, const void *data, const int len) {
    attr_builder_item* parent = this->attrs.at(attr_builder_parent_id);
    attr* attr = (struct attr*)((char*)parent->attr + RTA_ALIGN(parent->attr->rta_len));
    return this->insert_attr(new attr_builder_item{.attr = attr, .parent_id = attr_builder_parent_id}, type, data, len);
}

int NestedAttrBuilder::insert_attr(attr_builder_item *item, const int type, const void *data, const int len) {
    this->attrs.emplace_back(item);
    const int id = this->attrs.size() - 1;

    this->add_attr_length(id, RTA_LENGTH(len));
    item->attr->rta_type = type;
    if(data != nullptr) std::memcpy(RTA_DATA(item->attr), data, len);

    return id;
}

void NestedAttrBuilder::clear_attr_vector() {
    for(attr_builder_item* item : this->attrs) {
        std::free(item);
    }
    this->attrs.clear();
}

NestedAttrBuilder::~NestedAttrBuilder() {
    this->clear_attr_vector();
}

