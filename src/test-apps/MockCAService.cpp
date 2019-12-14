/*
 *
 *    Copyright (c) 2019 Google LLC.
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
 *      This file implements a derived unsolicited responder
 *      (i.e., server) for the Certificate Provisioned protocol of the
 *      Weave Security profile used for the Weave mock
 *      device command line functional testing tool.
 *
 */

#define __STDC_FORMAT_MACROS

#include <inttypes.h>

#include "ToolCommon.h"
#include "MockCAService.h"
#include <Weave/Support/CodeUtils.h>
#include <Weave/Support/NestCerts.h>
#include <Weave/Support/crypto/HashAlgos.h>
#include <Weave/Support/crypto/RSA.h>
#include <Weave/Profiles/WeaveProfiles.h>
#include <Weave/Profiles/common/CommonProfile.h>
#include <Weave/Profiles/security/WeaveSig.h>
#include <Weave/Profiles/security/WeaveCert.h>
#include <Weave/Profiles/security/WeavePrivateKey.h>
#include <Weave/Profiles/status-report/StatusReportProfile.h>

#if WEAVE_WITH_OPENSSL
#include <openssl/x509.h>
#endif

using namespace nl::Weave::Profiles;
using namespace nl::Weave::Profiles::Security;
using namespace nl::Weave::Profiles::Security::CertProvisioning;
using namespace nl::Weave::Encoding;
using namespace nl::Weave::TLV;
using namespace nl::Weave::ASN1;

#if WEAVE_WITH_OPENSSL

static WEAVE_ERROR ValidateX509DeviceCert(X509Cert *certSet, uint8_t certCount)
{
    WEAVE_ERROR err = WEAVE_NO_ERROR;
    BIO *certBuf[kMaxCertCount] = { NULL };
    X509 *cert[kMaxCertCount] = { NULL };
    X509_STORE *store = NULL;
    X509_STORE_CTX *ctx = NULL;
    X509_VERIFY_PARAM *param = NULL;
    int res;

    VerifyOrExit(certSet != NULL && certCount > 0 && certCount <= kMaxCertCount, err = WEAVE_ERROR_INVALID_ARGUMENT);

    store = X509_STORE_new();
    VerifyOrExit(store != NULL, err = WEAVE_ERROR_NO_MEMORY);

    for (int i = 0; i < certCount; i++)
    {
        VerifyOrExit(certSet[i].Cert != NULL && certSet[i].Len > 0, err = WEAVE_ERROR_INVALID_ARGUMENT);

        certBuf[i] = BIO_new_mem_buf(certSet[i].Cert, certSet[i].Len);
        VerifyOrExit(certBuf[i] != NULL, err = WEAVE_ERROR_NO_MEMORY);

        cert[i] = d2i_X509_bio(certBuf[i], NULL);
        VerifyOrExit(cert[i] != NULL, err = WEAVE_ERROR_NO_MEMORY);

        if (i > 0)
        {
            res = X509_STORE_add_cert(store, cert[i]);
            VerifyOrExit(res == 1, err = WEAVE_ERROR_NO_MEMORY);
        }
    }

    ctx = X509_STORE_CTX_new();
    VerifyOrExit(ctx != NULL, err = WEAVE_ERROR_NO_MEMORY);

    param = X509_VERIFY_PARAM_new();
    VerifyOrExit(param != NULL, err = WEAVE_ERROR_NO_MEMORY);

    X509_VERIFY_PARAM_clear_flags(param, X509_V_FLAG_USE_CHECK_TIME);
    X509_STORE_CTX_set0_param(ctx, param);

    res = X509_STORE_CTX_init(ctx, store, cert[0], NULL);
    VerifyOrExit(res == 1, err = WEAVE_ERROR_INVALID_ARGUMENT);

    res = X509_verify_cert(ctx);
    VerifyOrExit(res == 1, err = WEAVE_ERROR_INVALID_SIGNATURE);

exit:
    if (NULL != param) X509_VERIFY_PARAM_free(param);
    if (NULL != ctx) X509_STORE_CTX_free(ctx);
    if (NULL != store) X509_STORE_free(store);
    for (int i = 0; i < certCount; i++)
    {
        if (NULL != cert[i]) X509_free(cert[i]);
        if (NULL != certBuf[i]) BIO_free(certBuf[i]);
    }

    return err;
}

#endif // WEAVE_WITH_OPENSSL

GetCertificateRequestMessage::GetCertificateRequestMessage()
{
    mReqType = WeaveCertProvEngine::kReqType_NotSpecified;
    mManufAttestType = kManufAttestType_Undefined;

    AuthorizeInfoPairingToken = NULL;
    AuthorizeInfoPairingTokenLen = 0;
    AuthorizeInfoPairingInitData = NULL;
    AuthorizeInfoPairingInitDataLen = 0;

    memset(ManufAttestX509CertSet, 0, sizeof(ManufAttestX509CertSet));
    ManufAttestX509CertCount = 0;

    OperationalSigAlgo = kOID_NotSpecified;
    memset(&OperationalSig, 0, sizeof(OperationalSig));
    ManufAttestSigAlgo = kOID_NotSpecified;
    memset(&ManufAttestSig, 0, sizeof(ManufAttestSig));

    mOperationalCertSetInitialized = false;
    mManufAttestCertSetInitialized = false;

    mTBSDataStart = NULL;
    mTBSDataLen = 0;
}

