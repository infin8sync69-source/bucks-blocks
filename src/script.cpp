#include "script.h"
#include "crypto.h"

namespace Bucks {

// -----------------------------------------------------------------------------
// Entry point
// -----------------------------------------------------------------------------

bool ScriptInterpreter::execute(
    const Script& scriptSig,
    const Script& scriptPubKey,
    const std::vector<uint8_t>& sighash
) {
    Stack stack;

    if (!eval(scriptSig, stack))
        return false;

    if (!eval(scriptPubKey, stack))
        return false;

    return !stack.empty() && stack.back().size() == 1 && stack.back()[0] == 1;
}

// -----------------------------------------------------------------------------
// Script evaluation
// -----------------------------------------------------------------------------

bool ScriptInterpreter::eval(const Script& script, Stack& stack) {
    for (size_t pc = 0; pc < script.size();) {
        uint8_t opcode = script[pc++];

        // Push data (Bitcoin-style)
        if (opcode <= 75) {
            if (pc + opcode > script.size())
                return false;

            stack.emplace_back(
                script.begin() + pc,
                script.begin() + pc + opcode
            );
            pc += opcode;
            continue;
        }

        switch (opcode) {
            case OP_DUP:
                if (!opDup(stack)) return false;
                break;

            case OP_HASH160:
                if (!opHash160(stack)) return false;
                break;

            case OP_EQUALVERIFY:
                if (!opEqualVerify(stack)) return false;
                break;

            case OP_CHECKSIG:
                if (!opCheckSig(stack, {})) return false;
                break;

            default:
                return false; // Unknown opcode
        }
    }
    return true;
}

// -----------------------------------------------------------------------------
// Opcodes
// -----------------------------------------------------------------------------

bool ScriptInterpreter::opDup(Stack& stack) {
    if (stack.empty()) return false;
    stack.push_back(stack.back());
    return true;
}

bool ScriptInterpreter::opHash160(Stack& stack) {
    if (stack.empty()) return false;

    auto data = stack.back();
    stack.pop_back();

    Hash160 h = hash160(data);
    stack.emplace_back(h.begin(), h.end());
    return true;
}

bool ScriptInterpreter::opEqualVerify(Stack& stack) {
    if (stack.size() < 2) return false;

    auto a = stack.back(); stack.pop_back();
    auto b = stack.back(); stack.pop_back();

    if (a != b) return false;
    return true;
}

bool ScriptInterpreter::opCheckSig(
    Stack& stack,
    const std::vector<uint8_t>& sighash
) {
    if (stack.size() < 2) return false;

    auto pubkey = stack.back(); stack.pop_back();
    auto sig    = stack.back(); stack.pop_back();

    if (!verifySignature(
            std::string(sig.begin(), sig.end()),
            std::string(pubkey.begin(), pubkey.end()),
            sighash
        )) {
        return false;
    }

    stack.push_back({1});
    return true;
}

// -----------------------------------------------------------------------------
// Hash160 helper
// -----------------------------------------------------------------------------

Hash160 ScriptInterpreter::hash160(const std::vector<uint8_t>& data) {
    auto sha = sha256(data);
    auto rip = ripemd160(sha);

    Hash160 out{};
    std::copy(rip.begin(), rip.end(), out.begin());
    return out;
}

} // namespace Bucks
