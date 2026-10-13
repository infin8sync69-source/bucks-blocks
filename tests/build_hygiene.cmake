# Build hygiene checks, run by ctest as:
#   cmake -DBUCKS_ROOT=<source dir> -P build_hygiene.cmake
# Fails if build.sh drifts from CMakeLists.txt (sources or hardening flags) or
# if .gitignore stops covering wallets, secrets and tool output.

if(NOT BUCKS_ROOT)
    message(FATAL_ERROR "BUCKS_ROOT is not set")
endif()

set(errors "")
file(READ "${BUCKS_ROOT}/CMakeLists.txt" cmake_text)
file(READ "${BUCKS_ROOT}/build.sh" build_sh)
file(STRINGS "${BUCKS_ROOT}/.gitignore" gitignore_lines)

# --- build.sh compiles exactly the sources CMake compiles ---
string(REGEX MATCH "add_library\\(bucks_core STATIC[^)]*\\)" core_block "${cmake_text}")
if(NOT core_block)
    list(APPEND errors "could not find add_library(bucks_core ...) in CMakeLists.txt")
endif()
string(REGEX MATCHALL "src/[A-Za-z0-9_]+\\.cpp" cmake_cpp "${core_block}")
string(REGEX REPLACE "src/([A-Za-z0-9_]+)\\.cpp" "\\1" cmake_cpp "${cmake_cpp}")
list(APPEND cmake_cpp main)

string(REGEX MATCH "CPP_SOURCES=\\([^)]*\\)" sh_block "${build_sh}")
string(REGEX REPLACE "^CPP_SOURCES=\\(" "" sh_block "${sh_block}")
string(REGEX MATCHALL "[A-Za-z0-9_]+" sh_cpp "${sh_block}")

set(cmake_sorted ${cmake_cpp})
set(sh_sorted ${sh_cpp})
list(SORT cmake_sorted)
list(SORT sh_sorted)
if(NOT cmake_sorted STREQUAL sh_sorted)
    list(JOIN sh_cpp " " sh_str)
    list(JOIN cmake_cpp " " cmake_str)
    list(APPEND errors
        "build.sh CPP_SOURCES (${sh_str}) != CMakeLists.txt bucks_core + main (${cmake_str})")
endif()

string(REGEX MATCHALL "src/[A-Za-z0-9_]+\\.c[ \t\r\n)]" cmake_c "${core_block}")
foreach(c_src IN LISTS cmake_c)
    string(REGEX REPLACE "^src/([A-Za-z0-9_]+\\.c).*" "\\1" c_src "${c_src}")
    string(FIND "${build_sh}" "$SRC_DIR/${c_src}" pos)
    if(pos EQUAL -1)
        list(APPEND errors "build.sh does not compile src/${c_src}")
    endif()
endforeach()

# --- build.sh uses the same optimization and hardening as CMake ---
foreach(flag -O2 -fstack-protector-strong -D_FORTIFY_SOURCE=2 -fPIE -pie
             -Wl,-z,relro,-z,now -DBUCKS_SOURCE_DIR=)
    string(FIND "${build_sh}" "${flag}" pos)
    if(pos EQUAL -1)
        list(APPEND errors "build.sh is missing ${flag}")
    endif()
endforeach()

# --- vendored SQLite is built with the same definitions everywhere ---
foreach(def SQLITE_THREADSAFE=1 SQLITE_DQS=0 SQLITE_OMIT_LOAD_EXTENSION)
    string(FIND "${cmake_text}" "${def}" in_cmake)
    string(FIND "${build_sh}" "${def}" in_sh)
    if(in_cmake EQUAL -1 OR in_sh EQUAL -1)
        list(APPEND errors "${def} must be defined for sqlite3.c in both CMakeLists.txt and build.sh")
    endif()
endforeach()

# --- .gitignore keeps wallets, secrets and tool output out of git ---
foreach(pattern wallets.json *.cookie .env .env.* !.env.example *.key *.pem
                restore.json *.db *.sqlite build/ compile_commands.json .cache/
                node_modules/ .bucks/)
    list(FIND gitignore_lines "${pattern}" idx)
    if(idx EQUAL -1)
        list(APPEND errors ".gitignore is missing '${pattern}'")
    endif()
endforeach()

# --- deleted symbols must not come back (spec section 12) ---
file(GLOB_RECURSE guarded_files "${BUCKS_ROOT}/src/*.cpp" "${BUCKS_ROOT}/include/*.h")
foreach(symbol ScriptInterpreter legacySignatureHash wtxid signInput verifyInput MerkleTree findGenesisNonce makeGenesisBlock createGenesisBlock UTXODB BlockDB saveGoldReserve checkTransactionBasic checkTransactionInputs workForBits accumWork isValidBlock POW_LIMIT_BITS GOV_PREFIX GOV_VOTE GOV_PROP GOV_ELECT GOV_CAND GOV_VETO)
    foreach(f IN LISTS guarded_files)
        if(f MATCHES "/(sqlite3|httplib)\\.")
            continue()
        endif()
        file(STRINGS "${f}" hits REGEX "${symbol}")
        if(hits)
            list(APPEND errors "deleted symbol ${symbol} found in ${f}")
        endif()
    endforeach()
endforeach()

# --- no unsynchronized console output: use Bucks::logOut()/logErr() (include/log.h) ---
file(GLOB_RECURSE src_files "${BUCKS_ROOT}/src/*.cpp")
foreach(f IN LISTS src_files)
    if(f MATCHES "/sqlite3\\.")
        continue()
    endif()
    file(STRINGS "${f}" raw_stream REGEX "std::(cout|cerr) *<<")
    if(raw_stream)
        list(APPEND errors "std::cout/std::cerr used in ${f}; use Bucks::logOut()/logErr() (thread-safe)")
    endif()
endforeach()

# --- every fuzz target ships a seed corpus (tests/fuzz, regenerate with tools/make_fuzz_corpus.py) ---
foreach(target transaction block header undo netmessage compactsize)
    file(GLOB seeds "${BUCKS_ROOT}/tests/fuzz/corpus/${target}/*")
    if(NOT seeds)
        list(APPEND errors "tests/fuzz/corpus/${target} has no seed files")
    endif()
endforeach()

if(errors)
    string(REPLACE ";" "\n  " errors "${errors}")
    message(FATAL_ERROR "Build hygiene check failed:\n  ${errors}")
endif()
list(LENGTH sh_cpp sh_count)
message(STATUS "Build hygiene OK (${sh_count} C++ sources)")
