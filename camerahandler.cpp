#include "camerahandler.h"
#include <QDebug>
#include <QImage>
#include <QThread>
#include <QPointer>
#include <QMutexLocker>
#include <QtConcurrent/QtConcurrent>
#include <cstdio>
#include <cstring>
#include "yolodetect.h"

// FFmpeg headers
extern "C" {
#include <libavcodec/avcodec.h>     // 编解码器库
#include <libavdevice/avdevice.h>   // 设备输入输出库
#include <libavformat/avformat.h>   // 格式处理库
#include <libavutil/avutil.h>       // 通用工具函数
#include <libavutil/channel_layout.h> // 声道布局
#include <libavutil/imgutils.h>     // 图像工具函数
#include <libavutil/opt.h>          // 选项设置
#include <libavutil/pixfmt.h>       // 像素格式定义
#include <libavutil/time.h>         // 时间相关函数
#include <libswscale/swscale.h>     // 图像缩放和颜色空间转换
}

/**
 * @file camerahandler.cpp
 * @brief 摄像头采集与实时检测模块实现
 * @details 该文件实现了CameraHandler和CameraHandlerWorker类的具体功能，
 * 包括FFmpeg初始化、摄像头设备管理、视频帧采集与解码、格式转换、
 * 以及与YOLO目标检测模型的集成和异步处理。
 */

/**
 * @brief 将已废弃的YUVJ*格式转换为现代YUV格式并设置范围信息
 * @param[in] fmt 输入的像素格式
 * @param[out] srcRange 输出的色彩范围标识（0=有限范围，1=全范围）
 * @return 转换后的像素格式
 * @details 处理FFmpeg中已废弃的YUVJ*格式，将其映射到对应的现代YUV格式，
 * 并通过srcRange参数返回该格式的色彩范围信息，用于后续的颜色空间转换。
 */
static AVPixelFormat normalizeDeprecatedPixFmt(AVPixelFormat fmt, int &srcRange)
{
    srcRange = 0;
    switch (fmt) {
    case AV_PIX_FMT_YUVJ420P: srcRange = 1; return AV_PIX_FMT_YUV420P;
    case AV_PIX_FMT_YUVJ422P: srcRange = 1; return AV_PIX_FMT_YUV422P;
    case AV_PIX_FMT_YUVJ444P: srcRange = 1; return AV_PIX_FMT_YUV444P;
    case AV_PIX_FMT_YUVJ440P: srcRange = 1; return AV_PIX_FMT_YUV440P;
    default:
        return fmt;
    }
}

/**
 * @brief FFmpeg日志回调函数
 * @param[in] ptr 日志上下文指针
 * @param[in] level 日志级别（AV_LOG_ERROR、AV_LOG_WARNING等）
 * @param[in] fmt 日志格式化字符串
 * @param[in] vl 可变参数列表
 * @details 自定义FFmpeg日志输出处理，只输出错误级别以上的日志，
 * 并过滤掉"real-time buffer"相关的警告信息，减少日志输出量。
 */
static void ffmpegLogCallback(void *ptr, int level, const char *fmt, va_list vl)
{
    if (level > AV_LOG_ERROR) {
        return;
    }

    char msg[1024] = {0};
    vsnprintf(msg, sizeof(msg), fmt, vl);
    if (strstr(msg, "real-time buffer") != nullptr) {
        return;
    }

    fprintf(stderr, "%s", msg);
}

/**
 * @brief CameraHandler构造函数
 * @param parent 父对象指针
 * @details 初始化摄像头处理模块，包括FFmpeg库的初始化和YOLO检测实例的创建。
 */
CameraHandler::CameraHandler(QObject *parent)
    : QObject(parent)
    , m_cameraRunning(false)
    , m_yoloEnabled(false)
    , m_cameraThread(nullptr)
    , m_cameraWorker(nullptr)
    , m_yoloDetect(nullptr)
{
    initializeFFmpeg();
    m_yoloDetect = new YoloDetect(this);
}

/**
 * @brief CameraHandler析构函数
 * @details 清理资源，包括停止摄像头采集、释放FFmpeg资源和删除YOLO检测实例。
 */
CameraHandler::~CameraHandler()
{
    stopCamera();
    cleanupFFmpeg();
    delete m_yoloDetect;
    m_yoloDetect = nullptr;
}

