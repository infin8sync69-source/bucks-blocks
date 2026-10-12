#ifndef BUCKS_MERKLE_H
#define BUCKS_MERKLE_H

#include <vector>

#include "crypto.h"

namespace Bucks {

// Merkle root of `leaves` (txids in block order). One leaf: the root is that
// leaf. An odd level duplicates its last element. *mutated is set when two
// adjacent hashes in any level are equal (CVE-2012-2459): [a,b,c] and
// [a,b,c,c] share a root, so a block with mutated == true is a malleated copy
// and must not mark the real header invalid. Empty input gives an all-zero root.
Hash256 merkleRoot(const std::vector<Hash256>& leaves, bool* mutated = nullptr);

} // namespace Bucks

#endif // BUCKS_MERKLE_H