WEAVE_ERROR GetCertificateRequestMessage::Decode(PacketBuffer *msgBuf)
{
    WEAVE_ERROR err;
    TLVReader reader;
    TLVType outerContainer;
    WeaveCertificateData * certData;

    reader.Init(msgBuf);

    // Advance the reader to the start of the GetCertificateRequest message structure.
    err = reader.Next(kTLVType_Structure, AnonymousTag);
    SuccessOrExit(err);

    err = reader.EnterContainer(outerContainer);
    SuccessOrExit(err);

    // Request Type.
    {
        mTBSDataStart = reader.GetReadPoint();

        err = reader.Next(kTLVType_UnsignedInteger, ContextTag(kTag_GetCertReqMsg_ReqType));
        SuccessOrExit(err);

        err = reader.Get(mReqType);
        SuccessOrExit(err);

        VerifyOrExit(RequestType() == WeaveCertProvEngine::kReqType_GetInitialOpDeviceCert ||
                     RequestType() == WeaveCertProvEngine::kReqType_RotateOpDeviceCert, err = WEAVE_ERROR_INVALID_ARGUMENT);

        err = reader.Next();
        SuccessOrExit(err);
    }

    // Request authorization information - pairing token (optional).
    if (reader.GetType() == kTLVType_ByteString && reader.GetTag() == ContextTag(kTag_GetCertReqMsg_Authorize_PairingToken))
    {
        err = reader.GetDataPtr(AuthorizeInfoPairingToken);
        SuccessOrExit(err);

        AuthorizeInfoPairingTokenLen = reader.GetLength();

        err = reader.Next();
        SuccessOrExit(err);

        // Request authorization information - pairing init data (optional).
        if (reader.GetType() == kTLVType_ByteString && reader.GetTag() == ContextTag(kTag_GetCertReqMsg_Authorize_PairingInitData))
        {
            err = reader.GetDataPtr(AuthorizeInfoPairingInitData);
            SuccessOrExit(err);

            AuthorizeInfoPairingInitDataLen = reader.GetLength();

            err = reader.Next();
            SuccessOrExit(err);
        }
    }

    // Operational Device Certificate.
    {
        VerifyOrExit(reader.GetType() == kTLVType_Structure, err = WEAVE_ERROR_WRONG_TLV_TYPE);
        VerifyOrExit(reader.GetTag() == ContextTag(kTag_GetCertReqMsg_OpDeviceCert), err = WEAVE_ERROR_UNEXPECTED_TLV_ELEMENT);

        err = OperationalCertSet.Init(kMaxCertCount, nl::TestCerts::kTestCertBufSize);
        SuccessOrExit(err);

        mOperationalCertSetInitialized = true;

        // Load Weave operational device certificate.
        err = OperationalCertSet.LoadCert(reader, kDecodeFlag_GenerateTBSHash, certData);
        SuccessOrExit(err);

        mTBSDataLen = reader.GetReadPoint() - mTBSDataStart;

        err = reader.Next();
        SuccessOrExit(err);
    }

    // Load intermediate certificates (optional).
    if (reader.GetType() == kTLVType_Array && reader.GetTag() == ContextTag(kTag_GetCertReqMsg_OpRelatedCerts))
    {
        // Intermediate certificates are not expected when self-signed certificate is used
        // in the Get Initial Operational Device Certificate Request.
        VerifyOrExit(RequestType() != WeaveCertProvEngine::kReqType_GetInitialOpDeviceCert, err = WEAVE_ERROR_UNEXPECTED_TLV_ELEMENT);

        err = OperationalCertSet.LoadCerts(reader, kDecodeFlag_GenerateTBSHash);
        SuccessOrExit(err);

        mTBSDataLen = reader.GetReadPoint() - mTBSDataStart;

        err = reader.Next();
        SuccessOrExit(err);
    }

    // Manufacturer Attestation Information (optional).
    if (reader.GetType() == kTLVType_Structure && reader.GetTag() == ContextTag(kTag_GetCertReqMsg_ManufAttest_WeaveCert))
    {
        err = ManufAttestWeaveCertSet.Init(kMaxCertCount, nl::TestCerts::kTestCertBufSize);
        SuccessOrExit(err);

        mManufAttestCertSetInitialized = true;

        // Load manufacturer attestation Weave certificate.
        err = ManufAttestWeaveCertSet.LoadCert(reader, kDecodeFlag_GenerateTBSHash, certData);
        SuccessOrExit(err);

        mTBSDataLen = reader.GetReadPoint() - mTBSDataStart;

        err = reader.Next(kTLVType_Array, ContextTag(kTag_GetCertReqMsg_ManufAttest_WeaveRelCerts));

        if (err == WEAVE_NO_ERROR)
        {
            // Load intermediate certificate.
            err = ManufAttestWeaveCertSet.LoadCerts(reader, kDecodeFlag_GenerateTBSHash);
            SuccessOrExit(err);

            mTBSDataLen = reader.GetReadPoint() - mTBSDataStart;

            err = reader.Next();
            SuccessOrExit(err);
        }

        ManufAttestType(kManufAttestType_WeaveCert);
    }
    else if (reader.GetType() == kTLVType_ByteString && reader.GetTag() == ContextTag(kTag_GetCertReqMsg_ManufAttest_X509Cert))
    {
        err = reader.GetDataPtr(const_cast<const uint8_t *&>(ManufAttestX509CertSet[ManufAttestX509CertCount].Cert));
        SuccessOrExit(err);

        ManufAttestX509CertSet[ManufAttestX509CertCount++].Len = reader.GetLength();

        mTBSDataLen = ManufAttestX509CertSet[0].Cert + reader.GetLength() - mTBSDataStart;

        err = reader.Next(kTLVType_Array, ContextTag(kTag_GetCertReqMsg_ManufAttest_X509RelCerts));

        // Intermediate certificates (optional).
        if (err == WEAVE_NO_ERROR)
        {
            TLVType outerContainer2;

            err = reader.EnterContainer(outerContainer2);
            SuccessOrExit(err);

            err = reader.Next();

            while (err != WEAVE_END_OF_TLV)
            {
                SuccessOrExit(err);

                VerifyOrExit(ManufAttestX509CertCount < kMaxCertCount, err = WEAVE_ERROR_BUFFER_TOO_SMALL);

                err = reader.GetDataPtr(const_cast<const uint8_t *&>(ManufAttestX509CertSet[ManufAttestX509CertCount].Cert));
                SuccessOrExit(err);

                ManufAttestX509CertSet[ManufAttestX509CertCount++].Len = reader.GetLength();

                err = reader.Next();
            }

            err = reader.ExitContainer(outerContainer2);
            SuccessOrExit(err);

            mTBSDataLen = reader.GetReadPoint() - mTBSDataStart;

            err = reader.Next();
            SuccessOrExit(err);
        }

        ManufAttestType(kManufAttestType_X509Cert);
    }
    else if (reader.GetType() == kTLVType_UnsignedInteger && reader.GetTag() == ContextTag(kTag_GetCertReqMsg_ManufAttest_HMACKeyId))
    {
        err = reader.Get(ManufAttestHMACKeyId);
        SuccessOrExit(err);

        mTBSDataLen = reader.GetReadPoint() - mTBSDataStart;

        err = reader.Next();
        SuccessOrExit(err);

        // Request authorization information - pairing init data (optional).
        if (reader.GetType() == kTLVType_ByteString && reader.GetTag() == ContextTag(kTag_GetCertReqMsg_ManufAttest_HMACMetaData))
        {
            err = reader.GetDataPtr(ManufAttestHMACMetaData);
            SuccessOrExit(err);

            ManufAttestHMACMetaDataLen = reader.GetLength();

            mTBSDataLen = ManufAttestHMACMetaData + ManufAttestHMACMetaDataLen - mTBSDataStart;

            err = reader.Next();
            SuccessOrExit(err);
        }

        ManufAttestType(kManufAttestType_HMAC);
    }
    else
    {
        VerifyOrExit(!ManufAttestRequired(), err = WEAVE_ERROR_INVALID_ARGUMENT);
    }

    // Operational Device Signature.
    {
        VerifyOrExit(reader.GetType() == kTLVType_UnsignedInteger, err = WEAVE_ERROR_WRONG_TLV_TYPE);
        VerifyOrExit(reader.GetTag() == ContextTag(kTag_GetCertReqMsg_OpDeviceSigAlgo), err = WEAVE_ERROR_UNEXPECTED_TLV_ELEMENT);

        err = reader.Get(OperationalSigAlgo);
        SuccessOrExit(err);

        err = reader.Next(kTLVType_Structure, ContextTag(kTag_GetCertReqMsg_OpDeviceSig_ECDSA));
        SuccessOrExit(err);

        err = DecodeWeaveECDSASignature(reader, OperationalSig);
        SuccessOrExit(err);

        err = reader.Next();
    }

    // Manufacturer Attestation Signature (optional).
    if (ManufAttestPresent())
    {
        VerifyOrExit(reader.GetType() == kTLVType_UnsignedInteger, err = WEAVE_ERROR_WRONG_TLV_TYPE);
        VerifyOrExit(reader.GetTag() == ContextTag(kTag_GetCertReqMsg_ManufAttestSigAlgo), err = WEAVE_ERROR_UNEXPECTED_TLV_ELEMENT);

        err = reader.Get(ManufAttestSigAlgo);
        SuccessOrExit(err);

        err = reader.Next();
        SuccessOrExit(err);

        if (reader.GetType() == kTLVType_Structure && reader.GetTag() == ContextTag(kTag_GetCertReqMsg_ManufAttestSig_ECDSA))
        {
            VerifyOrExit(ManufAttestType() == kManufAttestType_WeaveCert, err = WEAVE_ERROR_UNEXPECTED_TLV_ELEMENT);

            err = DecodeWeaveECDSASignature(reader, ManufAttestSig.EC);
            SuccessOrExit(err);
        }
        else if (reader.GetType() == kTLVType_ByteString && reader.GetTag() == ContextTag(kTag_GetCertReqMsg_ManufAttestSig_RSA))
        {
            VerifyOrExit(ManufAttestType() == kManufAttestType_X509Cert, err = WEAVE_ERROR_UNEXPECTED_TLV_ELEMENT);

            err = ManufAttestSig.RSA.ReadSignature(reader, ContextTag(kTag_GetCertReqMsg_ManufAttestSig_RSA));
            SuccessOrExit(err);
        }
        else if (reader.GetType() == kTLVType_ByteString && reader.GetTag() == ContextTag(kTag_GetCertReqMsg_ManufAttestSig_HMAC))
        {
            VerifyOrExit(ManufAttestType() == kManufAttestType_HMAC, err = WEAVE_ERROR_UNEXPECTED_TLV_ELEMENT);

            err = ManufAttestSig.HMAC.ReadSignature(reader, ContextTag(kTag_GetCertReqMsg_ManufAttestSig_HMAC));
            SuccessOrExit(err);

            err = reader.Next();
        }
        else
        {
            // Any other manufacturer attestation types are not currently supported.
            ExitNow(err = WEAVE_ERROR_UNEXPECTED_TLV_ELEMENT);
        }
    }

    err = reader.VerifyEndOfContainer();
    SuccessOrExit(err);

    err = reader.ExitContainer(outerContainer);
    SuccessOrExit(err);

exit:
    return err;
}

