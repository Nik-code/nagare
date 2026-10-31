// This file Copyright © Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

@interface QBittorrentEntry : NSObject
@property(nonatomic, copy) NSString* torrentPath;
@property(nonatomic, copy) NSString* savePath;
@end

/// Reads what qBittorrent has on file. It only ever reads: qBittorrent's own folder is left as it is.
@interface QBittorrentImporter : NSObject

+ (NSArray<QBittorrentEntry*>*)entries;

@end

NS_ASSUME_NONNULL_END
