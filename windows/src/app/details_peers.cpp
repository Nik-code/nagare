#include <shlwapi.h>

#include <algorithm>
#include <string>
#include <vector>

#include "app/app.h"
#include "app/details_common.h"

namespace app::details
{

namespace
{

using namespace ui;

constexpr float kStatsTop = 14.0F;
constexpr float kSourcesGap = 10.0F;
constexpr float kSourcesHeight = 16.0F;
constexpr float kListGap = 8.0F;

// InfoPeersViewController.mm
constexpr float kRowHeight = 50.0F;
constexpr float kAddressTop = 8.0F;
constexpr float kAddressHeight = 18.0F;
constexpr float kDetailTop = 26.0F;
constexpr float kDetailHeight = 16.0F;
constexpr float kRatesWidth = 96.0F;
constexpr float kRateLineHeight = 16.0F;
constexpr float kLockSize = 14.0F;
constexpr float kLockGap = 12.0F;
constexpr float kWhoGap = 8.0F;
constexpr float kLineAlpha = 0.45F;
constexpr float kSelectionInset = 12.0F;

/// libtransmission's tr_peer_from, in the order TransferDetails::peersFrom uses.
enum PeerFrom
{
    kIncoming,
    kLocal,
    kTracker,
    kDht,
    kPex,
    kCache,
    kLtep,
};

constexpr std::array<wchar_t const*, 7> kConnectedLabels{ L" incoming", L" local discovery", L" tracker", L" DHT", L" PEX", L" cache", L" LTEP" };
constexpr std::array<PeerFrom, 7> kConnectedOrder{ kTracker, kIncoming, kCache, kLocal, kPex, kDht, kLtep };
constexpr std::array<wchar_t const*, 7> kFromLabels{ L"From: incoming connection", L"From: local peer discovery", L"From: tracker", L"From: distributed hash table", L"From: peer exchange", L"From: cache", L"From: libtorrent extension protocol handshake" };

bool isActive(engine::TransferInfo const& info)
{
    switch (info.state)
    {
    case engine::TransferState::None:
    case engine::TransferState::Paused:
    case engine::TransferState::Finished:
        return false;
    case engine::TransferState::Problem:
        return info.peersConnected > 0;
    default:
        return !info.isWaiting;
    }
}

double busyness(engine::PeerInfo const& peer)
{
    return (peer.isDownloadingFrom ? peer.downBytesPerSecond : 0.0) + (peer.isUploadingTo ? peer.upBytesPerSecond : 0.0);
}

bool samePeerRow(engine::PeerInfo const& a, engine::PeerInfo const& b)
{
    return a.address == b.address && a.client == b.client && a.flags == b.flags && a.progress == b.progress && a.isSeed == b.isSeed && a.isEncrypted == b.isEncrypted && a.isUtp == b.isUtp && a.isDownloadingFrom == b.isDownloadingFrom && a.isUploadingTo == b.isUploadingTo && a.downBytesPerSecond == b.downBytesPerSecond && a.upBytesPerSecond == b.upBytesPerSecond && a.port == b.port && a.from == b.from;
}

bool sameSeedRow(engine::WebseedInfo const& a, engine::WebseedInfo const& b)
{
    return a.url == b.url && a.isDownloading == b.isDownloading && a.downBytesPerSecond == b.downBytesPerSecond;
}

/// The Mac's tooltip for a peer, on one line. Windows has no hover text here, so the row shows it.
std::wstring peerSummary(engine::PeerInfo const& peer)
{
    TextBuffer buffer;
    std::wstring text = L"Progress: ";
    text += percentText(peer.progress, 1, buffer);
    if (peer.progress < 1.0F && peer.isSeed)
    {
        text += L" (Partial Seed)";
    }

    text += L"  ·  ";
    text += peer.isUtp ? L"µTP" : L"TCP";
    if (peer.isEncrypted)
    {
        text += L" (encrypted)";
    }

    text += L"  ·  Port: ";
    text += peer.port > 0 ? std::to_wstring(peer.port) : L"N/A";

    if (peer.from >= 0 && static_cast<size_t>(peer.from) < kFromLabels.size())
    {
        text += L"  ·  ";
        text += kFromLabels[static_cast<size_t>(peer.from)];
    }

    auto const flag = [&peer](wchar_t letter) { return peer.flags.find(letter) != std::wstring::npos; };
    if (flag(L'D'))
    {
        text += L"  ·  Downloading";
    }
    else if (flag(L'd'))
    {
        text += L"  ·  Peer is choking you";
    }
    if (flag(L'U'))
    {
        text += L"  ·  Uploading";
    }
    else if (flag(L'u'))
    {
        text += L"  ·  You are choking peer";
    }
    if (flag(L'K'))
    {
        text += L"  ·  Peer unchoked you, not interested";
    }
    if (flag(L'?'))
    {
        text += L"  ·  You unchoked peer, not interested";
    }
    return text;
}

/// Hover only: the Mac's peers can't be selected, so a click has nothing to do.
class PeerList final : public ListView
{
public:
    PeerList()
    {
        takesFocus_ = false;
    }

