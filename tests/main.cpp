// The test binary's entry point, and the one translation unit that instantiates doctest.
//
// Defining this in a file of its own keeps the framework implementation out of every test
// file, which otherwise all pay to recompile it.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
