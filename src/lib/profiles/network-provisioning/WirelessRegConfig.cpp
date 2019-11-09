/*
 *
 *    Copyright (c) 2013-2017 Nest Labs, Inc.
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

#ifndef __STDC_LIMIT_MACROS
#define __STDC_LIMIT_MACROS
#endif
#include <stdlib.h>
#include <stdint.h>

#include <Weave/Core/WeaveCore.h>
#include <Weave/Support/CodeUtils.h>
#include <Weave/Core/WeaveEncoding.h>
#include <Weave/Profiles/WeaveProfiles.h>
#include "WirelessRegConfig.h"

namespace nl {
namespace Weave {
namespace Profiles {
namespace NetworkProvisioning {

using namespace nl::Weave::Encoding;
using namespace nl::Weave::TLV;

const WirelessRegDomain WirelessRegDomain::WorldWide = { '0', '0' };

WEAVE_ERROR WirelessRegConfig::Encode(TLVWriter & writer) const
{
    WEAVE_ERROR err = WEAVE_NO_ERROR;
    TLVType outerContainer;

    err = writer.StartContainer(AnonymousTag, kTLVType_Structure, outerContainer);
    SuccessOrExit(err);

    if (IsRegDomainPresent())
    {
        err = writer.PutString(ContextTag(kTag_WirelessRegConfig_RegulatoryDomain), RegDomain.Code, sizeof(RegDomain.Code));
        SuccessOrExit(err);
    }

    if (IsOpLocationPresent())
    {
        err = writer.Put(ContextTag(kTag_WirelessRegConfig_OperatingLocation), OpLocation);
        SuccessOrExit(err);
    }

    if (NumSupportedRegDomains > 0)
    {
        TLVType outerContainer2;

        err = writer.StartContainer(ContextTag(kTag_WirelessRegConfig_SupportedRegulatoryDomains), kTLVType_Array, outerContainer2);
        SuccessOrExit(err);

        for (uint8_t i = 0; i < NumSupportedRegDomains; i++)
        {
            err = writer.PutString(AnonymousTag, SupportedRegDomains[i].Code, sizeof(SupportedRegDomains[i].Code));
            SuccessOrExit(err);
        }

        err = writer.EndContainer(outerContainer2);
        SuccessOrExit(err);
    }

    err = writer.EndContainer(outerContainer);
    SuccessOrExit(err);

exit:
    return err;
}

WEAVE_ERROR WirelessRegConfig::Decode(TLVReader & reader)
{
    WEAVE_ERROR err = WEAVE_NO_ERROR;
    TLVType outerContainer, outerContainer2;
    uint16_t maxSupportedRegDomains;

    maxSupportedRegDomains = NumSupportedRegDomains;
    NumSupportedRegDomains = 0;

    if (reader.GetType() == kTLVType_NotSpecified)
    {
        err = reader.Next();
        SuccessOrExit(err);
    }

    VerifyOrExit(reader.GetType() == kTLVType_Structure, err = WEAVE_ERROR_WRONG_TLV_TYPE);

    err = reader.EnterContainer(outerContainer);
    SuccessOrExit(err);

    while ((err = reader.Next()) == WEAVE_NO_ERROR)
    {
        uint64_t elemTag = reader.GetTag();

        if (!IsContextTag(elemTag))
            continue;

        switch (TagNumFromTag(elemTag))
        {
        case kTag_WirelessRegConfig_RegulatoryDomain:
            VerifyOrExit(reader.GetType() == kTLVType_UTF8String, err = WEAVE_ERROR_INVALID_TLV_ELEMENT);
            VerifyOrExit(!IsRegDomainPresent(), err = WEAVE_ERROR_INVALID_TLV_ELEMENT);
            VerifyOrExit(reader.GetLength() == sizeof(RegDomain.Code), err = WEAVE_ERROR_INVALID_ARGUMENT);
            err = reader.GetBytes((uint8_t *)RegDomain.Code, sizeof(WirelessRegDomain::Code));
            SuccessOrExit(err);
            break;

        case kTag_WirelessRegConfig_OperatingLocation:
            VerifyOrExit(!IsOpLocationPresent(), err = WEAVE_ERROR_INVALID_TLV_ELEMENT);
            err = reader.Get(OpLocation);
            SuccessOrExit(err);
            VerifyOrExit(OpLocation != 0, err = WEAVE_ERROR_INVALID_ARGUMENT);
            break;

        case kTag_WirelessRegConfig_SupportedRegulatoryDomains:
            VerifyOrExit(reader.GetType() == kTLVType_Array, err = WEAVE_ERROR_INVALID_TLV_ELEMENT);
            VerifyOrExit(NumSupportedRegDomains == 0, err = WEAVE_ERROR_INVALID_TLV_ELEMENT);

            err = reader.EnterContainer(outerContainer2);
            SuccessOrExit(err);

            while ((err = reader.Next()) == WEAVE_NO_ERROR)
            {
                VerifyOrExit(NumSupportedRegDomains < maxSupportedRegDomains, err = WEAVE_ERROR_BUFFER_TOO_SMALL);
                VerifyOrExit(reader.GetType() == kTLVType_UTF8String, err = WEAVE_ERROR_INVALID_TLV_ELEMENT);
                VerifyOrExit(reader.GetLength() == sizeof(RegDomain.Code), err = WEAVE_ERROR_INVALID_ARGUMENT);
                err = reader.GetBytes((uint8_t *)SupportedRegDomains[NumSupportedRegDomains].Code, sizeof(WirelessRegDomain::Code));
                SuccessOrExit(err);
                NumSupportedRegDomains++;
            }

            err = reader.ExitContainer(outerContainer2);
            SuccessOrExit(err);

            break;
        }
    }

    err = reader.ExitContainer(outerContainer);
    SuccessOrExit(err);

exit:
    return err;
}

WEAVE_ERROR WirelessRegConfig::DecodeInPlace(PacketBuffer * buf)
{
    TLVReader reader;

    // Arrange to store the array of supported regulatory domains at the beginning of
    // the packet buffer, overwriting the encoded config data.  Because the encoded size
    // of the array is always larger than the decoded size, writing the array will never
    // disrupt the reading of the encoded config data.
    SupportedRegDomains = reinterpret_cast<WirelessRegDomain *>(buf->Start());
    NumSupportedRegDomains = static_cast<uint16_t>(buf->MaxDataLength() / sizeof(WirelessRegDomain));

    reader.Init(buf);

    return Decode(reader);
}


} // namespace NetworkProvisioning
} // namespace Profiles
} // namespace Weave
} // namespace nl
