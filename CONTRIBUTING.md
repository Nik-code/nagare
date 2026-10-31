# Contributing to Nagare

Thanks for thinking about contributing. Nagare is a small project, so these are guidelines rather than rules.

## Before you start

Nagare is the macOS interface in `macosx`, built on Transmission's `libtransmission`.

- **Interface and Mac bugs** belong here. [File an issue](https://github.com/Nik-code/nagare/issues/new/choose) or open a pull request.
- **Engine bugs** in downloading, peers or trackers usually belong to [Transmission](https://github.com/transmission/transmission). If you aren't sure, file an issue here first. Please don't report Nagare problems to the Transmission project.

For anything bigger than a small fix, open an issue first so we can agree on the approach.

## Building

You need macOS 26 or later and Xcode 26.

```bash
macosx/build-nagare.sh
```

This builds an arm64 app and installs it to `/Applications/Nagare.app`. New source files need adding to both `Transmission.xcodeproj` and `macosx/CMakeLists.txt`.

## Style

- Match the code around your change.
- Keep the interface native: Liquid Glass, Tabler icons, and the colors and helpers in `macosx/Theme.h`.
- Build new views in code rather than adding nibs.
- Try not to change `libtransmission`, so upstream fixes keep merging cleanly.
- Run `./code_style.sh` on your code and fix any warnings.
- Use public domain content in screenshots, tests and examples.

## Licence

Contributions are made under the same terms as the rest of the project. See [COPYING](COPYING).
