//#include <opencv2/opencv.hpp>  
//#include <opencv2/core/core.hpp>  
//#include <opencv2/highgui/highgui.hpp>  
//#include <opencv2/imgproc.hpp>  
//#include<iostream>  
//using namespace std;  
//using namespace cv;  
//int main()  
//{  
//    Mat image = Mat::zeros(300, 600, CV_8UC3);  
//    circle(image, Point(300, 200), 100, Scalar(25, 110, 288),-100);  
//    circle(image, Point(400, 200), 100, Scalar(255, 123, 127), -100);  
//    imshow("Show Window", image);  
//    waitKey(0);  
//    return 0;  
//}  


#include <codec_api.h>
#include <cassert>
#include <cstring>
#include <vector>
#include <fstream>
#include <iostream>

//Tested with OpenCV 3.3
#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/highgui.hpp>
#include <opencv2/imgproc.hpp>

using namespace std;
using namespace cv;

int main()
{
    ISVCEncoder *encoder_ = nullptr;
    int rv = WelsCreateSVCEncoder (&encoder_);
    assert (0==rv);
    assert (encoder_ != nullptr);

    int width = 640;
    int height = 480;
    int total_num = 100;

    SEncParamBase param;
    memset (&param, 0, sizeof (SEncParamBase));
    param.iUsageType = CAMERA_VIDEO_REAL_TIME;
    param.fMaxFrameRate = 30;
    param.iPicWidth = width;
    param.iPicHeight = height;
    param.iTargetBitrate = 5000000;
    encoder_->Initialize (&param);

    Mat image = imread("test.jpeg", IMREAD_COLOR );
    Mat imageResized, imageYuv, imageYuvMini; 
    resize(image, imageResized, Size(width, height));
    Mat imageYuvCh[3], imageYuvMiniCh[3];
    cvtColor(imageResized, imageYuv, cv::COLOR_BGR2YUV);
    split(imageYuv, imageYuvCh);
    resize(imageYuv, imageYuvMini, Size(width/2, height/2));
    split(imageYuvMini, imageYuvMiniCh);

    SFrameBSInfo info;
    memset (&info, 0, sizeof (SFrameBSInfo));
    SSourcePicture pic;
    memset (&pic, 0, sizeof (SSourcePicture));
    pic.iPicWidth = width;
    pic.iPicHeight = height;
    pic.iColorFormat = videoFormatI420;
    pic.iStride[0] = imageYuvCh[0].step;
    pic.iStride[1] = imageYuvMiniCh[1].step;
    pic.iStride[2] = imageYuvMiniCh[2].step;
    pic.pData[0] = imageYuvCh[0].data;
    pic.pData[1] = imageYuvMiniCh[1].data;
    pic.pData[2] = imageYuvMiniCh[2].data;

    ofstream outFi;
    outFi.open ("test.264", ios::out | ios::binary);

    for(int num = 0; num<total_num; num++) 
    {
        //prepare input data
        rv = encoder_->EncodeFrame (&pic, &info);
        assert (rv == cmResultSuccess);
        if (info.eFrameType != videoFrameTypeSkip /*&& cbk != nullptr*/) 
        {
            //output bitstream
            for (int iLayer=0; iLayer < info.iLayerNum; iLayer++)
            {
                SLayerBSInfo* pLayerBsInfo = &info.sLayerInfo[iLayer];

                int iLayerSize = 0;
                int iNalIdx = pLayerBsInfo->iNalCount - 1;
                do {
                    iLayerSize += pLayerBsInfo->pNalLengthInByte[iNalIdx];
                    --iNalIdx;
                } while (iNalIdx >= 0);

                unsigned char *outBuf = pLayerBsInfo->pBsBuf;
                outFi.write((char *)outBuf, iLayerSize);
            }

        }
    }

    if (encoder_) {
        encoder_->Uninitialize();
        WelsDestroySVCEncoder (encoder_);
    }

    outFi.close();
}