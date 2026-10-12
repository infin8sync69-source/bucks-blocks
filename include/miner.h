#ifndef BUCKS_MINER_H
#define BUCKS_MINER_H

#include "blockchain.h"
#include "consensus.h"
#include "mempool.h"

#include <atomic>
#include <thread>

namespace Bucks {

class Miner {
public:
    Miner(Blockchain& bc, Mempool& mp, const ConsensusParams& params);
    ~Miner() { stop(); }

    void start(const Script& coinbaseScript);
    void stop();

    bool isMining() const { return mining; }

private:
    void mineLoop(Script coinbaseScript);
    Block createBlockTemplate(const Script& coinbaseScript);
    bool mineBlock(Block& block);
    uint32_t currentTimestamp() const;

    Blockchain& chain;
    Mempool& mempool;
    ConsensusParams consensus;

    std::atomic<bool> mining;
    std::thread minerThread;
};

} // namespace Bucks

#endif // BUCKS_MINER_H
