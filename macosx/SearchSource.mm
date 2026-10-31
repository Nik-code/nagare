// This file Copyright © Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import "SearchSource.h"

static NSUInteger const kMaxResponseBytes = 8 * 1024 * 1024;
static NSUInteger const kMaxResults = 200;

static NSString* const kReadme = @"Each .json file here adds a search source to Nagare. Sources are plain data: nothing here is ever run.\n"
                                  "\n"
                                  "{\n"
                                  "  \"name\": \"Example\",\n"
                                  "  \"url\": \"https://example.com/api/search?q={query}\",\n"
                                  "  \"results\": \"data.items\",\n"
                                  "  \"items\": \"\",\n"
                                  "  \"title\": \"{title}\",\n"
                                  "  \"size\": \"size_bytes\",\n"
                                  "  \"seeds\": \"seeders\",\n"
                                  "  \"link\": \"magnet:?xt=urn:btih:{hash}&dn={title}\"\n"
                                  "}\n"
                                  "\n"
                                  "url      The address to call, https only. {query} becomes the search text.\n"
                                  "results  Dotted path to the list of results in the JSON answer.\n"
                                  "items    Optional. Path to a list inside each result, when one result has several torrents.\n"
                                  "title    Text for the row. {name} inserts a field, and an item's fields win over its parent's.\n"
                                  "size     Field holding bytes, or text like 1.4 GB. Optional.\n"
                                  "seeds    Field holding the seed count. Optional.\n"
                                  "link     A magnet link or an https .torrent address, built the same way as title.\n";

static id valueAtPath(id object, NSString* path)
{
    id value = object;
    for (NSString* key in [path componentsSeparatedByString:@"."])
    {
        if (key.length == 0)
        {
            continue;
        }
        value = [value isKindOfClass:NSDictionary.class] ? ((NSDictionary*)value)[key] : nil;
    }
    return value;
}

static NSString* stringFor(id value)
{
    if ([value isKindOfClass:NSString.class])
    {
        return value;
    }
    return [value isKindOfClass:NSNumber.class] ? [value stringValue] : nil;
}

static NSString* encoded(NSString* value)
{
    NSMutableCharacterSet* allowed = [NSCharacterSet.URLQueryAllowedCharacterSet mutableCopy];
    [allowed removeCharactersInString:@"&=+?#/:;,@"];
    return [value stringByAddingPercentEncodingWithAllowedCharacters:allowed];
}

/// Replaces each {field} from the item, then its parent. A missing field leaves the result nil.
static NSString* expand(NSString* format, NSDictionary* item, NSDictionary* parent, BOOL encode)
{
    NSRegularExpression* regex = [NSRegularExpression regularExpressionWithPattern:@"\\{([A-Za-z0-9_.]+)\\}" options:0 error:nil];
    NSArray<NSTextCheckingResult*>* matches = [regex matchesInString:format options:0 range:NSMakeRange(0, format.length)];

    // A link that is one whole field is already a finished address.
    BOOL const whole = matches.count == 1 && matches[0].range.length == format.length;

    NSMutableString* output = [format mutableCopy];
    for (NSTextCheckingResult* match in matches.reverseObjectEnumerator)
    {
        NSString* key = [format substringWithRange:[match rangeAtIndex:1]];
        NSString* value = stringFor(valueAtPath(item, key)) ?: stringFor(valueAtPath(parent, key));
        if (value == nil)
        {
            return nil;
        }
        [output replaceCharactersInRange:match.range withString:encode && !whole ? encoded(value) : value];
    }
    return output;
}

static uint64_t parseSize(id value)
{
    if ([value isKindOfClass:NSNumber.class])
    {
        return [value unsignedLongLongValue];
    }
    if (![value isKindOfClass:NSString.class])
    {
        return 0;
    }

    NSScanner* scanner = [NSScanner scannerWithString:value];
    double number = 0;
    if (![scanner scanDouble:&number] || number < 0)
    {
        return 0;
    }
    NSString* unit = [[value substringFromIndex:scanner.scanLocation] stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceCharacterSet].uppercaseString;
    NSDictionary<NSString*, NSNumber*>* scale = @{ @"KB" : @1e3, @"MB" : @1e6, @"GB" : @1e9, @"TB" : @1e12, @"KIB" : @1024.0, @"MIB" : @1048576.0, @"GIB" : @1073741824.0, @"TIB" : @1099511627776.0 };
    return static_cast<uint64_t>(number * (scale[unit].doubleValue ?: 1.0));
}

static BOOL isSafeLink(NSString* link)
{
    NSString* lower = link.lowercaseString;
    if ([lower hasPrefix:@"magnet:?"])
    {
        return YES;
    }
    NSURL* url = [NSURL URLWithString:link];
    return [url.scheme.lowercaseString isEqualToString:@"https"] && url.host.length > 0;
}

@implementation SearchResult
@end

@interface SearchSource ()
@property(nonatomic, copy) NSDictionary* fDefinition;
@end

@implementation SearchSource

+ (NSArray<NSDictionary*>*)builtIn
{
    return @[ @{
        @"name" : @"Internet Archive",
        @"url" : @"https://archive.org/advancedsearch.php?q=title:({query})%20AND%20mediatype:(movies%20OR%20software%20OR%20audio)&fl%5B%5D=identifier&fl%5B%5D=title&fl%5B%5D=item_size&sort%5B%5D=downloads%20desc&rows=50&output=json",
        @"results" : @"response.docs",
        @"title" : @"{title}",
        @"size" : @"item_size",
        @"link" : @"https://archive.org/download/{identifier}/{identifier}_archive.torrent",
    } ];
}

