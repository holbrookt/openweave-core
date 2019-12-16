/*
 *
 *    Copyright (c) 2019 Google, LLC.
 *    All rights reserved.
 *
 *    Licensed under the Apache License, Version 2.0 (the "License");
 *    you may not use this file except in compliance with the License.
 *    You may obtain a copy of the License at
 *
 *        http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing, software
 *    distributed under the License is distributed on an "AS IS" BASIS,
 *    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *    See the License for the specific language governing permissions and
 *    limitations under the License.
 */

/**
 *    @file
 *      This file defines NLWDMClient interface
 *
 */

#import <Foundation/Foundation.h>
#import "NLWeaveErrorCodes.h"
#import "NLResourceIdentifier.h"
#import "NLGenericTraitUpdatableDataSink.h"

typedef void (^WDMClientCompletionBlock)(id owner, id data);
typedef void (^WDMClientFailureBlock)(id owner, NSError * error);

@interface NLWDMClient : NSObject
@property (copy, readonly) NSString * name;
@property (readonly) dispatch_queue_t resultCallbackQueue;
@property (weak) id owner;

/**
 *  @brief Disable default initializer inherited from NSObject
 */
- (instancetype)init NS_UNAVAILABLE;

/**
 *  @brief Forcifully release all resources and destroy all references.
 *
 *  There is no way to revive this device manager after this call.
 */
- (void)close:(WDMClientCompletionBlock)completionHandler;


// ----- Error Logging -----
- (NSString *)toErrorString:(WEAVE_ERROR)err;
//-(NSError *)toError:(WEAVE_ERROR)err;

- (NLGenericTraitUpdatableDataSink *)newDataSinkResourceIdentifier:(NLResourceIdentifier *)nlResourceIdentifier
                                                         profileId: (uint32_t)profileId
                                                        instanceId: (uint64_t)instanceId
                                                              path: (NSString *)path;

- (void)flushUpdate:(WDMClientCompletionBlock)completionHandler
            failure:(WDMClientFailureBlock)failureHandler;

- (void)refreshData:(WDMClientCompletionBlock)completionHandler failure:(WDMClientFailureBlock)failureHandler;

- (void)removeDataSinkRef:(long long)traitInstancePtr;

@end
