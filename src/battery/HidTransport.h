#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "core/Win.h"

namespace peek::hid {

// The Win32 half shared by the vendor battery queries: finding HID interfaces, describing one
// without asking it anything, and one request with its reply. What the bytes mean belongs to
// each vendor's protocol unit; nothing here builds or reads a frame.

// Every present HID interface on the machine, as device paths.
std::vector<std::wstring> interfacePaths();

struct Collection {
    std::uint16_t vendorId = 0;
    std::uint16_t productId = 0;
    std::uint16_t usagePage = 0;
    std::uint16_t usage = 0;
    std::size_t inputLength = 0;
    std::size_t outputLength = 0;
    std::wstring product;
};

// The ids, the top-level usage and the report sizes of one interface. Opened with no access at
// all, which is enough for everything asked here and is refused by nothing; no report is sent.
std::optional<Collection> describe(std::wstring const& path);

using ReplyFilter = bool (*)(std::span<std::uint8_t const>) noexcept;

struct ExchangePolicy {
    // One request and its reply have to fit in this; a lost request is asked again rather
    // than waited on.
    std::chrono::milliseconds attemptBudget{100};
    int attempts = 3;
    // Input reports read after the request before concluding the answer is not among them.
    // Every open handle sees every report, including replies to other programs' requests.
    int reportsToScan = 4;
};

struct Exchange {
    bool opened = false;
    DWORD error = 0;
    std::optional<std::vector<std::uint8_t>> reply;
};

// Sends `request` as an output report and waits for an input report of `replyLength` bytes that
// `isReply` accepts. The handle is opened for the exchange and closed after it, so its report
// queue starts empty and nothing from before the request can pass for the answer.
Exchange exchange(std::wstring const& path,
                  std::span<std::uint8_t const> request,
                  std::size_t replyLength,
                  ReplyFilter isReply,
                  ExchangePolicy const& policy = {});

}  // namespace peek::hid