+ (NSURL*)sourcesFolder
{
    NSFileManager* manager = NSFileManager.defaultManager;
    NSURL* support = [manager URLsForDirectory:NSApplicationSupportDirectory inDomains:NSUserDomainMask].firstObject;
    NSURL* folder = [support URLByAppendingPathComponent:@"Nagare/Search Sources" isDirectory:YES];
    if (![manager fileExistsAtPath:folder.path])
    {
        [manager createDirectoryAtURL:folder withIntermediateDirectories:YES attributes:nil error:nil];
        [kReadme writeToURL:[folder URLByAppendingPathComponent:@"README.txt"] atomically:YES encoding:NSUTF8StringEncoding error:nil];
    }
    return folder;
}

+ (NSArray<SearchSource*>*)allSources
{
    NSMutableArray<SearchSource*>* sources = [NSMutableArray array];
    for (NSDictionary* definition in self.builtIn)
    {
        [sources addObject:[[SearchSource alloc] initWithDefinition:definition]];
    }

    NSArray<NSURL*>* files = [NSFileManager.defaultManager contentsOfDirectoryAtURL:self.sourcesFolder includingPropertiesForKeys:nil options:0 error:nil];
    files = [files sortedArrayUsingComparator:^NSComparisonResult(NSURL* a, NSURL* b) {
        return [a.lastPathComponent localizedStandardCompare:b.lastPathComponent];
    }];
    for (NSURL* file in files)
    {
        if (![file.pathExtension.lowercaseString isEqualToString:@"json"])
        {
            continue;
        }
        NSData* data = [NSData dataWithContentsOfURL:file];
        id definition = data != nil ? [NSJSONSerialization JSONObjectWithData:data options:0 error:nil] : nil;
        SearchSource* source = [definition isKindOfClass:NSDictionary.class] ? [[SearchSource alloc] initWithDefinition:definition] : nil;
        if (source != nil)
        {
            [sources addObject:source];
        }
        else
        {
            NSLog(@"Ignoring invalid search source %@", file.lastPathComponent);
        }
    }
    return sources;
}

- (instancetype)initWithDefinition:(NSDictionary*)definition
{
    NSString* name = definition[@"name"];
    NSString* url = definition[@"url"];
    NSString* link = definition[@"link"];
    BOOL const complete = [name isKindOfClass:NSString.class] && name.length > 0 && [url isKindOfClass:NSString.class] &&
        [link isKindOfClass:NSString.class] && [definition[@"results"] isKindOfClass:NSString.class];
    if (!complete || ![url.lowercaseString hasPrefix:@"https://"] || [url rangeOfString:@"{query}"].location == NSNotFound)
    {
        return nil;
    }

    if ((self = [super init]))
    {
        _name = [name copy];
        _fDefinition = [definition copy];
    }
    return self;
}

- (NSURL*)URLForQuery:(NSString*)query
{
    NSString* format = self.fDefinition[@"url"];
    return [NSURL URLWithString:[format stringByReplacingOccurrencesOfString:@"{query}" withString:encoded(query)]];
}

- (NSArray<SearchResult*>*)resultsFromData:(NSData*)data error:(NSError**)error
{
    if (data.length > kMaxResponseBytes)
    {
        return @[];
    }

    id root = [NSJSONSerialization JSONObjectWithData:data options:0 error:error];
    NSDictionary* definition = self.fDefinition;
    NSArray* list = [root isKindOfClass:NSArray.class] && [definition[@"results"] length] == 0 ? root : valueAtPath(root, definition[@"results"]);
    if (![list isKindOfClass:NSArray.class])
    {
        return @[];
    }

    NSString* itemsPath = [definition[@"items"] isKindOfClass:NSString.class] ? definition[@"items"] : @"";
    NSString* titleFormat = [definition[@"title"] isKindOfClass:NSString.class] ? definition[@"title"] : @"{title}";
    NSString* sizeKey = [definition[@"size"] isKindOfClass:NSString.class] ? definition[@"size"] : @"";
    NSString* seedsKey = [definition[@"seeds"] isKindOfClass:NSString.class] ? definition[@"seeds"] : @"";

    NSMutableArray<SearchResult*>* results = [NSMutableArray array];
    for (id entry in list)
    {
        if (![entry isKindOfClass:NSDictionary.class])
        {
            continue;
        }

        NSArray* children = itemsPath.length > 0 ? valueAtPath(entry, itemsPath) : @[ entry ];
        if (![children isKindOfClass:NSArray.class])
        {
            continue;
        }

        for (id child in children)
        {
            if (![child isKindOfClass:NSDictionary.class] || results.count >= kMaxResults)
            {
                continue;
            }

            NSString* title = expand(titleFormat, child, entry, NO);
            NSString* link = expand(definition[@"link"], child, entry, YES);
            if (title.length == 0 || link == nil || !isSafeLink(link))
            {
                continue;
            }

            SearchResult* result = [[SearchResult alloc] init];
            result.title = title;
            result.link = link;
            result.sourceName = self.name;
            id size = sizeKey.length > 0 ? (valueAtPath(child, sizeKey) ?: valueAtPath(entry, sizeKey)) : nil;
            result.size = parseSize(size);
            id seeds = seedsKey.length > 0 ? (valueAtPath(child, seedsKey) ?: valueAtPath(entry, seedsKey)) : nil;
            result.seeds = [seeds respondsToSelector:@selector(integerValue)] ? [seeds integerValue] : -1;
            [results addObject:result];
        }
    }
    return results;
}

@end
