# libFuzzer targets for the two parsers that read files a user can hand the application:
# notification sounds (WavReader) and the settings / history files (Json).
#
# Built only by the "fuzz" preset. Each target is a plain executable whose main() comes
# from libFuzzer; run it with a corpus directory, for example
#   build\fuzz\fuzz\pp_fuzz_wav.exe fuzz\corpus\wav -max_total_time=60

function(pp_add_fuzzer name source)
    add_executable(${name} "${CMAKE_CURRENT_SOURCE_DIR}/${source}")
    target_compile_options(${name} PRIVATE /fsanitize=fuzzer)
    target_link_libraries(${name} PRIVATE pp_compile_settings PowerPeekCore windowsapp)
    pp_set_warnings(${name} ${PP_WARNINGS_AS_ERRORS})
    pp_add_cppwinrt_includes(${name})
endfunction()

pp_add_fuzzer(pp_fuzz_wav FuzzWav.cpp)
pp_add_fuzzer(pp_fuzz_json FuzzJson.cpp)
