#include "fsutil.h"

#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace Bucks {

bool readWholeFile(const std::string& path, std::string& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::ostringstream ss;
    ss << in.rdbuf();
    out = ss.str();
    return true;
}

// Write `data` to `path` with mode 0600 via temp file + fsync + rename, so
// a crash leaves either the old or the new file, never a truncated one.
bool writePrivateFileAtomic(const std::string& path, const std::string& data, std::string* err) {
    const std::string tmp = path + ".tmp";
    auto fail = [&](const std::string& what) {
        if (err) *err = what + ": " + std::strerror(errno);
        std::error_code ec;
        fs::remove(tmp, ec);
        return false;
    };
#ifndef _WIN32
    ::unlink(tmp.c_str());
    int fd = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    if (fd < 0) return fail("cannot create " + tmp);
    size_t off = 0;
    while (off < data.size()) {
        ssize_t n = ::write(fd, data.data() + off, data.size() - off);
        if (n < 0) {
            if (errno == EINTR) continue;
            ::close(fd);
            return fail("cannot write " + tmp);
        }
        off += static_cast<size_t>(n);
    }
    if (::fsync(fd) != 0) { ::close(fd); return fail("cannot fsync " + tmp); }
    if (::close(fd) != 0) return fail("cannot close " + tmp);
    if (::rename(tmp.c_str(), path.c_str()) != 0) return fail("cannot rename " + tmp);
    // Make the rename itself durable.
    fs::path dir = fs::path(path).parent_path();
    int dfd = ::open(dir.empty() ? "." : dir.string().c_str(), O_RDONLY);
    if (dfd >= 0) { ::fsync(dfd); ::close(dfd); }
    return true;
#else
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) return fail("cannot create " + tmp);
        out << data;
        if (!out) return fail("cannot write " + tmp);
    }
    std::error_code ec;
    fs::rename(tmp, path, ec);
    if (ec) {
        if (err) *err = "cannot rename " + tmp + ": " + ec.message();
        fs::remove(tmp, ec);
        return false;
    }
    return true;
#endif
}

} // namespace Bucks