WEAVE_ERROR GetCertificateRequestMessage::GenerateTBSHash(uint8_t *tbsHash)
{
    nl::Weave::Platform::Security::SHA256 sha256;

    sha256.Begin();
    sha256.AddData(mTBSDataStart, mTBSDataLen);
    sha256.Finish(tbsHash);

    return WEAVE_NO_ERROR;
}

WEAVE_ERROR MockCAService::GenerateServiceAssignedDeviceCert(WeaveCertificateData& certData, uint8_t *cert, uint16_t certBufSize, uint16_t& certLen)
{
    WEAVE_ERROR err;
    TLVWriter writer;
    TLVType containerType;
    TLVType containerType2;
    TLVType containerType3;

    // Update certificate fields for the service assigned certificate.
    certData.IssuerDN.AttrValue.WeaveId = nl::TestCerts::sTestCert_CA_Id;
    certData.AuthKeyId.Id               = nl::TestCerts::sTestCert_CA_SubjectKeyId;
    certData.AuthKeyId.Len              = nl::TestCerts::sTestCertLength_CA_SubjectKeyId;
    certData.SigAlgoOID                 = kOID_SigAlgo_ECDSAWithSHA256;

    writer.Init(cert, certBufSize);

    err = writer.StartContainer(ProfileTag(kWeaveProfile_Security, kTag_WeaveCertificate), kTLVType_Structure, containerType);
    SuccessOrExit(err);

    // Certificate serial number.
    {
        enum
        {
            kCertSerialNumber_Length          = 8,      // Length of the certificate serial number.
            kCertSerialNumber_FirstByteMask   = 0x7F,   // Mask applied on the first byte of the key Id value.
            kCertSerialNumber_FirstBytePrefix = 0x40,   // 4-bit Type value (0100) added at the beginning of the key Id value.
        };
        uint8_t certSerialNumber[kCertSerialNumber_Length];

        // Generate a random value to be used as the serial number.
        err = nl::Weave::Platform::Security::GetSecureRandomData(certSerialNumber, kCertSerialNumber_Length);
        SuccessOrExit(err);

        // Apply mask to avoid negative numbers.
        certSerialNumber[0] &= kCertSerialNumber_FirstByteMask;

        // Apply mask to guarantee the first byte is not zero.
        certSerialNumber[0] |= kCertSerialNumber_FirstBytePrefix;

        err = writer.PutBytes(ContextTag(kTag_SerialNumber), certSerialNumber, sizeof(certSerialNumber));
        SuccessOrExit(err);
    }

    // Weave signature algorithm.
    err = writer.Put(ContextTag(kTag_SignatureAlgorithm), static_cast<uint8_t>(certData.SigAlgoOID & ~kOIDCategory_Mask));
    SuccessOrExit(err);

    // Certificate issuer Id.
    {
        err = writer.StartContainer(ContextTag(kTag_Issuer), kTLVType_Path, containerType2);
        SuccessOrExit(err);

        err = writer.Put(ContextTag(kOID_AttributeType_WeaveDeviceId & kOID_Mask), certData.IssuerDN.AttrValue.WeaveId);
        SuccessOrExit(err);

        err = writer.EndContainer(containerType2);
        SuccessOrExit(err);
    }

    // Certificate validity times.
    {
        uint32_t packedNotBeforeTime;
        uint32_t packedNotAfterTime;
        ASN1UniversalTime validTime = {
            .Year   = 2019,
            .Month  = 8,
            .Day    = 1,
            .Hour   = 14,
            .Minute = 11,
            .Second = 54
        };

        err = PackCertTime(validTime, packedNotBeforeTime);
        SuccessOrExit(err);

        validTime.Year += 10;

        err = PackCertTime(validTime, packedNotAfterTime);
        SuccessOrExit(err);

        err = writer.Put(ContextTag(kTag_NotBefore), packedNotBeforeTime);
        SuccessOrExit(err);

        err = writer.Put(ContextTag(kTag_NotAfter), packedNotAfterTime);
        SuccessOrExit(err);
    }

    // Certificate subject Id.
    {
        err = writer.StartContainer(ContextTag(kTag_Subject), kTLVType_Path, containerType2);
        SuccessOrExit(err);

        err = writer.Put(ContextTag(kOID_AttributeType_WeaveDeviceId & kOID_Mask), certData.SubjectDN.AttrValue.WeaveId);
        SuccessOrExit(err);

        err = writer.EndContainer(containerType2);
        SuccessOrExit(err);
    }

    // EC public key algorithm.
    err = writer.Put(ContextTag(kTag_PublicKeyAlgorithm), static_cast<uint8_t>(certData.PubKeyAlgoOID & kOID_Mask));
    SuccessOrExit(err);

    // EC public key curve Id.
    err = writer.Put(ContextTag(kTag_EllipticCurveIdentifier), static_cast<uint32_t>(certData.PubKeyCurveId));
    SuccessOrExit(err);

    // EC public key.
    err = writer.PutBytes(ContextTag(kTag_EllipticCurvePublicKey), certData.PublicKey.EC.ECPoint, certData.PublicKey.EC.ECPointLen);
    SuccessOrExit(err);

    // Certificate extension: basic constraints.
    {
        err = writer.StartContainer(ContextTag(kTag_BasicConstraints), kTLVType_Structure, containerType2);
        SuccessOrExit(err);

        // This extension is critical.
        err = writer.PutBoolean(ContextTag(kTag_BasicConstraints_Critical), true);
        SuccessOrExit(err);

        err = writer.EndContainer(containerType2);
        SuccessOrExit(err);
    }

    // Certificate extension: key usage.
    {
        err = writer.StartContainer(ContextTag(kTag_KeyUsage), kTLVType_Structure, containerType2);
        SuccessOrExit(err);

        // This extension is critical.
        err = writer.PutBoolean(ContextTag(kTag_KeyUsage_Critical), true);
        SuccessOrExit(err);

        err = writer.Put(ContextTag(kTag_KeyUsage_KeyUsage), static_cast<uint16_t>(kKeyUsageFlag_DigitalSignature | kKeyUsageFlag_KeyEncipherment));
        SuccessOrExit(err);

        err = writer.EndContainer(containerType2);
        SuccessOrExit(err);
    }

    // Certificate extension: extended key usage.
    {
        err = writer.StartContainer(ContextTag(kTag_ExtendedKeyUsage), kTLVType_Structure, containerType2);
        SuccessOrExit(err);

        // This extension is critical.
        err = writer.PutBoolean(ContextTag(kTag_ExtendedKeyUsage_Critical), true);
        SuccessOrExit(err);

        err = writer.StartContainer(ContextTag(kTag_ExtendedKeyUsage_KeyPurposes), kTLVType_Array, containerType3);
        SuccessOrExit(err);

        // Key purpose is client authentication.
        err = writer.Put(AnonymousTag, static_cast<uint8_t>(kOID_KeyPurpose_ClientAuth & kOID_Mask));
        SuccessOrExit(err);

        // Key purpose is server authentication.
        err = writer.Put(AnonymousTag, static_cast<uint8_t>(kOID_KeyPurpose_ServerAuth & kOID_Mask));
        SuccessOrExit(err);

        err = writer.EndContainer(containerType3);
        SuccessOrExit(err);

        err = writer.EndContainer(containerType2);
        SuccessOrExit(err);
    }

    // Certificate extension: subject key identifier.
    {
        err = writer.StartContainer(ContextTag(kTag_SubjectKeyIdentifier), kTLVType_Structure, containerType2);
        SuccessOrExit(err);

        err = writer.PutBytes(ContextTag(kTag_SubjectKeyIdentifier_KeyIdentifier), certData.SubjectKeyId.Id, certData.SubjectKeyId.Len);
        SuccessOrExit(err);

        err = writer.EndContainer(containerType2);
        SuccessOrExit(err);
    }

    // Certificate extension: authority key identifier.
    {
        err = writer.StartContainer(ContextTag(kTag_AuthorityKeyIdentifier), kTLVType_Structure, containerType2);
        SuccessOrExit(err);

        err = writer.PutBytes(ContextTag(kTag_AuthorityKeyIdentifier_KeyIdentifier), certData.AuthKeyId.Id, certData.AuthKeyId.Len);
        SuccessOrExit(err);

        err = writer.EndContainer(containerType2);
        SuccessOrExit(err);
    }

    // Start the ECDSASignature structure.
    // Note that the ECDSASignature tag is added here but the actual certificate data (S and R values)
    // will be written later. This is needed to prevent DecodeConvertTBSCert() function from failing.
    // This function expects to read new non-hashable element after all TBS data is converted.
    err = writer.StartContainer(ContextTag(kTag_ECDSASignature), kTLVType_Structure, containerType2);
    SuccessOrExit(err);

    {
        enum
        {
            kCertDecodeBufferSize      = 1024,   // Maximum ASN1 encoded size of the operational device certificate.
        };
        TLVReader reader;
        ASN1Writer tbsWriter;
        uint8_t certDecodeBuf[kCertDecodeBufferSize];
        TLVType readContainerType;
        WeaveCertificateData certData2;

        reader.Init(cert, certBufSize);

        // Parse the beginning of the WeaveSignature structure.
        err = reader.Next(kTLVType_Structure, ProfileTag(kWeaveProfile_Security, kTag_WeaveCertificate));
        SuccessOrExit(err);

        // Enter the certificate structure.
        err = reader.EnterContainer(readContainerType);
        SuccessOrExit(err);

        // Initialize an ASN1Writer and convert the TBS (to-be-signed) portion of
        // the certificate to ASN.1 DER encoding.
        tbsWriter.Init(certDecodeBuf, kCertDecodeBufferSize);
        err = DecodeConvertTBSCert(reader, tbsWriter, certData2);
        SuccessOrExit(err);

        // Finish writing the ASN.1 DER encoding of the TBS certificate.
        err = tbsWriter.Finalize();
        SuccessOrExit(err);

        // Generate a SHA hash of the encoded TBS certificate.
        nl::Weave::Platform::Security::SHA256 sha256;
        sha256.Begin();
        sha256.AddData(certDecodeBuf, tbsWriter.GetLengthWritten());
        sha256.Finish(certData.TBSHash);

        uint32_t caCurveId;
        EncodedECPublicKey caPubKey;
        EncodedECPrivateKey caPrivKey;
        EncodedECDSASignature ecdsaSig;

        // Decode the CA private key.
        err = DecodeWeaveECPrivateKey(mCAPrivateKey, mCAPrivateKeyLength, caCurveId, caPubKey, caPrivKey);
        SuccessOrExit(err);

        // Reuse already allocated decode buffer to hold the generated signature value.
        ecdsaSig.R = certDecodeBuf;
        ecdsaSig.RLen = EncodedECDSASignature::kMaxValueLength;
        ecdsaSig.S = certDecodeBuf + EncodedECDSASignature::kMaxValueLength;
        ecdsaSig.SLen = EncodedECDSASignature::kMaxValueLength;

        // Generate an ECDSA signature for the given message hash.
        err = nl::Weave::Crypto::GenerateECDSASignature(WeaveCurveIdToOID(caCurveId), certData.TBSHash, nl::Weave::Platform::Security::SHA256::kHashLength, caPrivKey, ecdsaSig);
        SuccessOrExit(err);

        // Write the R value.
        err = writer.PutBytes(ContextTag(kTag_ECDSASignature_r), ecdsaSig.R, ecdsaSig.RLen);
        SuccessOrExit(err);

        // Write the S value.
        err = writer.PutBytes(ContextTag(kTag_ECDSASignature_s), ecdsaSig.S, ecdsaSig.SLen);
        SuccessOrExit(err);
    }

    err = writer.EndContainer(containerType2);
    SuccessOrExit(err);

    err = writer.EndContainer(containerType);
    SuccessOrExit(err);

    err = writer.Finalize();
    SuccessOrExit(err);

    certLen = static_cast<uint16_t>(writer.GetLengthWritten());

exit:
    return err;
}


