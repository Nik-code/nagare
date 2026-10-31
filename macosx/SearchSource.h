// This file Copyright © Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

@interface SearchResult : NSObject

@property(nonatomic, copy) NSString* title;
@property(nonatomic, copy) NSString* link;
@property(nonatomic, copy) NSString* sourceName;
@property(nonatomic) uint64_t size;
/// -1 when the source doesn't report it.
@property(nonatomic) NSInteger seeds;

@end

/// One place to search, described by data rather than code: a source is a dictionary
/// (a JSON file for user sources) that says which URL to call and how to read the answer.
/// Nothing from a source is ever executed.
@interface SearchSource : NSObject

@property(nonatomic, readonly, copy) NSString* name;

/// Built-in sources, then every valid JSON file in the sources folder.
+ (NSArray<SearchSource*>*)allSources;

/// ~/Library/Application Support/Nagare/Search Sources, created on first use.
+ (NSURL*)sourcesFolder;

/// nil when the definition is incomplete or its URL isn't https.
- (nullable instancetype)initWithDefinition:(NSDictionary*)definition;

- (nullable NSURL*)URLForQuery:(NSString*)query;

/// Results with an unsafe or missing link are dropped.
- (NSArray<SearchResult*>*)resultsFromData:(NSData*)data error:(NSError**)error;

@end

NS_ASSUME_NONNULL_END