/**
 * @brief 初始化FFmpeg库
 * @return 初始化成功返回true，失败返回false
 * @details 配置FFmpeg日志级别和回调函数，注册所有设备类型，
 * 为摄像头采集做好准备工作。
 */
bool CameraHandler::initializeFFmpeg()
{
    av_log_set_level(AV_LOG_ERROR);  // 设置日志级别为错误级别
    av_log_set_callback(ffmpegLogCallback);  // 设置自定义日志回调函数
    avdevice_register_all();  // 注册所有设备类型
    return true;
}

/**
 * @brief 清理FFmpeg资源
 * @details FFmpeg会自动清理其全局状态，此函数作为预留接口。
 */

void CameraHandler::cleanupFFmpeg()
{
    // FFmpeg会自动清理其全局状态，无需手动操作
}

/**
 * @brief 启动摄像头采集
 * @param cameraIndex 摄像头索引，默认为0
 * @param width 采集宽度，默认为640
 * @param height 采集高度，默认为480
 * @details 创建并启动摄像头采集工作线程，设置YOLO检测状态，
 * 建立信号槽连接，将工作线程的结果转发到UI线程。
 */
void CameraHandler::startCamera(int cameraIndex, int width, int height)
{
    if (m_cameraRunning) {
        emit errorOccurred("Camera is already running.");
        return;
    }

    // 创建工作线程和工作对象
    m_cameraThread = new QThread(this);
    m_cameraWorker = new CameraHandlerWorker(cameraIndex, width, height);

    // 设置YOLO检测状态和加载模型（如果已加载）
    m_cameraWorker->enableYoloDetection(m_yoloEnabled);
    if (!m_yoloModelPath.isEmpty()) {
        m_cameraWorker->loadYoloModel(m_yoloModelPath);
    }

    // 将工作对象移动到工作线程
    m_cameraWorker->moveToThread(m_cameraThread);

    // 连接线程生命周期相关信号槽
    connect(m_cameraThread, &QThread::started,
            m_cameraWorker, &CameraHandlerWorker::process, Qt::QueuedConnection);
    connect(m_cameraWorker, &CameraHandlerWorker::finished,
            m_cameraThread, &QThread::quit, Qt::DirectConnection);
    connect(m_cameraWorker, &CameraHandlerWorker::finished,
            m_cameraWorker, &CameraHandlerWorker::deleteLater, Qt::DirectConnection);
    connect(m_cameraThread, &QThread::finished,
            m_cameraThread, &QThread::deleteLater, Qt::DirectConnection);

    // 线程销毁时清空指针
    connect(m_cameraThread, &QThread::destroyed, this, [this]() {
        m_cameraThread = nullptr;
        m_cameraWorker = nullptr;
    });

    // 连接工作线程的功能信号槽
    connect(m_cameraWorker, &CameraHandlerWorker::errorOccurred,
            this, &CameraHandler::errorOccurred, Qt::QueuedConnection);
    connect(m_cameraWorker, &CameraHandlerWorker::cameraFrameReady,
            this, &CameraHandler::cameraFrameReady, Qt::QueuedConnection);
    connect(m_cameraWorker, &CameraHandlerWorker::detectionResultReady,
            this, &CameraHandler::detectionResultReady, Qt::QueuedConnection);
    connect(m_cameraWorker, &CameraHandlerWorker::finished,
            this, [this](bool success, const QString &message) {
        m_cameraRunning = false;
        emit operationFinished(success, message);
    }, Qt::QueuedConnection);

    // 启动线程并更新状态
    m_cameraRunning = true;
    m_cameraThread->start();

    emit operationFinished(true, "Camera started.");
}

/**
 * @brief 停止摄像头采集
 * @details 停止摄像头采集线程，等待线程正常结束，
 * 如果超时则强制终止线程，并清理相关资源。
 */
void CameraHandler::stopCamera()
{
    if (!m_cameraRunning) {
        return;
    }

    m_cameraRunning = false;

    // 请求工作线程停止
    if (m_cameraWorker) {
        m_cameraWorker->stop();
    }

    // 等待线程结束，超时则强制终止
    if (m_cameraThread && m_cameraThread->isRunning()) {
        if (!m_cameraThread->wait(3000)) {
            m_cameraThread->terminate();
            m_cameraThread->wait(1000);
        }
    }

    // 清空指针
    m_cameraThread = nullptr;
    m_cameraWorker = nullptr;

    emit operationFinished(true, "Camera stopped.");
}

