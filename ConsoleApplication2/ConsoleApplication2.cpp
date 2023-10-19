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
    // 创建H264编码器实例
    ISVCEncoder *encoder_ = nullptr;
    int rv = WelsCreateSVCEncoder (&encoder_);
    assert (0==rv);
    assert (encoder_ != nullptr);

    int width = 640;
    int height = 480;
    int total_num = 100;

    // 设置编码器参数
    SEncParamBase param;
    memset (&param, 0, sizeof (SEncParamBase));
    param.iUsageType = CAMERA_VIDEO_REAL_TIME;  // 使用类型为实时摄像头视频
    param.fMaxFrameRate = 30;  // 帧率为30
    param.iPicWidth = width;  // 图片宽度
    param.iPicHeight = height;  // 图片高度
    param.iTargetBitrate = 5000000;  // 目标比特率为5000000
    encoder_->Initialize (&param);  // 初始化编码器

	double t = (double)cv::getTickCount();  // 开始计时

    // 读取JPEG图片，并转换为YUV格式
    Mat image = imread("test.jpeg", IMREAD_COLOR );

    t = ((double)cv::getTickCount() - t) / cv::getTickFrequency();  // 计算经过的时间
    cout << "read jpg cost: " << t << endl;

    t = (double)cv::getTickCount();  // 开始计时

    Mat imageResized, imageYuv, imageYuvMini; 
    resize(image, imageResized, Size(width, height));// 调整图片大小
    Mat imageYuvCh[3], imageYuvMiniCh[3];
    cvtColor(imageResized, imageYuv, cv::COLOR_BGR2YUV);// 将图片转换为YUV格式

    //首先，split 函数用于将图片的三个通道（Y、U、V）分离。
    //这是因为在YUV格式中，Y通道（亮度）包含了大部分的视觉信息，而U和V通道（色度）的信息可以降采样以减少数据量。
    split(imageYuv, imageYuvCh);

    //然后，resize 函数用于将图片调整到编码器期望的大小。在这个例子中，图片被调整到了一半的大小，
    //这是因为在许多视频编码标准中，色度通道的分辨率通常是亮度通道的一半，这种技术被称为色度子采样（Chroma Subsampling）。
    resize(imageYuv, imageYuvMini, Size(width/2, height/2));

    //最后，再次使用 split 函数将调整大小后的图片的三个通道分离，以便于后续的编码操作。
    split(imageYuvMini, imageYuvMiniCh);

    t = ((double)cv::getTickCount() - t) / cv::getTickFrequency();  // 计算经过的时间
    cout << "cvtcolor cost: " << t << endl;

    

    //SFrameBSInfo 是OpenH264编码器用来存储编码后的比特流信息的结构体。全称应该是SFrameBitStreamInfo
    SFrameBSInfo info;
    memset (&info, 0, sizeof (SFrameBSInfo));

    

    // 将编码后的数据写入到文件中
    ofstream outFi;
    outFi.open ("test.264", ios::out | ios::binary);

    //SSourcePicture 是OpenH264编码器用来存储待编码的图片信息的结构体。
	SSourcePicture pic;  // 创建一个SSourcePicture结构体实例

    for(int num = 0; num<total_num; num++) 
    {
        t = (double)cv::getTickCount();  // 开始计时

        // 将YUV格式的图片数据传递给编码器进行编码

        /*
        if(num%2==0)
        {
	        memset (&pic, 0, sizeof (SSourcePicture));  // 使用memset函数将其初始化为0
		    pic.iPicWidth = width;  // 设置图片的宽度
		    pic.iPicHeight = height;  // 设置图片的高度
		    pic.iColorFormat = videoFormatI420;  // 设置图片的颜色格式为I420
	        pic.iStride[0] = imageYuvCh[0].step;  // 设置Y通道的步长
		    pic.iStride[1] = imageYuvMiniCh[1].step;  // 设置U通道的步长
		    pic.iStride[2] = imageYuvMiniCh[2].step;  // 设置V通道的步长
		    pic.pData[0] = imageYuvCh[0].data;  // 设置Y通道的数据指针
		    pic.pData[1] = imageYuvMiniCh[1].data;  // 设置U通道的数据指针
		    pic.pData[2] = imageYuvMiniCh[2].data;  // 设置V通道的数据指针
        }
        else//插入空帧
        {
	        memset (&pic, 0, sizeof (SSourcePicture));  // 使用memset函数将其初始化为0
		    pic.iPicWidth = width;  // 设置图片的宽度
		    pic.iPicHeight = height;  // 设置图片的高度
		    pic.iColorFormat = videoFormatI420;  // 设置图片的颜色格式为I420
	        pic.iStride[0] = 0;
			pic.iStride[1] = 0;
			pic.iStride[2] = 0;
			pic.pData[0] = NULL;
			pic.pData[1] = NULL;
			pic.pData[2] = NULL;
        }*/
        
        //实测发现，插入空帧后，文件变大了，播放的时候会闪烁。
        //在视频编码中，编码器通常会对连续的帧进行差分编码，也就是只编码相邻帧之间的差异。这种方法可以大大减少编码后的数据量，因为在许多情况下，相邻的帧之间的差异很小。
		//然而，当你插入一个空帧时，这个空帧与前一帧之间的差异可能会非常大，因为空帧中的所有像素都是0，而前一帧中的像素值可能有很大的变化。这就导致了编码器需要编码大量的差异数据，从而增加了编码后的数据量。
		//此外，由于空帧中的所有像素都是0，所以编码器可能无法有效地利用空帧中的空间冗余和时间冗余，这也可能导致编码后的数据量增加。

        //所以如果在录屏的时候想降低录制的帧率，跳过一些帧，那么方法是使用重复帧而不是空帧。
        //比如说游戏第10帧，第20帧，中间10帧要跳过，那么就一直使用第10帧的数据，而不是使用空帧。


    	if(num%2==0)
        {
            memset (&pic, 0, sizeof (SSourcePicture));  // 使用memset函数将其初始化为0
		    pic.iPicWidth = width;  // 设置图片的宽度
		    pic.iPicHeight = height;  // 设置图片的高度
		    pic.iColorFormat = videoFormatI420;  // 设置图片的颜色格式为I420
	        pic.iStride[0] = imageYuvCh[0].step;  // 设置Y通道的步长
		    pic.iStride[1] = imageYuvMiniCh[1].step;  // 设置U通道的步长
		    pic.iStride[2] = imageYuvMiniCh[2].step;  // 设置V通道的步长
		    pic.pData[0] = imageYuvCh[0].data;  // 设置Y通道的数据指针
		    pic.pData[1] = imageYuvMiniCh[1].data;  // 设置U通道的数据指针
		    pic.pData[2] = imageYuvMiniCh[2].data;  // 设置V通道的数据指针
        }
        else//使用重复帧，而不是使用空帧，即直接使用上一帧的数据
        {
	        
        }

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

        t = ((double)cv::getTickCount() - t) / cv::getTickFrequency();  // 计算经过的时间
		cout << "encode cost: " << t << endl;
    }

    if (encoder_) {
        encoder_->Uninitialize();
        WelsDestroySVCEncoder (encoder_);
    }

    outFi.close();
}