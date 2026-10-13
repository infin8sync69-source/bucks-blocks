#ifndef BUCKS_LOG_H
#define BUCKS_LOG_H

#include <iostream>
#include <mutex>
#include <sstream>
#include <string>

namespace Bucks {

// Thread-safe console logging. The network, server, miner and main threads all print; plain
// std::cout from several threads races on the stream's formatting state (ThreadSanitizer
// reports it). Each statement is formatted in a local buffer and written whole under one lock,
// so lines never interleave:
//
//     logOut() << "peer " << ip << " connected" << std::endl;
//
// The temporary lives until the end of the full expression, where the destructor writes it.
inline std::mutex& logMutex() {
    static std::mutex m;
    return m;
}

class LogLine {
public:
    explicit LogLine(std::ostream& out) : out_(out) {}
    LogLine(const LogLine&) = delete;
    LogLine& operator=(const LogLine&) = delete;
    ~LogLine() {
        std::lock_guard<std::mutex> lock(logMutex());
        out_ << buf_.str();
        out_.flush();
    }

    template <class T>
    LogLine& operator<<(const T& v) {
        buf_ << v;
        return *this;
    }
    LogLine& operator<<(std::ostream& (*manip)(std::ostream&)) {
        buf_ << manip;
        return *this;
    }
    LogLine& operator<<(std::ios_base& (*manip)(std::ios_base&)) {
        buf_ << manip;
        return *this;
    }

private:
    std::ostream& out_;
    std::ostringstream buf_;
};

inline LogLine logOut() { return LogLine(std::cout); }
inline LogLine logErr() { return LogLine(std::cerr); }

} // namespace Bucks

#endif // BUCKS_LOG_H
