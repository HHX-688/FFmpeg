#ifndef FFMPEGHANDLER_H
#define FFMPEGHANDLER_H

#include <QObject>
#include <QString>
#include <QThread>
#include "yolodetect.h"

class VideoConverter;
class FrameExtractor;
class CameraHandler;

/**
 * @file ffmpeghandler.h
 * @brief FFmpeg统一处理接口模块
 * @details 该文件定义了FFmpegHandler类，作为一个统一的接口层，
 * 封装了VideoConverter（视频转换）、FrameExtractor（帧提取）和CameraHandler（摄像头处理）
 * 三个功能模块，提供简洁的API接口供上层调用。
 */

/**
 * @class FFmpegHandler
 * @brief FFmpeg功能统一处理类
 * @details 作为应用程序与FFmpeg底层功能的中间层，提供统一的接口来处理
 * 视频转换、帧提取和摄像头采集等功能，简化上层应用的调用复杂度。
 */
class FFmpegHandler : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief 构造函数
     * @param parent 父对象指针
     * @details 初始化FFmpegHandler，创建并初始化各个功能模块实例。
     */
    explicit FFmpegHandler(QObject *parent = nullptr);
    
    /**
     * @brief 析构函数
     * @details 清理资源，停止所有正在运行的操作，释放各个功能模块实例。
     */
    ~FFmpegHandler();

public slots:
    /**
     * @brief 转换视频格式
     * @param inputFile 输入视频文件路径（仅支持MP4格式）
     * @param outputFile 输出视频文件路径
     * @param format 输出视频格式（如mp4、avi、mkv等）
     * @details 将输入视频转换为指定格式的输出视频，支持多种视频格式。
     */
    void convertVideo(const QString &inputFile, const QString &outputFile, const QString &format);
    
    /**
     * @brief 从视频中提取帧
     * @param inputFile 输入视频文件路径（仅支持MP4格式）
     * @param outputDir 输出帧图像的目录
     * @param framesPerSecond 每秒提取的帧数
     * @param imageFormat 输出图像格式，默认为png
     * @details 从视频中按指定帧率提取帧图像并保存到指定目录。
     */
    void extractFrames(const QString &inputFile, const QString &outputDir, int framesPerSecond, const QString &imageFormat = "png");
    
    /**
     * @brief 启动摄像头采集
     * @param cameraIndex 摄像头索引，默认为0
     * @param width 采集宽度，默认为640
     * @param height 采集高度，默认为480
     * @details 启动指定的摄像头进行视频采集。
     */
    void startCamera(int cameraIndex = 0, int width = 640, int height = 480);
    
    /**
     * @brief 停止摄像头采集
     * @details 停止当前正在进行的摄像头采集。
     */
    void stopCamera();
    
    /**
     * @brief 加载YOLO目标检测模型
     * @param modelPath YOLO模型文件路径（ONNX格式）
     * @return 加载成功返回true，失败返回false
     * @details 加载YOLO目标检测模型，用于摄像头实时检测功能。
     */
    bool loadYoloModel(const QString &modelPath);
    
    /**
     * @brief 启用/禁用YOLO目标检测
     * @param enable true为启用检测，false为禁用检测
     * @details 控制摄像头采集过程中是否启用YOLO目标检测。
     */
    void enableYoloDetection(bool enable);

signals:
    /**
     * @brief 进度更新信号
     * @param progress 当前进度值（0-100）
     * @details 在视频转换或帧提取过程中定期发送进度更新。
     */
    void progressChanged(int progress);
    
    /**
     * @brief 操作完成信号
     * @param success 操作是否成功
     * @param message 操作结果描述
     * @details 当视频转换、帧提取或摄像头操作完成时发送。
     */
    void operationFinished(bool success, const QString &message);
    
    /**
     * @brief 错误发生信号
     * @param errorMessage 错误描述信息
     * @details 当操作过程中发生错误时发送。
     */
    void errorOccurred(const QString &errorMessage);
    
    /**
     * @brief 摄像头帧采集完成信号
     * @param frame 采集到的视频帧（QImage格式）
     * @details 摄像头采集到新的视频帧时发送。
     */
    void cameraFrameReady(const QImage &frame);
    
    /**
     * @brief 目标检测结果信号
     * @param frame 检测的视频帧
     * @param results YOLO检测结果列表
     * @details 当摄像头采集过程中启用了YOLO检测时，发送检测结果。
     */
    void detectionResultReady(const QImage &frame, const std::vector<DetectionResult> &results);

private:
    /**
     * @brief 初始化FFmpeg库
     * @return 初始化成功返回true，失败返回false
     * @details 初始化FFmpeg相关库和资源。
     */
    bool initializeFFmpeg();
    
    /**
     * @brief 清理FFmpeg资源
     * @details 释放FFmpeg相关资源。
     */
    void cleanupFFmpeg();

private:
    VideoConverter *m_videoConverter;    ///< 视频转换模块实例
    FrameExtractor *m_frameExtractor;    ///< 帧提取模块实例
    CameraHandler *m_cameraHandler;      ///< 摄像头处理模块实例
};

#endif // FFMPEGHANDLER_H