    bool onMouse(MouseEvent const& event) override
    {
        switch (event.kind)
        {
        case MouseKind::Down:
        case MouseKind::DoubleClick:
        case MouseKind::Up:
        case MouseKind::RightClick:
            return true;
        default:
            return ListView::onMouse(event);
        }
    }
};

class PeersTab : public TabView
{
public:
    explicit PeersTab(engine::TransferId id)
        : id_(id)
    {
        for (wchar_t const* title : { L"Connected", L"Sending to you", L"Receiving from you" })
        {
            stats_.push_back(add<Stat>(title));
        }
        sources_ = add<Label>(L"", kSmall, ColorRole::Foreground);
        sources_->setTrim(Trim::End);

        list_ = add<PeerList>();
        list_->setRowHeight(kRowHeight);
        list_->setRowInsets(-1.0F, kSelectionInset);
        list_->drawRow = [this](ID2D1DeviceContext* context, int row, D2D1_RECT_F rect, RowState state) { paintRow(context, row, rect, state); };
    }

    void layout() override
    {
        float const contentWidth = width() - kMargin * 2.0F;
        layoutStats(stats_, kMargin, kStatsTop, contentWidth);
        float const sourcesTop = kStatsTop + Stat::kHeight + kSourcesGap;
        sources_->setFrame(D2D1::RectF(kMargin, sourcesTop, kMargin + contentWidth, sourcesTop + kSourcesHeight));
        list_->setFrame(D2D1::RectF(0.0F, sourcesTop + kSourcesHeight + kListGap, width(), height()));
    }

    void refresh(engine::TransferInfo const& info) override
    {
        auto& engine = App::get().engine();
        bool const active = isActive(info);

        previousPeers_.swap(peers_);
        previousSeeds_.swap(seeds_);
        if (active)
        {
            engine.peers(id_, peers_);
            std::sort(
                peers_.begin(),
                peers_.end(),
                [](engine::PeerInfo const& a, engine::PeerInfo const& b)
                {
                    // Busiest first, then whoever has the most, so the list holds still between updates.
                    double const first = busyness(a);
                    double const second = busyness(b);
                    if (first != second)
                    {
                        return first > second;
                    }
                    if (a.progress != b.progress)
                    {
                        return a.progress > b.progress;
                    }
                    return StrCmpLogicalW(a.address.c_str(), b.address.c_str()) < 0;
                });
        }
        else
        {
            peers_.clear();
        }
        engine.webseeds(id_, seeds_);
        showRows();

        auto const details = engine.details(id_);
        int connected = 0;
        int sending = 0;
        int receiving = 0;
        if (active && info.peersConnected > 0)
        {
            connected = info.peersConnected;
            sending = details.peersSendingToUs;
            receiving = details.peersGettingFromUs;
        }
        stats_[0]->setValue(std::to_wstring(connected));
        stats_[1]->setValue(std::to_wstring(sending));
        stats_[2]->setValue(std::to_wstring(receiving));
        sources_->setText(sourcesText(active, connected, details));
    }

private:
    static std::wstring sourcesText(bool active, int connected, engine::TransferDetails const& details)
    {
        if (!active)
        {
            return L"Transfer Not Active";
        }
        if (connected == 0)
        {
            return L"Looking for peers";
        }
        std::wstring text = L"Found through ";
        bool first = true;
        for (PeerFrom const from : kConnectedOrder)
        {
            int const count = details.peersFrom[from];
            if (count > 0)
            {
                text += first ? L"" : L", ";
                text += std::to_wstring(count);
                text += kConnectedLabels[from];
                first = false;
            }
        }
        return text;
    }

    void showRows()
    {
        int const count = static_cast<int>(peers_.size() + seeds_.size());
        if (count != list_->rowCount() || peers_.size() != previousPeers_.size())
        {
            list_->setRowCount(count);
            list_->reloadData();
            return;
        }
        for (size_t i = 0; i < peers_.size(); ++i)
        {
            if (!samePeerRow(peers_[i], previousPeers_[i]))
            {
                list_->reloadRow(static_cast<int>(i));
            }
        }
        for (size_t i = 0; i < seeds_.size(); ++i)
        {
            if (!sameSeedRow(seeds_[i], previousSeeds_[i]))
            {
                list_->reloadRow(static_cast<int>(peers_.size() + i));
            }
        }
    }

