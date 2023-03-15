#include <wels/codec_api.h>
#include <wels/codec_app_def.h>
#include <wels/codec_def.h>
#include <wels/utils/BufferedData.h>
#include <wels/utils/FileInputStream.h>
#include <wels/utils/InputStream.h>

#include <cassert>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

using namespace std;
namespace fs = std::filesystem;

const string testbinDir = "../testbin/";
const string weightsDir = testbinDir + "weights";
const string inputFileName = testbinDir + "test.yuv";
const string outDiffOnFileName = testbinDir + "out-with.h264";
const string outDiffOffFileName = testbinDir + "out-without.h264";

const int width = 1296;
const int height = 1440;
const float frameRate = 60;
const int iWidthInMb = width / 16;
const int iHeightInMb = height / 16;
const int iArraySize = iWidthInMb * iHeightInMb;

int isDiffEncoding = 0;

class BaseEncoderTest
{
public:
    struct Callback
    {
        virtual void onEncodeFrame(const SFrameBSInfo &frameInfo,
                                   const string &outFileName) = 0;
    };

    BaseEncoderTest();
    void SetUp();
    void TearDown();
    void EncodeFile(const char *fileName, SEncParamExt *pEncParamExt,
                    Callback *cbk, const string &outFileName);
    void EncodeStream(InputStream *in, SEncParamExt *pEncParamExt, Callback *cbk,
                      const string &outFileName);
    void CheckWeightLog(const string &fileName, int width, int height);
    void ReadPriorityArray(const string &fileName, float *priorityArray, int width,
                           int height);

    ISVCEncoder *encoder_;

private:
};

inline bool fileExists(const string &name)
{
    struct stat buffer;
    return (stat(name.c_str(), &buffer) == 0);
}

inline int countFiles(const fs::path &dir)
{
    int count = 0;
    for (auto &p : fs::directory_iterator(dir))
    {
        count++;
    }
    return count;
}

struct TestCallback : public BaseEncoderTest::Callback
{
    virtual void onEncodeFrame(const SFrameBSInfo &frameInfo,
                               const string &outFileName)
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
                FILE *fp = fopen(outFileName.c_str(), "ab");
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

void BaseEncoderTest::EncodeStream(InputStream *in, SEncParamExt *pEncParamExt,
                                   Callback *cbk, const string &outFileName)
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
    pic.pData[1] =
        pic.pData[0] + pEncParamExt->iPicWidth * pEncParamExt->iPicHeight;
    pic.pData[2] =
        pic.pData[1] + (pEncParamExt->iPicWidth * pEncParamExt->iPicHeight >> 2);

    int i = 1;
    while (in->read(buf.data(), frameSize) == frameSize)
    {
        int rv = -1;
        if (isDiffEncoding)
        {

            float priorityArray[iArraySize];
            float fAverageWeight = 0.0;
            const string weightLog = weightsDir + "/" + to_string(i++) + ".txt";
            ReadPriorityArray(weightLog, priorityArray, iWidthInMb, iHeightInMb);
            rv = encoder_->EncodeFrame(&pic, &info, priorityArray);
        }
        else
        {
            rv = encoder_->EncodeFrame(&pic, &info);
        }
        assert(rv == cmResultSuccess);
        if (info.eFrameType != videoFrameTypeSkip)
        {
            cbk->onEncodeFrame(info, outFileName);
        }
    }
}

void BaseEncoderTest::EncodeFile(const char *fileName,
                                 SEncParamExt *pEncParamExt, Callback *cbk,
                                 const string &outFileName)
{
    FileInputStream fileStream;
    bool res = fileStream.Open(fileName);
    if (fileExists(outFileName))
    {
        int removeRes = remove(outFileName.c_str());
        assert(removeRes == 0);
        cout << "Removed existing file before encoding: " << outFileName << endl;
    }
    assert(res == true);
    EncodeStream(&fileStream, pEncParamExt, cbk, outFileName);
}

// check if the weight log file is valid
void BaseEncoderTest::CheckWeightLog(const string &fileName, int width, int height)
{
    ifstream weightLog(fileName.c_str());
    string line;
    int lineNum = 0;
    while (getline(weightLog, line))
    {
        lineNum++;
        if (line.empty())
        {
            continue;
        }

        float num = 0;
        int colNum = 0;
        stringstream ss(line);
        while (ss >> num)
        {
            colNum++;
        }
        assert(colNum == width);
    }
    assert(lineNum - 1 == height);

    weightLog.close();
}

