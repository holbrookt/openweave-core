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
 *      This file implements NLWDMClient interface
 *
 */

#import <Foundation/Foundation.h>
#import "NLWeaveStack.h"
#import "NLLogging.h"

#include <Weave/Core/WeaveError.h>
#include <Weave/Support/CodeUtils.h>
#include <WeaveDeviceManager.h>

#import "NLWeaveDeviceManager_Protected.h"
#import "NLProfileStatusError.h"
#import "NLWeaveError_Protected.h"

#include <Weave/Core/WeaveCore.h>
#include <Weave/Profiles/WeaveProfiles.h>
#include <Weave/Profiles/common/CommonProfile.h>
#include <Weave/Profiles/data-management/Current/WdmManagedNamespace.h>
#include <WeaveDataManagementClient.h>
#include <Weave/Support/NLDLLUtil.h>
#include <Weave/Core/WeaveCore.h>
#include <Weave/Profiles/data-management/DataManagement.h>
#include <Weave/Profiles/data-management/SubscriptionClient.h>

#import "NLWDMClient_Protected.h"
#import "NLGenericTraitUpdatableDataSink.h"
#import "NLGenericTraitUpdatableDataSink_Protected.h"
#import "NLResourceIdentifier.h"
#import "NLResourceIdentifier_Protected.h"

using namespace nl::Weave::Profiles;
using namespace nl::Weave::Profiles::DataManagement;

static void bindingEventCallback (void * const apAppState, const nl::Weave::Binding::EventType aEvent,
                                  const nl::Weave::Binding::InEventParam & aInParam, nl::Weave::Binding::OutEventParam & aOutParam);

void bindingEventCallback (void * const apAppState, const nl::Weave::Binding::EventType aEvent,
                                  const nl::Weave::Binding::InEventParam & aInParam, nl::Weave::Binding::OutEventParam & aOutParam)
{
    WEAVE_ERROR err = WEAVE_NO_ERROR;

    WDM_LOG_DEBUG(@"%s: Event(%d)", __func__, aEvent);

    switch (aEvent)
    {
        case nl::Weave::Binding::kEvent_PrepareRequested:
            WDM_LOG_DEBUG(@"kEvent_PrepareRequested");
            aOutParam.PrepareRequested.PrepareError = err;
            SuccessOrExit(err);
            break;

        case nl::Weave::Binding::kEvent_PrepareFailed:
            err = aInParam.PrepareFailed.Reason;
            WDM_LOG_DEBUG(@"kEvent_PrepareFailed: reason %d", err);
            break;

        case nl::Weave::Binding::kEvent_BindingFailed:
            err = aInParam.BindingFailed.Reason;
            WDM_LOG_DEBUG(@"kEvent_BindingFailed: reason %d", err);
            break;

        case nl::Weave::Binding::kEvent_BindingReady:
            WDM_LOG_DEBUG(@"kEvent_BindingReady");
            break;

        case nl::Weave::Binding::kEvent_DefaultCheck:
            WDM_LOG_DEBUG(@"kEvent_DefaultCheck");

        default:
            nl::Weave::Binding::DefaultEventHandler(apAppState, aEvent, aInParam, aOutParam);
    }

exit:
    if (err != WEAVE_NO_ERROR)
    {
        WDM_LOG_DEBUG(@"error in BindingEventCallback");
    }
}

@interface NLWDMClient () {
    nl::Weave::DeviceManager::WDMClient * _mWeaveCppWDMClient;
    dispatch_queue_t _mWeaveWorkQueue;
    dispatch_queue_t _mAppCallbackQueue;

    WDMClientCompletionBlock _mCompletionHandler;
    WDMClientFailureBlock _mFailureHandler;
    NSString * _mRequestName;
    NSMutableDictionary * _mTraitMap;
}

- (NSString *)getCurrentRequest;
@end

@implementation NLWDMClient

@synthesize resultCallbackQueue = _mAppCallbackQueue;

/**
 @note
 This function can only be called by the ARC runtime
 */
- (void)dealloc
{
    // This method can only be called by ARC
    // Let's not rely on this unpredictable mechanism for de-initialization
    // application shall call ShutdownStack if it want to cleanly destroy everything before application termination
    WDM_LOG_METHOD_SIG();

    [self markTransactionCompleted];

    _mRequestName = @"dealloc-Shutdown";

    // we need to force the queue to be Weave work queue, as dealloc could be
    // called at random queues (most probably from UI, when the app de-reference this wdmClient)
    dispatch_sync(_mWeaveWorkQueue, ^() {
        [self shutdown_Internal];
    });
}

