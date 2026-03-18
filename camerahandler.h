#ifndef CAMERAHANDLER_H
#define CAMERAHANDLER_H

#include <QObject>
#include <QString>
#include <QThread>
#include <QMutex>
#include <atomic>
#include "yolodetect.h"
#include <QImage>

class CameraHandlerWorker;

/**
 * @file camerahandler.h
 * @brief 摄像头采集与实时检测模块
 * @details 该文件定义了CameraHandler类（控制器）和CameraHandlerWorker类（工作线程），
 * 负责摄像头的打开、视频帧采集、格式转换以及与YOLO目标检测模型的集成。
 */

/**
 * @class CameraHandler
 * @brief 摄像头采集控制器
 * @details 负责管理摄像头的启动、停止，YOLO模型的加载与检测开关，
 * 以及在UI线程和工作线程之间转发信号和结果。
 */
class CameraHandler : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief 构造函数
     * @param parent 父对象指针
     */
    explicit CameraHandler(QObject *parent = nullptr);
    /**
     * @brief 析构函数
     */
    ~CameraHandler();

public slots:
    /**
     * @brief 启动摄像头采集
     * @param cameraIndex 摄像头索引，默认为0
     * @param width 摄像头采集宽度，默认为640
     * @param height 摄像头采集高度，默认为480
     */
    void startCamera(int cameraIndex = 0, int width = 640, int height = 480);
    
    /**
     * @brief 停止摄像头采集
     */
    void stopCamera();
    
    /**
     * @brief 加载YOLO模型
     * @param modelPath YOLO模型文件路径（ONNX格式）
     * @return 加载成功返回true，失败返回false
     */
    bool loadYoloModel(const QString &modelPath);
    
    /**
     * @brief 启用/禁用YOLO目标检测
     * @param enable true为启用检测，false为禁用检测
     */
    void enableYoloDetection(bool enable);

    /**
     * @brief 查询摄像头是否正在运行
     * @return 正在运行返回true，否则返回false
     */
    bool isCameraRunning() const;
    
    /**
     * @brief 查询YOLO检测是否已启用
     * @return 已启用返回true，否则返回false
     */
    bool isYoloEnabled() const;

    /**
     * @brief 获取可用摄像头设备列表
     * @return 摄像头设备名称列表
     */
    QStringList getAvailableCameras() const;

signals:
    /**
     * @brief 采集到原始视频帧的信号
     * @param frame 转换为QImage格式的视频帧
     */
    void cameraFrameReady(const QImage &frame);
    
    /**
     * @brief 目标检测结果的信号
     * @param frame 检测的视频帧
     * @param results YOLO检测结果列表
     */
    void detectionResultReady(const QImage &frame, const std::vector<DetectionResult> &results);
    
    /**
     * @brief 操作完成的信号
     * @param success 操作是否成功
     * @param message 操作结果描述
     */
    void operationFinished(bool success, const QString &message);
    
    /**
     * @brief 错误发生的信号
     * @param errorMessage 错误描述信息
     */
    void errorOccurred(const QString &errorMessage);

private:
    /**
     * @brief 初始化FFmpeg库
     * @return 初始化成功返回true，失败返回false
     */
    bool initializeFFmpeg();
    
    /**
     * @brief 清理FFmpeg资源
     */
    void cleanupFFmpeg();

private:
    bool m_cameraRunning;        ///< 摄像头运行状态标志
    bool m_yoloEnabled;          ///< YOLO检测启用状态标志
    QString m_yoloModelPath;     ///< 保存YOLO模型路径，用于工作线程创建时加载
    QThread *m_cameraThread;     ///< 摄像头采集工作线程
    CameraHandlerWorker *m_cameraWorker; ///< 摄像头采集工作对象
    YoloDetect *m_yoloDetect;    ///< YOLO目标检测实例
};

/**
 * @class CameraHandlerWorker
 * @brief 摄像头采集工作线程
 * @details 在独立线程中执行实际的摄像头打开、视频帧采集、解码、格式转换以及
 * YOLO目标检测等CPU密集型操作，避免阻塞UI线程。
 */
class CameraHandlerWorker : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief 构造函数
     * @param cameraIndex 摄像头索引
     * @param width 采集宽度
     * @param height 采集高度
     * @param parent 父对象指针
     */
    CameraHandlerWorker(int cameraIndex, int width, int height, QObject *parent = nullptr);
    
    /**
     * @brief 析构函数
     */
    ~CameraHandlerWorker();

    /**
     * @brief 线程入口函数
     * @details 在独立线程中启动摄像头采集主循环
     */
    void process();
    
    /**
     * @brief 请求停止摄像头采集
     */
    void stop();
    
    /**
     * @brief 启用/禁用YOLO目标检测
     * @param enable true为启用检测，false为禁用检测
     */
    void enableYoloDetection(bool enable);
    
    /**
     * @brief 加载YOLO模型
     * @param modelPath YOLO模型文件路径（ONNX格式）
     * @return 加载成功返回true，失败返回false
     */
    bool loadYoloModel(const QString &modelPath);

    /**
     * @brief 查询是否已请求停止采集
     * @return 已请求停止返回true，否则返回false
     */
    bool isStopped() const;
    
    /**
     * @brief 查询YOLO检测是否已启用
     * @return 已启用返回true，否则返回false
     */
    bool isYoloEnabled() const;

signals:
    /**
     * @brief 采集到原始视频帧的信号
     * @param frame 转换为QImage格式的视频帧
     */
    void cameraFrameReady(const QImage &frame);
    
    /**
     * @brief 目标检测结果的信号
     * @param frame 检测的视频帧
     * @param results YOLO检测结果列表
     */
    void detectionResultReady(const QImage &frame, const std::vector<DetectionResult> &results);
    
    /**
     * @brief 线程结束的信号
     * @param success 操作是否成功
     * @param message 结束信息
     */
    void finished(bool success, const QString &message);
    
    /**
     * @brief 错误发生的信号
     * @param errorMessage 错误描述信息
     */
    void errorOccurred(const QString &errorMessage);
    
    /**
     * @brief 操作完成的信号
     * @param success 操作是否成功
     * @param message 操作结果描述
     */
    void operationFinished(bool success, const QString &message);

private:
    /**
     * @brief 摄像头采集主循环
     * @details 执行打开设备、读取数据包、解码、格式转换、发送视频帧以及
     * 执行YOLO目标检测等完整流程。
     * @param cameraIndex 摄像头索引
     * @param width 采集宽度
     * @param height 采集高度
     * @return 采集成功返回true，失败返回false
     */
    bool cameraStream(int cameraIndex, int width, int height);

private:
    int m_cameraIndex;           ///< 摄像头索引
    int m_width;                 ///< 采集宽度
    int m_height;                ///< 采集高度
    volatile bool m_stopRequested; ///< 停止请求标志
    bool m_yoloEnabled;          ///< YOLO检测启用状态
    int m_frameIndex;            ///< 帧计数器
    std::atomic_bool m_detectionBusy; ///< 检测任务忙碌状态（防止任务堆积）
    QMutex m_detectionMutex;     ///< 保护模型推理的互斥锁
    YoloDetect *m_yoloDetect;    ///< YOLO目标检测实例
};

#endif // CAMERAHANDLER_H
