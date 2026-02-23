#include <linux/rtnetlink.h>

#include <cstring>
#include <stdexcept>

#include "./include/NestedAttBuilder.h"
#include "./include/NetlinkSocket.h"

NestedAttrBuilder::NestedAttrBuilder(const int maxPayloadLength) : maxPayloadLength(maxPayloadLength) {
}

/**
 * @brief Add the new length of a given attribute
 * @param attrBuilderID ID of the attribute in the internal data structure
 * @param length The new length of the attribute
 */
void NestedAttrBuilder::addAttrLength(const int attrBuilderID, const int length) {
    attrBuilderItem* item = this->attrs.at(attrBuilderID);

    item->attr->rta_len += RTA_ALIGN(length);

    if (item->parentID.has_value()) {
        this->addAttrLength(item->parentID.value(), length);
    } else {
        const int nlh_len = NLMSG_ALIGN(this->nlh->nlmsg_len) + length;

        if (nlh_len > this->maxPayloadLength) {
            throw std::runtime_error("Message exceeds maximum length");
        }

        this->nlh->nlmsg_len = nlh_len;
    }
}

/**
 * @brief Add an attribute to the netlink message data structure
 * @param nlh Pointer to the netlink message header data
 * @param type The type of the attribute that will be added
 * @param data Pointer to the data of the attribute
 * @param len The length of the attribute to be added
 * @return Returns the ID of the added attribute in the internal data structure
 */
int NestedAttrBuilder::addAttribute(nlmsghdr* nlh, const int type, const void* data, const int len) {
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
int NestedAttrBuilder::addChildAttribute(const int attrBuilderParentID, const int type, const void* data,
                                         const int len) {
    attrBuilderItem* parent = this->attrs.at(attrBuilderParentID);
    auto* attr = (struct rtattr*)((char*)parent->attr + RTA_ALIGN(parent->attr->rta_len));
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
int NestedAttrBuilder::insertAttr(attrBuilderItem* item, const int type, const void* data, const int len) {
    item->attr->rta_len = RTA_LENGTH(len);
    item->attr->rta_type = type;
    this->attrs.emplace_back(item);
    int delta = RTA_ALIGN(item->attr->rta_len);

    if (data != nullptr) {
        std::memcpy(RTA_DATA(item->attr), data, len);
    }

    if (item->parentID.has_value()) {
        this->addAttrLength(item->parentID.value(), delta);
    } else {
        const int nlh_len = NLMSG_ALIGN(this->nlh->nlmsg_len) + delta;
        if (nlh_len > this->maxPayloadLength) {
            throw std::runtime_error("Message exceeds maximum length");
        }
        this->nlh->nlmsg_len = nlh_len;
    }

    return static_cast<int>(this->attrs.size() - 1);
}

/**
 * @brief Clear the internal data structure of all added attributes
 */
void NestedAttrBuilder::clearAttrVector() {
    for (attrBuilderItem* item : this->attrs) {
        std::free(item);
    }
    this->attrs.clear();
}

NestedAttrBuilder::~NestedAttrBuilder() {
    this->clearAttrVector();
}
