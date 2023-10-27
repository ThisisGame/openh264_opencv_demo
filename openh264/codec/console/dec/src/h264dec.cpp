/*!
 * \copy
 *     Copyright (c)  2004-2013, Cisco Systems
 *     All rights reserved.
 *
 *     Redistribution and use in source and binary forms, with or without
 *     modification, are permitted provided that the following conditions
 *     are met:
 *
 *        * Redistributions of source code must retain the above copyright
 *          notice, this list of conditions and the following disclaimer.
 *
 *        * Redistributions in binary form must reproduce the above copyright
 *          notice, this list of conditions and the following disclaimer in
 *          the documentation and/or other materials provided with the
 *          distribution.
 *
 *     THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *     "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *     LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 *     FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 *     COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 *     INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 *     BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 *     LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 *     CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 *     LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 *     ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *     POSSIBILITY OF SUCH DAMAGE.
 *
 * h264dec.cpp:         Wels Decoder Console Implementation file
 */

#if defined (_WIN32)
#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <tchar.h>
#else
#include <string.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#if defined (ANDROID_NDK)
#include <android/log.h>
#endif
#include "codec_def.h"
#include "codec_app_def.h"
#include "codec_api.h"
#include "read_config.h"
#include "typedefs.h"
#include "measure_time.h"
#include "d3d9_utils.h"

using namespace std;

#if defined (WINDOWS_PHONE)
double g_dDecTime = 0.0;
float  g_fDecFPS = 0.0;
int    g_iDecodedFrameNum = 0;
#endif

#if defined(ANDROID_NDK)
#define LOG_TAG "welsdec"
#define LOGI(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
#define printf LOGI
#define fprintf(a, ...) LOGI(__VA_ARGS__)
#endif
//using namespace WelsDec;

/**
 * @brief 从给定的缓冲区中读取一个位，并更新当前位的位置。
 * @param pBufPtr 指向缓冲区的指针
 * @param curBit 当前位的位置
 * @return 返回当前位的值
 */
int32_t readBit (uint8_t* pBufPtr, int32_t& curBit) {
  int nIndex = curBit / 8;  // 计算当前位在字节中的索引
  int nOffset = curBit % 8 + 1;  // 计算当前位在字节中的偏移量
  curBit++;  // 更新当前位
  return (pBufPtr[nIndex] >> (8 - nOffset)) & 0x01;  // 返回当前位的值
}

/**
 * @brief 从给定的缓冲区中读取n个位，并将它们合并为一个整数。
 * @param pBufPtr 指向缓冲区的指针
 * @param n 要读取的位数
 * @param curBit 当前位的位置
 * @return 返回合并后的整数
 */
int32_t readBits (uint8_t* pBufPtr, int32_t& n, int32_t& curBit) {
  int r = 0;
  for (int i = 0; i < n; i++) {
    r |= (readBit (pBufPtr, curBit) << (n - i - 1));  // 读取n个位，并将它们合并为一个整数
  }
  return r;
}

/**
 * @brief 从给定的缓冲区中读取一个无符号指数哥伦布编码。
 * @param pBufPtr 指向缓冲区的指针
 * @param curBit 当前位的位置
 * @return 返回无符号指数哥伦布编码
 */
int32_t bsGetUe (uint8_t* pBufPtr, int32_t& curBit) {
  int r = 0;
  int i = 0;
  while ((readBit (pBufPtr, curBit) == 0) && (i < 32)) {  // 读取连续的0，直到遇到1
    i++;
  }
  r = readBits (pBufPtr, i, curBit);  // 读取一个长度为i的无符号指数哥伦布编码
  r += (1 << i) - 1;
  return r;
}

/**
 * @brief 读取切片中的第一个宏块。
 * @param pSliceNalPtr 指向切片NAL的指针
 * @return 返回切片中的第一个宏块
 */
int32_t readFirstMbInSlice (uint8_t* pSliceNalPtr) {
  int32_t curBit = 0;
  int32_t firstMBInSlice = bsGetUe (pSliceNalPtr + 1, curBit);  // 读取切片中的第一个宏块
  return firstMBInSlice;
}

/**
 * @brief 从输入的H.264流中读取一张图片（或者说一帧）。
 * @param pBuf 指向输入H.264流的指针。
 * @param iFileSize 输入流的大小。
 * @param bufPos 当前在输入流中的位置。
 * @param pSpsBuf 用于存储序列参数集（SPS）的指针。
 * @param sps_byte_count SPS的字节大小。
 * @return 读取的字节数。如果可用的字节数小于4，返回可用的字节数。如果读取的字节数大于等于可用的字节数减4，返回可用的字节数。
 * @note 这个函数会从pBuf指向的位置开始，读取一个完整的编码帧，并将这个帧的大小返回。同时，如果这个帧包含一个新的SPS，那么这个函数还会更新pSpsBuf和sps_byte_count。
 */
