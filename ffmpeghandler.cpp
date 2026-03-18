#include "ffmpeghandler.h"
#include "videoconverter.h"
#include "frameextractor.h"
#include "camerahandler.h"
#include <QDebug>

/**
 * @file ffmpeghandler.cpp
 * @brief FFmpeg统一处理接口模块实现
 * @details 该文件实现了FFmpegHandler类的具体功能，作为应用程序与FFmpeg底层功能的中间层，
 * 封装了视频转换、帧提取和摄像头采集等功能，提供简洁统一的API接口。
 */

/**
 * @brief FFmpegHandler构造函数
 * @param parent 父对象指针
 * @details 初始化FFmpegHandler，包括FFmpeg库的初始化，创建视频转换、帧提取和摄像头处理模块实例，
 * 并建立信号槽连接，将各模块的信号转发到上层。
 */
FFmpegHandler::FFmpegHandler(QObject *parent) : QObject(parent)
{
    // 初始化FFmpeg库
    if (!initializeFFmpeg()) {
        qWarning() << "Failed to initialize FFmpeg";
    }
    
    // 创建三个处理器实例
    m_videoConverter = new VideoConverter(this);   // 视频转换模块实例
    m_frameExtractor = new FrameExtractor(this);   // 帧提取模块实例
    m_cameraHandler = new CameraHandler(this);     // 摄像头处理模块实例
    
    // 连接VideoConverter的信号到当前类
    connect(m_videoConverter, &VideoConverter::progressChanged, this, &FFmpegHandler::progressChanged);
    connect(m_videoConverter, &VideoConverter::operationFinished, this, &FFmpegHandler::operationFinished);
    connect(m_videoConverter, &VideoConverter::errorOccurred, this, &FFmpegHandler::errorOccurred);
    
    // 连接FrameExtractor的信号到当前类
    connect(m_frameExtractor, &FrameExtractor::progressChanged, this, &FFmpegHandler::progressChanged);
    connect(m_frameExtractor, &FrameExtractor::operationFinished, this, &FFmpegHandler::operationFinished);
    connect(m_frameExtractor, &FrameExtractor::errorOccurred, this, &FFmpegHandler::errorOccurred);
    
    // 连接CameraHandler的信号到当前类
    connect(m_cameraHandler, &CameraHandler::cameraFrameReady, this, &FFmpegHandler::cameraFrameReady);
    connect(m_cameraHandler, &CameraHandler::detectionResultReady, this, &FFmpegHandler::detectionResultReady);
    connect(m_cameraHandler, &CameraHandler::operationFinished, this, &FFmpegHandler::operationFinished);
    connect(m_cameraHandler, &CameraHandler::errorOccurred, this, &FFmpegHandler::errorOccurred);
}

FFmpegHandler::~FFmpegHandler()
{
    // 停止摄像头
    stopCamera();
    
    // 删除三个处理器实例
    delete m_videoConverter;
    delete m_frameExtractor;
    delete m_cameraHandler;
    
    cleanupFFmpeg();
}

bool FFmpegHandler::initializeFFmpeg()
{
    // 新版本FFmpeg不需要显式注册编解码器
    return true;
}

void FFmpegHandler::cleanupFFmpeg()
{
    // FFmpeg会自动清理资源
}

void FFmpegHandler::convertVideo(const QString &inputFile, const QString &outputFile, const QString &format)
{
    if (!inputFile.toLower().endsWith(".mp4")) {
        emit errorOccurred("当前仅支持 MP4 作为输入源");
        emit operationFinished(false, "仅支持 MP4 输入");
        return;
    }
    
    // 调用VideoConverter的convertVideo方法
    m_videoConverter->convertVideo(inputFile, outputFile, format);
}

void FFmpegHandler::extractFrames(const QString &inputFile, const QString &outputDir, int framesPerSecond, const QString &imageFormat)
{
    if (!inputFile.toLower().endsWith(".mp4")) {
        emit errorOccurred("当前仅支持 MP4 作为输入源");
        emit operationFinished(false, "仅支持 MP4 输入");
        return;
    }
    
    // 调用FrameExtractor的extractFrames方法
    m_frameExtractor->extractFrames(inputFile, outputDir, framesPerSecond, imageFormat);
}

void FFmpegHandler::startCamera(int cameraIndex, int width, int height)
{
    // 调用CameraHandler的startCamera方法
    m_cameraHandler->startCamera(cameraIndex, width, height);
}

void FFmpegHandler::stopCamera()
{
    // 调用CameraHandler的stopCamera方法
    m_cameraHandler->stopCamera();
}

bool FFmpegHandler::loadYoloModel(const QString &modelPath)
{
    // 调用CameraHandler的loadYoloModel方法
    return m_cameraHandler->loadYoloModel(modelPath);
}

void FFmpegHandler::enableYoloDetection(bool enable)
{
    // 调用CameraHandler的enableYoloDetection方法
    m_cameraHandler->enableYoloDetection(enable);
}