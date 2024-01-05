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
const string inputFileName = testbinDir + "cut.yuv";

const int width = 1824;
const int height = 1920;
const float inputFps = 60;
const float outputFps = 60;
const int iWidthInMb = width / 16;
const int iHeightInMb = height / 16;
const int iArraySize = iWidthInMb * iHeightInMb;

int isDiffEncoding = 0;

class BaseEncoderTest {
  public:
    struct Callback {
        virtual void onEncodeFrame(const SFrameBSInfo &frameInfo,
                                   const string &outFileName) = 0;
    };

    BaseEncoderTest();
    void SetUp();
    void TearDown();
    void EncodeFile(const char *fileName, SEncParamExt *pEncParamExt,
                    Callback *cbk, const string &outFileName);
    void EncodeStream(InputStream *in, SEncParamExt *pEncParamExt,
                      Callback *cbk, const string &outFileName);
    void CheckWeightLog(const string &fileName, int width, int height);
    void ReadPriorityArray(const string &fileName, float *priorityArray,
                           int width, int height);

    ISVCEncoder *encoder_;

  private:
};

inline bool fileExists(const string &name) {
    struct stat buffer;
    return (stat(name.c_str(), &buffer) == 0);
}

inline int countFiles(const fs::path &dir) {
    int count = 0;
    for (auto &p : fs::directory_iterator(dir)) {
        count++;
    }
    return count;
}

struct TestCallback : public BaseEncoderTest::Callback {
    virtual void onEncodeFrame(const SFrameBSInfo &frameInfo,
                               const string &outFileName) {
        int iLayer = 0;
        while (iLayer < frameInfo.iLayerNum) {
            const SLayerBSInfo *pLayerInfo = &frameInfo.sLayerInfo[iLayer++];
            if (pLayerInfo) {
                int iLayerSize = 0;
                int iNalIndex = pLayerInfo->iNalCount - 1;
                do {
                    iLayerSize += pLayerInfo->pNalLengthInByte[iNalIndex];
                    iNalIndex--;
                } while (iNalIndex >= 0);
                FILE *fp = nullptr;
                if (fopen_s(&fp, outFileName.c_str(), "ab") == 0) {
                    assert(fp != nullptr);
                    fwrite(pLayerInfo->pBsBuf, 1, iLayerSize, fp);
                    fclose(fp);
                }
            }
        }
    }
};

BaseEncoderTest::BaseEncoderTest() : encoder_(NULL) {}

void BaseEncoderTest::SetUp() {
    int rv = WelsCreateSVCEncoder(&encoder_);
    assert(rv == cmResultSuccess);
    assert(encoder_ != NULL);

    unsigned int uiTraceLevel = WELS_LOG_INFO;
    encoder_->SetOption(ENCODER_OPTION_TRACE_LEVEL, &uiTraceLevel);
}

void BaseEncoderTest::TearDown() {
    if (encoder_) {
        encoder_->Uninitialize();
        WelsDestroySVCEncoder(encoder_);
    }
}

void BaseEncoderTest::EncodeStream(InputStream *in, SEncParamExt *pEncParamExt,
                                   Callback *cbk, const string &outFileName) {
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
    pic.pData[2] = pic.pData[1] +
                   (pEncParamExt->iPicWidth * pEncParamExt->iPicHeight >> 2);

    int i = 1;
    while (in->read(buf.data(), frameSize) == frameSize) {
        int rv = -1;
        if (isDiffEncoding) {

            float priorityArray[iArraySize];
            float fAverageWeight = 0.0;
            const string weightLog = weightsDir + "/" + to_string(i++) + ".txt";
            ReadPriorityArray(weightLog, priorityArray, iWidthInMb,
                              iHeightInMb);
            rv = encoder_->EncodeFrame(&pic, &info, priorityArray);
        } else {
            rv = encoder_->EncodeFrame(&pic, &info);
        }
        assert(rv == cmResultSuccess);
        if (info.eFrameType != videoFrameTypeSkip) {
            cbk->onEncodeFrame(info, outFileName);
        }
    }
}

void BaseEncoderTest::EncodeFile(const char *fileName,
                                 SEncParamExt *pEncParamExt, Callback *cbk,
                                 const string &outFileName) {
    FileInputStream fileStream;
    bool res = fileStream.Open(fileName);
    if (fileExists(outFileName)) {
        int removeRes = remove(outFileName.c_str());
        assert(removeRes == 0);
        cout << "Removed existing file before encoding: " << outFileName
             << endl;
    }
    assert(res == true);
    EncodeStream(&fileStream, pEncParamExt, cbk, outFileName);
}

// check if the weight log file is valid
void BaseEncoderTest::CheckWeightLog(const string &fileName, int width,
                                     int height) {
    ifstream weightLog(fileName.c_str());
    string line;
    int lineNum = 0;
    while (getline(weightLog, line)) {
        lineNum++;
        if (line.empty()) {
            continue;
        }

        float num = 0;
        int colNum = 0;
        stringstream ss(line);
        while (ss >> num) {
            colNum++;
        }
        assert(colNum == width);
    }
    assert(lineNum - 1 == height);

    weightLog.close();
}

// read just one priority array from one file
void BaseEncoderTest::ReadPriorityArray(const string &fileName,
                                        float *priorityArray, int width,
                                        int height) {
    // CheckWeightLog(fileName, width, height);

    ifstream file(fileName.c_str());
    string line;
    while (getline(file, line)) {
        if (line.empty()) {
            continue;
        }

        float num = 0;
        stringstream ss(line);
        while (ss >> num) {
            *priorityArray++ = num;
        }
    }
    file.close();
}

