#include "yolodetect.h"
#include <QDebug>
#include <QFile>
#include <QPainter>
#include <algorithm>
#include <cmath>

// ONNX Runtime headers - 用于加载和运行ONNX模型
#include <onnxruntime_cxx_api.h>

/**
 * @brief YoloDetect类的内部实现结构体
 * @details 使用pImpl设计模式，隐藏YOLO检测的具体实现细节，提高封装性和可维护性
 */
struct YoloDetect::YoloImpl {
    Ort::Env env;                   // ONNX Runtime环境对象，用于初始化ONNX Runtime
    Ort::SessionOptions session_options; // 会话选项，用于配置模型运行参数（如线程数、优化级别等）
    Ort::Session* session;         // ONNX模型会话指针，用于加载和运行模型
    Ort::AllocatorWithDefaultOptions allocator; // 默认内存分配器，用于内存管理

    const int input_width = 640;    // 模型输入宽度（YOLOv5/YOLOv8默认输入尺寸）
    const int input_height = 640;   // 模型输入高度（YOLOv5/YOLOv8默认输入尺寸）
    const float score_threshold = 0.80f; // 检测置信度阈值，低于此值的检测结果将被过滤
    const float nms_threshold = 0.45f;   // 非极大值抑制阈值，用于去除重叠的检测框

    // COCO数据集的80个类别名称
    std::vector<QString> class_names = {
        "person", "bicycle", "car", "motorcycle", "airplane", "bus", "train", "truck", "boat", "traffic light",
        "fire hydrant", "stop sign", "parking meter", "bench", "bird", "cat", "dog", "horse", "sheep", "cow",
        "elephant", "bear", "zebra", "giraffe", "backpack", "umbrella", "handbag", "tie", "suitcase", "frisbee",
        "skis", "snowboard", "sports ball", "kite", "baseball bat", "baseball glove", "skateboard", "surfboard",
        "tennis racket", "bottle", "wine glass", "cup", "fork", "knife", "spoon", "bowl", "banana", "apple",
        "sandwich", "orange", "broccoli", "carrot", "hot dog", "pizza", "donut", "cake", "chair", "couch",
        "potted plant", "bed", "dining table", "toilet", "tv", "laptop", "mouse", "remote", "keyboard", "cell phone",
        "microwave", "oven", "toaster", "sink", "refrigerator", "book", "clock", "vase", "scissors", "teddy bear",
        "hair drier", "toothbrush"
    };

    /**
     * @brief YoloImpl构造函数
     * @details 初始化ONNX Runtime环境和会话选项
     */
    YoloImpl()
        : env(ORT_LOGGING_LEVEL_WARNING) // 创建ONNX环境，日志级别为警告，减少不必要的日志输出
        , session_options()              // 初始化会话选项
        , session(nullptr)               // 初始化会话指针为空
    {
        // 设置ONNX运行时参数
        session_options.SetInterOpNumThreads(1); // 设置操作间线程数为1
        session_options.SetIntraOpNumThreads(4); // 设置操作内线程数为4，根据CPU核心数调整
        session_options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL); // 启用所有图优化，提高模型推理速度
        // 此处配置为仅使用CPU执行，不使用GPU加速
    }

    /**
     * @brief YoloImpl析构函数
     * @details 释放ONNX会话资源，避免内存泄漏
     */
    ~YoloImpl()
    {
        delete session; // 释放会话资源
        session = nullptr;
    }
};

/**
 * @brief YoloDetect构造函数
 * @details 创建YoloImpl实例，实现pImpl设计模式
 * @param parent 父对象指针
 */
YoloDetect::YoloDetect(QObject *parent)
    : QObject(parent)
{
    impl = new YoloImpl(); // 创建YoloImpl实例，将实现细节隐藏
}

/**
 * @brief YoloDetect析构函数
 * @details 释放YoloImpl实例，避免内存泄漏
 */
YoloDetect::~YoloDetect()
{
    delete impl; // 释放YoloImpl实例
    impl = nullptr;
}

/**
 * @brief 加载YOLO ONNX模型
 * @param modelPath ONNX模型文件路径
 * @return 加载成功返回true，失败返回false
 */
bool YoloDetect::loadModel(const QString &modelPath)
{
    // 检查模型文件是否存在
    if (!QFile::exists(modelPath)) {
        return false;
    }

    try {
        // 创建ONNX会话，加载模型
        impl->session = new Ort::Session(impl->env, modelPath.toStdWString().c_str(), impl->session_options);
        return true;
    } catch (const Ort::Exception &) {
        // 加载失败，返回false
        return false;
    }
}

