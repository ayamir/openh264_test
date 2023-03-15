#include <fstream>
#include <iostream>
#include <cassert>
#include <wels/codec_api.h>
#include <wels/codec_app_def.h>
#include <wels/codec_def.h>
#include <wels/utils/BufferedData.h>
#include <wels/utils/InputStream.h>
#include <wels/utils/FileInputStream.h>

using namespace std;

class BaseEncoderTest
{
public:
    struct Callback
    {
        virtual void onEncodeFrame(const SFrameBSInfo &frameInfo) = 0;
    };

    BaseEncoderTest();
    void SetUp();
    void TearDown();
    void EncodeFile(const char *fileName, SEncParamExt *pEncParamExt, Callback *cbk);
    void EncodeStream(InputStream *in, SEncParamExt *pEncParamExt, Callback *cbk);

    ISVCEncoder *encoder_;

private:
};

struct TestCallback : public BaseEncoderTest::Callback
{
    virtual void onEncodeFrame(const SFrameBSInfo &frameInfo)
    {
        int iLayer = 0;
        while (iLayer < frameInfo.iLayerNum)
        {
            const SLayerBSInfo *pLayerInfo = &frameInfo.sLayerInfo[iLayer++];
            if (pLayerInfo)
            {
                int iLayerSize = 0;
                int iNalIndex = pLayerInfo->iNalCount - 1;
                do
                {
                    iLayerSize += pLayerInfo->pNalLengthInByte[iNalIndex];
                    iNalIndex--;
                } while (iNalIndex >= 0);
                FILE *fp = fopen("test.264", "ab");
                assert(fp != NULL);
                fwrite(pLayerInfo->pBsBuf, 1, iLayerSize, fp);
                fclose(fp);
            }
        }
    }
};

BaseEncoderTest::BaseEncoderTest() : encoder_(NULL) {}

void BaseEncoderTest::SetUp()
{
    int rv = WelsCreateSVCEncoder(&encoder_);
    assert(rv == cmResultSuccess);
    assert(encoder_ != NULL);

    unsigned int uiTraceLevel = WELS_LOG_INFO;
    encoder_->SetOption(ENCODER_OPTION_TRACE_LEVEL, &uiTraceLevel);
}

void BaseEncoderTest::TearDown()
{
    if (encoder_)
    {
        encoder_->Uninitialize();
        WelsDestroySVCEncoder(encoder_);
    }
}

void BaseEncoderTest::EncodeStream(InputStream *in, SEncParamExt *pEncParamExt, Callback *cbk)
{

    assert(NULL != pEncParamExt);

    int rv = encoder_->InitializeExt(pEncParamExt);
    assert(rv == cmResultSuccess);

    // I420: 1(Y) + 1/4(U) + 1/4(V)
    int frameSize = pEncParamExt->iPicWidth * pEncParamExt->iPicHeight * 3 / 2;

    BufferedData buf;
    buf.SetLength(frameSize);
    assert(buf.Length() == (size_t)frameSize);

    SFrameBSInfo info;
    memset(&info, 0, sizeof(SFrameBSInfo));

    SSourcePicture pic;
    memset(&pic, 0, sizeof(SSourcePicture));
    pic.iPicWidth = pEncParamExt->iPicWidth;
    pic.iPicHeight = pEncParamExt->iPicHeight;
    pic.iColorFormat = videoFormatI420;
    pic.iStride[0] = pic.iPicWidth;
    pic.iStride[1] = pic.iStride[2] = pic.iPicWidth >> 1;
    pic.pData[0] = buf.data();
    pic.pData[1] = pic.pData[0] + pEncParamExt->iPicWidth * pEncParamExt->iPicHeight;
    pic.pData[2] = pic.pData[1] + (pEncParamExt->iPicWidth * pEncParamExt->iPicHeight >> 2);

    while (in->read(buf.data(), frameSize) == frameSize)
    {
        rv = encoder_->EncodeFrame(&pic, &info);
        assert(rv == cmResultSuccess);
        if (info.eFrameType != videoFrameTypeSkip)
        {
            cbk->onEncodeFrame(info);
        }
    }
}

void BaseEncoderTest::EncodeFile(const char *fileName, SEncParamExt *pEncParamExt, Callback *cbk)
{
    FileInputStream fileStream;
    bool res = fileStream.Open(fileName);
    assert(res == true);
    EncodeStream(&fileStream, pEncParamExt, cbk);
}

int main()
{
    float frameRate = 60;
    int width = 1296;
    int height = 1440;
    SEncParamExt param;
    memset(&param, 0, sizeof(SEncParamExt));
    param.iUsageType = EUsageType::SCREEN_CONTENT_REAL_TIME;
    param.fMaxFrameRate = frameRate;
    param.iPicWidth = width;
    param.iPicHeight = height;
    param.iTargetBitrate = 10 * 1000; // 10Mbps
    param.pPriorityArray = nullptr;
    param.fAverageWeight = 0.0;

    TestCallback cbk;
    BaseEncoderTest *pTest = new BaseEncoderTest();
    pTest->SetUp();
    pTest->EncodeFile("test.yuv", &param, &cbk);
    pTest->TearDown();

    cout << "OK" << endl;

    return 0;
}