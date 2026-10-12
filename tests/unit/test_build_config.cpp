#include <gtest/gtest.h>
#include <fstream>
#include <string>

// Checks the build settings CMakeLists.txt gives code that links bucks_core.

#if defined(__has_feature)
#  if __has_feature(address_sanitizer) || __has_feature(thread_sanitizer) || \
      __has_feature(memory_sanitizer)
#    define BUCKS_TEST_INTERCEPTING_SANITIZER 1
#  endif
#endif
#if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__)
#  define BUCKS_TEST_INTERCEPTING_SANITIZER 1
#endif

TEST(BuildConfig, SourceDirPointsAtWordlists) {
#ifndef BUCKS_SOURCE_DIR
    FAIL() << "BUCKS_SOURCE_DIR is not defined for targets linking bucks_core";
#else
    std::ifstream wordlist(std::string(BUCKS_SOURCE_DIR) + "/wordlists/bip_8192.txt");
    EXPECT_TRUE(wordlist.good()) << "BUCKS_SOURCE_DIR=" << BUCKS_SOURCE_DIR;
#endif
}

#if !defined(_WIN32) && (defined(__GNUC__) || defined(__clang__))
TEST(BuildConfig, StackProtectorStrong) {
#if !defined(__SSP_STRONG__) && !defined(__SSP_ALL__)
    FAIL() << "built without -fstack-protector-strong";
#endif
}

TEST(BuildConfig, FortifySourceWhenOptimizing) {
    // CMake leaves FORTIFY off for Debug and for ASan/TSan/MSan builds.
#if defined(__OPTIMIZE__) && !defined(BUCKS_TEST_INTERCEPTING_SANITIZER)
#  if !defined(_FORTIFY_SOURCE) || _FORTIFY_SOURCE < 2
    FAIL() << "optimized build without -D_FORTIFY_SOURCE=2";
#  endif
#else
    GTEST_SKIP() << "unoptimized or sanitizer build";
#endif
}

#if defined(__linux__)
TEST(BuildConfig, PositionIndependentExecutable) {
#  if !defined(__PIE__) && !defined(__pie__)
    FAIL() << "built without -fPIE";
#  endif
}
#endif
#endif
