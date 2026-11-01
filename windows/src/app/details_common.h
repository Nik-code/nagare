#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "engine/engine.h"
#include "ui/widgets.h"

namespace app::details
{

enum class Tab
{
    Overview,
    Files,
    Peers,
    Trackers,
    Options,
};

inline constexpr int kTabCount = 5;

/// The sheet's side margin: the Mac's 20 points, in proportion to the wider panel.
inline constexpr float kMargin = 24.0F;

inline constexpr ui::TextStyle kBody{ 13.0F, DWRITE_FONT_WEIGHT_NORMAL, false };
inline constexpr ui::TextStyle kBodyMedium{ 13.0F, DWRITE_FONT_WEIGHT_MEDIUM, false };
inline constexpr ui::TextStyle kSmall{ 12.0F, DWRITE_FONT_WEIGHT_NORMAL, false };
inline constexpr ui::TextStyle kSmallMedium{ 12.0F, DWRITE_FONT_WEIGHT_MEDIUM, false };
inline constexpr ui::TextStyle kTiny{ 11.0F, DWRITE_FONT_WEIGHT_NORMAL, false };
inline constexpr ui::TextStyle kRowTitle{ 14.0F, DWRITE_FONT_WEIGHT_MEDIUM, false };
inline constexpr ui::TextStyle kStatValue{ 19.0F, DWRITE_FONT_WEIGHT_MEDIUM, true };

/// One screen of the panel. It reads the engine only while it is the one showing.
class TabView : public ui::View
{
public:
    /// Runs once per refresh while this tab shows. Reads the engine at most once per screenful.
    virtual void refresh(engine::TransferInfo const& info) = 0;
    /// Just became the visible tab, or the transfer's metadata changed under it.
    virtual void shown(engine::TransferInfo const& info) { refresh(info); }
    /// The transfer's metadata arrived, so anything built from it is stale.
    virtual void metadataChanged() {}
};

[[nodiscard]] std::unique_ptr<TabView> createOverviewTab(engine::TransferId id);
[[nodiscard]] std::unique_ptr<TabView> createPeersTab(engine::TransferId id);
[[nodiscard]] std::unique_ptr<TabView> createTrackersTab(engine::TransferId id);
[[nodiscard]] std::unique_ptr<TabView> createOptionsTab(engine::TransferId id);

/// Row delegates run once per visible row per frame, so these format into a stack buffer.
using TextBuffer = std::array<wchar_t, 96>;

[[nodiscard]] std::wstring_view fileSizeText(uint64_t bytes, TextBuffer& out);
[[nodiscard]] std::wstring_view speedText(double bytesPerSecond, TextBuffer& out);
/// 0 to 1, with up to `decimals` places, as the Mac's percentString.
[[nodiscard]] std::wstring_view percentText(double progress, int decimals, TextBuffer& out);
[[nodiscard]] std::wstring_view ratioText(double ratio, TextBuffer& out);
/// "Today at 2:44 AM", or the long date beyond yesterday.
[[nodiscard]] std::wstring dateText(int64_t unixSeconds);
/// "1h 5m 3s".
[[nodiscard]] std::wstring durationText(int64_t seconds);

[[nodiscard]] std::wstring trimmed(std::wstring_view text);
[[nodiscard]] bool pathExists(std::wstring const& path);

/// Selects the paths in Explorer, one window per folder.
void revealPaths(std::vector<std::wstring> const& paths);
void openPath(std::wstring const& path);
void copyText(std::wstring_view text);
[[nodiscard]] std::wstring pasteText();
/// The Mac's NSBeep, for a change the engine refused.
void refuse();

/// A title over a big number, from Theme's statWithTitle.
class Stat : public ui::View
{
public:
    static constexpr float kHeight = 42.0F;

    explicit Stat(std::wstring title);

    void setValue(std::wstring_view value);
    std::wstring const& value() const { return value_->text(); }

    void layout() override;

private:
    ui::Label* title_ = nullptr;
    ui::Label* value_ = nullptr;
};

/// The stats sit evenly across a row. `views` are placed left to right.
void layoutStats(std::vector<Stat*> const& views, float left, float top, float width);

} // namespace app::details
