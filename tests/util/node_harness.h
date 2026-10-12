#ifndef BUCKS_TESTS_NODE_HARNESS_H
#define BUCKS_TESTS_NODE_HARNESS_H

#include <filesystem>
#include <memory>
#include <string>

#include <unistd.h>

#include "node.h"

namespace Bucks {
namespace TestUtil {

// A regtest node on a private temporary datadir, removed with the object.
// Regtest blocks need about two hashes, so tests can mine through the API.
class NodeHarness {
public:
    explicit NodeHarness(const ChainParams& params = chainParams(NetworkId::REGTEST),
                         const LoadOptions& load = {}) {
        static int counter = 0;
        root_ = (std::filesystem::temp_directory_path() /
                 ("bucks-test-node-" + std::to_string(getpid()) + "-" + std::to_string(counter++))).string();
        std::filesystem::remove_all(root_);
        std::filesystem::create_directories(root_);
        chmod(root_.c_str(), 0700);
        std::string err;
        int code = 0;
        node = Node::open(root_ + "/chain", params, load, err, code);
        error = err;
        exitCode = code;
    }
    ~NodeHarness() {
        node.reset();
        std::filesystem::remove_all(root_);
    }

    // A directory for the server's wallet file and cookie, next to the chain.
    std::string dataDir() const { return root_ + "/data"; }
    const std::string& root() const { return root_; }

    std::unique_ptr<Node> node;
    std::string error;
    int exitCode = 0;

private:
    std::string root_;
};

} // namespace TestUtil
} // namespace Bucks

#endif
