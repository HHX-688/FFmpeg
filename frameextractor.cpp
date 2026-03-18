#include "frameextractor.h"
// 视频抽帧实现（FFmpeg 解码并保存为图片）
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QImage>

// FFmpeg头文件 - 用于视频解码和处理
extern "C" {
#include <libavcodec/avcodec.h>     // 编解码器库，用于视频解码
#include <libavformat/avformat.h>   // 格式处理库，用于打开和处理视频文件
#include <libavutil/avutil.h>       // 通用工具函数，提供各种辅助功能
#include <libavutil/time.h>         // 时间相关函数，用于时间戳处理
#include <libavutil/channel_layout.h> // 声道布局，用于音频处理
#include <libavutil/imgutils.h>     // 图像工具函数，用于图像数据处理
#include <libavutil/opt.h>          // 选项设置，用于配置FFmpeg组件
#include <libswscale/swscale.h>     // 图像缩放和颜色空间转换库
#include <libswresample/swresample.h> // 音频重采样库
}

/**
 * @brief FrameExtractor构造函数
 * @param parent 父对象指针
 * @details 初始化FrameExtractor对象，并调用initializeFFmpeg函数初始化FFmpeg
 */
FrameExtractor::FrameExtractor(QObject *parent) : QObject(parent)
{
    // 初始化 FFmpeg（新版本无需显式注册编解码器）
    initializeFFmpeg();
}

/**
 * @brief FrameExtractor析构函数
 * @details 调用cleanupFFmpeg函数清理FFmpeg资源
 */
FrameExtractor::~FrameExtractor()
{
    cleanupFFmpeg();
}

/**
 * @brief 初始化FFmpeg
 * @return 初始化成功返回true
 * @details 在新版本FFmpeg中，无需显式注册编解码器，此函数保持兼容
 */
bool FrameExtractor::initializeFFmpeg()
{
    // 新版本FFmpeg不需要显式注册编解码器，自动加载
    return true;
}

/**
 * @brief 清理FFmpeg资源
 * @details 在新版本FFmpeg中，大部分资源会自动清理，此函数保持兼容
 */
void FrameExtractor::cleanupFFmpeg()
{
    // FFmpeg会自动清理资源，无需手动释放
}

/**
 * @brief 提取视频帧的入口函数
 * @param inputFile 输入视频文件路径
 * @param outputDir 输出目录路径
 * @param framesPerSecond 每秒提取的帧数
 * @param imageFormat 输出图片格式
 * @details 创建后台线程执行视频帧提取操作，避免阻塞UI线程
 */
void FrameExtractor::extractFrames(const QString &inputFile, const QString &outputDir, int framesPerSecond, const QString &imageFormat)
{
    // 检查输入文件是否为MP4格式
    if (!inputFile.toLower().endsWith(".mp4")) {
        emit errorOccurred("当前仅支持 MP4 作为输入源");
        emit operationFinished(false, "仅支持 MP4 输入");
        return;
    }
    
    // 使用独立线程执行抽帧，避免阻塞UI线程
    QThread *thread = new QThread;
    FrameExtractorWorker *worker = new FrameExtractorWorker(inputFile, outputDir, framesPerSecond, imageFormat);
    
    // 将工作对象移动到新线程
    worker->moveToThread(thread);
    
    // 连接线程和工作对象的信号槽
    connect(thread, &QThread::started, worker, &FrameExtractorWorker::run);
    connect(worker, &FrameExtractorWorker::finished, thread, &QThread::quit);
    connect(worker, &FrameExtractorWorker::finished, worker, &FrameExtractorWorker::deleteLater);
    connect(thread, &QThread::finished, thread, &QThread::deleteLater);
    
    // 连接进度和状态信号到外部
    connect(worker, &FrameExtractorWorker::progressChanged, this, &FrameExtractor::progressChanged);
    connect(worker, &FrameExtractorWorker::finished, this, [this](bool success, const QString &message) {
        emit operationFinished(success, message);
    });
    connect(worker, &FrameExtractorWorker::errorOccurred, this, &FrameExtractor::errorOccurred);
    
    // 启动线程
    thread->start();
}

