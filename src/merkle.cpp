#include "merkle.h"

namespace Bucks {

Hash256 merkleRoot(const std::vector<Hash256>& leaves, bool* mutated) {
    if (mutated) *mutated = false;
    if (leaves.empty()) return Hash256{};

    std::vector<Hash256> level = leaves;
    while (level.size() > 1) {
        for (size_t i = 0; i + 1 < level.size(); i += 2) {
            if (level[i] == level[i + 1] && mutated) *mutated = true;
        }
        if (level.size() % 2 == 1) level.push_back(level.back());

        std::vector<Hash256> next;
        next.reserve(level.size() / 2);
        for (size_t i = 0; i < level.size(); i += 2) {
            std::vector<uint8_t> data;
            data.reserve(64);
            data.insert(data.end(), level[i].begin(), level[i].end());
            data.insert(data.end(), level[i + 1].begin(), level[i + 1].end());
            next.push_back(doubleSha256(data));
        }
        level = std::move(next);
    }
    return level[0];
}

} // namespace Bucks