// read just one priority array from one file
void BaseEncoderTest::ReadPriorityArray(const string &fileName, float *priorityArray, int width,
                                        int height)
{
    // CheckWeightLog(fileName, width, height);

    ifstream file(fileName.c_str());
    string line;
    while (getline(file, line))
    {
        if (line.empty())
        {
            continue;
        }

        float num = 0;
        stringstream ss(line);
        while (ss >> num)
        {
            *priorityArray++ = num;
        }
    }
    file.close();
}

void splitWeightLog(const string &fileName, const string &weightsDir)
{
    ifstream weightLog(fileName.c_str());
    string line;
    int frameNum = 1;

    while (getline(weightLog, line))
    {
        // write line to file with name: <weightsDir>/<frameNum>.txt
        string targetFileName = weightsDir + "/" + to_string(frameNum) + ".txt";
        fstream targetFile(targetFileName.c_str(), ios::app);
        targetFile << line << endl;
        if (line == "")
        {
            frameNum++;
        }
        targetFile.close();
    }

    weightLog.close();
}

int parseInt(string arg)
{
    assert(!arg.empty());
    int res = 0;
    try
    {
        size_t pos;
        res = stoi(arg, &pos);
        if (pos < arg.size())
        {
            cerr << "Trailing characters after number: " << arg << '\n';
        }
    }
    catch (invalid_argument const &ex)
    {
        cerr << "Invalid number: " << arg << '\n';
    }
    catch (out_of_range const &ex)
    {
        cerr << "Number out of range: " << arg << '\n';
    }
    return res;
}

int main(int argc, char const *argv[])
{
    int bps = parseInt(argv[1]);
    isDiffEncoding = parseInt(argv[2]);

    // split weight log file into multiple files for each frame
    // const string weightLog = "weight.log";
    // const string weightsDir = "weights";
    // splitWeightLog(weightLog, weightsDir);

    SEncParamExt param;
    memset(&param, 0, sizeof(SEncParamExt));
    param.iUsageType = EUsageType::SCREEN_CONTENT_REAL_TIME;
    param.fMaxFrameRate = frameRate;
    param.iPicWidth = width;
    param.iPicHeight = height;
    param.iRCMode = RC_BITRATE_MODE;
    param.iTargetBitrate = bps * 1000;
    param.iMaxBitrate = UNSPECIFIED_BIT_RATE;
    param.bEnableFrameSkip = false;
    param.uiIntraPeriod = 3000;
    param.eSpsPpsIdStrategy = EParameterSetStrategy::SPS_LISTING;
    param.bPrefixNalAddingCtrl = false;
    param.bSimulcastAVC = false;
    param.bEnableDenoise = false;
    param.bEnableBackgroundDetection = true;
    param.bEnableSceneChangeDetect = true;
    param.bEnableAdaptiveQuant = true;
    param.bEnableLongTermReference = false;
    param.iLtrMarkPeriod = 30;
    param.bIsLosslessLink = false;
    param.iComplexityMode = ECOMPLEXITY_MODE::LOW_COMPLEXITY;
    param.iNumRefFrame = 1;
    param.iEntropyCodingModeFlag = 0;
    param.uiMaxNalSize = 0;
    param.iLTRRefNum = 0;
    param.iMultipleThreadIdc = 1;
    param.iLoopFilterDisableIdc = 0;
    param.sSpatialLayers[0].sSliceArgument.uiSliceNum = 1;
    param.sSpatialLayers[0].sSliceArgument.uiSliceMode = SM_SINGLE_SLICE;
    param.sSpatialLayers[0].uiProfileIdc = PRO_BASELINE;
    param.sSpatialLayers[0].uiLevelIdc = LEVEL_5_2;

    TestCallback cbk;
    BaseEncoderTest *pTest = new BaseEncoderTest();
    pTest->SetUp();
    if (isDiffEncoding)
    {
        pTest->EncodeFile(inputFileName.c_str(), &param, &cbk, outDiffOnFileName);
    }
    else
    {
        pTest->EncodeFile(inputFileName.c_str(), &param, &cbk, outDiffOffFileName);
    }
    pTest->TearDown();

    cout << "OK" << endl;

    return 0;
}