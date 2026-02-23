#include "merkle.h"
#include "crypto.h"

namespace Bucks {

MerkleTree::MerkleTree(const std::vector<Hash256>& leaves) {
    if (!leaves.empty()) {
        root = buildTree(leaves);
    }
}

Hash256 MerkleTree::buildTree(std::vector<Hash256> level) {
    while (level.size() > 1) {
        std::vector<Hash256> next;

        for (size_t i = 0; i < level.size(); i += 2) {
            const Hash256& left = level[i];
            const Hash256& right = (i + 1 < level.size())
                ? level[i + 1]
                : level[i]; // Bitcoin rule: duplicate last

            next.push_back(hashPair(left, right));
        }

        level = std::move(next);
    }

    return level[0];
}

Hash256 MerkleTree::hashPair(const Hash256& left,
                             const Hash256& right) {
    std::vector<uint8_t> data;
    data.reserve(64);

    data.insert(data.end(), left.begin(), left.end());
    data.insert(data.end(), right.begin(), right.end());

    return Bucks::doubleSha256(data);
}

const Hash256& MerkleTree::getRoot() const {
    return root;
}

} // namespace Bucks
