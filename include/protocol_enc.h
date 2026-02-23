#ifndef PROTOCOL_ENC_H
#define PROTOCOL_ENC_H

#include <vector>
#include <cstdint>
#include "crypto.h"

namespace Bucks {

/**
 * P2P Session Encryption
 * 
 * Manages the ephemeral keys and symmetric session secret for a single peer connection.
 */
class P2PSessionEncryption {
public:
    P2PSessionEncryption();

    /**
     * Get the ephemeral public key to send to the peer
     */
    PublicKey getEphemeralPubKey() const;

    /**
     * Compute the session secret from the peer's public key
     */
    bool establishSharedSecret(const PublicKey& peerPub);

    /**
     * Encrypt a message payload
     */
    std::vector<uint8_t> encrypt(const std::vector<uint8_t>& plain);

    /**
     * Decrypt a message payload. Returns true if successful.
     */
    bool decrypt(const std::vector<uint8_t>& cipher, std::vector<uint8_t>& out);

    bool isEstablished() const { return established; }

private:
    PrivateKey priv;
    PublicKey pub;
    Hash256 sessionKey;
    bool established;
};

} // namespace Bucks

#endif // PROTOCOL_ENC_H
