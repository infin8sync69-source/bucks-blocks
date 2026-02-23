#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace Bucks {

using Hash256 = std::array<uint8_t, 32>;

class MerkleTree {
public:
    MerkleTree() = default;
    explicit MerkleTree(const std::vector<Hash256>& leaves);

    const Hash256& getRoot() const;

private:
    static Hash256 hashPair(const Hash256& left,
                            const Hash256& right);

    Hash256 buildTree(std::vector<Hash256> level);

    Hash256 root{};
};

} // namespace Bucks
