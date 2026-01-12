#include "NestedAttBuilder.h"
#include <cstring>
#include <NetlinkSocket.h>
#include <linux/rtnetlink.h>
#include <stdexcept>

NestedAttrBuilder::NestedAttrBuilder(const int maxPayloadLength) : maxPayloadLength(maxPayloadLength) {}

/**
 * @brief Add the new length of a given attribute
 * @param attrBuilderID ID of the attribute in the internal data structure
 * @param length The new length of the attribute
 */
void NestedAttrBuilder::propagateToParent(const int parentID, const int childLen) {
    auto *parent = attrs.at(parentID);
    parent->attr->rta_len += childLen;

    if (parent->parentID.has_value()) {
        propagateToParent(parent->parentID.value(), childLen);
    }
}

/**
 * @brief Add an attribute to the neadlink message data structure
 * @param nlh Pointer to the netlink message header data
 * @param type The type of the attribute that will be added
 * @param data Pointer to the data of the attribute
 * @param len The length of the attribute to be added
 * @return Returns the ID of the added attribute in the internal data structure
 */
int NestedAttrBuilder::addAttribute(nlmsghdr *nlh, const int type, const void *data, const int len) {
    this->clearAttrVector();

    rtattr* attr = NLMSG_TAIL(nlh);
    this->nlh = nlh;
    return this->insertAttr(new attrBuilderItem{.attr = attr}, type, data, len);
}

/**
 * @brief Add a child attribute to the parent attribute provided through the ID
 * @param attrBuilderParentID ID of the parent attribute
 * @param type The type of the attribute that will be added
 * @param data Pointer to the data of the attribute
 * @param len The length of the attribute to be added
 * @return 
 */
int NestedAttrBuilder::addAttribute(const int attrBuilderParentID, const int type, const void *data, const int len) {
    attrBuilderItem* parent = this->attrs.at(attrBuilderParentID);
    rtattr* attr = (struct rtattr*)((char*)parent->attr + RTA_ALIGN(parent->attr->rta_len));
    return this->insertAttr(new attrBuilderItem{.attr = attr, .parentID = attrBuilderParentID}, type, data, len);
}

/**
 * @brief Insert the attribute to the internal data structure
 * @param item A pointer to the item
 * @param type The type of the attribute to be inserted
 * @param data A pointer to the attribute that will be added
 * @param len The length of the attribute to be added
 * @return The ID of the attribute in the internal data structure
 */
int NestedAttrBuilder::insertAttr(attrBuilderItem *item, const int type, const void *data, const int len) {
    this->attrs.emplace_back(item);
    const int id = this->attrs.size() - 1;

    item->attr->rta_type = type;
    item->attr->rta_len = RTA_LENGTH(len);

    if(data != nullptr) std::memcpy(RTA_DATA(item->attr), data, len);

    if (item->parentID.has_value()) {
        propagateToParent(item->parentID.value(), RTA_ALIGN(item->attr->rta_len));
    } else {
        nlh->nlmsg_len = NLMSG_ALIGN(nlh->nlmsg_len) + RTA_ALIGN(item->attr->rta_len);
    }

    return id;
}

/**
 * @brief Clear the internal data structure of all added attributes
 */
void NestedAttrBuilder::clearAttrVector() {
    for(attrBuilderItem* item : this->attrs) {
        std::free(item);
    }
    this->attrs.clear();
}

NestedAttrBuilder::~NestedAttrBuilder() {
    this->clearAttrVector();
}