MockCAService::MockCAService()
{
    mExchangeMgr = NULL;
    mLogMessageData = false;
    mIncludeRelatedCerts = false;
    mDoNotRotateCert = false;

    mCACert = nl::TestCerts::sTestCert_CA_Weave;
    mCACertLength = nl::TestCerts::sTestCertLength_CA_Weave;

    mCAPrivateKey = nl::TestCerts::sTestCert_CA_PrivateKey_Weave;
    mCAPrivateKeyLength = nl::TestCerts::sTestCertLength_CA_PrivateKey_Weave;
}

WEAVE_ERROR MockCAService::Init(nl::Weave::WeaveExchangeManager *exchangeMgr)
{
    WEAVE_ERROR err;

    mExchangeMgr = exchangeMgr;

    // Register to receive unsolicited GetCertificateRequest messages from the exchange manager.
    err = mExchangeMgr->RegisterUnsolicitedMessageHandler(kWeaveProfile_Security, kMsgType_GetCertificateRequest, HandleClientRequest, this);
    SuccessOrExit(err);

exit:
    return err;
}

WEAVE_ERROR MockCAService::Shutdown()
{
    if (mExchangeMgr != NULL)
        mExchangeMgr->UnregisterUnsolicitedMessageHandler(kWeaveProfile_Security, kMsgType_GetCertificateRequest);
    return WEAVE_NO_ERROR;
}

