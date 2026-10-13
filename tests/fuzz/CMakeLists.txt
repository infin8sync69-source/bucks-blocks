# Fuzz targets (docs/design/consensus-validation.md section 11.3). Built with -DBUCKS_FUZZ=ON.
# With a clang that has the libFuzzer runtime they are real fuzzers; otherwise they replay
# their seed corpus (and any reproducer files) through the same entry point.

set(BUCKS_FUZZ_TARGETS transaction block header undo netmessage compactsize)

foreach(name IN LISTS BUCKS_FUZZ_TARGETS)
    add_executable(fuzz_${name} fuzz_${name}.cpp)
    target_include_directories(fuzz_${name} PRIVATE ${CMAKE_SOURCE_DIR}/include)
    target_link_libraries(fuzz_${name} PRIVATE bucks_core)
    set(args "")
    if(BUCKS_HAVE_LIBFUZZER)
        target_link_options(fuzz_${name} PRIVATE -fsanitize=fuzzer)
        set(args -runs=0)   # run every corpus file once and exit
    else()
        target_sources(fuzz_${name} PRIVATE standalone_driver.cpp)
    endif()
    add_test(NAME fuzz_${name}_corpus
             COMMAND fuzz_${name} ${CMAKE_CURRENT_SOURCE_DIR}/corpus/${name} ${args})
    set_tests_properties(fuzz_${name}_corpus PROPERTIES LABELS fuzz)
endforeach()
