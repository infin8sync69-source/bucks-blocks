#include "protocol_enc.h"
#include <openssl/rand.h>
#include <openssl/evp.h>
#include <cstring>
#include <stdexcept>

namespace Bucks {

P2PSessionEncryption::P2PSessionEncryption() : established(false) {
    generatePrivateKey(priv);
    derivePublicKey(priv, pub);
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
    if (!established) return plain;

    // IV (16 bytes for AES)
    std::vector<uint8_t> iv(16);
    RAND_bytes(iv.data(), 16);

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    EVP_EncryptInit_ex(ctx, EVP_aes_256_cbc(), nullptr, sessionKey.data(), iv.data());

    std::vector<uint8_t> ciphertext(plain.size() + 16);
    int len = 0;
    EVP_EncryptUpdate(ctx, ciphertext.data(), &len, plain.data(), plain.size());
    int ciphertextLen = len;

    EVP_EncryptFinal_ex(ctx, ciphertext.data() + len, &len);
    ciphertextLen += len;
    ciphertext.resize(ciphertextLen);

    EVP_CIPHER_CTX_free(ctx);

    // Final result is IV || ciphertext
    std::vector<uint8_t> out = iv;
    out.insert(out.end(), ciphertext.begin(), ciphertext.end());
    return out;
}

bool P2PSessionEncryption::decrypt(const std::vector<uint8_t>& cipher, std::vector<uint8_t>& out) {
    if (!established) {
        out = cipher;
        return true;
    }
    if (cipher.size() < 16) return false;

    // Extract IV
    std::vector<uint8_t> iv(cipher.begin(), cipher.begin() + 16);
    std::vector<uint8_t> ciphertext(cipher.begin() + 16, cipher.end());
    
    // std::cout << "DEBUG: decrypting cipher size " << cipher.size() << " (ct: " << ciphertext.size() << ")" << std::endl;

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    EVP_DecryptInit_ex(ctx, EVP_aes_256_cbc(), nullptr, sessionKey.data(), iv.data());

    std::vector<uint8_t> plain(ciphertext.size() + 16);
    int len = 0;
    EVP_DecryptUpdate(ctx, plain.data(), &len, ciphertext.data(), (int)ciphertext.size());
    int plainLen = len;

    if (EVP_DecryptFinal_ex(ctx, plain.data() + len, &len) == 0) {
        // unsigned long err = ERR_get_error();
        // std::cerr << "DEBUG: EVP_DecryptFinal_ex failed, error: " << err << std::endl;
        EVP_CIPHER_CTX_free(ctx);
        return false;
    }
    plainLen += len;
    plain.resize(plainLen);

    EVP_CIPHER_CTX_free(ctx);
    out = plain;
    return true;
}

} // namespace Bucks
