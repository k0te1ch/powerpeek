#pragma once

// What every test file in this suite is allowed to share: a scratch directory, a controller
// builder, and a fixed clock. Fixtures specific to one unit stay in that unit's test file.

#include <doctest/doctest.h>

#include <chrono>
#include <filesystem>
#include <string>
#include <string_view>

#include "battery/ControllerInfo.h"

// doctest prints an operand by streaming it, and there is no operator<< from std::wstring to
// a narrow stream. Without these two, every failed comparison of a controller id or a
// localised string reports "{?}" for both sides and the failure says nothing at all.
//
// They carry more weight than that, though, because the test binary is built with
// DOCTEST_CONFIG_DOUBLE_STRINGIFY: doctest stringifies twice, so a peek type whose own
// toString returns a wstring_view -- ChargeState and PowerSource both do -- lands here on the
// second pass. Without that arrangement, comparing two charge states is not a poor failure
// message but a compile error inside the framework. See tests/CMakeLists.txt.
namespace doctest {

template <>
struct StringMaker<std::wstring> {
    static String convert(std::wstring const& value);
};

template <>
struct StringMaker<std::wstring_view> {
    static String convert(std::wstring_view value);
};

}  // namespace doctest

namespace peek::test {

// A directory of its own under the system temp directory, removed with everything in it
// when the object dies.
//
// Every test that needs a path takes it from here. The application's real settings, history
// and log files live in %LOCALAPPDATA%, and a test suite that writes there would corrupt the
// state of the machine it is running on -- which on a developer's machine is their own
// installed copy of the application.
class TempDir {
public:
    TempDir();
    ~TempDir();

    TempDir(TempDir const&) = delete;
    TempDir& operator=(TempDir const&) = delete;

    std::filesystem::path const& path() const noexcept { return m_path; }
    std::filesystem::path file(std::wstring_view name) const { return m_path / name; }

private:
    std::filesystem::path m_path;
};

// A connected Xbox pad running on its battery, with everything else left at its default.
ControllerInfo makeController(std::wstring id, int percent,
                              ChargeState charge = ChargeState::Discharging);

// A fixed instant, so that a test which drives time forward tests the same instants on every
// run. Nothing in this suite may call system_clock::now(): the units that care about time all
// take it as an argument precisely so their rules can be checked.
std::chrono::system_clock::time_point testEpoch();

}  // namespace peek::test