- (instancetype)init:(NSString *)name
      weaveWorkQueue:(dispatch_queue_t)weaveWorkQueue
    appCallbackQueue:(dispatch_queue_t)appCallbackQueue
         exchangeMgr:(nl::Weave::WeaveExchangeManager *)exchangeMgr
         messageLayer:(nl::Weave::WeaveMessageLayer *)messageLayer
         nlWeaveDeviceManager:(NLWeaveDeviceManager *)deviceMgr
{
    WEAVE_ERROR err = WEAVE_NO_ERROR;
    long long deviceMgrCppPtr = 0;
    nl::Weave::Binding * pBinding = NULL;
    id result = nil;
    WDM_LOG_METHOD_SIG();

    self = [super init];
    VerifyOrExit(nil != self, err = WEAVE_ERROR_NO_MEMORY);

    _mWeaveWorkQueue = weaveWorkQueue;
    _mAppCallbackQueue = appCallbackQueue;

    _name = name;

    WDM_LOG_DEBUG(@"NewWDMClient() called");

    [deviceMgr GetDeviceMgrPtr:&deviceMgrCppPtr];
    pBinding = exchangeMgr->NewBinding(bindingEventCallback, (nl::Weave::DeviceManager::WeaveDeviceManager*)deviceMgrCppPtr);
    VerifyOrExit(NULL != pBinding, err = WEAVE_ERROR_NO_MEMORY);

    err = ((nl::Weave::DeviceManager::WeaveDeviceManager*)deviceMgrCppPtr)->ConfigureBinding(pBinding);
    SuccessOrExit(err);

    _mWeaveCppWDMClient = new nl::Weave::DeviceManager::WDMClient();
    VerifyOrExit(NULL != _mWeaveCppWDMClient, err = WEAVE_ERROR_NO_MEMORY);

    err = _mWeaveCppWDMClient->Init(messageLayer, pBinding);
    SuccessOrExit(err);

    _mWeaveCppWDMClient->mpAppState = (__bridge void *) self;

    _mTraitMap = [[NSMutableDictionary alloc] init];
    [self markTransactionCompleted];

exit:
    if (WEAVE_NO_ERROR == err) {
        result = self;
    } else {
        WDM_LOG_ERROR(@"Error in init : (%d) %@\n", err, [NSString stringWithUTF8String:nl::ErrorStr(err)]);
        [self shutdown_Internal];
        if (NULL != pBinding)
        {
            pBinding->Release();
        }
    }

    return result;
}

static void handleWDMClientComplete(void * wdmClient, void * reqState)
{
    WDM_LOG_DEBUG(@"handleWDMClientComplete");

    NLWDMClient * client = (__bridge NLWDMClient *) reqState;
    [client dispatchAsyncCompletionBlock:nil];
}

static void handleWDMClientError(void * wdmClient, void * appReqState, WEAVE_ERROR code,
    nl::Weave::DeviceManager::DeviceStatus * devStatus)
{
    WDM_LOG_DEBUG(@"handleWDMClientError");

    NSError * error = nil;
    NSDictionary * userInfo = nil;

    NLWDMClient * client = (__bridge NLWDMClient *) appReqState;

    WDM_LOG_DEBUG(@"%@: Received error response to request %@, wdmClientErr = %d, devStatus = %p\n", client.name,
        [client getCurrentRequest], code, devStatus);

    NLWeaveRequestError requestError;
    if (devStatus != NULL && code == WEAVE_ERROR_STATUS_REPORT_RECEIVED) {
        NLProfileStatusError * statusError = [[NLProfileStatusError alloc]
            initWithProfileId:devStatus->StatusProfileId
                   statusCode:devStatus->StatusCode
                    errorCode:devStatus->SystemErrorCode
                 statusReport:[client statusReportToString:devStatus->StatusProfileId statusCode:devStatus->StatusCode]];
        requestError = NLWeaveRequestError_ProfileStatusError;
        userInfo = @{ @"WeaveRequestErrorType" : @(requestError), @"errorInfo" : statusError };

        WDM_LOG_DEBUG(@"%@: status error: %@", client.name, userInfo);
    } else {
        NLWeaveError * weaveError = [[NLWeaveError alloc] initWithWeaveError:code
                                                                      report:[NSString stringWithUTF8String:nl::ErrorStr(code)]];
        requestError = NLWeaveRequestError_WeaveError;
        userInfo = @{ @"WeaveRequestErrorType" : @(requestError), @"errorInfo" : weaveError };
    }

    error = [NSError errorWithDomain:@"com.nest.error" code:code userInfo:userInfo];

    [client dispatchAsyncDefaultFailureBlock:error];
}

