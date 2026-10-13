# Third-party code

| Component | Version | Source | Integrity |
|---|---|---|---|
| SQLite amalgamation (`src/sqlite3.c`, `include/sqlite3.h`) | 3.54.0 | https://www.sqlite.org/2026/sqlite-amalgamation-3540000.zip | SHA3-256 `7b670a62fdfbd672b75fef004cb703c8a3e87d3a5cc7d675b4a08337004a2d93`, matching the value published on https://www.sqlite.org/download.html |

Build definitions (CMakeLists.txt and build.sh, kept identical by `build_hygiene`):
`SQLITE_THREADSAFE=1`, `SQLITE_DQS=0`, `SQLITE_OMIT_LOAD_EXTENSION`.
The storage spec requires 3.51.3 or newer; `src/chaindb.cpp` fails to compile on older headers.
