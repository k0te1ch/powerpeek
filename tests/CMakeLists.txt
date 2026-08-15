# The unit test binary.
#
# It links PowerPeekCore, which is the whole reason that library exists: these tests run on
# a build agent with no controller, no audio endpoint and no interactive session, so they
# can only cover the part of the application that needs none of those.

include(FetchContent)

# doctest is the single external dependency in this repository, and it is confined to the
# test binary. It is a header-only framework, so nothing it brings ends up in the shipped
# executable. Pinned to an exact tag rather than a branch: a test suite that changes
# behaviour because an upstream default moved is worse than no test suite.
set(DOCTEST_WITH_TESTS OFF CACHE BOOL "" FORCE)
set(DOCTEST_WITH_MAIN_IN_STATIC_LIB OFF CACHE BOOL "" FORCE)
set(DOCTEST_NO_INSTALL ON CACHE BOOL "" FORCE)

FetchContent_Declare(doctest
    GIT_REPOSITORY https://github.com/doctest/doctest.git
    GIT_TAG v2.4.11
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(doctest)

file(GLOB_RECURSE PP_TEST_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.h")

add_executable(pp_tests ${PP_TEST_SOURCES})

target_include_directories(pp_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

# Turns doctest's stringification into toString(toString(value)), and it is required rather
# than cosmetic. Several peek types already have a toString of their own returning a
# std::wstring_view; argument-dependent lookup finds that from inside the framework, which
# then cannot concatenate a wide view with its narrow buffer. With the second pass, that view
# reaches the StringMaker in TestSupport.h instead, and CHECK(a == b) on a ChargeState both
# compiles and prints the state names. Defined on the target so every translation unit,
# including the one instantiating doctest, agrees on it.
target_compile_definitions(pp_tests PRIVATE DOCTEST_CONFIG_DOUBLE_STRINGIFY)

target_link_libraries(pp_tests PRIVATE
    pp_compile_settings
    PowerPeekCore
    doctest::doctest
    # Same ordering rule as the application: windowsapp.lib re-exports symbols other
    # libraries own, so it goes last and never first.
    windowsapp)

pp_set_warnings(pp_tests ${PP_WARNINGS_AS_ERRORS})
pp_add_cppwinrt_includes(pp_tests)

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" PREFIX "tests" FILES ${PP_TEST_SOURCES})

# Registers every TEST_CASE with CTest individually, by running the binary at build time and
# asking it what it contains. One CTest entry per case means a failure names the case in the
# CI log without anyone having to open an artifact.
list(APPEND CMAKE_MODULE_PATH "${doctest_SOURCE_DIR}/scripts/cmake")
include(doctest)
doctest_discover_tests(pp_tests)
