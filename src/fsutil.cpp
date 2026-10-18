#include "fsutil.h"

#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#elif defined(_WIN32)
#include <windows.h>
#endif

namespace fs = std::filesystem;

namespace Bucks {

std::string executableDir() {
    std::string path;
#if defined(__APPLE__)
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::vector<char> buf(size + 1, '\0');
    if (size == 0 || _NSGetExecutablePath(buf.data(), &size) != 0) return "";
    path.assign(buf.data());
#elif defined(_WIN32)
    char buf[MAX_PATH];
    DWORD n = GetModuleFileNameA(nullptr, buf, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return "";
    path.assign(buf, n);
#elif defined(__linux__)
    char buf[4096];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n <= 0) return "";
    path.assign(buf, static_cast<size_t>(n));
#else
    return "";
#endif
    std::error_code ec;
    fs::path p = fs::weakly_canonical(fs::path(path), ec);
    if (ec) p = fs::absolute(fs::path(path), ec);
    if (ec) return "";
    return p.parent_path().string();
}

std::vector<std::string> bundledDirCandidates(const std::string& name, const std::string& sourceDir,
                                              const std::string& exeDir) {
    std::vector<std::string> out;
    if (!sourceDir.empty()) out.push_back((fs::path(sourceDir) / name).string());
    if (!exeDir.empty())
        out.push_back((fs::path(exeDir) / ".." / "share" / "bucks" / name).lexically_normal().string());
    return out;
}

std::vector<std::string> bundledDirCandidates(const std::string& name) {
#ifdef BUCKS_SOURCE_DIR
    const std::string sourceDir = BUCKS_SOURCE_DIR;
#else
    const std::string sourceDir;
#endif
    return bundledDirCandidates(name, sourceDir, executableDir());
}

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
