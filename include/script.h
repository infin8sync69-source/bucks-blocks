#ifndef SCRIPT_H
#define SCRIPT_H

#include <vector>
#include <cstdint>
#include <array>
#include <string>

namespace Bucks {

using Script = std::vector<uint8_t>;
using Stack  = std::vector<std::vector<uint8_t>>;
using Hash160 = std::array<uint8_t, 20>;

enum OpCode : uint8_t {
    OP_DUP         = 0x76,
    OP_HASH160     = 0xa9,
    OP_EQUALVERIFY = 0x88,
    OP_CHECKSIG    = 0xac,
    OP_RETURN      = 0x6a
};

class ScriptInterpreter {
public:
    static bool execute(
        const Script& scriptSig,
        const Script& scriptPubKey,
        const std::vector<uint8_t>& sighash
    );

private:
    static bool eval(const Script& script, Stack& stack);
    static bool opDup(Stack& stack);
    static bool opHash160(Stack& stack);
    static bool opEqualVerify(Stack& stack);
    static bool opCheckSig(Stack& stack, const std::vector<uint8_t>& sighash);

    static Hash160 hash160(const std::vector<uint8_t>& data);
};

} // namespace Bucks

#endif
