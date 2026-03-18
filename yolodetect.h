#ifndef YOLODETECT_H
#define YOLODETECT_H

#include <QObject>
#include <QImage>
#include <vector>

/**
 * @file yolodetect.h
 * @brief YOLO目标检测类的头文件
 * @details 该文件定义了YOLO目标检测的核心类和检测结果结构体，用于加载ONNX模型并执行目标检测
 */

/**
 * @struct DetectionResult
 * @brief 单个目标检测结果结构体
 * @details 该结构体存储了一个目标的检测信息，包括类别、置信度和目标框坐标
 */
struct DetectionResult {
    int classId;        ///< 检测到的目标类别ID
    QString className;  ///< 检测到的目标类别名称
    float confidence;   ///< 检测置信度，取值范围[0,1]
    float x;            ///< 目标框左上角x坐标（归一化坐标，取值范围[0,1]）
    float y;            ///< 目标框左上角y坐标（归一化坐标，取值范围[0,1]）
    float width;        ///< 目标框宽度（归一化坐标，取值范围[0,1]）
    float height;       ///< 目标框高度（归一化坐标，取值范围[0,1]）
};

// 声明元类型，使DetectionResult和其向量可以在Qt的信号槽系统中使用
Q_DECLARE_METATYPE(DetectionResult)
Q_DECLARE_METATYPE(std::vector<DetectionResult>)

/**
 * @class YoloDetect
 * @brief YOLO目标检测类
 * @details 该类封装了YOLO模型的加载、图像预处理、推理和后处理功能，使用pImpl设计模式隐藏实现细节
 */
class YoloDetect : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief 构造函数
     * @param parent 父对象指针
     */
    explicit YoloDetect(QObject *parent = nullptr);
    
    /**
     * @brief 析构函数
     */
    ~YoloDetect();

    /**
     * @brief 加载ONNX模型
     * @param modelPath ONNX模型文件路径
     * @return 加载成功返回true，失败返回false
     */
    bool loadModel(const QString &modelPath);
    
    /**
     * @brief 对输入图像执行目标检测
     * @param image 输入图像
     * @return 检测结果向量，每个元素包含一个目标的检测信息
     */
    std::vector<DetectionResult> detect(const QImage &image);

private:
    /**
     * @struct YoloImpl
     * @brief 内部实现结构体
     * @details 使用pImpl设计模式，隐藏YOLO检测的具体实现细节，提高封装性和可维护性
     */
    struct YoloImpl;
    
    YoloImpl *impl; ///< 指向内部实现的指针
};

#endif // YOLODETECT_H