/**
 * @brief Sigmoid激活函数
 * @param x 输入值
 * @return 经过sigmoid激活后的输出值，范围[0, 1]
 * @details 用于将模型输出转换为概率值
 */
static float sigmoid(float x)
{
    return 1.0f / (1.0f + std::exp(-x));
}

/**
 * @brief 对输入图像进行目标检测
 * @param image 输入的QImage图像
 * @return 检测结果列表，包含每个检测到的目标的类别、置信度和坐标
 */
std::vector<DetectionResult> YoloDetect::detect(const QImage &image)
{
    std::vector<DetectionResult> results; // 存储检测结果的向量
    static bool logged = false; // 用于控制只输出一次调试信息
    
    // 检查模型是否已加载
    if (!impl->session) {
        return results; // 如果模型未加载，返回空结果
    }

    try {
        // 1. 图像预处理阶段
        // 将输入图像按比例缩放到模型输入大小，保持宽高比
        QImage resizedImage = image.scaled(
            impl->input_width, impl->input_height, Qt::KeepAspectRatio, Qt::SmoothTransformation);

        // 创建一个黑色背景的图像，尺寸与模型输入一致
        QImage paddedImage(impl->input_width, impl->input_height, QImage::Format_RGB888);
        paddedImage.fill(Qt::black);

        // 计算居中放置的偏移量，确保缩放后的图像居中显示
        int xOffset = (impl->input_width - resizedImage.width()) / 2;
        int yOffset = (impl->input_height - resizedImage.height()) / 2;
        
        // 将缩放后的图像绘制到黑色背景上，实现居中填充
        QPainter painter(&paddedImage);
        painter.drawImage(xOffset, yOffset, resizedImage);
        painter.end();

        // 2. 将图像数据转换为ONNX模型所需的张量格式
        const int num_channels = 3; // RGB通道数（YOLO模型使用RGB输入）
        const int input_size = impl->input_width * impl->input_height * num_channels; // 输入张量总大小
        std::vector<float> input_tensor_values(input_size); // 存储输入张量数据的向量

        // 遍历图像的每个像素，将RGB值转换为模型需要的格式
        for (int c = 0; c < num_channels; ++c) { // 遍历RGB通道
            for (int y = 0; y < impl->input_height; ++y) { // 遍历高度
                for (int x = 0; x < impl->input_width; ++x) { // 遍历宽度
                    QRgb pixel = paddedImage.pixel(x, y); // 获取像素值（ARGB格式）
                    int index = c * impl->input_width * impl->input_height + y * impl->input_width + x; // 计算张量中的索引
                    
                    // 将RGB值归一化到[0, 1]范围，并按BGR或RGB顺序存储（根据模型要求）
                    if (c == 0) {
                        input_tensor_values[index] = static_cast<float>(qRed(pixel)) / 255.0f; // R通道
                    } else if (c == 1) {
                        input_tensor_values[index] = static_cast<float>(qGreen(pixel)) / 255.0f; // G通道
                    } else {
                        input_tensor_values[index] = static_cast<float>(qBlue(pixel)) / 255.0f; // B通道
                    }
                }
            }
        }

        // 3. 创建ONNX模型输入张量
        // 创建CPU内存信息
        Ort::MemoryInfo memory_info = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        // 定义输入张量的形状：[批次大小, 通道数, 高度, 宽度]
        std::vector<int64_t> input_shape = {
            1, 3, static_cast<int64_t>(impl->input_height), static_cast<int64_t>(impl->input_width)
        };

        // 创建输入张量，将图像数据转换为ONNX Runtime可处理的格式
        Ort::Value input_tensor = Ort::Value::CreateTensor<float>(
            memory_info, input_tensor_values.data(), input_size, input_shape.data(), input_shape.size());

        // 4. 获取模型的输入输出名称
        std::vector<const char*> input_names;
        std::vector<const char*> output_names;

        // 获取输入名称
        Ort::AllocatedStringPtr input_name_ptr = impl->session->GetInputNameAllocated(0, impl->allocator);
        input_names.push_back(input_name_ptr.get());
        
        // 获取输出名称
        Ort::AllocatedStringPtr output_name_ptr = impl->session->GetOutputNameAllocated(0, impl->allocator);
        output_names.push_back(output_name_ptr.get());

        // 5. 运行模型推理
        Ort::RunOptions run_options; // 运行选项，可用于设置终止等
        std::vector<Ort::Value> output_tensors = impl->session->Run(
            run_options, input_names.data(), &input_tensor, 1, output_names.data(), 1);

        if (output_tensors.empty()) {
            return results; // 如果推理失败，返回空结果
        }

        // 6. 解析模型输出
        Ort::Value &output_tensor = output_tensors[0];
        if (!output_tensor.IsTensor()) {
            return results; // 如果输出不是张量，返回空结果
        }

        // 获取输出张量的原始数据指针
        float *output_data = output_tensor.GetTensorMutableData<float>();
        if (!output_data) {
            return results; // 如果无法获取数据，返回空结果
        }

        // 获取输出张量的形状
        auto output_shape = output_tensor.GetTensorTypeAndShapeInfo().GetShape();
        
        // 输出调试信息（仅第一次运行时）
        if (!logged) {
            QString shapeText;
            for (size_t i = 0; i < output_shape.size(); ++i) {
                if (i > 0) {
                    shapeText += " x ";
                }
                shapeText += QString::number(output_shape[i]);
            }
            qDebug() << "YOLO output shape:" << shapeText;
        }

        // 检查输出形状是否符合预期（3维）
        if (output_shape.size() != 3) {
            return results;
        }

        // 解析检测框数量和每个检测框的大小
        int num_detections = 0; // 检测框总数
        int detection_size = 0; // 每个检测框的数据大小
        bool transposed = false; // 输出是否需要转置

        // YOLO模型输出可能有两种格式，需要根据形状判断
        if (output_shape[1] == 84) {
            // 格式1: [1, 84, N] 其中84是检测框数据大小（4个坐标+1个目标性分数+80个类别概率）
            detection_size = static_cast<int>(output_shape[1]);
            num_detections = static_cast<int>(output_shape[2]);
            transposed = true; // 需要转置处理
        } else {
            // 格式2: [1, N, 84] 其中N是检测框数量
            detection_size = static_cast<int>(output_shape[2]);
            num_detections = static_cast<int>(output_shape[1]);
            transposed = false;
        }

        // 计算缩放比例，将检测结果映射回原始图像尺寸
        float scale_x = static_cast<float>(image.width()) / static_cast<float>(resizedImage.width());
        float scale_y = static_cast<float>(image.height()) / static_cast<float>(resizedImage.height());

        // 定义候选框结构体，用于存储检测结果和置信度
        struct Candidate {
            DetectionResult det; // 检测结果
            float score;        // 置信度分数
        };

        // 存储符合条件的候选框
        std::vector<Candidate> candidates;
        candidates.reserve(num_detections); // 预分配内存，提高效率

        // 检查模型输出是否包含目标性(objectness)评分
        const bool has_objectness = (detection_size > 84);
        // 确定类别概率开始的位置
        const int class_start = has_objectness ? 5 : 4;

        // 用于调试的变量，记录最大置信度
        float max_obj = 0.0f;
        float max_cls = 0.0f;

        // 遍历所有检测框
        for (int i = 0; i < num_detections; ++i) {
            float *detection = output_data + i * detection_size; // 当前检测框的数据指针

            // 定义一个读取函数，根据是否需要转置来正确读取数据
            auto read_value = [&](int row) -> float {
                if (transposed) {
                    // 转置情况下，数据按不同顺序存储
                    return output_data[row * num_detections + i];
                }
                return detection[row]; // 非转置情况下直接读取
            };

            // 计算目标性(objectness)置信度
            float obj_conf = 1.0f; // 默认值为1.0（当没有目标性评分时）
            if (has_objectness) {
                float obj_raw = read_value(4); // 读取原始目标性分数
                // 如果分数大于1.0，需要应用sigmoid激活函数（某些模型可能未应用激活函数）
                obj_conf = (obj_raw > 1.0f) ? sigmoid(obj_raw) : obj_raw;
            }
            if (obj_conf > max_obj) {
                max_obj = obj_conf; // 更新最大目标性置信度（用于调试）
            }

            // 查找最大的类别置信度和对应的类别ID
            float max_conf = 0.0f; // 最大类别置信度
            int class_id = 0; // 对应的类别ID
            for (int j = class_start; j < detection_size; ++j) {
                float cls_raw = read_value(j); // 读取原始类别分数
                // 如果分数大于1.0，需要应用sigmoid激活函数（某些模型可能未应用激活函数）
                float cls_conf = (cls_raw > 1.0f) ? sigmoid(cls_raw) : cls_raw;
                
                if (cls_conf > max_cls) {
                    max_cls = cls_conf; // 更新最大类别置信度（用于调试）
                }
                
                if (cls_conf > max_conf) {
                    max_conf = cls_conf;
                    class_id = j - class_start; // 计算类别ID（减去目标性分数和坐标的位置）
                }
            }

            // 计算最终置信度分数（目标性 × 类别概率）
            float score = obj_conf * max_conf;
            
            // 如果分数低于阈值，跳过当前检测框
            if (score < impl->score_threshold) {
                continue;
            }

            // 读取边界框坐标（中心坐标和宽高）
            float x_center = read_value(0); // 边界框中心x坐标
            float y_center = read_value(1); // 边界框中心y坐标
            float width = read_value(2);    // 边界框宽度
            float height = read_value(3);   // 边界框高度

            // 检查坐标是否已经归一化，如果是则需要缩放
            if (x_center <= 1.5f && y_center <= 1.5f && width <= 1.5f && height <= 1.5f) {
                // 将归一化坐标转换为模型输入尺寸的坐标
                x_center *= impl->input_width;
                y_center *= impl->input_height;
                width *= impl->input_width;
                height *= impl->input_height;
            }

            // 将边界框坐标从模型输入尺寸映射回原始图像尺寸
            float x = (x_center - xOffset) * scale_x - width / 2 * scale_x; // 左上角x坐标
            float y = (y_center - yOffset) * scale_y - height / 2 * scale_y; // 左上角y坐标
            width *= scale_x; // 宽度缩放
            height *= scale_y; // 高度缩放

            // 确保边界框不会超出图像范围
            x = qMax(0.0f, x);
            y = qMax(0.0f, y);
            width = qMin(static_cast<float>(image.width()) - x, width);
            height = qMin(static_cast<float>(image.height()) - y, height);

            // 创建检测结果对象
            DetectionResult result;
            result.classId = class_id; // 类别ID
            
            // 获取类别名称
            if (class_id >= 0 && class_id < static_cast<int>(impl->class_names.size())) {
                result.className = impl->class_names[class_id];
            } else {
                result.className = "unknown"; // 未知类别
            }
            
            result.confidence = score; // 置信度分数
            result.x = x;             // 边界框左上角x坐标
            result.y = y;             // 边界框左上角y坐标
            result.width = width;     // 边界框宽度
            result.height = height;   // 边界框高度

            // 将检测结果添加到候选框列表
            candidates.push_back({result, score});
        }
        
        // 输出调试信息（仅第一次运行时）
        if (!logged) {
            qDebug() << "YOLO max obj/conf:" << max_obj << "max cls/conf:" << max_cls
                     << "has_objectness:" << has_objectness;
            logged = true;
        }

        // 7. 应用非极大值抑制(NMS)，去除重叠的检测框
        // 首先按置信度降序排序候选框
        std::sort(candidates.begin(), candidates.end(),
                  [](const Candidate &a, const Candidate &b) { return a.score > b.score; });

        // 计算两个边界框的交并比(IoU)的函数
        auto iou = [](const DetectionResult &a, const DetectionResult &b) -> float {
            // 计算交集的坐标
            float x1 = std::max(a.x, b.x);
            float y1 = std::max(a.y, b.y);
            float x2 = std::min(a.x + a.width, b.x + b.width);
            float y2 = std::min(a.y + a.height, b.y + b.height);
            
            // 计算交集的宽高
            float w = std::max(0.0f, x2 - x1);
            float h = std::max(0.0f, y2 - y1);
            
            // 计算交集面积和并集面积
            float inter = w * h;
            float union_area = a.width * a.height + b.width * b.height - inter;
            
            // 返回IoU值，即交集面积与并集面积的比值
            return union_area > 0.0f ? (inter / union_area) : 0.0f;
        };

        // 执行非极大值抑制
        for (const auto &cand : candidates) {
            bool keep = true; // 标记当前候选框是否需要保留
            
            // 检查当前候选框与已保留的检测框是否有重叠
            for (const auto &kept : results) {
                if (iou(cand.det, kept) > impl->nms_threshold) {
                    keep = false; // 如果重叠度超过阈值，不保留当前候选框
                    break;
                }
            }
            
            if (keep) {
                results.push_back(cand.det); // 保留当前候选框
            }
        }
    } catch (const Ort::Exception &) {
        return results; // 捕获ONNX Runtime异常，返回空结果
    } catch (...) {
        return results; // 捕获其他异常，返回空结果
    }

    return results; // 返回最终的检测结果
}
