#include <fstream>
#include <gtest/gtest.h>
#include "codec_def.h"
#include "codec_app_def.h"
#include "utils/BufferedData.h"
#include "BaseDecoderTest.h"

static bool ReadFrame (std::ifstream* file, BufferedData* buf) {
  // start code of a frame is {0, 0, 0, 1}
  int zeroCount = 0;
  char b;

  buf->Clear();
  for (;;) {
    file->read (&b, 1);
    if (file->gcount() != 1) { // end of file
      return true;
    }
    if (!buf->PushBack (b)) {
      std::cout << "unable to allocate memory" << std::endl;
      return false;
    }

    if (buf->Length() <= 4) {
      continue;
    }

    if (zeroCount < 3) {
      zeroCount = b != 0 ? 0 : zeroCount + 1;
    } else {
      if (b == 1) {
        if (file->seekg (-4, file->cur).good()) {
          if (-1 == buf->SetLength(buf->Length() - 4))
            return false;
          return true;
        } else {
          std::cout << "unable to seek file" << std::endl;
          return false;
        }
      } else if (b == 0) {
        zeroCount = 3;
      } else {
        zeroCount = 0;
      }
    }
  }
}

BaseDecoderTest::BaseDecoderTest()
  : decoder_ (NULL), decodeStatus_ (OpenFile) {}

// 设置解码器的函数
int32_t BaseDecoderTest::SetUp() {
  // 创建解码器实例
  long rv = WelsCreateDecoder (&decoder_);
  // 检查解码器是否创建成功
  EXPECT_EQ (0, rv);
  EXPECT_TRUE (decoder_ != NULL);
  // 如果解码器创建失败，返回错误码
  if (decoder_ == NULL) {
    return rv;
  }

  // 创建解码参数
  SDecodingParam decParam;
  // 初始化解码参数
  memset (&decParam, 0, sizeof (SDecodingParam));
  // 设置解码参数的值
  decParam.uiTargetDqLayer = UCHAR_MAX; // 目标质量层
  decParam.eEcActiveIdc = ERROR_CON_SLICE_COPY; // 错误控制模式
  decParam.sVideoProperty.eVideoBsType = VIDEO_BITSTREAM_DEFAULT; // 视频比特流类型

  // 初始化解码器
  rv = decoder_->Initialize (&decParam);
  // 检查解码器是否初始化成功
  EXPECT_EQ (0, rv);
  // 返回初始化结果
  return (int32_t)rv;
}

void BaseDecoderTest::TearDown() {
  if (decoder_ != NULL) {
    decoder_->Uninitialize();
    WelsDestroyDecoder (decoder_);
  }
}


// 解码一帧的函数
void BaseDecoderTest::DecodeFrame (const uint8_t* src, size_t sliceSize, Callback* cbk) {
  // 初始化数据和缓冲信息
  uint8_t* data[3];
  SBufferInfo bufInfo;
  memset (data, 0, sizeof (data));
  memset (&bufInfo, 0, sizeof (SBufferInfo));

  // 调用解码器的DecodeFrame2函数进行解码
  DECODING_STATE rv = decoder_->DecodeFrame2 (src, (int) sliceSize, data, &bufInfo);
  // 检查解码是否成功
  ASSERT_TRUE (rv == dsErrorFree);

  // 如果解码成功并且回调函数不为空，调用回调函数处理解码后的帧
  if (bufInfo.iBufferStatus == 1 && cbk != NULL) {
    const Frame frame = {
      // y plane
      data[0],
      bufInfo.UsrData.sSystemBuffer.iWidth,
      bufInfo.UsrData.sSystemBuffer.iHeight,
      bufInfo.UsrData.sSystemBuffer.iStride[0]
      // u plane
      data[1],
      bufInfo.UsrData.sSystemBuffer.iWidth / 2,
      bufInfo.UsrData.sSystemBuffer.iHeight / 2,
      bufInfo.UsrData.sSystemBuffer.iStride[1]
      // v plane
      data[2],
      bufInfo.UsrData.sSystemBuffer.iWidth / 2,
      bufInfo.UsrData.sSystemBuffer.iHeight / 2,
      bufInfo.UsrData.sSystemBuffer.iStride[1]
    };
    cbk->onDecodeFrame (frame);
  }
}