/**
 * @brief 加载YOLO目标检测模型
 * @param modelPath YOLO模型文件路径（ONNX格式）
 * @return 加载成功返回true，失败返回false
 * @details 加载YOLO目标检测模型，如果摄像头正在运行，则将模型加载到工作线程中，
 * 否则加载到当前对象的YOLO检测实例中。
 */
bool CameraHandler::loadYoloModel(const QString &modelPath)
{
    m_yoloModelPath = modelPath;

    // 如果摄像头正在运行，将模型加载到工作线程中
    if (m_cameraRunning && m_cameraWorker) {
        return m_cameraWorker->loadYoloModel(modelPath);
    }

    // 否则加载到当前对象的YOLO检测实例中
    bool success = m_yoloDetect->loadModel(modelPath);
    emit operationFinished(success, success ? "YOLO model loaded." : "Failed to load YOLO model.");
    return success;
}

/**
 * @brief 启用/禁用YOLO目标检测
 * @param enable true为启用检测，false为禁用检测
 * @details 设置YOLO目标检测的启用状态，如果摄像头正在运行，则同步更新工作线程的检测状态。
 */
void CameraHandler::enableYoloDetection(bool enable)
{
    m_yoloEnabled = enable;
    // 如果摄像头正在运行，同步更新工作线程的检测状态
    if (m_cameraWorker) {
        m_cameraWorker->enableYoloDetection(enable);
    }
    emit operationFinished(true, enable ? "YOLO enabled." : "YOLO disabled.");
}

/**
 * @brief 查询YOLO检测是否已启用
 * @return 已启用返回true，否则返回false
 */
bool CameraHandler::isYoloEnabled() const
{
    return m_yoloEnabled;
}

/**
 * @brief 查询摄像头是否正在运行
 * @return 正在运行返回true，否则返回false
 */
bool CameraHandler::isCameraRunning() const
{
    return m_cameraRunning;
}

/**
 * @brief 获取可用摄像头设备列表
 * @return 摄像头设备名称列表
 * @details 当前返回固定的摄像头设备列表，实际应用中应通过FFmpeg枚举系统中的摄像头设备。
 */
QStringList CameraHandler::getAvailableCameras() const
{
    QStringList cameras;
    cameras << "Ysd-Anzija" << "USB Video Device";
    return cameras;
}

/**
 * @brief CameraHandlerWorker构造函数
 * @param cameraIndex 摄像头索引
 * @param width 采集宽度
 * @param height 采集高度
 * @param parent 父对象指针
 * @details 初始化摄像头采集工作线程，创建YOLO目标检测实例。
 */
CameraHandlerWorker::CameraHandlerWorker(int cameraIndex, int width, int height, QObject *parent)
    : QObject(parent)
    , m_cameraIndex(cameraIndex)
    , m_width(width)
    , m_height(height)
    , m_stopRequested(false)
    , m_yoloEnabled(false)
    , m_frameIndex(0)
    , m_detectionBusy(false)
    , m_yoloDetect(nullptr)
{
    m_yoloDetect = new YoloDetect(this);
}

/**
 * @brief CameraHandlerWorker析构函数
 * @details 停止摄像头采集，释放YOLO检测实例资源。
 */
CameraHandlerWorker::~CameraHandlerWorker()
{
    stop();
    delete m_yoloDetect;
    m_yoloDetect = nullptr;
}

/**
 * @brief 线程入口函数
 * @details 在独立线程中执行摄像头采集主循环，处理可能的异常情况，
 * 并在采集结束时发送完成信号。
 */
void CameraHandlerWorker::process()
{
    bool success = false;
    QString message;

    try {
        // 在独立线程中启动摄像头采集主循环
        success = cameraStream(m_cameraIndex, m_width, m_height);
        message = success ? "Camera stream ended." : "Camera stream failed.";
    } catch (const std::exception &e) {
        // 捕获并处理异常
        message = QString("Exception: %1").arg(e.what());
        success = false;
    }

    // 发送采集完成信号
    emit finished(success, message);
}

