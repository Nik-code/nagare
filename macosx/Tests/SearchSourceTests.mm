// This file Copyright © Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

// Sources are untrusted input, so the rules that keep them harmless get tested directly:
//   xcrun clang++ -fobjc-arc -std=c++20 -framework Foundation -Imacosx macosx/SearchSource.mm macosx/Tests/SearchSourceTests.mm -o /tmp/search-tests && /tmp/search-tests

#import <Foundation/Foundation.h>

#import "SearchSource.h"

static int failures = 0;

static void check(BOOL condition, char const* name)
{
    printf("%s %s\n", condition ? "ok  " : "FAIL", name);
    failures += condition ? 0 : 1;
}

static NSData* json(id object)
{
    return [NSJSONSerialization dataWithJSONObject:object options:0 error:nil];
}

int main()
{
    @autoreleasepool
    {
        NSDictionary* base = @{ @"name" : @"Test", @"url" : @"https://example.com/s?q={query}", @"results" : @"data.items", @"link" : @"{link}", @"title" : @"{title}" };

        NSMutableDictionary* insecure = [base mutableCopy];
        insecure[@"url"] = @"http://example.com/s?q={query}";
        check([[SearchSource alloc] initWithDefinition:insecure] == nil, "rejects a source over plain http");

        NSMutableDictionary* noQuery = [base mutableCopy];
        noQuery[@"url"] = @"https://example.com/fixed";
        check([[SearchSource alloc] initWithDefinition:noQuery] == nil, "rejects a source that can't take a query");

        SearchSource* source = [[SearchSource alloc] initWithDefinition:base];
        check(source != nil, "accepts a complete source");
        check([[source URLForQuery:@"a b&c=d"].absoluteString isEqualToString:@"https://example.com/s?q=a%20b%26c%3Dd"], "encodes the query");

        NSArray<SearchResult*>* links = [source resultsFromData:json(@{ @"data" : @{ @"items" : @[
            @{ @"title" : @"file", @"link" : @"file:///etc/passwd" },
            @{ @"title" : @"script", @"link" : @"javascript:alert(1)" },
            @{ @"title" : @"http", @"link" : @"http://example.com/a.torrent" },
            @{ @"title" : @"magnet", @"link" : @"magnet:?xt=urn:btih:abc" },
            @{ @"title" : @"https", @"link" : @"https://example.com/a.torrent" },
            @{ @"title" : @"missing" },
            @"not a dictionary",
        ] } }) error:nil];
        check(links.count == 2 && [links[0].title isEqualToString:@"magnet"] && [links[1].title isEqualToString:@"https"], "keeps only magnet and https links");

        NSDictionary* nested = @{
            @"name" : @"Nested",
            @"url" : @"https://example.com/s?q={query}",
            @"results" : @"data.movies",
            @"items" : @"torrents",
            @"title" : @"{title} {quality}",
            @"size" : @"size",
            @"seeds" : @"seeds",
            @"link" : @"magnet:?xt=urn:btih:{hash}&dn={title}",
        };
        NSArray<SearchResult*>* movies = [[[SearchSource alloc] initWithDefinition:nested] resultsFromData:json(@{ @"data" : @{ @"movies" : @[
            @{ @"title" : @"A & B", @"torrents" : @[ @{ @"quality" : @"1080p", @"hash" : @"ff", @"size" : @"1.5 GB", @"seeds" : @42 } ] },
        ] } }) error:nil];
        check(movies.count == 1, "reads results nested inside results");
        check([movies.firstObject.title isEqualToString:@"A & B 1080p"], "titles take fields from item and parent");
        check([movies.firstObject.link isEqualToString:@"magnet:?xt=urn:btih:ff&dn=A%20%26%20B"], "encodes values inside a link");
        check(movies.firstObject.size == 1500000000ULL && movies.firstObject.seeds == 42, "parses size text and seeds");

        check([source resultsFromData:[@"not json" dataUsingEncoding:NSUTF8StringEncoding] error:nil].count == 0, "survives invalid JSON");
        check([source resultsFromData:[NSMutableData dataWithLength:9 * 1024 * 1024] error:nil].count == 0, "refuses oversized responses");
    }
    printf("%d failures\n", failures);
    return failures == 0 ? 0 : 1;
}
