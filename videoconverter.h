#ifndef VIDEOCONVERTER_H
#define VIDEOCONVERTER_H

#include <QObject>
#include <QString>
#include <QThread>

class VideoConverterWorker;

/**
 * @file videoconverter.h
 * @brief 视频转码模块的头文件
 * @details 该文件定义了视频转码的控制器类和工作线程类，用于将视频转换为不同的格式和容器
 */

/**
 * @class VideoConverter
 * @brief 视频转码控制器类
 * @details 该类是视频转码功能的入口点，负责创建后台工作线程、管理FFmpeg的初始化和清理，以及提供与UI交互的接口
 */
class VideoConverter : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief 构造函数
     * @param parent 父对象指针
     */
    explicit VideoConverter(QObject *parent = nullptr);
    
    /**
     * @brief 析构函数
     */
    ~VideoConverter();

public slots:
    /**
     * @brief 开始视频转码
     * @param inputFile 输入视频文件路径
     * @param outputFile 输出视频文件路径
     * @param format 输出视频格式（如mp4、avi等）
     */
    void convertVideo(const QString &inputFile, const QString &outputFile, const QString &format);

signals:
    /**
     * @brief 转码进度更新信号
     * @param progress 当前进度，取值范围0-100
     */
    void progressChanged(int progress);
    
    /**
     * @brief 转码操作完成信号
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
    
    /**
     * @brief 内部转码逻辑实现
     * @param inputFile 输入视频文件路径
     * @param outputFile 输出视频文件路径
     * @param format 输出视频格式
     * @return 转码成功返回true，失败返回false
     */
    bool convertVideoInternal(const QString &inputFile, const QString &outputFile, const QString &format);
    
    /**
     * @brief 根据格式获取文件扩展名
     * @param format 视频格式名称
     * @return 对应的文件扩展名
     */
    QString getFormatExtension(const QString &format) const;
};

/**
 * @class VideoConverterWorker
 * @brief 视频转码工作线程类
 * @details 该类继承自QThread，在后台线程中执行实际的视频转码工作，使用FFmpeg进行视频处理
 */
class VideoConverterWorker : public QThread
{
    Q_OBJECT

public:
    /**
     * @brief 构造函数
     * @param inputFile 输入视频文件路径
     * @param outputFile 输出视频文件路径
     * @param format 输出视频格式
     * @param parent 父对象指针
     */
    VideoConverterWorker(const QString &inputFile, const QString &outputFile, const QString &format, QObject *parent = nullptr);
    
    /**
     * @brief 线程入口函数
     */
    void run() override;

signals:
    /**
     * @brief 转码进度更新信号
     * @param progress 当前进度，取值范围0-100
     */
    void progressChanged(int progress);
    
    /**
     * @brief 转码操作完成信号
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
     * @brief 转码主流程实现
     * @param inputFile 输入视频文件路径
     * @param outputFile 输出视频文件路径
     * @param format 输出视频格式
     * @return 转码成功返回true，失败返回false
     */
    bool convertVideo(const QString &inputFile, const QString &outputFile, const QString &format);
    
    /**
     * @brief 根据格式获取文件扩展名
     * @param format 视频格式名称
     * @return 对应的文件扩展名
     */
    QString getFormatExtension(const QString &format) const;

private:
    QString m_inputFile;         ///< 输入视频文件路径
    QString m_outputFile;        ///< 输出视频文件路径
    QString m_format;            ///< 输出视频格式
};

#endif // VIDEOCONVERTER_H
