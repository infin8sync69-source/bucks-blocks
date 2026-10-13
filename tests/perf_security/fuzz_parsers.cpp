// FZ-1: deterministic random-mutation fuzzing of every parser that reads bytes from outside
// (docs/design/consensus-validation.md section 11.3).
//
// Properties checked for every mutated input:
//   * the parser either returns an object or throws ParseError (never another exception, never a crash;
//     under ASan/UBSan any memory error aborts the test);
//   * an input the parser accepts is the canonical encoding: serialize(parse(b)) == b;
//   * parsing never allocates more than 64 times the input length (normal builds only: a counting
//     operator new cannot coexist with the sanitizer runtimes' own).

#include <gtest/gtest.h>

#include <atomic>
#include <cstdlib>
#include <functional>
#include <new>
#include <random>

#include "block.h"
#include "chainparams.h"
#include "coins.h"
#include "genesis.h"
#include "network.h"
#include "script.h"
#include "serialize.h"
#include "transaction.h"
#include "tx_vector.h"
#include "undo.h"

#if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__)
#define BUCKS_NO_ALLOC_TRACKING 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer) || __has_feature(thread_sanitizer) || __has_feature(memory_sanitizer)
#define BUCKS_NO_ALLOC_TRACKING 1
#endif
#endif

#ifndef BUCKS_NO_ALLOC_TRACKING
namespace {
std::atomic<size_t> g_maxAlloc{0};
std::atomic<bool> g_tracking{false};
}
void* operator new(size_t n) {
    if (g_tracking.load(std::memory_order_relaxed)) {
        size_t cur = g_maxAlloc.load(std::memory_order_relaxed);
        while (n > cur && !g_maxAlloc.compare_exchange_weak(cur, n)) {}
    }
    void* p = std::malloc(n ? n : 1);
    if (!p) throw std::bad_alloc();
    return p;
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, size_t) noexcept { std::free(p); }
#endif

using namespace Bucks;