void MockCAService::HandleClientRequest(nl::Weave::ExchangeContext *ec, const nl::Inet::IPPacketInfo *pktInfo,
                                        const nl::Weave::WeaveMessageInfo *msgInfo, uint32_t profileId,
                                        uint8_t msgType, PacketBuffer *reqMsg)
{
    WEAVE_ERROR err = WEAVE_NO_ERROR;
    MockCAService *server = static_cast<MockCAService *>(ec->AppState);
    GetCertificateRequestMessage getCertMsg;
    PacketBuffer *respMsg = NULL;
    char ipAddrStr[64];
    ec->PeerAddr.ToString(ipAddrStr, sizeof(ipAddrStr));

    VerifyOrExit(profileId == kWeaveProfile_Security &&
                 msgType == kMsgType_GetCertificateRequest, err = WEAVE_ERROR_NO_MEMORY);

    printf("GetCertificate request received from node %" PRIX64 " (%s)\n", ec->PeerNodeId, ipAddrStr);

    err = server->ProcessGetCertificateRequest(reqMsg, getCertMsg);
    SuccessOrExit(err);

    if ((getCertMsg.RequestType() == WeaveCertProvEngine::kReqType_RotateOpDeviceCert) && server->mDoNotRotateCert)
    {
        err = server->SendStatusReport(ec, Security::kStatusCode_NoNewCertRequired);
        SuccessOrExit(err);
    }
    else
    {
        respMsg = PacketBuffer::New();
        VerifyOrExit(respMsg != NULL, err = WEAVE_ERROR_NO_MEMORY);

        err = server->GenerateGetCertificateResponse(respMsg, *getCertMsg.OperationalCertSet.Certs);
        SuccessOrExit(err);

        err = ec->SendMessage(kWeaveProfile_Security, kMsgType_GetCertificateResponse, respMsg, 0);
        respMsg = NULL;
        SuccessOrExit(err);
    }

exit:
    if (err != WEAVE_NO_ERROR)
        server->SendStatusReport(ec, Security::kStatusCode_UnathorizedGetCertRequest);

    if (reqMsg != NULL)
        PacketBuffer::Free(reqMsg);

    if (respMsg != NULL)
        PacketBuffer::Free(respMsg);
}

