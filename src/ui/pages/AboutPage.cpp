#include "ui/pages/AboutPage.h"

#include <shellapi.h>
#include <shlobj.h>

#include <filesystem>
#include <string>
#include <system_error>
#include <utility>

#include "core/AppPaths.h"
#include "core/Logger.h"
#include "core/Strings.h"
#include "ui/pages/PageWidgets.h"

namespace peek::ui {
namespace {

constexpr wchar_t kRepositoryUrl[] = L"https://github.com/k0te1ch/powerpeek";
constexpr wchar_t kAuthorUrl[] = L"https://github.com/k0te1ch";
constexpr wchar_t kSupportUrl[] = L"https://boosty.to/k0te1ch";

// ShellExecute returns a fake HINSTANCE whose value below 32 is the error code.
void openWithShell(std::wstring const& target, HWND owner) {
    auto const result = reinterpret_cast<INT_PTR>(
        ShellExecuteW(owner, L"open", target.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
    if (result <= 32) {
        log::warning(L"Could not open {}: shell error {}", target, result);
    }
}

// Opens Explorer on the log's folder with the log itself selected, so the file to attach to a
// bug report is the one already highlighted. Before the first line is written there is no
// file to select, and the folder alone is the next best thing.
void revealLog(HWND owner) {
    std::filesystem::path const file = paths::logFile();
    std::error_code code;
    if (!std::filesystem::exists(file, code)) {
        openWithShell(file.parent_path().wstring(), owner);
        return;
    }

    PIDLIST_ABSOLUTE item = ILCreateFromPathW(file.c_str());
    if (!item) {
        openWithShell(file.parent_path().wstring(), owner);
        return;
    }
    HRESULT const hr = SHOpenFolderAndSelectItems(item, 0, nullptr, 0);
    ILFree(item);
    if (FAILED(hr)) {
        log::warning(L"Could not reveal {}: {}", file.wstring(), describeHresult(hr));
        openWithShell(file.parent_path().wstring(), owner);
    }
}

}  // namespace

AboutPage::AboutPage(PageContext context) : Page(std::move(context)) {}

void AboutPage::build(StackPanel& column) {
    column.emplace<PageHeader>(std::wstring(text(Text::AppName)),
                               std::wstring(text(Text::AppTagline)));

    auto* group = column.emplace<SettingsGroup>(std::wstring(text(Text::NavAbout)));

    auto* version =
        group->addCard(glyph::kInfo, formatText(Text::AboutVersion, widen(PP_VERSION_STRING)));
    version->setDescription(std::wstring(text(Text::AboutDescription)));

    HWND const owner = m_context.owner;
    auto* folder = group->addCard(glyph::kFolder, std::wstring(text(Text::OpenDataFolder)));
    folder->setOnClick([owner] { openWithShell(paths::dataDir().wstring(), owner); });

    auto* logs = group->addCard(glyph::kFolder, std::wstring(text(Text::OpenLogsFolder)));
    logs->setOnClick([owner] { revealLog(owner); });

    auto* repository =
        group->addCard(glyph::kChevronRight, std::wstring(text(Text::OpenSourceRepository)));
    repository->setOnClick([owner] { openWithShell(kRepositoryUrl, owner); });

    auto* author = group->addCard(glyph::kContact, std::wstring(text(Text::AboutAuthor)));
    author->setOnClick([owner] { openWithShell(kAuthorUrl, owner); });

    auto* support = group->addCard(glyph::kHeart, std::wstring(text(Text::SupportAuthor)));
    support->setOnClick([owner] { openWithShell(kSupportUrl, owner); });
}

}  // namespace peek::ui