// 刷新帧的函数，用于获取解码器缓冲区中剩余的帧
void BaseDecoderTest::FlushFrame (Callback* cbk) {
  // 初始化数据和缓冲信息
  uint8_t* data[3];
  SBufferInfo bufInfo;
  memset (data, 0, sizeof (data));
  memset (&bufInfo, 0, sizeof (SBufferInfo));

  // 调用解码器的FlushFrame函数获取剩余的帧
  DECODING_STATE rv = decoder_->FlushFrame (data, &bufInfo);
  // 检查是否成功获取到帧
  ASSERT_TRUE (rv == dsErrorFree);

  // 如果成功获取到帧并且回调函数不为空，调用回调函数处理帧
  if (bufInfo.iBufferStatus == 1 && cbk != NULL) {
    const Frame frame = {
      // y plane
      data[0],
      bufInfo.UsrData.sSystemBuffer.iWidth,
      bufInfo.UsrData.sSystemBuffer.iHeight,
      bufInfo.UsrData.sSystemBuffer.iStride[0]
      // u plane
      data[1],
      bufInfo.UsrData.sSystemBuffer.iWidth / 2,
      bufInfo.UsrData.sSystemBuffer.iHeight / 2,
      bufInfo.UsrData.sSystemBuffer.iStride[1]
      // v plane
      data[2],
      bufInfo.UsrData.sSystemBuffer.iWidth / 2,
      bufInfo.UsrData.sSystemBuffer.iHeight / 2,
      bufInfo.UsrData.sSystemBuffer.iStride[1]
    };
    cbk->onDecodeFrame (frame);
  }
}

// 解码文件的函数，从文件中读取H264数据，然后解码每一帧
bool BaseDecoderTest::DecodeFile (const char* fileName, Callback* cbk) {
  // 打开文件
  std::ifstream file (fileName, std::ios::in | std::ios::binary);
  if (!file.is_open())
    return false;

  // 创建缓冲区
  BufferedData buf;
  while (true) {
    // 读取一帧
    if (false == ReadFrame(&file, &buf))
      return false;
    if (::testing::Test::HasFatalFailure()) {
      return false;
    }
    // 如果读取到的帧长度为0，说明文件已经读取完毕，跳出循环
    if (buf.Length() == 0) {
      break;
    }
    // 解码一帧
    DecodeFrame (buf.data(), buf.Length(), cbk);
    if (::testing::Test::HasFatalFailure()) {
      return false;
    }
  }

  // 设置解码器的结束标志
  int32_t iEndOfStreamFlag = 1;
  decoder_->SetOption (DECODER_OPTION_END_OF_STREAM, &iEndOfStreamFlag);

  // 获取并解码剩余的帧
  DecodeFrame (NULL, 0, cbk);
  // 刷新解码器的缓冲区，获取并解码剩余的帧
  int32_t num_of_frames_in_buffer = 0;
  decoder_->GetOption (DECODER_OPTION_NUM_OF_FRAMES_REMAINING_IN_BUFFER, &num_of_frames_in_buffer);
  for (int32_t i = 0; i < num_of_frames_in_buffer; ++i) {
    FlushFrame (cbk);
  }
  return true;
}

// 打开文件的函数，用于开始解码过程
bool BaseDecoderTest::Open (const char* fileName) {
  if (decodeStatus_ == OpenFile) {
    // 打开文件
    file_.open (fileName, std::ios_base::out | std::ios_base::binary);
    if (file_.is_open()) {
      // 如果文件打开成功，设置解码状态为Decoding
      decodeStatus_ = Decoding;
      return true;
    }
  }
  return false;
}

// 解码下一帧的函数，根据解码状态进行不同的操作
bool BaseDecoderTest::DecodeNextFrame (Callback* cbk) {
  switch (decodeStatus_) {
  case Decoding:
    // 如果解码状态为Decoding，读取一帧
    if (false == ReadFrame(&file_, &buf_))
      return false;
    // 如果读取失败，返回false
    if (::testing::Test::HasFatalFailure()) {
      return false;
    }
    // 如果读取到的帧长度为0，说明文件已经读取完毕，设置解码状态为EndOfStream
    if (buf_.Length() == 0) {
      decodeStatus_ = EndOfStream;
      return true;
    }
    // 解码一帧
    DecodeFrame (buf_.data(), buf_.Length(), cbk);
    // 如果解码失败，返回false
    if (::testing::Test::HasFatalFailure()) {
      return false;
    }
    return true;
  case EndOfStream:
    // 如果解码状态为EndOfStream，设置解码器的结束标志
    int32_t iEndOfStreamFlag = 1;
    decoder_->SetOption (DECODER_OPTION_END_OF_STREAM, &iEndOfStreamFlag);
    // 解码剩余的帧
    DecodeFrame (NULL, 0, cbk);
    // 设置解码状态为End
    decodeStatus_ = End;
    break;
  case OpenFile:
  case End:
    // 如果解码状态为OpenFile或End，不做任何操作
    break;
  }
  return false;
}