int32_t readPicture (uint8_t* pBuf, const int32_t& iFileSize, const int32_t& bufPos, uint8_t*& pSpsBuf,
                     int32_t& sps_byte_count) {
  // 计算可用的字节数
  int32_t bytes_available = iFileSize - bufPos;
  // 如果可用的字节数小于4，直接返回
  if (bytes_available < 4) {
    return bytes_available;
  }
  // 初始化指针位置
  uint8_t* ptr = pBuf + bufPos;
  // 初始化读取的字节数
  int32_t read_bytes = 0;
  // 初始化SPS、PPS、非IDR图像、IDR图像和NAL分隔符的数量
  int32_t sps_count = 0;
  int32_t pps_count = 0;
  int32_t non_idr_pict_count = 0;
  int32_t idr_pict_count = 0;
  int32_t nal_deliminator = 0;
  // 初始化SPS缓冲区和SPS字节数
  pSpsBuf = NULL;
  sps_byte_count = 0;
  // 遍历所有可用的字节
  while (read_bytes < bytes_available - 4) {
    // 检查是否存在4字节的起始码
    bool has4ByteStartCode = ptr[0] == 0 && ptr[1] == 0 && ptr[2] == 0 && ptr[3] == 1;
    // 如果不存在4字节的起始码，检查是否存在3字节的起始码
    bool has3ByteStartCode = false;
    if (!has4ByteStartCode) {
      has3ByteStartCode = ptr[0] == 0 && ptr[1] == 0 && ptr[2] == 1;
    }
    // 如果存在4字节或3字节的起始码
    if (has4ByteStartCode || has3ByteStartCode) {
      // 计算NAL单元类型
      int32_t byteOffset = has4ByteStartCode ? 4 : 3;
      uint8_t nal_unit_type = has4ByteStartCode ? (ptr[4] & 0x1F) : (ptr[3] & 0x1F);
      // 如果NAL单元类型为1（非IDR图像）
      if (nal_unit_type == 1) {
        // 读取第一个宏块在切片中的位置
        int32_t firstMBInSlice = readFirstMbInSlice (ptr + byteOffset);
        // 如果非IDR图像数量大于等于1，且IDR图像数量大于等于1，且第一个宏块在切片中的位置为0，返回读取的字节数
        if (++non_idr_pict_count >= 1 && idr_pict_count >= 1 && firstMBInSlice == 0) {
          return read_bytes;
        }
      } else if (nal_unit_type == 5) {
        // 如果NAL单元类型为5（IDR图像）
        // 读取第一个宏块在切片中的位置
        int32_t firstMBInSlice = readFirstMbInSlice (ptr + byteOffset);
        // 如果IDR图像数量大于等于1，且非IDR图像数量大于等于1，且第一个宏块在切片中的位置为0，返回读取的字节数
        if (++idr_pict_count >= 1 && non_idr_pict_count >= 1 && firstMBInSlice == 0) {
          return read_bytes;
        }
        // 如果IDR图像数量大于等于2，且第一个宏块在切片中的位置为0，返回读取的字节数
        if (idr_pict_count >= 2 && firstMBInSlice == 0) {
          return read_bytes;
        }
      } else if (nal_unit_type == 7) {
        // 如果NAL单元类型为7（SPS）
        // 设置SPS缓冲区的位置
        pSpsBuf = ptr + (has4ByteStartCode ? 4 : 3);
        // 如果SPS数量大于等于1，且非IDR图像数量大于等于1或IDR图像数量大于等于1，返回读取的字节数
        if ((++sps_count >= 1) && (non_idr_pict_count >= 1 || idr_pict_count >= 1)) {
          return read_bytes;
        }
        // 如果SPS数量等于2，返回读取的字节数
        if (sps_count == 2) {
          return read_bytes;
        }
      } else if (nal_unit_type == 8) {
        // 如果NAL单元类型为8（PPS）
        // 如果PPS数量等于1且SPS数量等于1，设置SPS字节数
        if (++pps_count == 1 && sps_count == 1) {
          sps_byte_count = int32_t (ptr - pSpsBuf);
        }
        // 如果PPS数量大于等于1，且非IDR图像数量大于等于1或IDR图像数量大于等于1，返回读取的字节数
        if (pps_count >= 1 && (non_idr_pict_count >= 1 || idr_pict_count >= 1)) {
          return read_bytes;
        }
      } else if (nal_unit_type == 9) {
        // 如果NAL单元类型为9（NAL分隔符）
        // 如果NAL分隔符数量等于2，返回读取的字节数
        if (++nal_deliminator == 2) {
          return read_bytes;
        }
      }
      // 如果读取的字节数大于等于可用的字节数减4，返回可用的字节数
      if (read_bytes >= bytes_available - 4) {
        return bytes_available;
      }
      // 读取下一个字节
      read_bytes += 4;
      ptr += 4;
    } else {
      // 如果不存在起始码，移动到下一个字节
      ++ptr;
      ++read_bytes;
    }
  }
  // 返回可用的字节数
  return bytes_available;
}