namespace {

constexpr int kIterations = 100000;

using Bytes = std::vector<uint8_t>;

// One mutation: bit flips, byte changes, inserts, deletes, truncation, splicing, duplicated chunks,
// and size fields overwritten with huge CompactSize values.
Bytes mutate(const Bytes& seed, std::mt19937& rng) {
    Bytes b = seed;
    const int ops = 1 + rng() % 3;
    for (int k = 0; k < ops; ++k) {
        switch (rng() % 9) {
            case 0:
                if (!b.empty()) b[rng() % b.size()] ^= uint8_t(1u << (rng() % 8));
                break;
            case 1:
                if (!b.empty()) b[rng() % b.size()] = uint8_t(rng());
                break;
            case 2:
                b.insert(b.begin() + (b.empty() ? 0 : rng() % (b.size() + 1)), uint8_t(rng()));
                break;
            case 3:
                if (!b.empty()) b.erase(b.begin() + rng() % b.size());
                break;
            case 4:
                if (!b.empty()) b.resize(rng() % b.size());
                break;
            case 5: {   // append garbage
                const size_t n = rng() % 16;
                for (size_t i = 0; i < n; ++i) b.push_back(uint8_t(rng()));
                break;
            }
            case 6:     // duplicate a chunk
                if (b.size() > 4) {
                    const size_t from = rng() % (b.size() - 2), len = 1 + rng() % std::min<size_t>(32, b.size() - from);
                    Bytes chunk(b.begin() + from, b.begin() + from + len);
                    b.insert(b.begin() + rng() % (b.size() + 1), chunk.begin(), chunk.end());
                }
                break;
            case 7:     // a huge or non-minimal CompactSize at a random place
                if (b.size() > 9) {
                    static const Bytes forms[] = {
                        {0xFE, 0xFF, 0xFF, 0xFF, 0xFF}, {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF},
                        {0xFD, 0xFC, 0x00}, {0xFE, 0x00, 0x01, 0x00, 0x00}, {0xFD, 0xFF, 0xFF}, {0xFC}, {0xFD, 0x00, 0x00}};
                    const Bytes& f = forms[rng() % 7];
                    const size_t at = rng() % (b.size() - f.size());
                    std::copy(f.begin(), f.end(), b.begin() + at);
                }
                break;
            default:    // overwrite a 4-byte field with an extreme value
                if (b.size() > 8) {
                    static const uint32_t extremes[] = {0, 1, 0x7FFFFFFF, 0x80000000u, 0xFFFFFFFFu, 0xFFFFFFF0u};
                    const uint32_t v = extremes[rng() % 6];
                    const size_t at = rng() % (b.size() - 4);
                    for (int i = 0; i < 4; ++i) b[at + i] = uint8_t(v >> (8 * i));
                }
                break;
        }
    }
    return b;
}

struct Stats { size_t accepted = 0, rejected = 0; };

// Runs one parser over mutations of one seed. `parse` returns the canonical re-encoding of what it
// parsed (and throws ParseError on a malformed input).
void fuzz(const char* name, const Bytes& seed, const std::function<Bytes(const Bytes&)>& parse,
          uint32_t rngSeed, const std::function<Bytes(Bytes, std::mt19937&)>& fixup = {}) {
    std::mt19937 rng(rngSeed);
    Stats st;

    // The seed itself must round-trip.
    ASSERT_EQ(parse(seed), seed) << name << ": the seed is not canonical";

    for (int i = 0; i < kIterations; ++i) {
        Bytes input = mutate(seed, rng);
        if (fixup) input = fixup(std::move(input), rng);

#ifndef BUCKS_NO_ALLOC_TRACKING
        g_maxAlloc = 0;
        g_tracking = true;
#endif
        bool accepted = false;
        Bytes again;
        try {
            again = parse(input);
            accepted = true;
        } catch (const ParseError&) {
        } catch (const std::exception& e) {
#ifndef BUCKS_NO_ALLOC_TRACKING
            g_tracking = false;
#endif
            FAIL() << name << ": unexpected exception '" << e.what() << "' on input of " << input.size() << " bytes (iteration " << i << ")";
        }
#ifndef BUCKS_NO_ALLOC_TRACKING
        g_tracking = false;
        // The re-encoding allocates too; the bound is about what the parser asks for from the input.
        ASSERT_LE(g_maxAlloc.load(), input.size() * 64 + 4096)
            << name << ": a " << input.size() << "-byte input caused a " << g_maxAlloc.load() << "-byte allocation";
#endif
        if (accepted) {
            ASSERT_EQ(again, input) << name << ": accepted a non-canonical encoding (iteration " << i << ")";
            ++st.accepted;
        } else {
            ++st.rejected;
        }
    }
    std::cout << "[ FUZZ ] " << name << ": " << st.accepted << " accepted, " << st.rejected << " rejected\n";
    EXPECT_GT(st.rejected, size_t(kIterations / 20)) << name << ": mutations rarely break the input";
}

Bytes regtestGenesisBlock() { return Genesis::build(chainParams(NetworkId::REGTEST)).serialize(); }

Bytes sampleBlock() {
    Block b = Genesis::build(chainParams(NetworkId::REGTEST));
    Transaction t = Transaction::deserialize(parseHex(TxVector::fullTxHex()));
    b.transactions.push_back(t);
    t.lockTime = 7;
    b.transactions.push_back(t);
    b.header.merkleRoot = b.computeMerkleRoot();
    return b.serialize();
}

Bytes sampleUndo() {
    BlockUndo u;
    u.blockHash.fill(0x5A);
    for (int i = 0; i < 3; ++i) {
        Coin c;
        c.amount = 1000 + i;
        Hash160 h;
        h.fill(uint8_t(i + 1));
        c.scriptPubKey = makeP2PKH(h);
        c.height = 10 + i;
        c.isCoinbase = i == 1;
        u.spent.push_back(c);
    }
    u.goldUndo = {1, 2, 3, 4};
    return u.serialize();
}

// Mutating an undo record almost always breaks its checksum, which would stop every input at the
// first check. Half the time the checksum is recomputed so the field parsers get exercised too.
Bytes fixUndoChecksum(Bytes b, std::mt19937& rng) {
    if (rng() % 2 == 0 || b.size() < 32) return b;
    Bytes body(b.begin(), b.end() - 32);
    Hash256 c = doubleSha256(body);
    b.assign(body.begin(), body.end());
    b.insert(b.end(), c.begin(), c.end());
    return b;
}

} // namespace

TEST(FuzzParsers, Transaction) {
    fuzz("Transaction", parseHex(TxVector::fullTxHex()),
         [](const Bytes& b) { return Transaction::deserialize(b).serialize(); }, 1);
}

