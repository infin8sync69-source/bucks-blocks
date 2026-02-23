#include "hd_8192.h"
#include <cstring>
#include <iostream>
#include <algorithm>

namespace Bucks {

ExtendedKey HD8192::createMaster(const std::vector<uint8_t>& seed) {
    const std::vector<uint8_t> hmacKey = {'B', 'I', 'P', '-', '8', '1', '9', '2', ' ', 's', 'e', 'e', 'd'};
    std::vector<uint8_t> I = hmac_sha512(hmacKey, seed);

    ExtendedKey master;
    master.isPrivate = true;
    master.depth = 0;
    master.parentFingerprint = 0;
    master.childNumber = 0;
    
    master.key.assign(I.begin(), I.begin() + 32);
    std::copy(I.begin() + 32, I.end(), master.chainCode.begin());

    return master;
}

ExtendedKey HD8192::deriveChild(const ExtendedKey& parent, uint32_t childIndex) {
    ExtendedKey child;
    child.depth = parent.depth + 1;
    child.parentFingerprint = getFingerprint(parent);
    child.childNumber = childIndex;

    bool hardened = (childIndex >= 0x80000000);
    std::vector<uint8_t> data;

    if (parent.isPrivate) {
        if (hardened) {
            data.push_back(0x00);
            data.insert(data.end(), parent.key.begin(), parent.key.end());
        } else {
            PublicKey pub;
            PrivateKey priv;
            std::copy(parent.key.begin(), parent.key.end(), priv.begin());
            derivePublicKey(priv, pub);
            data.insert(data.end(), pub.begin(), pub.end());
        }
    } else {
        if (hardened) {
            std::cerr << "ERROR: Hardened derivation from public parent is impossible" << std::endl;
            return {};
        }
        data.insert(data.end(), parent.key.begin(), parent.key.end());
    }

    // Append childIndex in big-endian
    data.push_back((childIndex >> 24) & 0xFF);
    data.push_back((childIndex >> 16) & 0xFF);
    data.push_back((childIndex >> 8) & 0xFF);
    data.push_back(childIndex & 0xFF);

    std::vector<uint8_t> hKey(parent.chainCode.begin(), parent.chainCode.end());
    std::vector<uint8_t> I = hmac_sha512(hKey, data);

    std::vector<uint8_t> IL(I.begin(), I.begin() + 32);
    std::vector<uint8_t> IR(I.begin() + 32, I.end());
    std::copy(IR.begin(), IR.end(), child.chainCode.begin());

    if (parent.isPrivate) {
        child.isPrivate = true;
        PrivateKey p1, p2, res;
        std::copy(parent.key.begin(), parent.key.end(), p1.begin());
        std::copy(IL.begin(), IL.end(), p2.begin());
        if (!addScalar(p1, p2, res)) return {};
        child.key.assign(res.begin(), res.end());
    } else {
        child.isPrivate = false;
        PublicKey p1 = parent.key;
        PrivateKey p2;
        std::copy(IL.begin(), IL.end(), p2.begin());
        PublicKey res;
        if (!addPoint(p1, p2, res)) return {};
        child.key = res;
    }

    return child;
}

uint32_t HD8192::getFingerprint(const ExtendedKey& key) {
    PublicKey pub;
    if (key.isPrivate) {
        PrivateKey priv;
        std::copy(key.key.begin(), key.key.end(), priv.begin());
        derivePublicKey(priv, pub);
    } else {
        pub = key.key;
    }

    Hash160 h = hash160(pub);
    // Fingerprint is first 4 bytes of hash160(pubkey)
    return (h[0] << 24) | (h[1] << 16) | (h[2] << 8) | h[3];
}

ExtendedKey HD8192::derivePath(const ExtendedKey& master, const std::string& path) {
    ExtendedKey current = master;
    
    // Simple path parser for "m/0'/1/2"
    if (path.empty() || path[0] != 'm') return master;
    
    size_t pos = 1;
    while (pos < path.size()) {
        if (path[pos] == '/') {
            pos++;
            uint32_t val = 0;
            size_t start = pos;
            while (pos < path.size() && isdigit(path[pos])) {
                val = val * 10 + (path[pos] - '0');
                pos++;
            }
            if (pos < path.size() && path[pos] == '\'') {
                val += 0x80000000;
                pos++;
            }
            current = deriveChild(current, val);
        } else {
            pos++;
        }
    }
    
    return current;
}

} // namespace Bucks