WEAVE_ERROR MockCAService::SendStatusReport(nl::Weave::ExchangeContext *ec, uint16_t statusCode)
{
    WEAVE_ERROR err;
    PacketBuffer *statusMsg;
    StatusReporting::StatusReport statusReport;

    statusMsg = PacketBuffer::New();
    VerifyOrExit(statusMsg != NULL, err = WEAVE_ERROR_NO_MEMORY);

    statusReport.mProfileId = kWeaveProfile_Security;
    statusReport.mStatusCode = statusCode;

    err = statusReport.pack(statusMsg);
    SuccessOrExit(err);

    err = ec->SendMessage(kWeaveProfile_Common, Common::kMsgType_StatusReport, statusMsg, 0);
    statusMsg = NULL;
    SuccessOrExit(err);

exit:
    if (statusMsg != NULL)
        PacketBuffer::Free(statusMsg);

    return err;
}

WEAVE_ERROR MockCAService::ProcessGetCertificateRequest(PacketBuffer *msgBuf, GetCertificateRequestMessage & msg)
{
    WEAVE_ERROR err;
    uint8_t tbsHash[SHA256::kHashLength];

    err = msg.Decode(msgBuf);
    SuccessOrExit(err);

    // Validate Manufacturer Attestation Information if present.
    if (msg.AuthorizeInfoPresent())
    {
        int res;

        VerifyOrExit(msg.AuthorizeInfoPairingTokenLen == TestDevice1_PairingTokenLength, err = WEAVE_ERROR_INVALID_ARGUMENT);

        res = memcmp(msg.AuthorizeInfoPairingToken, TestDevice1_PairingToken, TestDevice1_PairingTokenLength);
        VerifyOrExit(res == 0, err = WEAVE_ERROR_INVALID_ARGUMENT);

        VerifyOrExit(msg.AuthorizeInfoPairingInitDataLen == TestDevice1_PairingInitDataLength, err = WEAVE_ERROR_INVALID_ARGUMENT);

        res = memcmp(msg.AuthorizeInfoPairingInitData, TestDevice1_PairingInitData, TestDevice1_PairingInitDataLength);
        VerifyOrExit(res == 0, err = WEAVE_ERROR_INVALID_ARGUMENT);
    }

    err = ValidateWeaveDeviceCert(msg.OperationalCertSet);
    SuccessOrExit(err);

    if (msg.ManufAttestRequired())
    {
        VerifyOrExit(msg.ManufAttestPresent(), err = WEAVE_ERROR_INVALID_ARGUMENT);
    }

    // Validate Manufacturer Attestation Information if present.
    if (msg.ManufAttestPresent())
    {
        if (msg.ManufAttestType() == kManufAttestType_WeaveCert)
        {
            err = ValidateWeaveDeviceCert(msg.ManufAttestWeaveCertSet);
            SuccessOrExit(err);
        }
        else if (msg.ManufAttestType() == kManufAttestType_X509Cert)
        {
            // Add Trusted X509 Root Certificate.
            msg.ManufAttestX509CertSet[msg.ManufAttestX509CertCount].Cert = TestDevice_X509_RSA_RootCert;
            msg.ManufAttestX509CertSet[msg.ManufAttestX509CertCount++].Len = TestDevice_X509_RSA_RootCertLength;

            err = ValidateX509DeviceCert(msg.ManufAttestX509CertSet, msg.ManufAttestX509CertCount);
            SuccessOrExit(err);
        }
        else if (msg.ManufAttestType() == kManufAttestType_HMAC)
        {
            VerifyOrExit(msg.ManufAttestHMACKeyId == TestDevice1_ManufAttest_HMACKeyId, err = WEAVE_ERROR_INVALID_ARGUMENT);

            if (msg.ManufAttestHMACMetaData != NULL)
            {
                int res;

                VerifyOrExit(msg.ManufAttestHMACMetaDataLen == TestDevice1_ManufAttest_HMACMetaDataLength, err = WEAVE_ERROR_INVALID_ARGUMENT);

                res = memcmp(msg.ManufAttestHMACMetaData, TestDevice1_ManufAttest_HMACMetaData, TestDevice1_ManufAttest_HMACMetaDataLength);
                VerifyOrExit(res == 0, err = WEAVE_ERROR_INVALID_ARGUMENT);
            }
        }
        else
        {
            ExitNow(WEAVE_ERROR_INVALID_ARGUMENT);
        }
    }

    err = msg.GenerateTBSHash(tbsHash);
    SuccessOrExit(err);

    // Only ECDSAWithSHA256 algorithm is allowed for operational signature.
    VerifyOrExit(msg.OperationalSigAlgo == kOID_SigAlgo_ECDSAWithSHA256, err = WEAVE_ERROR_INVALID_ARGUMENT);

    // Verify operational signature.
    err = VerifyECDSASignature(WeaveCurveIdToOID(msg.OperationalCertSet.Certs->PubKeyCurveId),
                               tbsHash, SHA256::kHashLength, msg.OperationalSig, msg.OperationalCertSet.Certs->PublicKey.EC);
    SuccessOrExit(err);

    // Verify Manufacturer Attestation Signature if present.
    if (msg.ManufAttestPresent())
    {
        if (msg.ManufAttestSigAlgo == kOID_SigAlgo_ECDSAWithSHA256)
        {
            err = VerifyECDSASignature(WeaveCurveIdToOID(msg.ManufAttestWeaveCertSet.Certs->PubKeyCurveId),
                                       tbsHash, SHA256::kHashLength, msg.ManufAttestSig.EC, msg.ManufAttestWeaveCertSet.Certs->PublicKey.EC);
            SuccessOrExit(err);
        }
        else if (msg.ManufAttestSigAlgo == kOID_SigAlgo_SHA256WithRSAEncryption)
        {
#if WEAVE_WITH_OPENSSL
            err = VerifyRSASignature(kOID_SigAlgo_SHA256WithRSAEncryption, tbsHash, SHA256::kHashLength, msg.ManufAttestSig.RSA,
                                     msg.ManufAttestX509CertSet[0].Cert, msg.ManufAttestX509CertSet[0].Len);
            SuccessOrExit(err);
#else
            ExitNow(err = WEAVE_ERROR_NOT_IMPLEMENTED);
#endif
        }
        else if (msg.ManufAttestSigAlgo == kOID_SigAlgo_HMACWithSHA256)
        {
            err = VerifyHMACSignature(kOID_SigAlgo_HMACWithSHA256, msg.mTBSDataStart, msg.mTBSDataLen, msg.ManufAttestSig.HMAC,
                                      TestDevice1_ManufAttest_HMACKey, TestDevice1_ManufAttest_HMACKeyLength);
            SuccessOrExit(err);
        }
        else
        {
            ExitNow(err = WEAVE_ERROR_UNSUPPORTED_SIGNATURE_TYPE);
        }
    }

exit:
    return err;
}

