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

// 海康威视MVS SDK头文件
#include "MvCameraControl.h"

/**
 * @file camerahandler_hikvision.cpp
 * @brief 海康威视摄像头采集与实时检测模块实现
 * @details 该文件实现了使用海康威视MVS SDK的CameraHandler和CameraHandlerWorker类，
 * 包括MVS SDK初始化、摄像头设备管理、视频帧采集、格式转换、
 * 以及与YOLO目标检测模型的集成和异步处理。
 */

/**
 * @brief CameraHandler构造函数
 * @param parent 父对象指针
 * @details 初始化摄像头处理模块，包括MVS SDK的初始化和YOLO检测实例的创建。
 */
CameraHandler::CameraHandler(QObject *parent)
    : QObject(parent)
    , m_cameraRunning(false)
    , m_yoloEnabled(false)
    , m_cameraThread(nullptr)
    , m_cameraWorker(nullptr)
    , m_yoloDetect(nullptr)
{
    // MVS SDK初始化在工作线程中进行
    m_yoloDetect = new YoloDetect(this);
}

/**
 * @brief CameraHandler析构函数
 * @details 清理资源，包括停止摄像头采集和删除YOLO检测实例。
 */
CameraHandler::~CameraHandler()
{
    stopCamera();
    delete m_yoloDetect;
    m_yoloDetect = nullptr;
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
 * @details 使用MVS SDK枚举系统中的海康威视摄像头设备。
 */
QStringList CameraHandler::getAvailableCameras() const
{
    QStringList cameras;
    
    // 初始化MVS SDK
    if (MV_CC_Initialize() != MV_OK) {
        cameras << "MVS SDK initialization failed";
        return cameras;
    }
    
    // 枚举设备
    MV_CC_DEVICE_INFO_LIST deviceList;
    if (MV_CC_EnumDevices(MV_GIGE_DEVICE | MV_USB_DEVICE, &deviceList) == MV_OK) {
        for (unsigned int i = 0; i < deviceList.nDeviceNum; i++) {
            // 获取设备名称
            char deviceName[256] = {0};
            if (deviceList.pDeviceInfo[i]->nDeviceType == MV_GIGE_DEVICE) {
                strncpy(deviceName, deviceList.pDeviceInfo[i]->SpecialInfo.stGigEInfo.chModelName, sizeof(deviceName) - 1);
            } else if (deviceList.pDeviceInfo[i]->nDeviceType == MV_USB_DEVICE) {
                strncpy(deviceName, deviceList.pDeviceInfo[i]->SpecialInfo.stUsb3VInfo.chModelName, sizeof(deviceName) - 1);
            }
            cameras << QString(deviceName);
        }
    } else {
        cameras << "No Hikvision cameras found";
    }
    
    // 释放MVS SDK资源
    MV_CC_Finalize();
    
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
    , m_hCamera(nullptr)
    , m_isCameraOpen(false)
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
 * @brief 将MVS SDK的帧数据转换为QImage
 * @param frameInfo 帧信息
 * @param pData 帧数据
 * @param image 输出的QImage
 * @return 转换成功返回true，失败返回false
 */
bool CameraHandlerWorker::convertMvsFrameToQImage(const MV_FRAME_OUT_INFO_EX& frameInfo, unsigned char* pData, QImage& image)
{
    int width = frameInfo.nWidth;
    int height = frameInfo.nHeight;
    
    // 根据像素格式进行转换
    switch (frameInfo.enPixelType) {
    case PixelType_Gvsp_BayerRG8:
    case PixelType_Gvsp_BayerBG8:
    case PixelType_Gvsp_BayerGR8:
    case PixelType_Gvsp_BayerGB8: {
        // 创建RGB图像
        image = QImage(width, height, QImage::Format_RGB888);
        
        // 简化的Bayer转RGB（实际应用中应使用更复杂的去马赛克算法）
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                int index = y * width + x;
                unsigned char val = pData[index];
                image.setPixel(x, y, qRgb(val, val, val));
            }
        }
        break;
    }
    case PixelType_Gvsp_RGB8_Packed:
    case PixelType_Gvsp_BGR8_Packed:
    {
        // 直接使用RGB数据
        image = QImage(pData, width, height, QImage::Format_RGB888);
        if (frameInfo.enPixelType == PixelType_Gvsp_BGR8_Packed) {
            // 如果是BGR格式，转换为RGB
            image = image.rgbSwapped();
        }
        break;
    }
    case PixelType_Gvsp_Mono8:
    {
        // 灰度图像转换为RGB
        image = QImage(width, height, QImage::Format_RGB888);
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                int index = y * width + x;
                unsigned char val = pData[index];
                image.setPixel(x, y, qRgb(val, val, val));
            }
        }
        break;
    }
    default:
        // 不支持的像素格式
        emit errorOccurred("Unsupported pixel format.");
        return false;
    }
    
    return true;
}

