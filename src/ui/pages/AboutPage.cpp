#include "ui/pages/AboutPage.h"

#include <shellapi.h>

#include <string>
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

    auto* repository =
        group->addCard(glyph::kChevronRight, std::wstring(text(Text::OpenSourceRepository)));
    repository->setOnClick([owner] { openWithShell(kRepositoryUrl, owner); });

    auto* author = group->addCard(glyph::kContact, std::wstring(text(Text::AboutAuthor)));
    author->setOnClick([owner] { openWithShell(kAuthorUrl, owner); });

    auto* support = group->addCard(glyph::kHeart, std::wstring(text(Text::SupportAuthor)));
    support->setOnClick([owner] { openWithShell(kSupportUrl, owner); });
}

}  // namespace peek::ui
