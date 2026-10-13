#ifndef CONSENSUS_H
#define CONSENSUS_H

#include <cstdint>

namespace Bucks {
namespace Consensus {

/**
 * ============================================================
 *  CONSENSUS PARAMETERS
 *  ------------------------------------------------------------
 *  ⚠️ ANY CHANGE IN THIS FILE IS A HARD FORK ⚠️
 * ============================================================
 */

/* ---------------- Block & Chain ---------------- */

constexpr uint32_t BLOCK_VERSION = 2;       // exact
constexpr uint32_t TX_VERSION = 2;          // exact
constexpr uint32_t BLOCK_HEADER_SIZE = 84;

/**
 * Maximum block weight.
 * (Bitcoin uses 4,000,000 weight units)
 * You may later switch to weight-based accounting.
 */
constexpr uint64_t MAX_BLOCK_SIZE = 1'000'000; // 1 MB (classic)

/**
 * Maximum allowed future block time (seconds).
 */
constexpr uint32_t MAX_FUTURE_BLOCK_TIME = 2 * 60 * 60; // 2 hours (7,200 s)

/** Median-time-past window (blocks). */
constexpr uint32_t MTP_WINDOW = 11;

/** Grace for the first block of a retarget period (BIP94 style), seconds. */
constexpr uint32_t TIMEWARP_GRACE = 600;

/** locktime below this is a height, at/above it a UNIX time. */
constexpr uint32_t LOCKTIME_THRESHOLD = 500'000'000;

/** Parser cap on transactions per block (1,000,000 / 65). */
constexpr uint32_t MAX_BLOCK_TXS_PARSE = 15'384;

/**
 * Coinbase maturity:
 * Number of blocks before coinbase outputs can be spent.
 */
constexpr uint32_t COINBASE_MATURITY = 100;


/* ---------------- Monetary Policy ---------------- */

/**
 * Base unit: awraq
 * 1 coin = 100,000,000 awraq (Bitcoin-style)
 */
constexpr uint64_t COIN = 100'000'000;

/**
 * Initial block reward.
 */
constexpr uint64_t INITIAL_SUBSIDY = 50 * COIN;

/**
 * Maximum number of coins that can ever exist.
 * (Bitcoin-style cap)
 */
constexpr uint64_t MAX_MONEY = 21'000'000 * COIN;


/* ---------------- Proof of Work ---------------- */

/**
 * Target block spacing (seconds).
 */
constexpr uint32_t TARGET_BLOCK_TIME = 10 * 60; // 10 minutes
constexpr int64_t TARGET_SPACING = TARGET_BLOCK_TIME;

/**
 * Difficulty retarget interval.
 */
constexpr uint32_t DIFFICULTY_ADJUSTMENT_INTERVAL = 2016;
constexpr uint32_t RETARGET_INTERVAL = DIFFICULTY_ADJUSTMENT_INTERVAL;

/** T: expected seconds per retarget period (1,209,600). */
constexpr int64_t TARGET_TIMESPAN =
    static_cast<int64_t>(RETARGET_INTERVAL) * TARGET_SPACING;


/* ---------------- Transactions ---------------- */

/**
 * Maximum number of inputs per transaction.
 * (DoS protection)
 */
constexpr uint32_t MAX_TX_INPUTS = 2'000;

/**
 * Maximum number of outputs per transaction.
 */
constexpr uint32_t MAX_TX_OUTPUTS = 3'000;

/**
 * Maximum serialized transaction size (bytes).
 */
constexpr uint32_t MAX_TX_SIZE = 100'000;


/* ---------------- Script & Validation ---------------- */

/** Coinbase scriptSig length bounds (bytes). */
constexpr uint32_t COINBASE_SCRIPTSIG_MIN = 4;
constexpr uint32_t COINBASE_SCRIPTSIG_MAX = 100;

/** Largest NULL_DATA payload (bytes); gold events must fit. */
constexpr uint32_t MAX_NULLDATA_PAYLOAD = 1'024;

/** The only allowed signature hash type. */
constexpr uint8_t SIGHASH_ALL = 0x01;

/**
 * Maximum signature operations per block.
 * (Protects against CPU exhaustion)
 */
constexpr uint32_t MAX_SIGOPS_PER_BLOCK = 20'000;


/* ---------------- Locktime & Sequence ---------------- */

/**
 * Sequence value that disables relative lock-time.
 */
constexpr uint32_t SEQUENCE_FINAL = 0xFFFFFFFF;


/* ---------------- Network-Independent ---------------- */

/**
 * Enforce strict DER signatures.
 */
constexpr bool REQUIRE_STRICT_DER = true;

/**
 * Enforce low-S values (anti-malleability).
 */
constexpr bool REQUIRE_LOW_S = true;

/* ---------------- Gold Standard ---------------- */

/**
 * Magic bytes for Gold Injection OP_RETURN.
 * "GOLD" in ASCII = 0x474F4C44
 */
constexpr uint32_t GOLD_MARKER = 0x474F4C44;

/**
 * Magic bytes for Gold Redemption OP_RETURN.
 * "REDM" in ASCII = 0x5245444D
 * FIX: Added REDEEM_MARKER — Bucks v1 security patch
 */
constexpr uint32_t REDEEM_MARKER = 0x5245444D;

/**
 * Vault Authority — 3-of-5 multisig threshold scheme.
 * FIX: 3-of-5 multisig vault authority — Bucks v1 security patch
 *
 * Any gold minting (GOLD) or redemption (REDM) event requires valid ECDSA
 * signatures from at least VAULT_MULTISIG_THRESHOLD distinct keyholders.
 *
 * OP_RETURN payload format (after 15-byte GOLD/REDM header):
 *   [key_idx:1][sig_len:1][sig_bytes:sig_len]  ... repeated per signer
 *
 * ⚠️ Replace ALL placeholder pubkeys with real compressed secp256k1 pubkeys
 *    before mainnet launch.
 */
constexpr int VAULT_MULTISIG_THRESHOLD = 3;
constexpr int VAULT_MULTISIG_TOTAL     = 5;

// 66-character hex strings (33 bytes compressed secp256k1 pubkeys)
// Replace with actual vault authority pubkeys before mainnet
inline const char* VAULT_AUTHORITY_PUBKEYS[5] = {
    "02aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa01", // Keyholder 1: Founding Team
    "02aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa02", // Keyholder 2: Independent Auditor
    "02aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa03", // Keyholder 3: Community Representative
    "02aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa04", // Keyholder 4: Standards Board
    "02aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa05", // Keyholder 5: Geographic Reserve
};

} // namespace Consensus
} // namespace Bucks

#endif // CONSENSUS_H