/**
 * @brief 摄像头采集主循环
 * @param cameraIndex 摄像头索引
 * @param width 采集宽度
 * @param height 采集高度
 * @return 采集成功返回true，失败返回false
 * @details 执行海康威视摄像头的打开、视频流采集、格式转换、帧发送以及
 * YOLO目标检测等完整流程。这是摄像头采集功能的核心实现。
 */
bool CameraHandlerWorker::cameraStream(int cameraIndex, int width, int height)
{
    // 1. 初始化MVS SDK
    if (MV_CC_Initialize() != MV_OK) {
        emit errorOccurred("Failed to initialize MVS SDK.");
        return false;
    }

    // 2. 枚举设备
    MV_CC_DEVICE_INFO_LIST deviceList;
    if (MV_CC_EnumDevices(MV_GIGE_DEVICE | MV_USB_DEVICE, &deviceList) != MV_OK) {
        emit errorOccurred("Failed to enum devices.");
        MV_CC_Finalize();
        return false;
    }

    if (deviceList.nDeviceNum <= 0) {
        emit errorOccurred("No camera found.");
        MV_CC_Finalize();
        return false;
    }

    // 3. 选择指定索引的相机
    if (cameraIndex >= deviceList.nDeviceNum) {
        emit errorOccurred("Camera index out of range.");
        MV_CC_Finalize();
        return false;
    }

    // 4. 创建相机句柄
    if (MV_CC_CreateHandle(&m_hCamera, deviceList.pDeviceInfo[cameraIndex]) != MV_OK) {
        emit errorOccurred("Failed to create camera handle.");
        MV_CC_Finalize();
        return false;
    }

    // 5. 打开相机
    if (MV_CC_OpenDevice(m_hCamera) != MV_OK) {
        emit errorOccurred("Failed to open camera.");
        MV_CC_DestroyHandle(m_hCamera);
        MV_CC_Finalize();
        return false;
    }
    m_isCameraOpen = true;

    // 6. 设置相机参数
    // 设置宽度和高度
    MV_CC_SetIntValue(m_hCamera, "Width", width);
    MV_CC_SetIntValue(m_hCamera, "Height", height);
    // 设置曝光时间（可根据实际情况调整）
    MV_CC_SetFloatValue(m_hCamera, "ExposureTime", 10000.0);

    // 7. 开始采集
    if (MV_CC_StartGrabbing(m_hCamera) != MV_OK) {
        emit errorOccurred("Failed to start grabbing.");
        MV_CC_CloseDevice(m_hCamera);
        MV_CC_DestroyHandle(m_hCamera);
        MV_CC_Finalize();
        return false;
    }

    // 8. 主循环：获取图像
    MV_FRAME_OUT_INFO_EX frameInfo = {0};
    unsigned char* pData = nullptr;
    int nDataSize = 0;

    // 获取图像缓存大小
    MV_CC_GetIntValue(m_hCamera, "PayloadSize", &nDataSize);
    pData = new unsigned char[nDataSize];
    if (!pData) {
        emit errorOccurred("Failed to allocate memory for image data.");
        goto cleanup;
    }

    while (!m_stopRequested) {
        // 获取一帧图像
        if (MV_CC_GetOneFrameTimeout(m_hCamera, pData, nDataSize, &frameInfo, 1000) == MV_OK) {
            // 9. 转换图像格式为RGB
            QImage image;
            if (convertMvsFrameToQImage(frameInfo, pData, image)) {
                // 10. 发送帧到UI
                QImage frameImage = image.copy();
                emit cameraFrameReady(frameImage);

                // 11. 执行YOLO检测（与现有逻辑相同）
                if (m_yoloEnabled && m_yoloDetect) {
                    if (!m_detectionBusy.exchange(true)) {
                        QPointer<CameraHandlerWorker> self(this);
                        QImage detectImage = frameImage.copy();

                        QtConcurrent::run([self, detectImage]() {
                            if (!self) return;

                            std::vector<DetectionResult> results;
                            {
                                QMutexLocker locker(&self->m_detectionMutex);
                                results = self->m_yoloDetect->detect(detectImage);
                            }

                            if (self) {
                                emit self->detectionResultReady(detectImage, results);
                                self->m_detectionBusy = false;
                            }
                        });
                    }
                }

                ++m_frameIndex;
            }
        } else {
            QThread::msleep(10);  // 短暂休眠后重试
        }
    }

    // 释放图像数据内存
    if (pData) {
        delete[] pData;
        pData = nullptr;
    }

    // 12. 停止采集
    MV_CC_StopGrabbing(m_hCamera);

cleanup:
    // 13. 关闭相机
    if (m_isCameraOpen) {
        MV_CC_CloseDevice(m_hCamera);
        m_isCameraOpen = false;
    }

    // 14. 销毁句柄
    MV_CC_DestroyHandle(m_hCamera);
    m_hCamera = nullptr;

    // 15. 释放SDK资源
    MV_CC_Finalize();

    return true;
}
