#include "protocol_enc.h"
#include "log.h"
#include <openssl/rand.h>
#include <openssl/evp.h>
#include <cstring>
#include <stdexcept>
#include <iostream>
namespace Bucks {

P2PSessionEncryption::P2PSessionEncryption() : established(false) {
    // Fail closed if the RNG fails: never run a session on a zero/stale key.
    if (!generatePrivateKey(priv) || !derivePublicKey(priv, pub))
        throw std::runtime_error("P2P session key generation failed");
}

PublicKey P2PSessionEncryption::getEphemeralPubKey() const {
    return pub;
}

bool P2PSessionEncryption::establishSharedSecret(const PublicKey& peerPub) {
    if (computeSharedSecret(priv, peerPub, sessionKey)) {
        established = true;
        return true;
    }
    return false;
}

std::vector<uint8_t> P2PSessionEncryption::encrypt(const std::vector<uint8_t>& plain) {
    // FIX: AES-256-GCM replaces AES-256-CBC — Bucks v1 security patch
    // CBC without a MAC is vulnerable to bit-flipping attacks that corrupt data
    // in transit and can be used to forge messages.  GCM is AEAD: it both
    // encrypts AND authenticates in one pass.
    // Wire format: IV(12) || AUTH_TAG(16) || CIPHERTEXT
    if (!established) return plain;

    std::vector<uint8_t> iv(12);
    RAND_bytes(iv.data(), 12);   // 96-bit IV is the GCM standard

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr);
    EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, 12, nullptr);
    EVP_EncryptInit_ex(ctx, nullptr, nullptr, sessionKey.data(), iv.data());

    std::vector<uint8_t> ciphertext(plain.size());
    int len = 0;
    EVP_EncryptUpdate(ctx, ciphertext.data(), &len, plain.data(), (int)plain.size());
    int ciphertextLen = len;

    EVP_EncryptFinal_ex(ctx, ciphertext.data() + len, &len);
    ciphertextLen += len;
    ciphertext.resize(ciphertextLen);

    // Retrieve the 16-byte GCM authentication tag
    std::vector<uint8_t> tag(16);
    EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, 16, tag.data());

    EVP_CIPHER_CTX_free(ctx);

    // Output: IV(12) || TAG(16) || CIPHERTEXT
    std::vector<uint8_t> out;
    out.reserve(12 + 16 + ciphertextLen);
    out.insert(out.end(), iv.begin(), iv.end());
    out.insert(out.end(), tag.begin(), tag.end());
    out.insert(out.end(), ciphertext.begin(), ciphertext.end());
    return out;
}

bool P2PSessionEncryption::decrypt(const std::vector<uint8_t>& cipher, std::vector<uint8_t>& out) {
    // FIX: AES-256-GCM authenticated decryption — Bucks v1 security patch
    // Wire format: IV(12) || AUTH_TAG(16) || CIPHERTEXT
    // If the GCM tag does not verify the message is silently dropped — this
    // prevents bit-flip attacks from corrupting peer state.
    if (!established) { out = cipher; return true; }
    if (cipher.size() < 28) return false;   // 12 IV + 16 TAG minimum

    std::vector<uint8_t> iv       (cipher.begin(),      cipher.begin() + 12);
    std::vector<uint8_t> tag      (cipher.begin() + 12, cipher.begin() + 28);
    std::vector<uint8_t> ciphertext(cipher.begin() + 28, cipher.end());

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr);
    EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, 12, nullptr);
    EVP_DecryptInit_ex(ctx, nullptr, nullptr, sessionKey.data(), iv.data());

    std::vector<uint8_t> plain(ciphertext.size());
    int len = 0;
    EVP_DecryptUpdate(ctx, plain.data(), &len, ciphertext.data(), (int)ciphertext.size());
    int plainLen = len;

    // Set expected auth tag BEFORE calling DecryptFinal
    EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, 16, (void*)tag.data());

    int finalRet = EVP_DecryptFinal_ex(ctx, plain.data() + len, &len);
    EVP_CIPHER_CTX_free(ctx);

    if (finalRet <= 0) {
        Bucks::logErr() << "DEBUG: AES-GCM authentication failed — message dropped" << std::endl;
        return false;  // Authentication failed: drop the message
    }

    plainLen += len;
    plain.resize(plainLen);
    out = plain;
    return true;
}

} // namespace Bucks