/**
 * @brief 请求停止摄像头采集
 * @details 设置停止请求标志，摄像头采集主循环会检测此标志并安全退出。
 */
void CameraHandlerWorker::stop()
{
    m_stopRequested = true;
}

/**
 * @brief 查询是否已请求停止采集
 * @return 已请求停止返回true，否则返回false
 */
bool CameraHandlerWorker::isStopped() const
{
    return m_stopRequested;
}

/**
 * @brief 启用/禁用YOLO目标检测
 * @param enable true为启用检测，false为禁用检测
 * @details 设置YOLO检测的启用状态，采集主循环会根据此状态决定是否执行检测。
 */
void CameraHandlerWorker::enableYoloDetection(bool enable)
{
    m_yoloEnabled = enable;
}

/**
 * @brief 加载YOLO目标检测模型
 * @param modelPath YOLO模型文件路径（ONNX格式）
 * @return 加载成功返回true，失败返回false
 * @details 加载YOLO目标检测模型到工作线程的YOLO检测实例中。
 */
bool CameraHandlerWorker::loadYoloModel(const QString &modelPath)
{
    bool success = m_yoloDetect->loadModel(modelPath);
    emit operationFinished(success, success ? "YOLO model loaded." : "Failed to load YOLO model.");
    return success;
}

/**
 * @brief 查询YOLO检测是否已启用
 * @return 已启用返回true，否则返回false
 */
bool CameraHandlerWorker::isYoloEnabled() const
{
    return m_yoloEnabled;
}

/**
 * @brief 摄像头采集主循环
 * @param cameraIndex 摄像头索引
 * @param width 采集宽度
 * @param height 采集高度
 * @return 采集成功返回true，失败返回false
 * @details 执行摄像头设备的打开、视频流解码、格式转换、帧发送以及
 * YOLO目标检测等完整流程。这是摄像头采集功能的核心实现。
 */
