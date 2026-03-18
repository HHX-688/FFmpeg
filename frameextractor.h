#ifndef FRAMEEXTRACTOR_H
#define FRAMEEXTRACTOR_H

#include <QObject>
#include <QString>
#include <QThread>

class FrameExtractorWorker;

/**
 * @file frameextractor.h
 * @brief 视频帧提取模块的头文件
 * @details 该文件定义了视频帧提取的控制器类和工作线程类，用于从视频文件中按指定帧率提取帧并保存为图片
 */

/**
 * @class FrameExtractor
 * @brief 视频帧提取控制器类
 * @details 该类是视频帧提取功能的入口点，负责创建后台工作线程、管理FFmpeg的初始化和清理，以及提供与UI交互的接口
 */
class FrameExtractor : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief 构造函数
     * @param parent 父对象指针
     */
    explicit FrameExtractor(QObject *parent = nullptr);
    
    /**
     * @brief 析构函数
     */
    ~FrameExtractor();

public slots:
    /**
     * @brief 开始从视频中提取帧
     * @param inputFile 输入视频文件路径
     * @param outputDir 输出图片保存目录
     * @param framesPerSecond 每秒提取的帧数
     * @param imageFormat 输出图片格式，默认为"png"
     */
    void extractFrames(const QString &inputFile, const QString &outputDir, int framesPerSecond, const QString &imageFormat = "png");

signals:
    /**
     * @brief 抽帧进度更新信号
     * @param progress 当前进度，取值范围0-100
     */
    void progressChanged(int progress);
    
    /**
     * @brief 抽帧操作完成信号
     * @param success 操作是否成功
     * @param message 操作结果消息
     */
    void operationFinished(bool success, const QString &message);
    
    /**
     * @brief 错误发生信号
     * @param errorMessage 错误消息描述
     */
    void errorOccurred(const QString &errorMessage);

private:
    /**
     * @brief 初始化FFmpeg库
     * @return 初始化成功返回true，失败返回false
     */
    bool initializeFFmpeg();
    
    /**
     * @brief 清理FFmpeg库资源
     */
    void cleanupFFmpeg();
};

/**
 * @class FrameExtractorWorker
 * @brief 视频帧提取工作线程类
 * @details 该类继承自QThread，在后台线程中执行实际的视频解码和帧提取工作，使用FFmpeg进行视频处理
 */
class FrameExtractorWorker : public QThread
{
    Q_OBJECT

public:
    /**
     * @brief 构造函数
     * @param inputFile 输入视频文件路径
     * @param outputDir 输出图片保存目录
     * @param framesPerSecond 每秒提取的帧数
     * @param imageFormat 输出图片格式
     * @param parent 父对象指针
     */
    FrameExtractorWorker(const QString &inputFile, const QString &outputDir, int framesPerSecond, const QString &imageFormat, QObject *parent = nullptr);
    
    /**
     * @brief 析构函数
     */
    ~FrameExtractorWorker();
    
    /**
     * @brief 线程入口函数，执行实际的抽帧工作
     */
    void run() override;

signals:
    /**
     * @brief 抽帧进度更新信号
     * @param progress 当前进度，取值范围0-100
     */
    void progressChanged(int progress);
    
    /**
     * @brief 抽帧操作完成信号
     * @param success 操作是否成功
     * @param message 操作结果消息
     */
    void finished(bool success, const QString &message);
    
    /**
     * @brief 错误发生信号
     * @param errorMessage 错误消息描述
     */
    void errorOccurred(const QString &errorMessage);

private:
    /**
     * @brief 抽帧主流程实现
     * @param inputFile 输入视频文件路径
     * @param outputDir 输出图片保存目录
     * @param framesPerSecond 每秒提取的帧数
     * @param imageFormat 输出图片格式
     * @return 抽帧成功返回true，失败返回false
     */
    bool extractFrames(const QString &inputFile, const QString &outputDir, int framesPerSecond, const QString &imageFormat);

private:
    QString m_inputFile;         ///< 输入视频文件路径
    QString m_outputDir;         ///< 输出图片保存目录
    int m_framesPerSecond;       ///< 每秒提取的帧数
    QString m_imageFormat;       ///< 输出图片格式
};

#endif // FRAMEEXTRACTOR_H