- (void)markTransactionCompleted
{
    _mRequestName = nil;
    _mCompletionHandler = nil;
    _mFailureHandler = nil;
}

- (NSString *)getCurrentRequest
{
    return _mRequestName;
}

- (void)dispatchAsyncFailureBlock:(WEAVE_ERROR)code taskName:(NSString *)taskName handler:(WDMClientFailureBlock)handler
{
    NSError * error =
        [NSError errorWithDomain:@"com.nest.error"
                            code:code
                        userInfo:[NSDictionary dictionaryWithObjectsAndKeys:[NSString stringWithUTF8String:nl::ErrorStr(code)],
                                               @"error", nil]];
    [self dispatchAsyncFailureBlockWithError:error taskName:taskName handler:handler];
}

- (void)dispatchAsyncFailureBlockWithError:(NSError *)error taskName:(NSString *)taskName handler:(WDMClientFailureBlock)handler
{
    if (NULL != handler) {
        // we use async because we don't need to wait for completion of this final completion report
        dispatch_async(_mAppCallbackQueue, ^() {
            WDM_LOG_DEBUG(@"%@: Calling failure handler for %@", _name, taskName);
            handler(_owner, error);
        });
    } else {
        WDM_LOG_DEBUG(@"%@: Skipping failure handler for %@", _name, taskName);
    }
}

- (void)dispatchAsyncDefaultFailureBlockWithCode:(WEAVE_ERROR)code
{
    NSError * error = [NSError errorWithDomain:@"com.nest.error" code:code userInfo:nil];
    [self dispatchAsyncDefaultFailureBlock:error];
}

- (void)dispatchAsyncDefaultFailureBlock:(NSError *)error
{
    NSString * taskName = _mRequestName;
    WDMClientFailureBlock failureHandler = _mFailureHandler;

    [self markTransactionCompleted];
    [self dispatchAsyncFailureBlockWithError:error taskName:taskName handler:failureHandler];
}

- (void)dispatchAsyncCompletionBlock:(id)data
{
    WDMClientCompletionBlock completionHandler = _mCompletionHandler;

    [self markTransactionCompleted];

    if (nil != completionHandler) {
        dispatch_async(_mAppCallbackQueue, ^() {
            completionHandler(_owner, data);
        });

    }
}

- (void)dispatchAsyncResponseBlock:(id)data
{
    WDMClientCompletionBlock completionHandler = _mCompletionHandler;

    if (nil != completionHandler) {
        dispatch_async(_mAppCallbackQueue, ^() {
            completionHandler(_owner, data);
        });
    }
}

- (NSString *)toErrorString:(WEAVE_ERROR)err
{
    WDM_LOG_METHOD_SIG();

    __block NSString * msg = nil;

    dispatch_sync(_mWeaveWorkQueue, ^() {
        msg = [NSString stringWithUTF8String:nl::ErrorStr(err)];
    });

    return msg;
}

- (NSString *)statusReportToString:(NSUInteger)profileId statusCode:(NSInteger)statusCode
{
    WDM_LOG_METHOD_SIG();

    NSString * report = nil;

    const char * result = nl::StatusReportStr((uint32_t) profileId, statusCode);

    if (result) {
        report = [NSString stringWithUTF8String:result];
    }

    return report;
}

- (void)shutdown_Internal
{
    WDM_LOG_METHOD_SIG();

    NSArray * keys =[_mTraitMap allKeys];
 
    for(NSString * key in keys)
    {
       NLGenericTraitUpdatableDataSink * pDataSink =[_mTraitMap objectForKey:key];
       NSLog(@"key=%@, pDataSink=%@",key, pDataSink);
       if ((NSNull *)pDataSink != [NSNull null])
       {
           [pDataSink close];
       }
    }
    [_mTraitMap removeAllObjects];
    _mTraitMap = nil;

    // there is no need to release Objective C objects, as we have ARC for them
    if (_mWeaveCppWDMClient) {
        WDM_LOG_ERROR(@"Shutdown C++ Weave WDMClient");

        _mWeaveCppWDMClient->Close();

        delete _mWeaveCppWDMClient;
        _mWeaveCppWDMClient = NULL;
    }

    [self dispatchAsyncCompletionBlock:nil];
}

- (void)close:(WDMClientCompletionBlock)completionHandler
{
    WDM_LOG_METHOD_SIG();

    dispatch_sync(_mWeaveWorkQueue, ^() {
        if (nil != _mRequestName) {
            WDM_LOG_ERROR(@"%@: Forcefully close while we're still executing %@, continue close", _name, _mRequestName);
        }

        [self markTransactionCompleted];

        _mCompletionHandler = [completionHandler copy];
        _mRequestName = @"close";
        [self shutdown_Internal];
    });
}

