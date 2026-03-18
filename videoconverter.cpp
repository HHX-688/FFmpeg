#include "videoconverter.h"
// 视频转码模块实现（FFmpeg 读写容器/编码）
#include <QDebug>
#include <QFileInfo>
#include <QDir>

// FFmpeg头文件 - 用于视频转码和处理
extern "C" {
#include <libavcodec/avcodec.h>     // 编解码器库
#include <libavformat/avformat.h>   // 格式处理库
#include <libavutil/avutil.h>       // 通用工具函数
#include <libavutil/time.h>         // 时间相关函数
#include <libavutil/channel_layout.h> // 声道布局
#include <libavutil/imgutils.h>     // 图像工具函数
#include <libavutil/opt.h>          // 选项设置
#include <libswscale/swscale.h>     // 图像缩放和颜色空间转换
#include <libswresample/swresample.h> // 音频重采样
}

/**
 * @brief VideoConverter构造函数
 * @param parent 父对象指针
 */
VideoConverter::VideoConverter(QObject *parent) : QObject(parent)
{
    // 初始化 FFmpeg（新版本无需显式注册编解码器，保留入口）
    initializeFFmpeg();
}

/**
 * @brief VideoConverter析构函数
 */
VideoConverter::~VideoConverter()
{
    cleanupFFmpeg();
}

/**
 * @brief 初始化FFmpeg
 * @return 初始化成功返回true
 */
bool VideoConverter::initializeFFmpeg()
{
    // 新版本FFmpeg不需要显式注册编解码器，自动加载
    return true;
}

/**
 * @brief 清理FFmpeg资源
 */
void VideoConverter::cleanupFFmpeg()
{
    // FFmpeg会自动清理资源，无需手动释放
}

/**
 * @brief 视频转码入口函数
 * @param inputFile 输入视频文件路径
 * @param outputFile 输出视频文件路径
 * @param format 输出视频格式
 */
void VideoConverter::convertVideo(const QString &inputFile, const QString &outputFile, const QString &format)
{
    // 检查输入文件是否为MP4格式
    if (!inputFile.toLower().endsWith(".mp4")) {
        emit errorOccurred("当前仅支持 MP4 作为输入源");
        emit operationFinished(false, "仅支持 MP4 输入");
        return;
    }
    
    // 使用后台线程执行转码，避免阻塞UI线程
    QThread *thread = new QThread;
    VideoConverterWorker *worker = new VideoConverterWorker(inputFile, outputFile, format);
    
    // 将工作对象移动到新线程
    worker->moveToThread(thread);
    
    // 连接线程和工作对象的信号槽
    connect(thread, &QThread::started, worker, &VideoConverterWorker::run);
    connect(worker, &VideoConverterWorker::finished, thread, &QThread::quit);
    connect(worker, &VideoConverterWorker::finished, worker, &VideoConverterWorker::deleteLater);
    connect(thread, &QThread::finished, thread, &QThread::deleteLater);
    
    // 连接进度和状态信号到外部
    connect(worker, &VideoConverterWorker::progressChanged, this, &VideoConverter::progressChanged);
    connect(worker, &VideoConverterWorker::finished, this, [this](bool success, const QString &message) {
        emit operationFinished(success, message);
    });
    connect(worker, &VideoConverterWorker::errorOccurred, this, &VideoConverter::errorOccurred);
    
    // 启动线程
    thread->start();
}

/**
 * @brief 视频转码内部实现方法（快速转码，直接复制流数据）
 * @param inputFile 输入视频文件路径
 * @param outputFile 输出视频文件路径
 * @param format 输出视频格式
 * @return 转码成功返回true，失败返回false
 */