/**
 * @brief 清空解码器中的所有待处理帧，并处理所有已解码的帧。
 * @param pDecoder 指向解码器的指针。
 * @param iTotal 用于记录解码时间的变量。
 * @param pYuvFile 指向YUV文件的指针，用于存储解码后的帧。
 * @param pOptionFile 指向选项文件的指针，用于存储帧数、宽度和高度。
 * @param iFrameCount 用于记录帧数的变量。
 * @param uiTimeStamp 时间戳。
 * @param iWidth 用于记录宽度的变量。
 * @param iHeight 用于记录高度的变量。
 * @param iLastWidth 用于记录上一帧的宽度的变量。
 * @param iLastHeight 用于记录上一帧的高度的变量。
 * @note 这个函数首先获取解码器中剩余的帧数，然后遍历这些帧，对每一帧进行解码，并将解码后的帧写入到YUV文件中。如果选项文件存在，且帧的宽度或高度发生变化，那么这个函数还会将帧数、宽度和高度写入到选项文件中。
 */
void FlushFrames (ISVCDecoder* pDecoder, int64_t& iTotal, FILE* pYuvFile, FILE* pOptionFile, int32_t& iFrameCount,
                  unsigned long long& uiTimeStamp, int32_t& iWidth, int32_t& iHeight, int32_t& iLastWidth, int32_t iLastHeight) {
  // 初始化数据和目标缓冲区
  uint8_t* pData[3] = { NULL };
  uint8_t* pDst[3] = { NULL };
  // 初始化缓冲区信息
  SBufferInfo sDstBufInfo;
  // 初始化缓冲区中的帧数
  int32_t num_of_frames_in_buffer = 0;
  // 创建输出模块对象
  CUtils cOutputModule;
  // 获取缓冲区中剩余的帧数
  pDecoder->GetOption (DECODER_OPTION_NUM_OF_FRAMES_REMAINING_IN_BUFFER, &num_of_frames_in_buffer);
  // 遍历缓冲区中的所有帧
  for (int32_t i = 0; i < num_of_frames_in_buffer; ++i) {
    // 记录开始时间
    int64_t iStart = WelsTime();
    // 清空数据缓冲区和缓冲区信息
    pData[0] = NULL;
    pData[1] = NULL;
    pData[2] = NULL;
    memset (&sDstBufInfo, 0, sizeof (SBufferInfo));
    // 设置时间戳
    sDstBufInfo.uiInBsTimeStamp = uiTimeStamp;
    // 刷新帧
    pDecoder->FlushFrame (pData, &sDstBufInfo);
    // 如果缓冲区状态为1，表示解码成功
    if (sDstBufInfo.iBufferStatus == 1) {
      // 获取解码后的数据
      pDst[0] = sDstBufInfo.pDst[0];
      pDst[1] = sDstBufInfo.pDst[1];
      pDst[2] = sDstBufInfo.pDst[2];
    }
    // 记录结束时间，并累加总时间
    int64_t iEnd = WelsTime();
    iTotal += iEnd - iStart;
    // 如果缓冲区状态为1，表示解码成功
    if (sDstBufInfo.iBufferStatus == 1) {
      // 处理解码后的数据
      cOutputModule.Process ((void**)pDst, &sDstBufInfo, pYuvFile);
      // 获取解码后的宽度和高度
      iWidth = sDstBufInfo.UsrData.sSystemBuffer.iWidth;
      iHeight = sDstBufInfo.UsrData.sSystemBuffer.iHeight;
      // 如果选项文件存在，且宽度或高度发生变化，将帧数、宽度和高度写入选项文件
      if (pOptionFile != NULL) {
        if (iWidth != iLastWidth && iHeight != iLastHeight) {
          // 将帧数、宽度和高度写入选项文件
          fwrite (&iFrameCount, sizeof (iFrameCount), 1, pOptionFile);
          fwrite (&iWidth, sizeof (iWidth), 1, pOptionFile);
          fwrite (&iHeight, sizeof (iHeight), 1, pOptionFile);
          // 更新最后的宽度和高度
          iLastWidth = iWidth;
          iLastHeight = iHeight;
        }
      }
      // 增加帧数
      ++iFrameCount;
    }
  }
}
void H264DecodeInstance (ISVCDecoder* pDecoder, const char* kpH264FileName, const char* kpOuputFileName,
                         int32_t& iWidth, int32_t& iHeight, const char* pOptionFileName, const char* pLengthFileName,
                         int32_t iErrorConMethod,
                         bool bLegacyCalling) {
  // 初始化各种文件指针                        
  FILE* pH264File   = NULL;
  FILE* pYuvFile    = NULL;
  FILE* pOptionFile = NULL;
// Lenght input mode support
  FILE* fpTrack = NULL;

  // 如果解码器为空，则直接返回
  if (pDecoder == NULL) return;

  // 初始化各种参数和缓冲区
  int32_t pInfo[4];
  unsigned long long uiTimeStamp = 0;
  int64_t iStart = 0, iEnd = 0, iTotal = 0;
  int32_t iSliceSize;
  int32_t iSliceIndex = 0;
  uint8_t* pBuf = NULL;
  uint8_t uiStartCode[4] = {0, 0, 0, 1};

  uint8_t* pData[3] = {NULL};
  uint8_t* pDst[3] = {NULL};
  SBufferInfo sDstBufInfo;

  int32_t iBufPos = 0;
  int32_t iFileSize;
  int32_t iLastWidth = 0, iLastHeight = 0;
  int32_t iFrameCount = 0;
  int32_t iEndOfStreamFlag = 0;

  // 设置解码器的错误掩盖方法
  pDecoder->SetOption (DECODER_OPTION_ERROR_CON_IDC, &iErrorConMethod);

  // 初始化工具类
  CUtils cOutputModule;
  double dElapsed = 0;
  uint8_t uLastSpsBuf[32];
  int32_t iLastSpsByteCount = 0;

  // 获取解码器的线程数
  int32_t iThreadCount = 1;
  pDecoder->GetOption (DECODER_OPTION_NUM_OF_THREADS, &iThreadCount);

  // 如果H264文件名不为空，则打开文件
  pH264File = fopen (kpH264FileName, "rb");

  // 如果输出文件名不为空，则打开文件
  pYuvFile = fopen (kpOuputFileName, "wb");


  printf ("------------------------------------------------------\n");

  // 获取H264文件的大小
  fseek (pH264File, 0L, SEEK_END);
  iFileSize = (int32_t) ftell (pH264File);
  if (iFileSize <= 4) {
    fprintf (stderr, "Current Bit Stream File is too small, read error!!!!\n");
    goto label_exit;
  }
  fseek (pH264File, 0L, SEEK_SET);

  // 分配缓冲区
  pBuf = new uint8_t[iFileSize + 4];
  if (pBuf == NULL) {// 如果缓冲区分配失败，则打印错误信息并跳转到退出标签
    fprintf (stderr, "new buffer failed!\n");
    goto label_exit;
  }

  // 读取H264文件到缓冲区
  if (fread (pBuf, 1, iFileSize, pH264File) != (uint32_t)iFileSize) {
    fprintf (stderr, "Unable to read whole file\n");
    goto label_exit;
  }

  // 设置缓冲区的结束标志
  memcpy (pBuf + iFileSize, &uiStartCode[0], 4); //confirmed_safe_unsafe_usage

  // 开始解码过程
  while (true) {
    // 如果缓冲区位置大于等于文件大小，表示已经读取完所有数据
    if (iBufPos >= iFileSize) {
      // 设置结束标志
      iEndOfStreamFlag = true;
      // 如果结束标志为真，设置解码器的结束标志选项
      if (iEndOfStreamFlag)
        pDecoder->SetOption (DECODER_OPTION_END_OF_STREAM, (void*)&iEndOfStreamFlag);
      break;
    }
    // 如果长度文件存在，从文件中读取长度
    if (fpTrack) {
      if (fread (pInfo, 4, sizeof (int32_t), fpTrack) < 4)
        goto label_exit;
      iSliceSize = static_cast<int32_t> (pInfo[2]);
    } else {
      // 如果线程数大于等于1，读取图片
      if (iThreadCount >= 1) {
        uint8_t* uSpsPtr = NULL;
        int32_t iSpsByteCount = 0;
        iSliceSize = readPicture (pBuf, iFileSize, iBufPos, uSpsPtr, iSpsByteCount);

        /*
        如果新序列与前序列不同，必须刷新所有待处理的帧，然后才能开始解码新序列
        这段代码的目的是在新的序列开始解码之前，清空解码器中所有待处理的帧。
        在这里，"序列"是指一组连续的帧，它们共享相同的参数设置，例如分辨率、帧率等。在H.264编码中，这些参数设置通常在序列参数集（SPS）中定义。
        当新的序列开始时（即新的SPS出现），我们需要确保解码器中没有旧序列的帧。
        这是因为旧序列的帧可能使用了与新序列不同的参数设置，如果我们不清空这些帧，就可能导致解码错误或者视频播放异常。
        */
        if (iLastSpsByteCount > 0 && iSpsByteCount > 0) {
          if (iSpsByteCount != iLastSpsByteCount || memcmp (uSpsPtr, uLastSpsBuf, iLastSpsByteCount) != 0) {
            //whenever new sequence is different from preceding sequence. All pending frames must be flushed out before the new sequence can start to decode.
            FlushFrames (pDecoder, iTotal, pYuvFile, pOptionFile, iFrameCount, uiTimeStamp, iWidth, iHeight, iLastWidth,
                         iLastHeight);
          }
        }
        // 如果SPS字节计数大于0，复制SPS缓冲区
        if (iSpsByteCount > 0 && uSpsPtr != NULL) {
          if (iSpsByteCount > 32) iSpsByteCount = 32;
          iLastSpsByteCount = iSpsByteCount;
          memcpy (uLastSpsBuf, uSpsPtr, iSpsByteCount);
        }
      } else {
        // 如果线程数小于1，查找下一个NALU的开始位置
        int i = 0;
        for (i = 0; i < iFileSize; i++) {
          if ((pBuf[iBufPos + i] == 0 && pBuf[iBufPos + i + 1] == 0 && pBuf[iBufPos + i + 2] == 0 && pBuf[iBufPos + i + 3] == 1
               && i > 0) || (pBuf[iBufPos + i] == 0 && pBuf[iBufPos + i + 1] == 0 && pBuf[iBufPos + i + 2] == 1 && i > 0)) {
            break;
          }
        }
        iSliceSize = i;
      }
    }
    // 如果NALU的大小小于4，表示没有有效数据，忽略
    if (iSliceSize < 4) { //too small size, no effective data, ignore
      iBufPos += iSliceSize;
      continue;
    }

    // 获取解码器的各种选项
    //for coverage test purpose
    int32_t iEndOfStreamFlag;
    pDecoder->GetOption (DECODER_OPTION_END_OF_STREAM, &iEndOfStreamFlag);
    int32_t iCurIdrPicId;
    pDecoder->GetOption (DECODER_OPTION_IDR_PIC_ID, &iCurIdrPicId);
    int32_t iFrameNum;
    pDecoder->GetOption (DECODER_OPTION_FRAME_NUM, &iFrameNum);
    int32_t bCurAuContainLtrMarkSeFlag;
    pDecoder->GetOption (DECODER_OPTION_LTR_MARKING_FLAG, &bCurAuContainLtrMarkSeFlag);
    int32_t iFrameNumOfAuMarkedLtr;
    pDecoder->GetOption (DECODER_OPTION_LTR_MARKED_FRAME_NUM, &iFrameNumOfAuMarkedLtr);
    int32_t iFeedbackVclNalInAu;
    pDecoder->GetOption (DECODER_OPTION_VCL_NAL, &iFeedbackVclNalInAu);
    int32_t iFeedbackTidInAu;
    pDecoder->GetOption (DECODER_OPTION_TEMPORAL_ID, &iFeedbackTidInAu);
    //~end for

    // 记录开始时间
    iStart = WelsTime();
    pData[0] = NULL;
    pData[1] = NULL;
    pData[2] = NULL;
    uiTimeStamp ++;
    memset (&sDstBufInfo, 0, sizeof (SBufferInfo));
    sDstBufInfo.uiInBsTimeStamp = uiTimeStamp;

    // sDstBufInfo是一个SBufferInfo类型的结构体，它存储了解码后的帧的信息。具体来说，它包含以下字段：
    // - iBufferStatus：缓冲区状态，如果为1，表示解码成功。
    // - uiInBsTimeStamp：时间戳。
    // - UsrData.sSystemBuffer.iWidth和UsrData.sSystemBuffer.iHeight：解码后的帧的宽度和高度。
    // - pDst：解码后的帧数据，包括Y（亮度）分量、U（色差）分量和V（色差）分量。

    // 在解码H.264流时，sDstBufInfo被用于存储解码后的帧的信息，以便于后续的处理，例如写入到YUV文件中。

    // 调用解码器的解码函数
    if (!bLegacyCalling) {
      pDecoder->DecodeFrameNoDelay (pBuf + iBufPos, iSliceSize, pData, &sDstBufInfo);
    } else {
      pDecoder->DecodeFrame2 (pBuf + iBufPos, iSliceSize, pData, &sDstBufInfo);
    }

    // 如果解码成功，处理解码后的数据
    if (sDstBufInfo.iBufferStatus == 1) {
      //sDstBufInfo.pDst存储的是解码后的帧数据。
      //在H.264解码中，每一帧都由三个部分组成：Y（亮度）分量、U（色差）分量和V（色差）分量。
      //这三个分量分别存储在sDstBufInfo.pDst[0]、sDstBufInfo.pDst[1]和sDstBufInfo.pDst[2]中。
      pDst[0] = sDstBufInfo.pDst[0];
      pDst[1] = sDstBufInfo.pDst[1];
      pDst[2] = sDstBufInfo.pDst[2];
    }
    // 记录结束时间，并累加总时间
    iEnd    = WelsTime();
    iTotal += iEnd - iStart;
    // 如果解码成功，处理解码后的数据
    if (sDstBufInfo.iBufferStatus == 1) {
      cOutputModule.Process ((void**)pDst, &sDstBufInfo, pYuvFile);
      iWidth  = sDstBufInfo.UsrData.sSystemBuffer.iWidth;
      iHeight = sDstBufInfo.UsrData.sSystemBuffer.iHeight;

      // 如果选项文件存在，且宽度或高度发生变化，将帧数、宽度和高度写入选项文件
      if (pOptionFile != NULL) {
        if (iWidth != iLastWidth && iHeight != iLastHeight) {
          fwrite (&iFrameCount, sizeof (iFrameCount), 1, pOptionFile);
          fwrite (&iWidth, sizeof (iWidth), 1, pOptionFile);
          fwrite (&iHeight, sizeof (iHeight), 1, pOptionFile);
          iLastWidth  = iWidth;
          iLastHeight = iHeight;
        }
      }

      // 增加帧数
      ++ iFrameCount;
    }

    // 如果使用旧的调用方式，再次调用解码函数
    if (bLegacyCalling) {
      iStart = WelsTime();
      pData[0] = NULL;
      pData[1] = NULL;
      pData[2] = NULL;
      memset (&sDstBufInfo, 0, sizeof (SBufferInfo));
      sDstBufInfo.uiInBsTimeStamp = uiTimeStamp;
      pDecoder->DecodeFrame2 (NULL, 0, pData, &sDstBufInfo);

      // 如果解码成功，处理解码后的数据
      if (sDstBufInfo.iBufferStatus == 1) {
        pDst[0] = sDstBufInfo.pDst[0];
        pDst[1] = sDstBufInfo.pDst[1];
        pDst[2] = sDstBufInfo.pDst[2];
      }

      // 记录结束时间，并累加总时间
      iEnd    = WelsTime();
      iTotal += iEnd - iStart;

      // 如果解码成功，处理解码后的数据
      if (sDstBufInfo.iBufferStatus == 1) {
        cOutputModule.Process ((void**)pDst, &sDstBufInfo, pYuvFile);
        iWidth  = sDstBufInfo.UsrData.sSystemBuffer.iWidth;
        iHeight = sDstBufInfo.UsrData.sSystemBuffer.iHeight;

        // 如果选项文件存在，且宽度或高度发生变化，将帧数、宽度和高度写入选项文件
        if (pOptionFile != NULL) {
          if (iWidth != iLastWidth && iHeight != iLastHeight) {
            fwrite (&iFrameCount, sizeof (iFrameCount), 1, pOptionFile);
            fwrite (&iWidth, sizeof (iWidth), 1, pOptionFile);
            fwrite (&iHeight, sizeof (iHeight), 1, pOptionFile);
            iLastWidth  = iWidth;
            iLastHeight = iHeight;
          }
        }
        // 增加帧数
        ++ iFrameCount;
      }
    }
    // 更新缓冲区位置
    iBufPos += iSliceSize;
    // 增加切片索引
    ++ iSliceIndex;
  }

  // 刷新所有待处理的帧
  FlushFrames (pDecoder, iTotal, pYuvFile, pOptionFile, iFrameCount, uiTimeStamp, iWidth, iHeight, iLastWidth,
               iLastHeight);
  
  // 计算解码所用的时间
  dElapsed = iTotal / 1e6;
  // 打印解码信息
  fprintf (stderr, "-------------------------------------------------------\n");
  fprintf (stderr, "iWidth:\t\t%d\nheight:\t\t%d\nFrames:\t\t%d\ndecode time:\t%f sec\nFPS:\t\t%f fps\n",
           iWidth, iHeight, iFrameCount, dElapsed, (iFrameCount * 1.0) / dElapsed);
  fprintf (stderr, "-------------------------------------------------------\n");

#if defined (WINDOWS_PHONE)
  // 如果定义了WINDOWS_PHONE，设置全局变量
  g_dDecTime = dElapsed;
  g_fDecFPS = (iFrameCount * 1.0f) / (float) dElapsed;
  g_iDecodedFrameNum = iFrameCount;
#endif

  // 清理工作，释放内存，关闭文件
label_exit:
  // 如果缓冲区不为空，则删除缓冲区并将指针设为NULL
  if (pBuf) {
    delete[] pBuf;
    pBuf = NULL;
  }
  // 如果H264文件打开，则关闭文件并将指针设为NULL
  if (pH264File) {
    fclose (pH264File);
    pH264File = NULL;
  }
  // 如果YUV文件打开，则关闭文件并将指针设为NULL
  if (pYuvFile) {
    fclose (pYuvFile);
    pYuvFile = NULL;
  }
  // 如果选项文件打开，则关闭文件并将指针设为NULL
  if (pOptionFile) {
    fclose (pOptionFile);
    pOptionFile = NULL;
  }
  // 如果跟踪文件打开，则关闭文件并将指针设为NULL
  if (fpTrack) {
    fclose (fpTrack);
    fpTrack = NULL;
  }
}