/**
 * @brief FrameExtractorWorker构造函数
 * @param inputFile 输入视频文件路径
 * @param outputDir 输出目录路径
 * @param framesPerSecond 每秒提取的帧数
 * @param imageFormat 输出图片格式
 * @param parent 父对象指针
 * @details 初始化FrameExtractorWorker对象，存储抽帧所需的参数
 */
FrameExtractorWorker::FrameExtractorWorker(const QString &inputFile, const QString &outputDir, int framesPerSecond, const QString &imageFormat, QObject *parent)
    : QThread(parent)
    , m_inputFile(inputFile)         // 输入文件路径
    , m_outputDir(outputDir)         // 输出目录路径
    , m_framesPerSecond(framesPerSecond) // 每秒提取的帧数
    , m_imageFormat(imageFormat)     // 输出图片格式
{}

/**
 * @brief FrameExtractorWorker析构函数
 * @details 析构FrameExtractorWorker对象，释放相关资源
 */
FrameExtractorWorker::~FrameExtractorWorker()
{}

/**
 * @brief 线程运行函数
 * @details 线程入口点，执行视频帧提取操作并处理异常
 */
void FrameExtractorWorker::run()
{
    bool success = false;
    QString message;
    
    try {
        // 执行抽帧主流程（解码视频并保存成图片）
        success = extractFrames(m_inputFile, m_outputDir, m_framesPerSecond, m_imageFormat);
        message = success ? "视频帧提取完成" : "视频帧提取失败";
    } catch (const std::exception &e) {
        message = QString("发生异常: %1").arg(e.what());
        success = false;
    }
    
    emit finished(success, message); // 发送完成信号
}

/**
 * @brief 提取视频帧的核心方法
 * @param inputFile 输入视频文件路径
 * @param outputDir 输出目录路径
 * @param framesPerSecond 每秒提取的帧数
 * @param imageFormat 输出图片格式
 * @return 提取成功返回true，失败返回false
 * @details 使用FFmpeg解码视频并将帧保存为图片，包含完整的视频处理流程
 */