bool VideoConverter::convertVideoInternal(const QString &inputFile, const QString &outputFile, const QString &format)
{
    // 初始化FFmpeg相关结构体
    AVFormatContext *inputContext = nullptr;   // 输入格式上下文
    AVFormatContext *outputContext = nullptr;  // 输出格式上下文
    AVPacket *packet = nullptr;                // 数据包
    int64_t startTime = 0;                     // 转码开始时间
    int ret = 0;                               // 返回值
    int trailerRet = 0;                        // 写入文件尾的返回值

    // 1) 打开输入文件并解析流信息
    // 设置输入文件打开选项
    AVDictionary *options = nullptr;
    av_dict_set(&options, "probesize", "10000000", 0);      // 探测大小（10MB）
    av_dict_set(&options, "analyzeduration", "10000000", 0); // 分析时长（10秒）
    
    // 打开输入文件
    ret = avformat_open_input(&inputContext, inputFile.toLocal8Bit().data(), nullptr, &options);
    
    if (ret < 0) {
        char errorBuf[1024];
        av_strerror(ret, errorBuf, sizeof(errorBuf));
        emit errorOccurred("无法打开输入文件: " + inputFile + "，错误: " + errorBuf);
        av_dict_free(&options);
        return false;
    }
    
    av_dict_free(&options); // 释放选项字典
    
    // 设置错误处理为忽略模式，提高容错性
    inputContext->error_recognition |= AV_EF_IGNORE_ERR;
    
    // 获取流信息
    if (avformat_find_stream_info(inputContext, nullptr) < 0) {
        emit errorOccurred("无法获取流信息");
        avformat_close_input(&inputContext);
        return false;
    }
    
    // 2) 创建输出上下文（根据目标格式）
    if (avformat_alloc_output_context2(&outputContext, nullptr, format.toLocal8Bit().data(), outputFile.toLocal8Bit().data()) < 0) {
        emit errorOccurred("无法创建输出上下文");
        avformat_close_input(&inputContext);
        return false;
    }
    
    // 3) 为输出创建对应的流并复制参数
    for (unsigned int i = 0; i < inputContext->nb_streams; i++) {
        AVStream *inStream = inputContext->streams[i];     // 输入流
        AVStream *outStream = avformat_new_stream(outputContext, nullptr); // 输出流
        
        if (!outStream) {
            emit errorOccurred("无法创建输出流");
            goto cleanup;
        }
        
        // 复制编解码器参数到输出流
        if (avcodec_parameters_copy(outStream->codecpar, inStream->codecpar) < 0) {
            emit errorOccurred("无法复制编解码器参数");
            goto cleanup;
        }
        
        // 设置codec_tag为0，让FFmpeg自动选择合适的tag
        outStream->codecpar->codec_tag = 0;
    }
    
    // 4) 打开输出文件并写入文件头
    if (!(outputContext->oformat->flags & AVFMT_NOFILE)) {
        // 打开输出文件
        if (avio_open(&outputContext->pb, outputFile.toLocal8Bit().data(), AVIO_FLAG_WRITE) < 0) {
            emit errorOccurred("无法打开输出文件: " + outputFile);
            goto cleanup;
        }
    }
    
    // 写入文件头
    if (avformat_write_header(outputContext, nullptr) < 0) {
        emit errorOccurred("无法写入文件头");
        goto cleanup;
    }
    
    // 分配数据包
    packet = av_packet_alloc();
    if (!packet) {
        emit errorOccurred("无法分配数据包");
        goto cleanup;
    }
    
    // 5) 主循环：读包 -> 时间戳重映射 -> 写入输出
    startTime = av_gettime(); // 记录转码开始时间
    
    while (av_read_frame(inputContext, packet) >= 0) { // 读取一帧数据
        AVStream *inStream = inputContext->streams[packet->stream_index];  // 输入流
        AVStream *outStream = outputContext->streams[packet->stream_index]; // 输出流
        
        // 重映射时间戳（将输入时间基准转换为输出时间基准）
        packet->pts = av_rescale_q_rnd(packet->pts, inStream->time_base, outStream->time_base, AVRounding(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
        packet->dts = av_rescale_q_rnd(packet->dts, inStream->time_base, outStream->time_base, AVRounding(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
        packet->duration = av_rescale_q(packet->duration, inStream->time_base, outStream->time_base);
        packet->pos = -1; // 重置位置信息
        
        // 写入数据包到输出文件
        if (av_interleaved_write_frame(outputContext, packet) < 0) {
            emit errorOccurred("写入数据包失败");
            av_packet_unref(packet);
            goto cleanup;
        }
        
        // 释放数据包资源
        av_packet_unref(packet);
        
        // 简单进度估算（按耗时比例）
        int64_t currentTime = av_gettime();
        int progress = (currentTime - startTime) * 100 / (10 * 1000000); // 假设最大耗时10秒
        progress = qMin(progress, 100); // 确保进度不超过100%
        emit progressChanged(progress);
    }
    
    // 6) 写入文件尾并清理资源
    trailerRet = av_write_trailer(outputContext); // 写入文件尾
    if (trailerRet < 0) {
        char err[256];
        av_strerror(trailerRet, err, sizeof(err));
        qDebug() << "写入文件尾失败:" << err; // 记录错误但不影响转码结果
    }
    
cleanup:
    // 释放所有分配的资源
    if (packet) av_packet_free(&packet); // 释放数据包
    if (inputContext) {
        avformat_close_input(&inputContext); // 关闭输入格式上下文
    }
    if (outputContext && !(outputContext->oformat->flags & AVFMT_NOFILE)) {
        avio_closep(&outputContext->pb); // 关闭输出文件
    }
    if (outputContext) {
        avformat_free_context(outputContext); // 释放输出格式上下文
    }
    
    // 返回转码结果（写入文件尾的错误不影响结果）
    return ret >= 0;
}

/**
 * @brief 获取视频格式对应的文件扩展名
 * @param format 视频格式名称
 * @return 对应的文件扩展名（带点号）
 */
QString VideoConverter::getFormatExtension(const QString &format) const
{
    // 定义格式与扩展名的映射关系
    QHash<QString, QString> extensions;
    extensions["mp4"] = ".mp4";
    extensions["avi"] = ".avi";
    extensions["mkv"] = ".mkv";
    extensions["mov"] = ".mov";
    extensions["wmv"] = ".wmv";
    extensions["flv"] = ".flv";
    extensions["webm"] = ".webm";
    
    // 返回对应的扩展名，如果没有找到则默认返回.mp4
    return extensions.value(format.toLower(), ".mp4");
}

/**
 * @brief VideoConverterWorker构造函数
 * @param inputFile 输入视频文件路径
 * @param outputFile 输出视频文件路径
 * @param format 输出视频格式
 * @param parent 父对象指针
 */
VideoConverterWorker::VideoConverterWorker(const QString &inputFile, const QString &outputFile, const QString &format, QObject *parent)
    : QThread(parent)
    , m_inputFile(inputFile)     // 输入文件路径
    , m_outputFile(outputFile)   // 输出文件路径
    , m_format(format)           // 输出格式
{}

/**
 * @brief 线程运行函数
 */
void VideoConverterWorker::run()
{
    bool success = false;
    QString message;
    
    try {
        // 执行完整转码流程（解码 -> 重编码 -> 封装）
        success = convertVideo(m_inputFile, m_outputFile, m_format);
        message = success ? "视频转换完成" : "视频转换失败";
    } catch (const std::exception &e) {
        message = QString("发生异常: %1").arg(e.what());
        success = false;
    }
    
    emit finished(success, message); // 发送转码完成信号
}

bool VideoConverterWorker::convertVideo(const QString &inputFile, const QString &outputFile, const QString &format)
{
    AVFormatContext *inputContext = nullptr;
    AVFormatContext *outputContext = nullptr;
    AVPacket *packet = nullptr;
    AVFrame *frame = nullptr;
    AVFrame *videoFrame = nullptr;
    AVFrame *audioFrame = nullptr;
    SwsContext *swsCtx = nullptr;
    SwrContext *swrCtx = nullptr;
    
    // 1) 打开输入并解析流信息
    int videoStreamIndex = -1;
    int audioStreamIndex = -1;
    AVStream *outVideoStream = nullptr;
    AVStream *outAudioStream = nullptr;
    AVCodecContext *videoDecCtx = nullptr;
    AVCodecContext *audioDecCtx = nullptr;
    AVCodecContext *videoEncCtx = nullptr;
    AVCodecContext *audioEncCtx = nullptr;
    
    int64_t startTime = 0;
    bool success = true;
    
    auto cleanup = [&]() {
        if (packet) av_packet_free(&packet);
        if (frame) av_frame_free(&frame);
        if (videoFrame) av_frame_free(&videoFrame);
        if (audioFrame) av_frame_free(&audioFrame);
        if (swsCtx) sws_freeContext(swsCtx);
        if (swrCtx) swr_free(&swrCtx);
        if (videoDecCtx) avcodec_free_context(&videoDecCtx);
        if (audioDecCtx) avcodec_free_context(&audioDecCtx);
        if (videoEncCtx) avcodec_free_context(&videoEncCtx);
        if (audioEncCtx) avcodec_free_context(&audioEncCtx);
        if (inputContext) avformat_close_input(&inputContext);
        if (outputContext && !(outputContext->oformat->flags & AVFMT_NOFILE)) {
            avio_closep(&outputContext->pb);
        }
        if (outputContext) avformat_free_context(outputContext);
    };
    
    AVDictionary *options = nullptr;
    av_dict_set(&options, "probesize", "10000000", 0);
    av_dict_set(&options, "analyzeduration", "10000000", 0);
    int ret = avformat_open_input(&inputContext, inputFile.toLocal8Bit().data(), nullptr, &options);
    av_dict_free(&options);
    if (ret < 0) {
        char err[256]; av_strerror(ret, err, sizeof(err));
        emit errorOccurred("无法打开输入文件: " + inputFile + "，错误: " + QString::fromLatin1(err));
        cleanup();
        return false;
    }
    inputContext->error_recognition |= AV_EF_IGNORE_ERR;
    if (avformat_find_stream_info(inputContext, nullptr) < 0) {
        emit errorOccurred("无法获取流信息");
        cleanup();
        return false;
    }
    
    // 2) 根据目标格式选择可用的编码器
    auto findEncoderName = [](std::initializer_list<const char*> names) -> QByteArray {
        for (auto n : names) {
            if (!n) continue;
            if (avcodec_find_encoder_by_name(n)) {
                return QByteArray(n);
            }
        }
        return QByteArray();
    };
    
    auto chooseCodecs = [&](const QString &fmt, QByteArray &vName, QByteArray &aName) {
        const QString f = fmt.toLower();
        if (f == "mp4" || f == "mov" || f == "mkv") {
            vName = findEncoderName({"libx264", "h264", "mpeg4"});
            aName = findEncoderName({"aac", "libmp3lame", "mp3"});
        } else if (f == "flv") {
            vName = findEncoderName({"libx264", "h264", "flv1"});
            aName = findEncoderName({"aac", "mp3"});
        } else if (f == "avi") {
            vName = findEncoderName({"mpeg4", "h264"});
            aName = findEncoderName({"mp3", "aac"});
        } else if (f == "webm") {
            vName = findEncoderName({"vp8", "libvpx", "vp9", "libvpx-vp9"});
            aName = findEncoderName({"libopus", "opus", "libvorbis", "vorbis"});
        } else if (f == "wmv") {
            vName = findEncoderName({"wmv2", "msmpeg4v2"});
            aName = findEncoderName({"wmav2", "wmav1", "mp3"});
        } else {
            vName = findEncoderName({"libx264", "h264", "mpeg4"});
            aName = findEncoderName({"aac", "mp3"});
        }
    };
    
    QByteArray vCodecName;
    QByteArray aCodecName;
    chooseCodecs(format, vCodecName, aCodecName);
    if (vCodecName.isEmpty()) {
        emit errorOccurred("未找到可用的视频编码器（请检查 FFmpeg 是否带 libx264/h264/mpeg4 等）");
        cleanup();
        return false;
    }
    if (aCodecName.isEmpty()) {
        emit errorOccurred("未找到可用的音频编码器（请检查 FFmpeg 是否带 aac/mp3 等）");
        cleanup();
        return false;
    }
    
    auto muxerNameFor = [](const QString &f) -> QByteArray {
        const QString lf = f.toLower();
        if (lf == "mp4") return "mp4";
        if (lf == "mov") return "mov";
        if (lf == "mkv") return "matroska";
        if (lf == "webm") return "webm";
        if (lf == "flv") return "flv";
        if (lf == "avi") return "avi";
        if (lf == "wmv") return "asf";
        return lf.toLocal8Bit();
    };
    
    QByteArray fmtName = muxerNameFor(format);
    // 3) 创建输出封装器（容器）上下文
    const char *fmt = fmtName.isEmpty() ? nullptr : fmtName.constData();
    if (avformat_alloc_output_context2(&outputContext, nullptr, fmt, outputFile.toLocal8Bit().data()) < 0) {
        emit errorOccurred("无法创建输出上下文");
        cleanup();
        return false;
    }
    
    for (unsigned int i = 0; i < inputContext->nb_streams; ++i) {
        AVStream *st = inputContext->streams[i];
        if (st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO && videoStreamIndex == -1) {
            videoStreamIndex = static_cast<int>(i);
        } else if (st->codecpar->codec_type == AVMEDIA_TYPE_AUDIO && audioStreamIndex == -1) {
            audioStreamIndex = static_cast<int>(i);
        }
    }
    
    // 4) 打开输入的解码器（视频/音频）
    auto openDecoder = [&](int idx, AVCodecContext *&decCtx) -> bool {
        if (idx < 0) return true;
        AVStream *st = inputContext->streams[idx];
        const AVCodec *dec = avcodec_find_decoder(st->codecpar->codec_id);
        if (!dec) return false;
        decCtx = avcodec_alloc_context3(dec);
        if (!decCtx) return false;
        if (avcodec_parameters_to_context(decCtx, st->codecpar) < 0) return false;
        if (avcodec_open2(decCtx, dec, nullptr) < 0) return false;
        return true;
    };
    
    if (!openDecoder(videoStreamIndex, videoDecCtx)) {
        emit errorOccurred("无法打开视频解码器");
        cleanup();
        return false;
    }
    if (!openDecoder(audioStreamIndex, audioDecCtx)) {
        emit errorOccurred("无法打开音频解码器");
        cleanup();
        return false;
    }
    
    // 5) 创建并打开视频编码器与输出视频流
    auto addVideoStream = [&]() -> bool {
        if (videoStreamIndex < 0) return true;
        const AVCodec *enc = avcodec_find_encoder_by_name(vCodecName.constData());
        if (!enc) {
            emit errorOccurred("未找到视频编码器: " + QString::fromLatin1(vCodecName));
            return false;
        }
        AVStream *inVideoStream = inputContext->streams[videoStreamIndex];
        videoEncCtx = avcodec_alloc_context3(enc);
        if (!videoEncCtx) {
            emit errorOccurred("无法分配视频编码器上下文");
            return false;
        }
        
        // 限制最大分辨率，避免MB rate过高
        int maxWidth = 3840;
        int maxHeight = 2160;
        int inWidth = videoDecCtx->width;
        int inHeight = videoDecCtx->height;
        
        if (inWidth > maxWidth || inHeight > maxHeight) {
            // 按比例缩放
            double aspectRatio = static_cast<double>(inWidth) / inHeight;
            if (inWidth > inHeight) {
                inWidth = maxWidth;
                inHeight = static_cast<int>(maxWidth / aspectRatio);
            } else {
                inHeight = maxHeight;
                inWidth = static_cast<int>(maxHeight * aspectRatio);
            }
            // 确保宽度和高度是2的倍数
            inWidth = inWidth & ~1;
            inHeight = inHeight & ~1;
        }
        
        videoEncCtx->width = inWidth;
        videoEncCtx->height = inHeight;
        
        AVRational fps = {0,1};
        if (videoDecCtx->framerate.num && videoDecCtx->framerate.den)
            fps = videoDecCtx->framerate;
        else if (inVideoStream->avg_frame_rate.num && inVideoStream->avg_frame_rate.den)
            fps = inVideoStream->avg_frame_rate;
        else if (inVideoStream->r_frame_rate.num && inVideoStream->r_frame_rate.den)
            fps = inVideoStream->r_frame_rate;
        if (fps.num == 0 || fps.den == 0) fps = AVRational{25,1};
        
        // 计算当前帧率值
        double frameRate = static_cast<double>(fps.num) / fps.den;
        // 限制最大帧率
        double maxFrameRate = 60.0;
        if (frameRate > maxFrameRate) {
            fps = AVRational{static_cast<int>(maxFrameRate), 1};
            frameRate = maxFrameRate;
        }
        
        av_reduce(&fps.num, &fps.den, fps.num, fps.den, 65535);
        videoEncCtx->time_base = av_inv_q(fps);
        videoEncCtx->framerate = fps;
        videoEncCtx->gop_size = 12;
        videoEncCtx->max_b_frames = 2;
        videoEncCtx->pix_fmt = enc->pix_fmts ? enc->pix_fmts[0] : videoDecCtx->pix_fmt;
        videoEncCtx->bit_rate = 2000000;
        if (outputContext->oformat->flags & AVFMT_GLOBALHEADER) {
            videoEncCtx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        }
        
        // 确保在avi格式下使用mpeg4编码器时设置正确的参数
        if (fmtName == "avi" && enc->id == AV_CODEC_ID_MPEG4) {
            videoEncCtx->max_b_frames = 0;
            videoEncCtx->flags |= AV_CODEC_FLAG_LOW_DELAY;
        } else if (enc->id == AV_CODEC_ID_H263 || enc->id == AV_CODEC_ID_H263P ||
            enc->id == AV_CODEC_ID_FLV1 || enc->id == AV_CODEC_ID_MPEG4 ||
            enc->id == AV_CODEC_ID_MSMPEG4V2 || enc->id == AV_CODEC_ID_MSMPEG4V3 ||
            enc->id == AV_CODEC_ID_WMV1 || enc->id == AV_CODEC_ID_WMV2) {
            videoEncCtx->max_b_frames = 0;
        }
        int ret = avcodec_open2(videoEncCtx, enc, nullptr);
        if (ret < 0) {
            char err[256];
            av_strerror(ret, err, sizeof(err));
            emit errorOccurred("无法打开视频编码器: " + QString::fromLatin1(err));
            return false;
        }
        
        if (strcmp(enc->name, "libx264") == 0 || strcmp(enc->name, "h264") == 0) {
            av_opt_set(videoEncCtx->priv_data, "preset", "veryfast", 0);
        } else if (strstr(enc->name, "libvpx") != nullptr || strcmp(enc->name, "vp8") == 0) {
            av_opt_set_int(videoEncCtx->priv_data, "cpu-used", 8, 0);
            av_opt_set(videoEncCtx->priv_data, "deadline", "realtime", 0);
        }
        outVideoStream = avformat_new_stream(outputContext, nullptr);
        if (!outVideoStream) {
            emit errorOccurred("创建视频输出流失败");
            return false;
        }
        outVideoStream->time_base = videoEncCtx->time_base;
        if (avcodec_parameters_from_context(outVideoStream->codecpar, videoEncCtx) < 0) {
            emit errorOccurred("无法设置视频流参数");
            return false;
        }
        
        // 当分辨率或像素格式变化时，需要创建swsCtx进行转换
        if (videoDecCtx->pix_fmt != videoEncCtx->pix_fmt || 
            videoDecCtx->width != videoEncCtx->width || 
            videoDecCtx->height != videoEncCtx->height) {
            swsCtx = sws_getContext(videoDecCtx->width, videoDecCtx->height, videoDecCtx->pix_fmt,
                                    videoEncCtx->width, videoEncCtx->height, videoEncCtx->pix_fmt,
                                    SWS_BILINEAR, nullptr, nullptr, nullptr);
            if (!swsCtx) {
                emit errorOccurred("无法创建视频转换上下文");
                return false;
            }
            videoFrame = av_frame_alloc();
            if (!videoFrame) {
                emit errorOccurred("无法分配视频输出帧");
                return false;
            }
            videoFrame->format = videoEncCtx->pix_fmt;
            videoFrame->width = videoEncCtx->width;
            videoFrame->height = videoEncCtx->height;
            if (av_frame_get_buffer(videoFrame, 32) < 0) {
                emit errorOccurred("无法为视频输出帧分配缓冲区");
                return false;
            }
        }
        return true;
    };
    
    // 6) 创建并打开音频编码器与输出音频流
    auto addAudioStream = [&]() -> bool {
        if (audioStreamIndex < 0) return true;
        const AVCodec *enc = avcodec_find_encoder_by_name(aCodecName.constData());
        if (!enc) {
            emit errorOccurred("未找到音频编码器: " + QString::fromLatin1(aCodecName));
            return false;
        }
        audioEncCtx = avcodec_alloc_context3(enc);
        if (!audioEncCtx) {
            emit errorOccurred("无法分配音频编码器上下文");
            return false;
        }
        
        if (audioDecCtx && audioDecCtx->ch_layout.nb_channels > 0) {
            av_channel_layout_copy(&audioEncCtx->ch_layout, &audioDecCtx->ch_layout);
        } else {
            av_channel_layout_default(&audioEncCtx->ch_layout, 2);
        }
        
        if (enc->id == AV_CODEC_ID_OPUS) {
            audioEncCtx->sample_rate = 48000;
        } else {
            audioEncCtx->sample_rate = audioDecCtx ? audioDecCtx->sample_rate : 48000;
        }
        // 对于mp3编码器，优先使用s16p采样格式（avi格式下兼容性更好）
        if (enc->id == AV_CODEC_ID_MP3) {
            audioEncCtx->sample_fmt = AV_SAMPLE_FMT_S16P;
        } else {
            audioEncCtx->sample_fmt = enc->sample_fmts ? enc->sample_fmts[0] : AV_SAMPLE_FMT_FLTP;
        }
        audioEncCtx->time_base = AVRational{1, audioEncCtx->sample_rate};
        audioEncCtx->bit_rate = 192'000;
        if (outputContext->oformat->flags & AVFMT_GLOBALHEADER) {
            audioEncCtx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        }
        int ret = avcodec_open2(audioEncCtx, enc, nullptr);
        if (ret < 0) {
            char err[256];
            av_strerror(ret, err, sizeof(err));
            emit errorOccurred("无法打开音频编码器: " + QString::fromLatin1(err));
            return false;
        }
        
        outAudioStream = avformat_new_stream(outputContext, nullptr);
        if (!outAudioStream) {
            emit errorOccurred("创建音频输出流失败");
            return false;
        }
        outAudioStream->time_base = audioEncCtx->time_base;
        if (avcodec_parameters_from_context(outAudioStream->codecpar, audioEncCtx) < 0) {
            emit errorOccurred("无法设置音频流参数");
            return false;
        }
        
        bool needResample = false;
        if (audioDecCtx) {
            needResample = (audioDecCtx->sample_fmt != audioEncCtx->sample_fmt) ||
                           (audioDecCtx->sample_rate != audioEncCtx->sample_rate) ||
                           (av_channel_layout_compare(&audioDecCtx->ch_layout, &audioEncCtx->ch_layout) != 0);
        }
        if (audioEncCtx->frame_size > 0) {
            needResample = true;
        }
        if (needResample && audioDecCtx) {
            if (swr_alloc_set_opts2(&swrCtx,
                                    &audioEncCtx->ch_layout, audioEncCtx->sample_fmt, audioEncCtx->sample_rate,
                                    &audioDecCtx->ch_layout, audioDecCtx->sample_fmt, audioDecCtx->sample_rate,
                                    0, nullptr) < 0) {
                emit errorOccurred("无法配置音频重采样上下文");
                return false;
            }
            if (!swrCtx || swr_init(swrCtx) < 0) {
                emit errorOccurred("无法初始化音频重采样上下文");
                return false;
            }
        }
        return true;
    };
    
    if (!addVideoStream() || !addAudioStream()) {
        emit errorOccurred("创建输出流失败");
        cleanup();
        return false;
    }
    
    QFileInfo outInfo(outputFile);
    QDir outDir(outInfo.path());
    if (!outDir.exists() && !outDir.mkpath(".")) {
        emit errorOccurred("无法创建输出目录: " + outInfo.path());
        cleanup();
        return false;
    }
    
    // 7) 打开输出文件并写入文件头
    if (!(outputContext->oformat->flags & AVFMT_NOFILE)) {
        if (avio_open(&outputContext->pb, outputFile.toLocal8Bit().data(), AVIO_FLAG_WRITE) < 0) {
            emit errorOccurred("无法打开输出文件: " + outputFile);
            cleanup();
            return false;
        }
    }
    
    if (avformat_write_header(outputContext, nullptr) < 0) {
        emit errorOccurred("无法写入文件头");
        cleanup();
        return false;
    }
    
    packet = av_packet_alloc();
    frame = av_frame_alloc();
    if (!packet || !frame) {
        emit errorOccurred("内存分配失败");
        cleanup();
        return false;
    }
    
    startTime = av_gettime();
    
    // 8) 编码并写入输出包（视频/音频共用）
    auto encodeAndWrite = [&](AVCodecContext *encCtx, AVStream *outSt, AVFrame *encFrame) -> bool {
        if (!encCtx || !outSt) return true;
        
        int r = avcodec_send_frame(encCtx, encFrame);
        if (r < 0 && r != AVERROR(EAGAIN) && r != AVERROR_EOF) {
            // 只有非预期的错误才返回false
            char err[256];
            av_strerror(r, err, sizeof(err));
            qDebug() << "发送帧失败:" << err;
            return false;
        }
        
        while (true) {
            AVPacket *pkt = av_packet_alloc();
            if (!pkt) return false;
            
            r = avcodec_receive_packet(encCtx, pkt);
            if (r == AVERROR(EAGAIN) || r == AVERROR_EOF) {
                av_packet_free(&pkt);
                break;
            }
            if (r < 0) {
                av_packet_free(&pkt);
                char err[256];
                av_strerror(r, err, sizeof(err));
                qDebug() << "接收包失败:" << err;
                return false;
            }
            
            av_packet_rescale_ts(pkt, encCtx->time_base, outSt->time_base);
            pkt->stream_index = outSt->index;
            
            if (av_interleaved_write_frame(outputContext, pkt) < 0) {
                av_packet_free(&pkt);
                return false;
            }
            
            av_packet_free(&pkt);
        }
        return true;
    };
    
    // 9) 主循环：读取包 -> 解码 -> 需要时重采样/重缩放 -> 编码写入
    while (av_read_frame(inputContext, packet) >= 0) {
        AVStream *inStream = inputContext->streams[packet->stream_index];
        AVCodecContext *decCtx = (packet->stream_index == videoStreamIndex) ? videoDecCtx :
                                 (packet->stream_index == audioStreamIndex) ? audioDecCtx : nullptr;
        AVCodecContext *encCtx = (packet->stream_index == videoStreamIndex) ? videoEncCtx :
                                 (packet->stream_index == audioStreamIndex) ? audioEncCtx : nullptr;
        AVStream *outSt = (packet->stream_index == videoStreamIndex) ? outVideoStream :
                          (packet->stream_index == audioStreamIndex) ? outAudioStream : nullptr;
        
        if (!decCtx || !encCtx || !outSt) {
            av_packet_unref(packet);
            continue;
        }
        
        int sendRet = avcodec_send_packet(decCtx, packet);
        if (sendRet < 0) {
            // 记录错误但继续处理，avcodec_send_packet的错误通常可以恢复
            char err[256];
            av_strerror(sendRet, err, sizeof(err));
            qDebug() << "发送包失败:" << err;
            av_packet_unref(packet);
            continue;
        }
        av_packet_unref(packet);
        
        while (true) {
            int retRecv = avcodec_receive_frame(decCtx, frame);
            if (retRecv == AVERROR(EAGAIN) || retRecv == AVERROR_EOF) break;
            if (retRecv < 0) {
                // 记录错误但继续处理，大多数解码器错误可以恢复
                char err[256];
                av_strerror(retRecv, err, sizeof(err));
                qDebug() << "接收帧失败:" << err;
                continue;
            }
            
            frame->pts = frame->best_effort_timestamp;
            
            if (decCtx == videoDecCtx && outVideoStream) {
                if (swsCtx && videoFrame) {
                    sws_scale(swsCtx, frame->data, frame->linesize, 0, decCtx->height,
                            videoFrame->data, videoFrame->linesize);
                    videoFrame->pts = frame->pts;
                    if (!encodeAndWrite(videoEncCtx, outVideoStream, videoFrame)) {
                        qDebug() << "编码并写入视频帧失败";
                        continue; // 跳过当前帧，继续处理
                    }
                } else {
                    if (!encodeAndWrite(videoEncCtx, outVideoStream, frame)) {
                        qDebug() << "编码并写入视频帧失败";
                        continue; // 跳过当前帧，继续处理
                    }
                }
            } else if (decCtx == audioDecCtx && outAudioStream) {
                if (swrCtx) {
                // 简化的音频重采样处理
                AVFrame *outFrame = av_frame_alloc();
                if (!outFrame) {
                    qDebug() << "无法分配音频输出帧";
                    continue; // 跳过当前帧，继续处理
                }
                
                int dstNbSamples = av_rescale_rnd(swr_get_delay(swrCtx, decCtx->sample_rate) + frame->nb_samples, 
                                                audioEncCtx->sample_rate, decCtx->sample_rate, AV_ROUND_UP);
                
                // 设置音频帧的必要参数
                outFrame->format = audioEncCtx->sample_fmt;
                outFrame->ch_layout = audioEncCtx->ch_layout;
                outFrame->sample_rate = audioEncCtx->sample_rate;
                outFrame->nb_samples = dstNbSamples;
                
                if (av_frame_get_buffer(outFrame, 0) < 0) {
                    qDebug() << "无法为音频输出帧分配缓冲区";
                    av_frame_free(&outFrame);
                    continue; // 跳过当前帧，继续处理
                }
                
                int swrRet = swr_convert(swrCtx, outFrame->data, dstNbSamples,
                               (const uint8_t **)frame->data, frame->nb_samples);
                if (swrRet < 0) {
                    char err[256];
                    av_strerror(swrRet, err, sizeof(err));
                    qDebug() << "音频重采样失败:" << err;
                    av_frame_free(&outFrame);
                    continue; // 跳过当前帧，继续处理
                }
                
                outFrame->nb_samples = swrRet; // 使用实际输出的样本数
                outFrame->ch_layout = audioEncCtx->ch_layout;
                outFrame->sample_rate = audioEncCtx->sample_rate;
                outFrame->pts = frame->pts;
                
                if (!encodeAndWrite(audioEncCtx, outAudioStream, outFrame)) {
                    qDebug() << "编码并写入音频帧失败";
                    av_frame_free(&outFrame);
                    continue; // 跳过当前帧，继续处理
                }
                av_frame_free(&outFrame);
            } else {
                if (!encodeAndWrite(audioEncCtx, outAudioStream, frame)) {
                    qDebug() << "编码并写入音频帧失败";
                    continue; // 跳过当前帧，继续处理
                }
            }
            }
        }
        
        // 10) 进度估算并上报
        int64_t currentTime = av_gettime();
        int64_t durationUs = inputContext->duration;
        int progress = 0;
        if (durationUs > 0) {
            // 使用当前packet的pts来计算进度
            // 这里简化处理，使用当前时间与总时长的比例
            progress = static_cast<int>((currentTime - startTime) * 100LL / durationUs);
        } else {
            // 简单估算进度
            progress = static_cast<int>((currentTime - startTime) * 100LL / (10 * AV_TIME_BASE));
        }
        progress = qBound(0, progress, 100);
        emit progressChanged(progress);
    }
    
    // flush编码器，忽略flush过程中的错误，因为这是编码的最后阶段
    // 不检查返回值，确保即使flush失败也能完成转换
    encodeAndWrite(videoEncCtx, outVideoStream, nullptr);
    encodeAndWrite(audioEncCtx, outAudioStream, nullptr);
    
    // 写入文件尾，记录错误但不影响转换结果
    int trailerRet = av_write_trailer(outputContext);
    if (trailerRet < 0) {
        char err[256];
        av_strerror(trailerRet, err, sizeof(err));
        qDebug() << "写入文件尾失败:" << err;
    }
    
    cleanup();
    
    return true;
}

QString VideoConverterWorker::getFormatExtension(const QString &format) const
{
    QHash<QString, QString> extensions;
    extensions["mp4"] = ".mp4";
    extensions["avi"] = ".avi";
    extensions["mkv"] = ".mkv";
    extensions["mov"] = ".mov";
    extensions["wmv"] = ".wmv";
    extensions["flv"] = ".flv";
    extensions["webm"] = ".webm";
    
    return extensions.value(format.toLower(), ".mp4");
}
