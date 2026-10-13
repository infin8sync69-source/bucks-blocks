#ifndef BUCKS_FSUTIL_H
#define BUCKS_FSUTIL_H

#include <string>

namespace Bucks {

// Write `data` to `path` with mode 0600 via temp file + fsync + rename, so a
// crash leaves either the old or the new file, never a truncated one.
bool writePrivateFileAtomic(const std::string& path, const std::string& data, std::string* err);

// Whole file into `out`; false if it cannot be opened.
bool readWholeFile(const std::string& path, std::string& out);

} // namespace Bucks

#endif // BUCKS_FSUTIL_H
