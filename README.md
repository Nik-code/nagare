# Nagare

Nagare (流れ, "flow") is a Liquid Glass torrent client for macOS. It is a redesigned fork of [Transmission](https://transmissionbt.com): the engine is Transmission's `libtransmission`, unchanged, and the Mac interface is new.

![Nagare downloading public-domain films](docs/nagare/demo.gif)

Short clips are attached to the [latest release](https://github.com/Nik-code/nagare/releases/latest).

- **Swell header:** the water is a graph. The blue swell is the last ten minutes of download speed and the sand in front of it is upload.
- **Droplet rows:** each dot is a share of the file. While seeding, the dots become a tide that rises with your share ratio and moves only during upload. Seeds and peers sit beside the speed.
- **One Add panel:** torrents and magnet links open the same glass panel. Rename the transfer or any file in place before it starts.
- **Search:** sources are JSON files, so they are data and nothing in them is ever run. Internet Archive is built in, and more go in `~/Library/Application Support/Nagare/Search Sources`.
- **qBittorrent import:** on first launch Nagare reads qBittorrent's records, adds its torrents paused and checks the files where they already are. qBittorrent itself is only read, never changed. Run it again from the File menu.
- **Native:** Liquid Glass, Tabler icons, Outfit, with light paper and dark warm grey themes.

![Nagare in the light theme](docs/nagare/light.png)

<p>
  <img src="docs/nagare/search.png" width="49%" alt="Search in Liquid Glass">
  <img src="docs/nagare/add.png" width="49%" alt="The Add panel">
</p>

<p>
  <img src="docs/nagare/details-overview.png" width="32%" alt="Details overview">
  <img src="docs/nagare/details-files.png" width="32%" alt="Details files">
  <img src="docs/nagare/details-peers.png" width="32%" alt="Details peers">
</p>

Nagare has no auto-updater, so it never replaces itself with anything else.

## Install

Needs an Apple Silicon Mac on macOS 26 or later.

1. Download the disk image from the [latest release](https://github.com/Nik-code/nagare/releases/latest) and drag Nagare to Applications.
2. Open it once. macOS will refuse it, as explained below.
3. Go to System Settings, then Privacy & Security, and choose **Open Anyway**.

Or clear the download flag from Terminal:

```bash
xattr -dr com.apple.quarantine /Applications/Nagare.app
```

> [!IMPORTANT]
> **Nagare is not notarised.** This is a free hobby project without a paid Apple Developer membership, so releases are signed ad hoc and Apple has not scanned them. macOS will warn you, and you have to choose to open it yourself. If you would rather not trust a prebuilt app, the source is all here and you can [build it yourself](#building).

On first launch Nagare asks macOS to make it the default for magnet links and torrent files. You can say no, and change it later in Settings.

## Building

Needs macOS 26 or later and Xcode 26.

```bash
macosx/build-nagare.sh
```

This builds an arm64 app and installs it to `/Applications/Nagare.app`, signed ad hoc. To make a release disk image instead:

```bash
macosx/package-nagare.sh 1.0.0
```

## Updating from Transmission

The `macosx` folder has diverged a lot, but `libtransmission` is untouched. Upstream engine and security fixes should merge cleanly; Mac interface changes will not.

## How it was made

Designed and built with [Claude](https://claude.com/claude-code), which wrote most of the code under my direction.

## Licence

GPL, like Transmission. See [COPYING](COPYING). Built on Transmission by its authors and contributors, and not affiliated with or endorsed by the Transmission project. Please report Nagare problems here, not to them. Icons are [Tabler](https://tabler.io/icons) (MIT). Outfit is under the SIL Open Font License, included in `macosx/Fonts`.

Use it for things you have the right to share.
