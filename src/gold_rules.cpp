#include "gold_rules.h"

namespace Bucks {
namespace GoldRules {

namespace {

Classification stubClassify(const std::vector<uint8_t>&) { return Classification{}; }

ClassifyFn g_classify = stubClassify;

} // namespace

Classification classify(const std::vector<uint8_t>& payload) { return g_classify(payload); }

void setClassifierForTesting(ClassifyFn fn) { g_classify = fn ? fn : stubClassify; }

GoldUndoBuilder beginBlock(const GoldState&) { return GoldUndoBuilder{}; }

bool connectOutput(const Transaction&, uint32_t, uint32_t, const Hash256&, uint32_t,
                   GoldState&, GoldUndoBuilder&, ValidationState&) {
    return true;
}

bool disconnectBlock(GoldState&, const std::vector<uint8_t>& goldUndo) {
    return goldUndo.empty();
}

} // namespace GoldRules
} // namespace Bucks
