#ifndef BIP_CUSTOM_H
#define BIP_CUSTOM_H

#include <string>
#include <vector>
#include <cstdint>

#include "bip_8192.h"

namespace Bucks {

/**
 * @file bip_custom.h
 *
 * ⚠️ Compatibility wrapper.
 * Canonical implementation: BIP-8192
 */

/**
 * @deprecated Use bip8192MnemonicFromEntropy directly.
 */
inline std::string generateCustomMnemonicFromEntropy(
    const std::vector<uint8_t>& entropy,
    const std::string& /*unused*/
) {
    static constexpr const char* WORDLIST =
        "wordlists/bip_8192.txt";

    return bip8192MnemonicFromEntropy(entropy, WORDLIST, nullptr);
}

/**
 * @deprecated Use bip8192ValidateMnemonic directly.
 */
inline bool validateCustomMnemonic(
    const std::string& mnemonic,
    const std::string& /*unused*/
) {
    static constexpr const char* WORDLIST =
        "wordlists/bip_8192.txt";

    return bip8192ValidateMnemonic(mnemonic, WORDLIST)
           == Bip8192Error::OK;
}

/**
 * @deprecated Use bip8192MnemonicToSeed directly.
 */
inline std::vector<uint8_t> mnemonicToSeedCustom(
    const std::string& mnemonic,
    const std::string& passphrase
) {
    return bip8192MnemonicToSeed(mnemonic, passphrase);
}

} // namespace Bucks

#endif // BIP_CUSTOM_H
