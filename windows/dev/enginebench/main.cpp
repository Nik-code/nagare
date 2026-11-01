// A console libtransmission with no UI, so engine settings and build flags can be compared on their own.
// It prints one CSV line a second, then the time the download took.
//
//   nagare_enginebench --config <dir> --dir <dir> --add <magnet or .torrent> [--seconds 300] [--seed]
//                      [--cache 4] [--global 200] [--per-torrent 60] [--port 51499]

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>
#include <thread>

#include <libtransmission/transmission.h>

#include <libtransmission/error.h>
#include <libtransmission/log.h>
#include <libtransmission/utils.h>
#include <libtransmission/variant.h>

namespace
{

struct Options
{
    std::string config;
    std::string dir;
    std::string add;
    int seconds = 300;
    int cacheMib = 4;
    int globalPeers = 200;
    int perTorrent = 60;
    int port = 51499;
    bool seed = false;
    bool noDht = false;
    bool noUtp = false;
};

Options parse(int argc, char** argv)
{
    auto options = Options{};
    for (int i = 1; i < argc; ++i)
    {
        auto const arg = std::string_view{ argv[i] };
        auto const next = [&] { return i + 1 < argc ? argv[++i] : ""; };
        if (arg == "--config")
        {
            options.config = next();
        }
        else if (arg == "--dir")
        {
            options.dir = next();
        }
        else if (arg == "--add")
        {
            options.add = next();
        }
        else if (arg == "--seconds")
        {
            options.seconds = std::atoi(next());
        }
        else if (arg == "--cache")
        {
            options.cacheMib = std::atoi(next());
        }
        else if (arg == "--global")
        {
            options.globalPeers = std::atoi(next());
        }
        else if (arg == "--per-torrent")
        {
            options.perTorrent = std::atoi(next());
        }
        else if (arg == "--port")
        {
            options.port = std::atoi(next());
        }
        else if (arg == "--seed")
        {
            options.seed = true;
        }
        else if (arg == "--no-dht")
        {
            options.noDht = true;
        }
        else if (arg == "--no-utp")
        {
            options.noUtp = true;
        }
    }
    return options;
}

} // namespace

int main(int argc, char** argv)
{
    auto const options = parse(argc, argv);
    if (options.config.empty() || options.dir.empty() || options.add.empty())
    {
        std::fputs("needs --config, --dir and --add\n", stderr);
        return 2;
    }

    tr_lib_init();

    auto map = tr_variant::make_map();
    tr_variantDictAddInt(&map, TR_KEY_cache_size_mib, options.cacheMib);
    tr_variantDictAddStr(&map, TR_KEY_download_dir, options.dir);
    tr_variantDictAddInt(&map, TR_KEY_peer_limit_global, options.globalPeers);
    tr_variantDictAddInt(&map, TR_KEY_peer_limit_per_torrent, options.perTorrent);
    tr_variantDictAddInt(&map, TR_KEY_peer_port, options.port);
    tr_variantDictAddBool(&map, TR_KEY_rpc_enabled, false);
    tr_variantDictAddBool(&map, TR_KEY_lpd_enabled, false);
    tr_variantDictAddBool(&map, TR_KEY_dht_enabled, !options.noDht);
    tr_variantDictAddBool(&map, TR_KEY_utp_enabled, !options.noUtp);
    tr_variantDictAddBool(&map, TR_KEY_port_forwarding_enabled, false);
    tr_variantDictAddInt(&map, TR_KEY_message_level, TR_LOG_WARN);

    auto const settings = tr_sessionLoadSettings(options.config, &map);
    auto* const session = tr_sessionInit(options.config, true, settings);

    auto* const ctor = tr_ctorNew(session);
    tr_ctorSetDownloadDir(ctor, TR_FORCE, options.dir.c_str());
    tr_ctorSetPaused(ctor, TR_FORCE, false);
    auto error = tr_error{};
    bool const added = options.add.rfind("magnet:", 0) == 0 ? tr_ctorSetMetainfoFromMagnetLink(ctor, options.add.c_str(), &error) :
                                                               tr_ctorSetMetainfoFromFile(ctor, options.add.c_str(), &error);
    if (!added)
    {
        std::fprintf(stderr, "could not read %s\n", options.add.c_str());
        return 2;
    }
    auto* const torrent = tr_torrentNew(ctor, nullptr);
    tr_ctorFree(ctor);
    if (torrent == nullptr)
    {
        std::fputs("could not add\n", stderr);
        return 2;
    }

    if (options.seed)
    {
        tr_torrentVerify(torrent);
    }

    auto const start = std::chrono::steady_clock::now();
    auto const elapsed = [&] { return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count(); };

    std::puts("t,meta,peers,sending,piece_kbps,have_mb,done,tracker,dht,pex,incoming");
    double metadataAt = -1.0;
    double firstByteAt = -1.0;
    double doneAt = -1.0;
    for (int tick = 0; tick < options.seconds * 2; ++tick)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds{ 500 });
        auto const* const stat = tr_torrentStat(torrent);
        auto const t = elapsed();
        if (metadataAt < 0.0 && stat->metadataPercentComplete >= 1.0F)
        {
            metadataAt = t;
        }
        if (firstByteAt < 0.0 && stat->haveValid + stat->haveUnchecked > 0)
        {
            firstByteAt = t;
        }
        if (tick % 2 == 1)
        {
            std::printf(
                "%.1f,%.2f,%u,%u,%.0f,%.1f,%.3f,%u,%u,%u,%u\n",
                t,
                stat->metadataPercentComplete,
                stat->peersConnected,
                stat->peersSendingToUs,
                stat->pieceDownloadSpeed_KBps,
                static_cast<double>(stat->haveValid) / 1048576.0,
                stat->percentDone,
                stat->peersFrom[TR_PEER_FROM_TRACKER],
                stat->peersFrom[TR_PEER_FROM_DHT],
                stat->peersFrom[TR_PEER_FROM_PEX],
                stat->peersFrom[TR_PEER_FROM_INCOMING]);
            std::fflush(stdout);
        }
        if (!options.seed && stat->percentDone >= 1.0F && stat->metadataPercentComplete >= 1.0F)
        {
            doneAt = t;
            break;
        }
    }

    std::printf("# metadata %.1f s, first byte %.1f s, done %.1f s\n", metadataAt, firstByteAt, doneAt);
    std::fflush(stdout);
    tr_sessionClose(session, 5);
    return doneAt >= 0.0 || options.seed ? 0 : 1;
}
