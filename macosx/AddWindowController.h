// This file Copyright © Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import <AppKit/AppKit.h>

@class Controller;
@class Torrent;

/// The glass panel that adds a transfer, from a .torrent file or a magnet link.
@interface AddWindowController : NSWindowController

- (instancetype)initWithTorrent:(Torrent*)torrent
                          destination:(NSString*)path
                      lockDestination:(BOOL)lockDestination
                           controller:(Controller*)controller
                          torrentFile:(NSString*)torrentFile
    deleteTorrentCheckEnableInitially:(BOOL)deleteTorrent
                      canToggleDelete:(BOOL)canToggleDelete;

- (instancetype)initWithMagnet:(Torrent*)torrent destination:(NSString*)path controller:(Controller*)controller;

@property(nonatomic, readonly) Torrent* torrent;

@end