bool CameraHandlerWorker::cameraStream(int cameraIndex, int width, int height)
{
    AVFormatContext *formatContext = nullptr;
    AVCodecContext *codecContext = nullptr;
    const AVCodec *codec = nullptr;
    AVFrame *frame = nullptr;
    AVPacket *packet = nullptr;
    SwsContext *swsCtx = nullptr;

    int videoStreamIndex = -1;
    int ret = 0;

    // 1) 创建输入上下文，负责打开摄像头设备与读取数据
    formatContext = avformat_alloc_context();
    if (!formatContext) {
        emit errorOccurred("Failed to allocate format context.");
        return false;
    }

    // 2) 使用 DirectShow 枚举摄像头设备
    AVDeviceInfoList *deviceList = nullptr;
    const AVInputFormat *dshowFormat = av_find_input_format("dshow");

    int deviceCount = avdevice_list_input_sources(dshowFormat, "video", nullptr, &deviceList);
    qDebug() << "Found" << deviceCount << "video devices.";

    QString deviceName;
    if (deviceCount > 0 && cameraIndex < deviceCount) {
        // 从枚举到的设备列表中选择指定索引的设备
        deviceName = deviceList->devices[cameraIndex]->device_name;
        qDebug() << "Using device" << cameraIndex << ":" << deviceName;
    } else {
        // 枚举失败时使用默认设备名称
        deviceName = (cameraIndex == 0) ? "Ysd-Anzija" : "USB Video Device";
        qDebug() << "Device enumeration failed, using default:" << deviceName;
    }

    avdevice_free_list_devices(&deviceList);
    QString inputUrl = QString("video=%1").arg(deviceName);

    // 3) 打开摄像头，设置分辨率与低延迟参数
    AVDictionary *options = nullptr;
    QString videoSize = QString("%1x%2").arg(width).arg(height);
    av_dict_set(&options, "video_size", videoSize.toLocal8Bit().constData(), 0);  // 设置分辨率
    av_dict_set(&options, "rtbufsize", "1M", 0);  // 设置实时缓冲区大小
    av_dict_set(&options, "fflags", "nobuffer", 0);  // 禁用缓冲以降低延迟
    av_dict_set(&options, "flags", "low_delay", 0);  // 设置低延迟模式

    qDebug() << "Opening camera:" << inputUrl;
    qDebug() << "Input format:" << (dshowFormat ? QString(dshowFormat->name) : "not found");

    // 尝试打开摄像头设备
    ret = avformat_open_input(&formatContext, inputUrl.toLocal8Bit().data(),
                              const_cast<AVInputFormat*>(dshowFormat), &options);
    if (ret < 0) {
        // 第一次尝试失败，释放选项字典并重试一次（不使用额外选项）
        av_dict_free(&options);
        avformat_free_context(formatContext);
        formatContext = avformat_alloc_context();

        ret = avformat_open_input(&formatContext, inputUrl.toLocal8Bit().data(),
                                  const_cast<AVInputFormat*>(dshowFormat), nullptr);
        if (ret < 0) {
            // 两次尝试都失败，生成详细错误信息
            char errorBuf[1024] = {0};
            av_strerror(ret, errorBuf, sizeof(errorBuf));
            QString detailedError = QString("Failed to open camera '%1': %2\n"
                                            "Possible reasons:\n"
                                            "1) Device is in use\n"
                                            "2) Resolution %3x%4 is not supported\n"
                                            "3) Driver issue\n"
                                            "4) Permission denied")
                                        .arg(inputUrl).arg(QString(errorBuf)).arg(width).arg(height);
            emit errorOccurred(detailedError);
            avformat_free_context(formatContext);
            return false;
        }
    }
    av_dict_free(&options);

    // 设置格式上下文标志以降低延迟
    formatContext->flags |= AVFMT_FLAG_NOBUFFER;    // 禁用缓冲
    formatContext->flags |= AVFMT_FLAG_FLUSH_PACKETS;  // 立即刷新数据包

    // 4) 读取流信息，定位视频流
    if (avformat_find_stream_info(formatContext, nullptr) < 0) {
        emit errorOccurred("Failed to read stream info.");
        avformat_close_input(&formatContext);
        return false;
    }

    // 查找视频流索引
    for (unsigned int i = 0; i < formatContext->nb_streams; i++) {
        if (formatContext->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            videoStreamIndex = static_cast<int>(i);
            break;
        }
    }

    if (videoStreamIndex == -1) {
        emit errorOccurred("No video stream found.");
        avformat_close_input(&formatContext);
        return false;
    }

    // 5) 打开对应的视频解码器
    codec = avcodec_find_decoder(formatContext->streams[videoStreamIndex]->codecpar->codec_id);
    if (!codec) {
        emit errorOccurred("No suitable decoder found.");
        avformat_close_input(&formatContext);
        return false;
    }

    // 分配解码器上下文
    codecContext = avcodec_alloc_context3(codec);
    if (!codecContext) {
        emit errorOccurred("Failed to allocate codec context.");
        avformat_close_input(&formatContext);
        return false;
    }

    // 将流参数复制到解码器上下文
    if (avcodec_parameters_to_context(codecContext, formatContext->streams[videoStreamIndex]->codecpar) < 0) {
        emit errorOccurred("Failed to copy codec parameters.");
        avcodec_free_context(&codecContext);
        avformat_close_input(&formatContext);
        return false;
    }

    // 打开解码器
    if (avcodec_open2(codecContext, codec, nullptr) < 0) {
        emit errorOccurred("Failed to open decoder.");
        avcodec_free_context(&codecContext);
        avformat_close_input(&formatContext);
        return false;
    }

    // 分配帧和数据包结构
    frame = av_frame_alloc();
    if (!frame) {
        emit errorOccurred("Failed to allocate frame.");
        goto cleanup;
    }

    packet = av_packet_alloc();
    if (!packet) {
        emit errorOccurred("Failed to allocate packet.");
        goto cleanup;
    }

    // 6) 主循环：读包 -> 解码 -> 转成 QImage -> 发给 UI，同时异步送入检测
    while (!m_stopRequested) {
        // 读取一帧数据
        ret = av_read_frame(formatContext, packet);
        if (ret < 0) {
            if (m_stopRequested) break;
            QThread::msleep(10);  // 读取失败时短暂休眠后重试
            continue;
        }

        // 只处理视频流数据
        if (packet->stream_index == videoStreamIndex) {
            // 将数据包发送到解码器
            ret = avcodec_send_packet(codecContext, packet);
            if (ret < 0) {
                av_packet_unref(packet);
                continue;
            }

            // 循环接收解码后的帧
            while (ret >= 0) {
                ret = avcodec_receive_frame(codecContext, frame);
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                    break;  // 没有更多帧或需要更多数据
                } else if (ret < 0) {
                    av_frame_unref(frame);
                    break;
                }

                // 处理像素格式和色彩范围
                int srcRange = 0;
                AVPixelFormat rawFmt = static_cast<AVPixelFormat>(frame->format);
                AVPixelFormat srcFmt = normalizeDeprecatedPixFmt(rawFmt, srcRange);
                if (srcRange == 0 && frame->color_range == AVCOL_RANGE_JPEG) {
                    srcRange = 1;
                }

                // 创建或获取图像转换上下文（SwsContext）
                swsCtx = sws_getCachedContext(swsCtx,
                                              frame->width, frame->height, srcFmt,
                                              frame->width, frame->height, AV_PIX_FMT_RGB24,
                                              SWS_BILINEAR, nullptr, nullptr, nullptr);
                if (!swsCtx) {
                    emit errorOccurred("Failed to create sws context.");
                    av_frame_unref(frame);
                    break;
                }

                // 设置色彩空间转换参数
                int cs = frame->colorspace;
                if (cs == AVCOL_SPC_UNSPECIFIED) {
                    cs = AVCOL_SPC_SMPTE170M;  // 默认使用SMPTE 170M色彩空间
                }

                const int *coeffs = sws_getCoefficients(cs);
                const int dstRange = 1;
                sws_setColorspaceDetails(swsCtx, coeffs, srcRange, coeffs, dstRange,
                                         0, 1 << 16, 1 << 16);

                int imageWidth = frame->width;
                int imageHeight = frame->height;

                // 将解码后的帧转换为 RGB，并拷贝到 QImage 里
                QImage image(imageWidth, imageHeight, QImage::Format_RGB888);
                if (image.isNull()) {
                    av_frame_unref(frame);
                    break;
                }

                // 设置目标图像的指针和行大小
                uint8_t *destData[1] = { image.bits() };
                int destLinesize[1] = { static_cast<int>(image.bytesPerLine()) };
                
                // 执行图像格式转换
                sws_scale(swsCtx, frame->data, frame->linesize, 0, imageHeight,
                          destData, destLinesize);

                // 创建帧图像的副本，避免在后续处理中被覆盖
                QImage frameImage = image.copy();
                
                // 立即把当前帧发给 UI 显示（不等待检测结果）
                emit cameraFrameReady(frameImage);

                // 如果启用了YOLO检测且检测实例有效
                if (m_yoloEnabled && m_yoloDetect) {
                    // 使用原子操作检查检测是否忙碌，避免检测任务堆积
                    if (!m_detectionBusy.exchange(true)) {
                        QPointer<CameraHandlerWorker> self(this);
                        QImage detectImage = frameImage.copy();
                        
                        // 在单独的线程中执行YOLO检测，避免阻塞采集线程
                        QtConcurrent::run([self, detectImage]() {
                            if (!self) {
                                return;
                            }

                            std::vector<DetectionResult> results;
                            {
                                // 使用互斥锁保护模型推理过程，避免并发冲突
                                QMutexLocker locker(&self->m_detectionMutex);
                                results = self->m_yoloDetect->detect(detectImage);
                            }

                            if (self) {
                                // 检测完成后发送结果到UI线程
                                // 注意：检测结果可能会在下一帧显示时叠加
                                emit self->detectionResultReady(detectImage, results);
                                // 标记检测已完成，允许处理下一个检测任务
                                self->m_detectionBusy = false;
                            }
                        });
                    }
                }

                ++m_frameIndex;  // 更新帧计数器

                av_frame_unref(frame);  // 释放当前帧资源
            }
        }

        av_packet_unref(packet);  // 释放当前数据包资源
    }

    // 释放图像转换上下文
    if (swsCtx) {
        sws_freeContext(swsCtx);
    }

cleanup:
    // 释放所有分配的资源
    if (frame) av_frame_free(&frame);
    if (packet) av_packet_free(&packet);
    if (codecContext) avcodec_free_context(&codecContext);
    if (formatContext) avformat_close_input(&formatContext);

    return true;
}
