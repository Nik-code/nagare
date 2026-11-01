#pragma once

#include <functional>
#include <memory>
#include <string>
#include <string_view>

#include "ui/widgets.h"

namespace app
{

/// Takes a magnet or an https .torrent link as far as the Add panel, the Mac's openURL. `done` hears
/// an empty string once it's on its way, or says why not. A fetch outlives whoever asked for it.
void openLink(std::wstring const& link, std::function<void(std::wstring const& error)> done);

/// The Mac's test for whether the clipboard is worth prefilling.
bool looksLikeLink(std::wstring_view text);

/// Paste a magnet or torrent link.
class LinkPanel final : public ui::Panel
{
public:
    LinkPanel();

    ui::TextField* field() const { return field_; }
    ui::Button* getButton() const { return get_; }
    ui::Button* cancelButton() const { return cancel_; }
    ui::Label* status() const { return status_; }

    void get();

protected:
    void layoutContent() override;
    void adjustGlass(GlassShape& shape) const override;

private:
    void updateButton();

    ui::TextField* field_ = nullptr;
    ui::Label* status_ = nullptr;
    ui::Button* cancel_ = nullptr;
    ui::Button* get_ = nullptr;
    bool working_ = false;
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
};

} // namespace app