- (void) removeDataSinkRef:(long long)traitInstancePtr;
{
    NSString *address = [NSString stringWithFormat:@"%lld", traitInstancePtr];

    if ([_mTraitMap objectForKey:address] != nil) 
    {
        _mTraitMap[address] = [NSNull null];
    }
}

- (NLGenericTraitUpdatableDataSink *)newDataSinkResourceIdentifier:(NLResourceIdentifier *)nlResourceIdentifier
                                                         profileId: (uint32_t)profileId
                                                        instanceId: (uint64_t)instanceId
                                                              path: (NSString *)path;
{
    __block WEAVE_ERROR err = WEAVE_NO_ERROR;

    __block nl::Weave::DeviceManager::GenericTraitUpdatableDataSink * pDataSink = NULL;
    WDM_LOG_METHOD_SIG();

    //VerifyOrExit(NULL != _mWeaveCppWDMClient, err = WEAVE_ERROR_INCORRECT_STATE);

    dispatch_sync(_mWeaveWorkQueue, ^() {
        ResourceIdentifier resId = [nlResourceIdentifier toResourceIdentifier];
        err = _mWeaveCppWDMClient->NewDataSink(resId, profileId, instanceId, [path UTF8String], pDataSink);
    });
 
     if (err != WEAVE_NO_ERROR || pDataSink == NULL)
    {
        WDM_LOG_ERROR(@"pDataSink is not ready");
        return nil;
    }

    NSString *address = [NSString stringWithFormat:@"%lld", (long long)pDataSink];

    if ([_mTraitMap objectForKey:address] == nil) 
    {
        WDM_LOG_DEBUG(@"creating new NLGenericTraitUpdatableDataSink with profild id %d", profileId);
        NLGenericTraitUpdatableDataSink * pTrait = [[NLGenericTraitUpdatableDataSink alloc] init:_name
                                                                                            weaveWorkQueue:_mWeaveWorkQueue
                                                                                            appCallbackQueue:_mAppCallbackQueue
                                                                                            genericTraitUpdatableDataSinkPtr: pDataSink
                                                                                            nlWDMClient:self];
        [_mTraitMap setObject:pTrait forKey:address];
    }

    return [_mTraitMap objectForKey:address];
}

- (void)flushUpdate:(WDMClientCompletionBlock)completionHandler
            failure:(WDMClientFailureBlock)failureHandler
{
    WDM_LOG_METHOD_SIG();

    NSString * taskName = @"FlushUpdate";

    // we use async for the results are sent back to caller via async means also
    dispatch_async(_mWeaveWorkQueue, ^() {
        if (nil == _mRequestName) {
            _mRequestName = taskName;
            _mCompletionHandler = [completionHandler copy];
            _mFailureHandler = [failureHandler copy];

            WEAVE_ERROR err = _mWeaveCppWDMClient->FlushUpdate((__bridge void *) self, handleWDMClientComplete, handleWDMClientError);

            if (WEAVE_NO_ERROR != err) {
                [self dispatchAsyncDefaultFailureBlockWithCode:err];
            }
        } else {
            WDM_LOG_ERROR(@"%@: Attemp to %@ while we're still executing %@, ignore", _name, taskName, _mRequestName);

            // do not change _mRequestName, as we're rejecting this request
            [self dispatchAsyncFailureBlock:WEAVE_ERROR_INCORRECT_STATE taskName:taskName handler:failureHandler];
        }
    });
}

- (void)refreshData:(WDMClientCompletionBlock)completionHandler
            failure:(WDMClientFailureBlock)failureHandler
{
    WDM_LOG_METHOD_SIG();

    NSString * taskName = @"RefreshData";

    // we use async for the results are sent back to caller via async means also
    dispatch_async(_mWeaveWorkQueue, ^() {
        if (nil == _mRequestName) {
            _mRequestName = taskName;
            _mCompletionHandler = [completionHandler copy];
            _mFailureHandler = [failureHandler copy];

            WEAVE_ERROR err = _mWeaveCppWDMClient->RefreshData((__bridge void *) self, handleWDMClientComplete, handleWDMClientError, NULL);

            if (WEAVE_NO_ERROR != err) {
                [self dispatchAsyncDefaultFailureBlockWithCode:err];
            }
        } else {
            WDM_LOG_ERROR(@"%@: Attemp to %@ while we're still executing %@, ignore", _name, taskName, _mRequestName);

            // do not change _mRequestName, as we're rejecting this request
            [self dispatchAsyncFailureBlock:WEAVE_ERROR_INCORRECT_STATE taskName:taskName handler:failureHandler];
        }
    });
}

@end