WEAVE_ERROR MockCAService::GenerateGetCertificateResponse(PacketBuffer *msgBuf, WeaveCertificateData& currentOpDeviceCert)
{
    WEAVE_ERROR err = WEAVE_NO_ERROR;
    TLVWriter writer;
    TLVType containerType;
    uint8_t certBuf[nl::TestCerts::kTestCertBufSize];
    uint16_t certLen;

    err = GenerateServiceAssignedDeviceCert(currentOpDeviceCert, certBuf, sizeof(certBuf), certLen);
    SuccessOrExit(err);

    writer.Init(msgBuf);

    err = writer.StartContainer(AnonymousTag, kTLVType_Structure, containerType);
    SuccessOrExit(err);

    err = writer.CopyContainer(ContextTag(kTag_GetCertRespMsg_OpDeviceCert), certBuf, certLen);
    SuccessOrExit(err);

    if (mIncludeRelatedCerts)
    {
        TLVType containerType2;

        // Start the RelatedCertificates array. This contains the list of certificates the signature verifier
        // will need to verify the signature.
        err = writer.StartContainer(ContextTag(kTag_GetCertRespMsg_RelatedCerts), kTLVType_Array, containerType2);
        SuccessOrExit(err);

        // Copy the intermediate test device CA certificate.
        err = writer.CopyContainer(AnonymousTag, mCACert, mCACertLength);
        SuccessOrExit(err);

        err = writer.EndContainer(containerType2);
        SuccessOrExit(err);
    }

    err = writer.EndContainer(containerType);
    SuccessOrExit(err);

    err = writer.Finalize();
    SuccessOrExit(err);

exit:
    return err;
}
