#ifndef BUCKS_FSUTIL_H
#define BUCKS_FSUTIL_H

#include <string>
#include <vector>

namespace Bucks {

// Write `data` to `path` with mode 0600 via temp file + fsync + rename, so a
// crash leaves either the old or the new file, never a truncated one.
bool writePrivateFileAtomic(const std::string& path, const std::string& data, std::string* err);

// Whole file into `out`; false if it cannot be opened.
bool readWholeFile(const std::string& path, std::string& out);

// Directory of the running executable, "" if the platform cannot tell.
std::string executableDir();

// Where a bundled data directory ("web", "wordlists") is looked for when no option names one, best
// first: <sourceDir>/<name> (the tree the binary was built from), then <exeDir>/../share/bucks/<name>
// (the install layout: <prefix>/bin/bucks, <prefix>/share/bucks/<name>). Empty inputs are skipped.
std::vector<std::string> bundledDirCandidates(const std::string& name, const std::string& sourceDir,
                                              const std::string& exeDir);
// The same for this binary: BUCKS_SOURCE_DIR (when compiled in) and executableDir().
std::vector<std::string> bundledDirCandidates(const std::string& name);

} // namespace Bucks

#endif // BUCKS_FSUTIL_H