void splitWeightLog(const string &fileName, const string &weightsDir) {
    ifstream weightLog(fileName.c_str());
    string line;
    int frameNum = 1;

    while (getline(weightLog, line)) {
        // write line to file with name: <weightsDir>/<frameNum>.txt
        string targetFileName = weightsDir + "/" + to_string(frameNum) + ".txt";
        fstream targetFile(targetFileName.c_str(), ios::app);
        targetFile << line << endl;
        if (line == "") {
            frameNum++;
        }
        targetFile.close();
    }

    weightLog.close();
}

int parseInt(const string &s) {
    assert(!s.empty());
    int res = 0;
    try {
        size_t pos;
        res = stoi(s, &pos);
        if (pos < s.size()) {
            cerr << "Trailing characters after number: " << s << '\n';
        }
    } catch (invalid_argument const &) {
        cerr << "Invalid number: " << s << '\n';
    } catch (out_of_range const &) {
        cerr << "Number out of range: " << s << '\n';
    }
    return res;
}

float parseFloat(const string &s) {
    assert(!s.empty());
    float res = 0.0;
    try {
        size_t pos;
        res = stof(s, &pos);
        if (pos < s.size()) {
            cerr << "Trailing characters after number: " << s << '\n';
        }
    } catch (invalid_argument const &) {
        cerr << "Invalid number: " << s << '\n';
    } catch (out_of_range const &) {
        cerr << "Number out of range: " << s << '\n';
    }
    return res;
}

int main(int argc, char const *argv[]) {
    // split weight log file into multiple files for each frame
    const string weightsDir = testbinDir + "weights";
    const string weightLog = testbinDir + "weight_cut.log";
    if (!fs::is_directory(weightsDir)) {
        fs::create_directory(weightsDir);
        splitWeightLog(weightLog, weightsDir);
    }

    // parse input and process yuv file
    isDiffEncoding = parseInt(argv[1]);
    float targetBitrate = parseFloat(argv[2]);

    SEncParamExt param;
    memset(&param, 0, sizeof(SEncParamExt));
    param.iUsageType = EUsageType::CAMERA_VIDEO_REAL_TIME;
    param.bSimulcastAVC = false;
    param.iPicWidth = width;
    param.iPicHeight = height;
    param.fMaxFrameRate = outputFps;
    param.iTemporalLayerNum = 1;
    param.uiIntraPeriod = 0;
    param.eSpsPpsIdStrategy = EParameterSetStrategy::INCREASING_ID;
    param.bEnableFrameCroppingFlag = 1;
    param.iEntropyCodingModeFlag = 0;
    param.uiMaxNalSize = 0;
    param.iComplexityMode = ECOMPLEXITY_MODE::LOW_COMPLEXITY;
    param.iLoopFilterDisableIdc = 0;
    param.iLoopFilterAlphaC0Offset = 0;
    param.iLoopFilterBetaOffset = 0;
    param.iMultipleThreadIdc = 1;
    param.bUseLoadBalancing = true;
    param.iRCMode = RC_BITRATE_MODE;
    param.iTargetBitrate = 288000000;
    param.iMaxBitrate = UNSPECIFIED_BIT_RATE;
    param.bEnableFrameSkip = false;
    param.iMaxQp = 51;
    param.iMinQp = 0;
    param.bEnableDenoise = false;
    param.bEnableSceneChangeDetect = false;
    param.bEnableBackgroundDetection = false;
    param.bEnableAdaptiveQuant = false;
    param.bEnableLongTermReference = false;
    param.iLtrMarkPeriod = 30;
    param.bPrefixNalAddingCtrl = false;
    param.iSpatialLayerNum = 1;

    SSpatialLayerConfig *pDLayer = &param.sSpatialLayers[0];
    pDLayer->iVideoWidth = width;
    pDLayer->iVideoHeight = height;
    pDLayer->fFrameRate = outputFps;
    pDLayer->uiProfileIdc = PRO_BASELINE;
    pDLayer->iSpatialBitrate = (int)(targetBitrate * 1000 * 1000);
    pDLayer->iMaxSpatialBitrate = UNSPECIFIED_BIT_RATE;
    pDLayer->iDLayerQp = 24;
    pDLayer->sSliceArgument.uiSliceMode = SM_SINGLE_SLICE;
    // param.iMinQp = iMinQp;
    // param.iMaxQp = iMaxQp;

    const string diffSuffix = isDiffEncoding ? "-diff" : "";
    // const string qpSuffix = "-minqp" + to_string(iMinQp) + "-maxqp" +
    // to_string(iMaxQp); const string outFileName = testbinDir + "out" +
    // diffSuffix + qpSuffix + ".h264";
    const string outFileDir = testbinDir + to_string(targetBitrate) + "m/";
    if (!fs::is_directory(outFileDir)) {
        assert(fs::create_directory(outFileDir) == true);
    }
    const string outFileName = outFileDir + "out" + diffSuffix + ".h264";

    TestCallback cbk;
    BaseEncoderTest *pTest = new BaseEncoderTest();
    pTest->SetUp();
    pTest->EncodeFile(inputFileName.c_str(), &param, &cbk, outFileName);
    pTest->TearDown();

    cout << "OK" << endl;

    return 0;
}
