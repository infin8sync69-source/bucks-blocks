#include "hd_8192.h"
#include "log.h"
#include <openssl/crypto.h>
#include <cstring>
#include <iostream>
#include <algorithm>

namespace Bucks {

namespace {

void cleanse(std::vector<uint8_t>& v) {
    if (!v.empty()) OPENSSL_cleanse(v.data(), v.size());
}

void cleanse(PrivateKey& k) {
    OPENSSL_cleanse(k.data(), k.size());
}

} // namespace

std::string defaultReceivePath() {
    return "m/44'/" + std::to_string(BUCKS_COIN_TYPE) + "'/0'/0/0";
}

ExtendedKey HD8192::createMaster(const std::vector<uint8_t>& seed) {
    // BIP-32: I = HMAC-SHA512(key = "Bitcoin seed", data = seed)
    static const std::vector<uint8_t> hmacKey = {'B', 'i', 't', 'c', 'o', 'i', 'n', ' ', 's', 'e', 'e', 'd'};
    if (seed.empty()) return {};
    std::vector<uint8_t> I = hmac_sha512(hmacKey, seed);
    if (I.size() != 64) return {};

    // BIP-32: the master key is invalid if IL == 0 or IL >= n.
    PrivateKey IL;
    std::copy(I.begin(), I.begin() + 32, IL.begin());
    if (!isValidPrivateKey(IL)) { cleanse(IL); cleanse(I); return {}; }

    ExtendedKey master;
    master.isPrivate = true;
    master.depth = 0;
    master.parentFingerprint = 0;
    master.childNumber = 0;

    master.key.assign(I.begin(), I.begin() + 32);
    std::copy(I.begin() + 32, I.end(), master.chainCode.begin());

    cleanse(IL);
    cleanse(I);
    return master;
}

ExtendedKey HD8192::deriveChild(const ExtendedKey& parent, uint32_t childIndex) {
    // Fail closed on a malformed parent (e.g. the empty key of a failed derivation).
    if (parent.key.size() != (parent.isPrivate ? 32u : 33u)) return {};

    ExtendedKey child;
    child.depth = parent.depth + 1;
    child.parentFingerprint = getFingerprint(parent);
    child.childNumber = childIndex;

    bool hardened = (childIndex >= HD_HARDENED_BIT);
    std::vector<uint8_t> data;

    if (parent.isPrivate) {
        if (hardened) {
            data.push_back(0x00);
            data.insert(data.end(), parent.key.begin(), parent.key.end());
        } else {
            PublicKey pub;
            PrivateKey priv;
            std::copy(parent.key.begin(), parent.key.end(), priv.begin());
            bool ok = derivePublicKey(priv, pub);
            cleanse(priv);
            if (!ok) return {};
            data.insert(data.end(), pub.begin(), pub.end());
        }
    } else {
        if (hardened) {
            Bucks::logErr() << "ERROR: Hardened derivation from public parent is impossible" << std::endl;
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
    cleanse(data);
    if (I.size() != 64) return {};

    PrivateKey IL;
    std::copy(I.begin(), I.begin() + 32, IL.begin());
    std::copy(I.begin() + 32, I.end(), child.chainCode.begin());
    cleanse(I);

    bool ok;
    if (parent.isPrivate) {
        child.isPrivate = true;
        PrivateKey p1, res;
        std::copy(parent.key.begin(), parent.key.end(), p1.begin());
        ok = addScalar(p1, IL, res);   // fails if IL >= n or the child key is 0
        if (ok) child.key.assign(res.begin(), res.end());
        cleanse(p1);
        cleanse(res);
    } else {
        child.isPrivate = false;
        PublicKey res;
        ok = addPoint(parent.key, IL, res);
        if (ok) child.key = res;
    }
    cleanse(IL);

    if (!ok) return {};
    return child;
}

uint32_t HD8192::getFingerprint(const ExtendedKey& key) {
    PublicKey pub;
    if (key.isPrivate) {
        if (key.key.size() != 32) return 0;
        PrivateKey priv;
        std::copy(key.key.begin(), key.key.end(), priv.begin());
        bool ok = derivePublicKey(priv, pub);
        cleanse(priv);
        if (!ok) return 0;
    } else {
        pub = key.key;
    }

    Hash160 h = hash160(pub);
    // Fingerprint is first 4 bytes of hash160(pubkey)
    return (uint32_t(h[0]) << 24) | (uint32_t(h[1]) << 16) | (uint32_t(h[2]) << 8) | uint32_t(h[3]);
}

bool HD8192::parsePath(const std::string& path, std::vector<uint32_t>& indices) {
    indices.clear();
    if (path.empty() || path[0] != 'm') return false;

    size_t pos = 1;
    while (pos < path.size()) {
        if (path[pos] != '/') return false;
        ++pos;

        size_t start = pos;
        while (pos < path.size() && path[pos] >= '0' && path[pos] <= '9') ++pos;
        size_t digits = pos - start;
        if (digits == 0) return false;                       // empty component or non-digit
        if (digits > 1 && path[start] == '0') return false;  // leading zero
        if (digits > 10) return false;                       // cannot fit in 32 bits

        uint64_t val = 0;
        for (size_t i = start; i < pos; ++i)
            val = val * 10 + static_cast<uint64_t>(path[i] - '0');
        if (val >= HD_HARDENED_BIT) return false;            // index must be < 2^31

        uint32_t index = static_cast<uint32_t>(val);
        if (pos < path.size() && (path[pos] == '\'' || path[pos] == 'h' || path[pos] == 'H')) {
            index |= HD_HARDENED_BIT;
            ++pos;
        }
        indices.push_back(index);
    }
    return true;
}

ExtendedKey HD8192::derivePath(const ExtendedKey& master, const std::string& path) {
    std::vector<uint32_t> indices;
    if (!parsePath(path, indices)) return {};   // malformed path: fail closed

    ExtendedKey current = master;
    for (uint32_t index : indices) {
        current = deriveChild(current, index);
        if (current.key.empty()) return {};     // derivation failed
    }
    return current;
}

} // namespace Bucks