TEST(FuzzParsers, CoinbaseTransaction) {
    fuzz("Transaction(coinbase)", Genesis::build(chainParams(NetworkId::REGTEST)).transactions[0].serialize(),
         [](const Bytes& b) { return Transaction::deserialize(b).serialize(); }, 2);
}

TEST(FuzzParsers, BlockHeader) {
    fuzz("BlockHeader", Genesis::build(chainParams(NetworkId::REGTEST)).header.serialize(),
         [](const Bytes& b) { return BlockHeader::deserialize(b).serialize(); }, 3);
}

TEST(FuzzParsers, GenesisBlock) {
    fuzz("Block(genesis)", regtestGenesisBlock(),
         [](const Bytes& b) { return Block::deserialize(b).serialize(); }, 4);
}

TEST(FuzzParsers, BlockWithTransactions) {
    fuzz("Block(3 txs)", sampleBlock(),
         [](const Bytes& b) { return Block::deserialize(b).serialize(); }, 5);
}

TEST(FuzzParsers, UndoRecord) {
    fuzz("BlockUndo", sampleUndo(),
         [](const Bytes& b) { return BlockUndo::deserialize(b).serialize(); }, 6, fixUndoChecksum);
}

TEST(FuzzParsers, NetMessageFrames) {
    const auto magic = chainParams(NetworkId::REGTEST).netMagic;
    const Bytes seed = NetMessage(MessageType::TX, parseHex(TxVector::fullTxHex())).serialize(magic);
    fuzz("NetMessage", seed,
         [&](const Bytes& b) {
             try {
                 NetMessage m = NetMessage::deserialize(b, magic);
                 return m.serialize(magic);
             } catch (const NetMalformed& e) {
                 throw ParseError(e.what());
             } catch (const NetIncomplete& e) {
                 throw ParseError(e.what());
             }
         },
         7);
}

// CompactSize: accepted encodings are exactly the minimal ones.
TEST(FuzzParsers, CompactSize) {
    std::mt19937 rng(8);
    for (int i = 0; i < kIterations; ++i) {
        Bytes b(1 + rng() % 9);
        for (auto& x : b) x = uint8_t(rng() % 4 == 0 ? rng() : (rng() % 2 ? 0xFF : 0));
        if (rng() % 3 == 0) b[0] = uint8_t(0xFC + rng() % 4);   // the boundary tags
        Reader r(b);
        try {
            const uint64_t v = r.compactSize(MAX_COMPACT_SIZE);
            Writer w;
            w.compactSize(v);
            ASSERT_EQ(Bytes(b.begin(), b.begin() + r.position()), w.data()) << "non-minimal form accepted";
            ASSERT_LE(v, MAX_COMPACT_SIZE);
        } catch (const ParseError&) {
        }
    }
}

// The script templates accept exactly the encodings the builders produce.
TEST(FuzzParsers, ScriptTemplates) {
    std::mt19937 rng(9);
    Bytes seedNull;
    {
        Script s;
        makeNullData(Bytes(100, 7), s);
        seedNull = s;
    }
    Hash160 h;
    h.fill(3);
    const Script p2pkh = makeP2PKH(h);
    CompressedPubKey pub{};
    pub[0] = 2;
    Script unlock;
    makeP2PKHUnlock(Bytes(70, 0x30), pub, unlock);

    for (int i = 0; i < kIterations; ++i) {
        const Bytes a = mutate(seedNull, rng);
        Bytes payload;
        if (parseNullData(a, &payload)) {
            Script rebuilt;
            ASSERT_TRUE(makeNullData(payload, rebuilt));
            ASSERT_EQ(Bytes(rebuilt), a) << "non-canonical NULL_DATA accepted";
        }
        const Bytes b = mutate(p2pkh, rng);
        Hash160 got;
        if (isP2PKH(b, &got)) ASSERT_EQ(makeP2PKH(got), b);
        const Bytes c = mutate(unlock, rng);
        Bytes der;
        CompressedPubKey k;
        if (parseP2PKHUnlock(c, der, k)) {
            Script rebuilt;
            ASSERT_TRUE(makeP2PKHUnlock(der, k, rebuilt));
            ASSERT_EQ(Bytes(rebuilt), c) << "non-canonical unlock script accepted";
        }
    }
}
