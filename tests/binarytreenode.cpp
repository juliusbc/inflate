#include <iostream>
#include <vector>
#include <string>
#include <sstream>
#include <stdexcept>
#include <functional>
#include "binarytreenode.h"

// Serialize a general binary tree with preorder traversal
template <typename treetype>
std::string serialize(const std::string delimiter, treetype* root,
        int treetype::*value = &treetype::value, 
        treetype* treetype::*left = &treetype::left,
        treetype* treetype::*right = &treetype::right) {
    std::string data = "";
    std::vector<const treetype*> path(1, root);

    // preorder traversal with a stack
    while(!path.empty()) {
        const treetype* curr = path.back();
        path.pop_back();
        if (curr == nullptr) {
            data += delimiter + " ";
        }
        else {
            data += std::to_string(curr->*value) + " ";
            path.push_back(curr->*right);
            path.push_back(curr->*left);
        }
    }
    return data;
}


// Deserialize data into a binary tree
template <typename treetype>
treetype* deserialize(
        const std::string data, const std::string delimiter) {
    typedef std::reference_wrapper<treetype*> BTNodeRef;
    treetype* root;
    std::vector<BTNodeRef> openpoints(1, static_cast<BTNodeRef>(root));

    std::stringstream datain(data);
    std::string symbol;
    while(datain >> symbol) {
        if (openpoints.empty()) {
            throw std::invalid_argument("Corrupted data: " + data);
        }
        auto& curr = openpoints.back().get();
        openpoints.pop_back();
        if (symbol != delimiter) {
            int value = std::stoi(symbol, nullptr);
            curr = new treetype(value);
            openpoints.push_back(static_cast<BTNodeRef>(curr->right));
            openpoints.push_back(static_cast<BTNodeRef>(curr->left));
        }
    }
    return root;
}
    