bool FrameExtractorWorker::extractFrames(const QString &inputFile, const QString &outputDir, int framesPerSecond, const QString &imageFormat)
{
    // 初始化FFmpeg相关结构体
    AVFormatContext *formatContext = nullptr; // 格式上下文，用于处理视频文件格式
    AVCodecContext *codecContext = nullptr;   // 编解码器上下文，包含编解码参数
    const AVCodec *codec = nullptr;           // 编解码器，用于视频解码
    AVFrame *frame = nullptr;                 // 存储解码后的原始帧
    AVFrame *rgbFrame = nullptr;              // 存储转换为RGB后的帧，用于保存为图片
    AVPacket *packet = nullptr;               // 存储编码后的数据包
    SwsContext *swsCtx = nullptr;             // 用于颜色空间转换
    uint8_t *rgbBuffer = nullptr;             // RGB帧缓冲区
    QDir dir(outputDir);                      // 输出目录
    
    int videoStreamIndex = -1;  // 视频流索引，用于标识视频流
    int ret = 0;                // 返回值，用于检查FFmpeg函数调用结果
    int processedFrames = 0;    // 已处理的帧数，包括所有解码的帧
    int savedFrames = 0;        // 已保存的帧数，仅包括实际保存为图片的帧
    int64_t totalFrames = 0;    // 视频总帧数（估算），用于计算进度
    int numBytes = 0;           // RGB缓冲区大小
    
    // 确保图片格式是小写
    QString format = imageFormat.toLower();
    // 验证并默认使用png格式
    if (format != "png" && format != "jpg" && format != "jpeg") {
        format = "png";
    }
    
    // 打开输入文件
    // 设置选项以忽略UDTA解析错误，提高兼容性
    AVDictionary *options = nullptr;
    av_dict_set(&options, "movflags", "faststart", 0);         // 优化MP4文件结构，将moov原子移到文件开头
    av_dict_set(&options, "ignore_editlist", "1", 0);          // 忽略编辑列表，避免跳过部分内容
    av_dict_set(&options, "err_detect", "ignore_err", 0);      // 忽略错误检测，提高容错性
    av_dict_set(&options, "skip_unknown", "1", 0);             // 跳过未知流，避免处理不支持的流类型
    
    // 打开视频文件
    if (avformat_open_input(&formatContext, inputFile.toLocal8Bit().data(), nullptr, &options) < 0) {
        emit errorOccurred("无法打开输入文件: " + inputFile);
        av_dict_free(&options);
        return false;
    }
    av_dict_free(&options); // 释放选项字典
    
    // 设置错误处理为忽略模式，提高容错性
    formatContext->error_recognition |= AV_EF_IGNORE_ERR;
    
    // 获取流信息
    if (avformat_find_stream_info(formatContext, nullptr) < 0) {
        emit errorOccurred("无法获取流信息");
        goto cleanup;
    }
    
    // 查找视频流索引
    for (unsigned int i = 0; i < formatContext->nb_streams; i++) {
        if (formatContext->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            videoStreamIndex = i;
            break;
        }
    }
    
    // 检查是否找到视频流
    if (videoStreamIndex == -1) {
        emit errorOccurred("未找到视频流");
        goto cleanup;
    }
    
    // 获取视频流对应的编解码器
    codec = avcodec_find_decoder(formatContext->streams[videoStreamIndex]->codecpar->codec_id);
    if (!codec) {
        emit errorOccurred("未找到合适的编解码器");
        goto cleanup;
    }
    
    // 创建编解码器上下文
    codecContext = avcodec_alloc_context3(codec);
    // 将视频流的编解码器参数复制到编解码器上下文中
    if (avcodec_parameters_to_context(codecContext, formatContext->streams[videoStreamIndex]->codecpar) < 0) {
        emit errorOccurred("无法复制编解码器参数");
        goto cleanup;
    }
    
    // 打开编解码器，准备解码
    if (avcodec_open2(codecContext, codec, nullptr) < 0) {
        emit errorOccurred("无法打开编解码器");
        goto cleanup;
    }
    
    // 创建输出目录
    if (!dir.exists()) {
        if (!dir.mkpath(".")) { // 创建输出目录，如果不存在
            emit errorOccurred("无法创建输出目录: " + outputDir);
            goto cleanup;
        }
    }
    
    // 分配原始帧缓冲区，用于存储解码后的帧
    frame = av_frame_alloc();
    if (!frame) {
        emit errorOccurred("无法分配帧");
        goto cleanup;
    }
    
    // 分配数据包缓冲区，用于存储编码后的数据包
    packet = av_packet_alloc();
    if (!packet) {
        emit errorOccurred("无法分配数据包");
        goto cleanup;
    }
    
    // 准备颜色空间转换，将视频帧转换为RGB格式以保存为图片
    swsCtx = sws_getContext(
        codecContext->width, codecContext->height, codecContext->pix_fmt,  // 源宽度、高度、像素格式
        codecContext->width, codecContext->height, AV_PIX_FMT_RGB24,       // 目标宽度、高度、像素格式（RGB24用于保存图片）
        SWS_BILINEAR, nullptr, nullptr, nullptr                           // 插值方法为双线性，其他参数为nullptr
    );
    if (!swsCtx) {
        emit errorOccurred("无法创建颜色转换上下文");
        goto cleanup;
    }
    
    // 分配RGB帧缓冲区，用于存储转换后的RGB帧
    rgbFrame = av_frame_alloc();
    if (!rgbFrame) {
        emit errorOccurred("无法分配RGB帧");
        goto cleanup;
    }
    
    // 计算RGB缓冲区所需的大小
    numBytes = av_image_get_buffer_size(AV_PIX_FMT_RGB24, codecContext->width, codecContext->height, 1);
    // 分配RGB缓冲区内存
    rgbBuffer = (uint8_t*)av_malloc(numBytes);
    if (!rgbBuffer) {
        emit errorOccurred("无法分配RGB缓冲区");
        goto cleanup;
    }
    
    // 将RGB缓冲区填充到RGB帧的数组中
    if (av_image_fill_arrays(rgbFrame->data, rgbFrame->linesize, rgbBuffer, AV_PIX_FMT_RGB24,
                             codecContext->width, codecContext->height, 1) < 0) {
        emit errorOccurred("无法填充RGB缓冲区");
        goto cleanup;
    }
    
    // 估算视频总帧数，用于显示进度
    if (formatContext->streams[videoStreamIndex]->nb_frames > 0) {
        // 如果视频流中有明确的帧数信息，直接使用
        totalFrames = formatContext->streams[videoStreamIndex]->nb_frames;
    } else {
        // 否则根据视频时长和帧率估算总帧数
        AVRational fr = formatContext->streams[videoStreamIndex]->avg_frame_rate;
        if (fr.num > 0 && fr.den > 0 && formatContext->duration > 0) {
            // 使用av_rescale_q将时间转换为帧数
            totalFrames = av_rescale_q(formatContext->duration, AVRational{1, AV_TIME_BASE}, fr);
        }
    }
    
    // 主循环：读取视频帧并保存为图片
    while (av_read_frame(formatContext, packet) >= 0) {
        // 检查是否为视频流的数据包
        if (packet->stream_index == videoStreamIndex) {
            // 将数据包发送到解码器进行解码
            ret = avcodec_send_packet(codecContext, packet);
            if (ret < 0) {
                // 解码失败，释放数据包并继续处理下一个
                av_packet_unref(packet);
                continue;
            }
            
            // 接收解码后的帧
            while (ret >= 0) {
                ret = avcodec_receive_frame(codecContext, frame);
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                    // 需要更多数据或已经读取到文件末尾，跳出当前循环
                    break;
                } else if (ret < 0) {
                    // 解码错误
                    emit errorOccurred("解码错误");
                    goto cleanup;
                }
                
                // 根据帧率控制提取频率
                double frameRate = av_q2d(formatContext->streams[videoStreamIndex]->avg_frame_rate);
                if (frameRate <= 0) frameRate = av_q2d(formatContext->streams[videoStreamIndex]->r_frame_rate);
                
                // 计算提取间隔：每多少帧提取一帧
                int extractInterval = 1;
                if (frameRate > 0 && framesPerSecond > 0) {
                    extractInterval = qMax(1, static_cast<int>(frameRate / framesPerSecond));
                }
                
                // 检查当前帧是否需要提取
                if (processedFrames % extractInterval != 0) {
                    processedFrames++;
                    av_frame_unref(frame); // 释放当前帧
                    continue; // 跳过当前帧，继续处理下一帧
                }
                
                // 将原始帧转换为RGB格式
                sws_scale(
                    swsCtx,              // 颜色转换上下文
                    frame->data,         // 源帧数据
                    frame->linesize,     // 源帧行大小
                    0,                   // 源起始行
                    codecContext->height, // 源高度
                    rgbFrame->data,      // 目标帧数据
                    rgbFrame->linesize   // 目标帧行大小
                );
                
                // 构建输出文件路径，使用6位数字序号
                QString outputPath = QString("%1/frame_%2.%3").arg(outputDir).arg(savedFrames, 6, 10, QChar('0')).arg(format);
                
                // 创建QImage对象并保存为图片
                QImage image(rgbFrame->data[0], codecContext->width, codecContext->height, rgbFrame->linesize[0], QImage::Format_RGB888);
                if (!image.copy().save(outputPath, format.toLatin1().constData())) {
                    emit errorOccurred("保存帧失败: " + outputPath);
                    goto cleanup;
                }
                
                // 更新计数器
                processedFrames++;
                savedFrames++;
                
                // 计算并发送进度信息
                int progress = 0;
                if (totalFrames > 0) {
                    progress = static_cast<int>((processedFrames * 100) / totalFrames);
                } else {
                    progress = qMin(savedFrames, 100);
                }
                // 确保进度在0-99之间，保留1%的空间用于最终完成
                progress = qBound(0, progress, 99);
                emit progressChanged(progress);
                
                // 释放当前帧资源
                av_frame_unref(frame);
            }
        }
        
        // 释放数据包资源
        av_packet_unref(packet);
    }
    
cleanup:
    // 释放所有分配的资源，避免内存泄漏
    if (frame) av_frame_free(&frame);           // 释放原始帧
    if (rgbFrame) av_frame_free(&rgbFrame);     // 释放RGB帧
    if (rgbBuffer) av_free(rgbBuffer);          // 释放RGB缓冲区
    if (packet) av_packet_free(&packet);        // 释放数据包
    if (swsCtx) sws_freeContext(swsCtx);        // 释放颜色转换上下文
    if (codecContext) avcodec_free_context(&codecContext); // 释放编解码器上下文
    if (formatContext) avformat_close_input(&formatContext); // 关闭格式上下文
    
    // 如果成功保存了至少一帧，则返回true
    return savedFrames > 0;
}
