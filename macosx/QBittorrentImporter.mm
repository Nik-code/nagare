// This file Copyright © Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import "QBittorrentImporter.h"

static NSInteger const kMaxDepth = 32;

/// Walks one bencoded value, returning the offset just past it, or 0 if it's malformed.
/// `strings` collects the top level's string values.
static size_t skipValue(uint8_t const* bytes, size_t size, size_t offset, NSInteger depth, NSMutableDictionary<NSString*, NSData*>* strings)
{
    if (offset >= size || depth > kMaxDepth)
    {
        return 0;
    }

    uint8_t const type = bytes[offset];
    if (type == 'i')
    {
        while (++offset < size)
        {
            if (bytes[offset] == 'e')
            {
                return offset + 1;
            }
        }
        return 0;
    }

    if (type == 'l' || type == 'd')
    {
        BOOL const dictionary = type == 'd';
        ++offset;
        while (offset < size && bytes[offset] != 'e')
        {
            NSString* key;
            if (dictionary)
            {
                size_t const keyStart = offset;
                offset = skipValue(bytes, size, offset, depth + 1, nil);
                if (offset == 0)
                {
                    return 0;
                }
                NSData* raw = [NSData dataWithBytes:bytes + keyStart length:offset - keyStart];
                NSString* text = [[NSString alloc] initWithData:raw encoding:NSUTF8StringEncoding];
                NSRange const colon = [text rangeOfString:@":"];
                key = colon.location != NSNotFound ? [text substringFromIndex:NSMaxRange(colon)] : nil;
            }

            size_t const valueStart = offset;
            offset = skipValue(bytes, size, offset, depth + 1, nil);
            if (offset == 0)
            {
                return 0;
            }

            if (dictionary && strings != nil && depth == 0 && key != nil && bytes[valueStart] >= '0' && bytes[valueStart] <= '9')
            {
                size_t colon = valueStart;
                while (colon < offset && bytes[colon] != ':')
                {
                    ++colon;
                }
                strings[key] = [NSData dataWithBytes:bytes + colon + 1 length:offset - colon - 1];
            }
        }
        return offset < size ? offset + 1 : 0;
    }

    size_t length = 0;
    while (offset < size && bytes[offset] >= '0' && bytes[offset] <= '9')
    {
        if (length > size)
        {
            return 0;
        }
        length = length * 10 + (bytes[offset++] - '0');
    }
    if (offset >= size || bytes[offset] != ':' || length > size - offset - 1)
    {
        return 0;
    }
    return offset + 1 + length;
}

@implementation QBittorrentEntry
@end

@implementation QBittorrentImporter

+ (NSArray<QBittorrentEntry*>*)entries
{
    NSString* support = NSSearchPathForDirectoriesInDomains(NSApplicationSupportDirectory, NSUserDomainMask, YES).firstObject;
    NSString* folder = [support stringByAppendingPathComponent:@"qBittorrent/BT_backup"];
    NSFileManager* manager = NSFileManager.defaultManager;

    NSMutableArray<QBittorrentEntry*>* entries = [NSMutableArray array];
    for (NSString* file in [manager contentsOfDirectoryAtPath:folder error:nil])
    {
        if (![file.pathExtension isEqualToString:@"fastresume"])
        {
            continue;
        }

        // A magnet that never got its metadata has no .torrent, and there's nothing to carry over.
        NSString* torrentPath = [folder stringByAppendingPathComponent:[file.stringByDeletingPathExtension stringByAppendingPathExtension:@"torrent"]];
        NSData* data = [NSData dataWithContentsOfFile:[folder stringByAppendingPathComponent:file]];
        if (data == nil || ![manager fileExistsAtPath:torrentPath])
        {
            continue;
        }

        NSMutableDictionary<NSString*, NSData*>* strings = [NSMutableDictionary dictionary];
        if (skipValue(static_cast<uint8_t const*>(data.bytes), data.length, 0, 0, strings) == 0)
        {
            continue;
        }

        NSData* save = strings[@"qBt-savePath"].length > 0 ? strings[@"qBt-savePath"] : strings[@"save_path"];
        NSString* savePath = save != nil ? [[NSString alloc] initWithData:save encoding:NSUTF8StringEncoding] : nil;
        if (savePath.length == 0)
        {
            continue;
        }

        QBittorrentEntry* entry = [[QBittorrentEntry alloc] init];
        entry.torrentPath = torrentPath;
        entry.savePath = savePath.stringByExpandingTildeInPath;
        [entries addObject:entry];
    }
    return entries;
}

@end
