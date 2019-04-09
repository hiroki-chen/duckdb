#pragma once
#import "node.hpp"

namespace duckdb {

    class Node256 : public Node{
    public:
        Node* child[256];

        Node256() : Node(NodeType::N256) {
            memset(child,0,sizeof(child));
        }
        Node *getChild(const uint8_t k) const ;
    };
}