#if (defined(ANDROID_NDK)||defined(APPLE_IOS) || defined (WINDOWS_PHONE))
int32_t DecMain (int32_t iArgC, char* pArgV[]) {
#else
int32_t main (int32_t iArgC, char* pArgV[]) {
#endif
  ISVCDecoder* pDecoder = NULL;

  SDecodingParam sDecParam = {0};
  string strInputFile (""), strOutputFile (""), strOptionFile (""), strLengthFile ("");
  int iLevelSetting = (int) WELS_LOG_WARNING;
  bool bLegacyCalling = false;

  sDecParam.sVideoProperty.size = sizeof (sDecParam.sVideoProperty);
  sDecParam.eEcActiveIdc = ERROR_CON_SLICE_MV_COPY_CROSS_IDR_FREEZE_RES_CHANGE;

  if (iArgC < 2) {
    printf ("usage 1: h264dec.exe welsdec.cfg\n");
    printf ("usage 2: h264dec.exe welsdec.264 out.yuv\n");
    printf ("usage 3: h264dec.exe welsdec.264\n");
    return 1;
  } else if (iArgC == 2) {
    if (strstr (pArgV[1], ".cfg")) { // read config file //confirmed_safe_unsafe_usage
      CReadConfig cReadCfg (pArgV[1]);
      string strTag[4];
      string strReconFile ("");

      if (!cReadCfg.ExistFile()) {
        printf ("Specified file: %s not exist, maybe invalid path or parameter settting.\n", cReadCfg.GetFileName().c_str());
        return 1;
      }

      while (!cReadCfg.EndOfFile()) {
        long nRd = cReadCfg.ReadLine (&strTag[0]);
        if (nRd > 0) {
          if (strTag[0].compare ("InputFile") == 0) {
            strInputFile = strTag[1];
          } else if (strTag[0].compare ("OutputFile") == 0) {
            strOutputFile = strTag[1];
          } else if (strTag[0].compare ("RestructionFile") == 0) {
            strReconFile = strTag[1];
            int32_t iLen = (int32_t)strReconFile.length();
            sDecParam.pFileNameRestructed = new char[iLen + 1];
            if (sDecParam.pFileNameRestructed != NULL) {
              sDecParam.pFileNameRestructed[iLen] = 0;
            }

            strncpy (sDecParam.pFileNameRestructed, strReconFile.c_str(), iLen); //confirmed_safe_unsafe_usage
          } else if (strTag[0].compare ("TargetDQID") == 0) {
            sDecParam.uiTargetDqLayer = (uint8_t)atol (strTag[1].c_str());
          } else if (strTag[0].compare ("ErrorConcealmentIdc") == 0) {
            sDecParam.eEcActiveIdc = (ERROR_CON_IDC)atol (strTag[1].c_str());
          } else if (strTag[0].compare ("CPULoad") == 0) {
            sDecParam.uiCpuLoad = (uint32_t)atol (strTag[1].c_str());
          } else if (strTag[0].compare ("VideoBitstreamType") == 0) {
            sDecParam.sVideoProperty.eVideoBsType = (VIDEO_BITSTREAM_TYPE)atol (strTag[1].c_str());
          }
        }
      }
      if (strOutputFile.empty()) {
        printf ("No output file specified in configuration file.\n");
        return 1;
      }
    } else if (strstr (pArgV[1],
                       ".264")) { // no output dump yuv file, just try to render the decoded pictures //confirmed_safe_unsafe_usage
      strInputFile = pArgV[1];
      sDecParam.uiTargetDqLayer = (uint8_t) - 1;
      sDecParam.eEcActiveIdc = ERROR_CON_SLICE_COPY;
      sDecParam.sVideoProperty.eVideoBsType = VIDEO_BITSTREAM_DEFAULT;
    }
  } else { //iArgC > 2
    strInputFile = pArgV[1];
    strOutputFile = pArgV[2];
    sDecParam.uiTargetDqLayer = (uint8_t) - 1;
    sDecParam.eEcActiveIdc = ERROR_CON_SLICE_COPY;
    sDecParam.sVideoProperty.eVideoBsType = VIDEO_BITSTREAM_DEFAULT;
    if (iArgC > 3) {
      for (int i = 3; i < iArgC; i++) {
        // 获取命令行参数
        char* cmd = pArgV[i];

        if (!strcmp (cmd, "-options")) {
          // 如果参数是"-options"，获取选项文件的路径
          if (i + 1 < iArgC)
            strOptionFile = pArgV[++i];
          else {
            printf ("options file not specified.\n");
            return 1;
          }
        } else if (!strcmp (cmd, "-trace")) {
          // 如果参数是"-trace"，获取日志级别
          if (i + 1 < iArgC)
            iLevelSetting = atoi (pArgV[++i]);
          else {
            printf ("trace level not specified.\n");
            return 1;
          }
        } else if (!strcmp (cmd, "-length")) {
          // 如果参数是"-length"，获取长度文件的路径
          if (i + 1 < iArgC)
            strLengthFile = pArgV[++i];
          else {
            printf ("length file not specified.\n");
            return 1;
          }
        } else if (!strcmp (cmd, "-ec")) {
          // 如果参数是"-ec"，获取错误掩盖的IDC
          if (i + 1 < iArgC) {
            int iEcActiveIdc = atoi (pArgV[++i]);
            sDecParam.eEcActiveIdc = (ERROR_CON_IDC)iEcActiveIdc;
            printf ("ERROR_CON(cealment) is set to %d.\n", iEcActiveIdc);
          }
        } else if (!strcmp (cmd, "-legacy")) {
          // 如果参数是"-legacy"，设置使用旧的调用方式
          bLegacyCalling = true;
        }
      }
    }

    if (strOutputFile.empty()) {
      printf ("No output file specified in configuration file.\n");
      return 1;
    }
  }

  if (strInputFile.empty()) {
    printf ("No input file specified in configuration file.\n");
    return 1;
  }




  if (WelsCreateDecoder (&pDecoder)  || (NULL == pDecoder)) {
    printf ("Create Decoder failed.\n");
    return 1;
  }
  if (iLevelSetting >= 0) {
    pDecoder->SetOption (DECODER_OPTION_TRACE_LEVEL, &iLevelSetting);
  }

  int32_t iThreadCount = 0;
  pDecoder->SetOption (DECODER_OPTION_NUM_OF_THREADS, &iThreadCount);

  if (pDecoder->Initialize (&sDecParam)) {
    printf ("Decoder initialization failed.\n");
    return 1;
  }


  int32_t iWidth = 0;
  int32_t iHeight = 0;


  H264DecodeInstance (pDecoder, strInputFile.c_str(), !strOutputFile.empty() ? strOutputFile.c_str() : NULL, iWidth,
                      iHeight,
                      (!strOptionFile.empty() ? strOptionFile.c_str() : NULL), (!strLengthFile.empty() ? strLengthFile.c_str() : NULL),
                      (int32_t)sDecParam.eEcActiveIdc,
                      bLegacyCalling);

  if (sDecParam.pFileNameRestructed != NULL) {
    delete []sDecParam.pFileNameRestructed;
    sDecParam.pFileNameRestructed = NULL;
  }

  if (pDecoder) {
    pDecoder->Uninitialize();

    WelsDestroyDecoder (pDecoder);
  }

  return 0;
}