    static void paintRates(ID2D1DeviceContext* context, D2D1_RECT_F rect, bool hasDown, double down, bool hasUp, double up)
    {
        Theme& theme = Theme::get();
        Palette const& colors = theme.colors();
        float const left = rect.right - kMargin - kRatesWidth;
        float const right = rect.right - kMargin;
        float const midY = (rect.top + rect.bottom) / 2.0F;
        // Both lines, or one centred, as the Mac's stack does when a field hides.
        float const lines = (hasDown ? 1.0F : 0.0F) + (hasUp ? 1.0F : 0.0F);
        float top = midY - lines * kRateLineHeight / 2.0F;

        TextBuffer buffer;
        TextBuffer line;
        auto const rate = [&](wchar_t arrow, double speed, D2D1_COLOR_F color)
        {
            auto const text = speedText(speed, buffer);
            int const length = swprintf_s(line.data(), line.size(), L"%c %.*s", arrow, static_cast<int>(text.size()), text.data());
            theme.text(context, std::wstring_view(line.data(), static_cast<size_t>(std::max(length, 0))), kSmall, D2D1::RectF(left, top, right, top + kRateLineHeight), color, DWRITE_TEXT_ALIGNMENT_TRAILING, Trim::None, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            top += kRateLineHeight;
        };
        if (hasDown)
        {
            rate(L'↓', down, colors.accent);
        }
        if (hasUp)
        {
            rate(L'↑', up, colors.seed);
        }
    }

    void paintRow(ID2D1DeviceContext* context, int row, D2D1_RECT_F rect, RowState state)
    {
        Theme& theme = Theme::get();
        Palette const& colors = theme.colors();
        float const left = rect.left + kMargin;
        float const lockRight = rect.right - kMargin - kRatesWidth - kLockGap;
        float const textRight = lockRight - kLockSize - kWhoGap;
        D2D1_RECT_F const address = D2D1::RectF(left, rect.top + kAddressTop, textRight, rect.top + kAddressTop + kAddressHeight);
        D2D1_RECT_F const detail = D2D1::RectF(left, rect.top + kDetailTop, textRight, rect.top + kDetailTop + kDetailHeight);

        auto const index = static_cast<size_t>(row);
        if (index >= peers_.size())
        {
            auto const& seed = seeds_[index - peers_.size()];
            theme.text(context, seed.url, kBody, address, colors.bright, DWRITE_TEXT_ALIGNMENT_LEADING, Trim::Middle);
            theme.text(context, L"Web seed", kSmall, detail, colors.foreground);
            paintRates(context, rect, seed.isDownloading, seed.downBytesPerSecond, false, 0.0);
            return;
        }

        auto const& peer = peers_[index];
        theme.text(context, peer.address, kBody, address, colors.bright, DWRITE_TEXT_ALIGNMENT_LEADING, Trim::Middle);

        if (state.hover)
        {
            theme.text(context, peerSummary(peer), kSmall, detail, colors.foreground);
        }
        else
        {
            TextBuffer buffer;
            std::wstring text(peer.isSeed ? std::wstring_view(L"Seed") : percentText(peer.progress, 1, buffer));
            if (!peer.client.empty())
            {
                text += L"  ·  ";
                text += peer.client;
            }
            theme.text(context, text, kSmall, detail, colors.foreground);
        }

        if (peer.isEncrypted)
        {
            float const midY = (rect.top + rect.bottom) / 2.0F;
            theme.icon(context, L"Lock", D2D1::RectF(lockRight - kLockSize, midY - kLockSize / 2.0F, lockRight, midY + kLockSize / 2.0F), colors.foreground);
        }
        paintRates(context, rect, peer.isDownloadingFrom, peer.downBytesPerSecond, peer.isUploadingTo, peer.upBytesPerSecond);

        // How much of the transfer this peer has, as a thin line along the bottom of its row.
        float const right = rect.right - kMargin;
        D2D1_RECT_F const line = D2D1::RectF(left, rect.bottom - 1.0F, right, rect.bottom);
        context->FillRectangle(line, theme.brush(colors.border));
        D2D1_RECT_F const filled = D2D1::RectF(left, line.top, left + (right - left) * peer.progress, line.bottom);
        context->FillRectangle(filled, theme.brush(withAlpha(peer.isSeed ? colors.seed : colors.accent, kLineAlpha)));
    }

    engine::TransferId id_;
    std::vector<Stat*> stats_;
    Label* sources_ = nullptr;
    PeerList* list_ = nullptr;
    std::vector<engine::PeerInfo> peers_;
    std::vector<engine::PeerInfo> previousPeers_;
    std::vector<engine::WebseedInfo> seeds_;
    std::vector<engine::WebseedInfo> previousSeeds_;
};

} // namespace

std::unique_ptr<TabView> createPeersTab(engine::TransferId id)
{
    return std::make_unique<PeersTab>(id);
}

} // namespace app::details
