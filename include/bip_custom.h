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
 * Compatibility wrapper around the Bucks Mnemonic v2 API (bip_8192.h).
 * The wordlist-path arguments are ignored: lists are always loaded from the
 * manifest directory (see setWordlistDir / BUCKS_WORDLIST_DIR). New code
 * should call mnemonicFromEntropy / validateMnemonic / mnemonicToSeed.
 */

/**
 * @deprecated Use mnemonicFromEntropy directly.
 */
inline std::string generateCustomMnemonicFromEntropy(
    const std::vector<uint8_t>& entropy,
    const std::string& /*unused*/
) {
    return mnemonicFromEntropy(entropy, "", nullptr);
}

/**
 * @deprecated Use validateMnemonic directly.
 */
inline bool validateCustomMnemonic(
    const std::string& mnemonic,
    const std::string& /*unused*/
) {
    return validateMnemonic(mnemonic) == MnemonicError::OK;
}

/**
 * @deprecated Use mnemonicToSeed directly.
 */
inline std::vector<uint8_t> mnemonicToSeedCustom(
    const std::string& mnemonic,
    const std::string& passphrase
) {
    return mnemonicToSeed(mnemonic, passphrase);
}

} // namespace Bucks

#endif // BIP_CUSTOM_H
