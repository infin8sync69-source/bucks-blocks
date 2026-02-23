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

constexpr uint32_t BLOCK_VERSION = 1;

/**
 * Maximum block weight.
 * (Bitcoin uses 4,000,000 weight units)
 * You may later switch to weight-based accounting.
 */
constexpr uint64_t MAX_BLOCK_SIZE = 1'000'000; // 1 MB (classic)

/**
 * Maximum allowed future block time (seconds).
 */
constexpr uint32_t MAX_FUTURE_BLOCK_TIME = 2 * 60 * 60; // 2 hours

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
constexpr uint64_t INITIAL_BLOCK_REWARD = 50 * COIN;

/**
 * Reward halving interval (in blocks).
 */
constexpr uint32_t HALVING_INTERVAL = 210'000;

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

/**
 * Difficulty retarget interval.
 */
constexpr uint32_t DIFFICULTY_ADJUSTMENT_INTERVAL = 2016;

/**
 * Compact representation of the maximum PoW target.
 * (Lowest difficulty allowed)
 */
constexpr uint32_t POW_LIMIT_BITS = 0x1f00ffff;


/* ---------------- Transactions ---------------- */

/**
 * Maximum number of inputs per transaction.
 * (DoS protection)
 */
constexpr uint32_t MAX_TX_INPUTS = 10'000;

/**
 * Maximum number of outputs per transaction.
 */
constexpr uint32_t MAX_TX_OUTPUTS = 10'000;

/**
 * Maximum serialized transaction size (bytes).
 */
constexpr uint32_t MAX_TX_SIZE = 100'000;


/* ---------------- Script & Validation ---------------- */

/**
 * Maximum script size (bytes).
 */
constexpr uint32_t MAX_SCRIPT_SIZE = 10'000;

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
 * Vault Authority Public Key (Compressed).
 * For prototype, we use a fixed known key.
 * In production, this would be a secure offline key.
 * Corresponds to a specific private key we will use for "admin" tools.
 */
// Demo Key (derived from 'vault' seed)
// PubKey: 03... (Placeholder)
// We'll trust any input signed by this pubkey as an authorized update.
// For now, we define the 33-byte hex array or just a helper function.
// Let's rely on checking the input's pubkey in the scriptSig.

} // namespace Consensus
} // namespace Bucks

#endif // CONSENSUS_H
