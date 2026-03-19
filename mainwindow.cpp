/**
 * @file mainwindow.cpp
 * @brief 主窗口实现文件,包含视频转码、视频抽帧和摄像头处理的功能实现,123
 */
/**
你好
*/
#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "camerahandler.h"    // 摄像头处理类1
#include "frameextractor.h"   // 视频抽帧类
#include "videoconverter.h"   // 视频转码类
#include <QFileDialog>         // 文件对话框
#include <QFileInfo>           // 文件信息
#include <QMessageBox>         // 消息框
#include <QPainter>            // 绘图工具
#include <QSizePolicy>         // 尺寸策略

/**
 * @brief MainWindow构造函数
 * @param parent 父窗口指针
 */
MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , m_videoConverter(new VideoConverter(this))  // 创建视频转码对象
    , m_frameExtractor(new FrameExtractor(this))   // 创建视频抽帧对象
    , m_cameraHandler(new CameraHandler(this))     // 创建摄像头处理对象
{
    ui->setupUi(this);  // 初始化UI
    setupUI();          // 设置UI样式和属性

    // 加载YOLO模型
    const QString modelPath = "yolo11n.onnx";
    if (m_cameraHandler->loadYoloModel(modelPath)) {
        m_cameraHandler->enableYoloDetection(true);  // 启用YOLO检测
        ui->logTextEdit->append("YOLO model loaded and enabled.");
    } else {
        ui->logTextEdit->append("Failed to load YOLO model. Check yolo11n.onnx.");
    }

    setupConnections();  // 设置信号槽连接
}

/**
 * @brief MainWindow析构函数
 */
MainWindow::~MainWindow()
{
    if (m_cameraHandler) {
        disconnect(m_cameraHandler, nullptr, this, nullptr);  // 断开摄像头处理对象的所有信号槽连接
        m_cameraHandler->stopCamera();  // 停止摄像头
    }
    delete ui;  // 释放UI资源
}

/**
 * @brief 设置UI样式和属性
 */
void MainWindow::setupUI()
{
    // 设置摄像头显示标签的尺寸策略为忽略，以便可以自由缩放
    ui->cameraLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);

    // 设置应用程序的样式表
    const QString style = R"(
        QWidget { background-color: #1e1f22; color: #e6e6e6; }
        QLineEdit, QComboBox, QSpinBox, QTextEdit {
            background-color: #2b2d31;
            border: 1px solid #3a3d43;
            padding: 6px;
            border-radius: 4px;
            color: #e6e6e6;
        }
        QPushButton {
            background-color: #3b82f6;
            border: none;
            padding: 8px 12px;
            border-radius: 4px;
            color: white;
            font-weight: 600;
        }
        QPushButton:disabled {
            background-color: #4b5563;
            color: #cbd5e1;
        }
        QPushButton:hover:!disabled { background-color: #2563eb; }
        QGroupBox {
            border: 1px solid #3a3d43;
            border-radius: 6px;
            margin-top: 10px;
            padding-top: 10px;
        }
        QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 4px; }
        QTabWidget::pane { border: 1px solid #3a3d43; }
        QTabBar::tab {
            background: #2b2d31;
            padding: 8px 14px;
            border: 1px solid #3a3d43;
            border-bottom: none;
            border-top-left-radius: 6px;
            border-top-right-radius: 6px;
            color: #e6e6e6;
            min-width: 90px;
        }
        QTabBar::tab:selected { background: #3b82f6; color: #fff; }
        QProgressBar {
            border: 1px solid #3a3d43;
            border-radius: 4px;
            background: #2b2d31;
            text-align: center;
        }
        QProgressBar::chunk {
            background-color: #22c55e;
            border-radius: 4px;
        }
    )";
    setStyleSheet(style);  // 应用样式表
}

/**
 * @brief 设置信号槽连接
 */
void MainWindow::setupConnections()
{
    // 视频转码标签页的信号槽连接
    connect(ui->inputFileButton, &QPushButton::clicked, this, &MainWindow::onSelectInputFile);
    connect(ui->outputFileButton, &QPushButton::clicked, this, &MainWindow::onSelectOutputFile);
    connect(ui->convertButton, &QPushButton::clicked, this, &MainWindow::onConvertVideo);

    // 视频抽帧标签页的信号槽连接
    connect(ui->frameInputFileButton, &QPushButton::clicked, this, &MainWindow::onSelectInputFile);
    connect(ui->frameOutputDirButton, &QPushButton::clicked, this, &MainWindow::onSelectOutputDirectory);
    connect(ui->extractButton, &QPushButton::clicked, this, &MainWindow::onExtractFrames);

    // 摄像头标签页的信号槽连接
    connect(ui->startCameraButton, &QPushButton::clicked, this, &MainWindow::onStartCamera);
    connect(ui->stopCameraButton, &QPushButton::clicked, this, &MainWindow::onStopCamera);

    // 视频转码类的信号槽连接
    connect(m_videoConverter, &VideoConverter::operationFinished,
            this, &MainWindow::onOperationFinished);
    connect(m_videoConverter, &VideoConverter::progressChanged,
            this, &MainWindow::onProgressChanged);
    connect(m_videoConverter, &VideoConverter::errorOccurred,
            this, &MainWindow::onErrorOccurred);

    // 视频抽帧类的信号槽连接
    connect(m_frameExtractor, &FrameExtractor::operationFinished,
            this, &MainWindow::onOperationFinished);
    connect(m_frameExtractor, &FrameExtractor::progressChanged,
            this, &MainWindow::onProgressChanged);
    connect(m_frameExtractor, &FrameExtractor::errorOccurred,
            this, &MainWindow::onErrorOccurred);

    // 摄像头处理类的信号槽连接
    connect(m_cameraHandler, &CameraHandler::operationFinished,
            this, &MainWindow::onOperationFinished);
    connect(m_cameraHandler, &CameraHandler::errorOccurred,
            this, &MainWindow::onErrorOccurred);
    connect(m_cameraHandler, &CameraHandler::cameraFrameReady,
            this, &MainWindow::onCameraFrameReady);
    connect(m_cameraHandler, &CameraHandler::detectionResultReady,
            this, &MainWindow::onDetectionResultReady);
}

/**
 * @brief 选择输入文件的事件处理
 */
void MainWindow::onSelectInputFile()
{
    // 打开文件对话框，选择MP4视频文件
    const QString fileName = QFileDialog::getOpenFileName(
        this, "Select video file", "", "MP4 Files (*.mp4)");

    if (fileName.isEmpty()) {
        return;  // 用户取消了选择
    }

    // 根据当前选中的标签页设置输入文件路径
    if (ui->tabWidget->currentIndex() == 0) {  // 视频转码标签页
        ui->inputFileEdit->setText(fileName);
        // 自动生成默认的输出文件路径
        QFileInfo fileInfo(fileName);
        const QString outputFile = fileInfo.path() + "/" + fileInfo.completeBaseName() + "_converted";
        ui->outputFileEdit->setText(outputFile);
    } else if (ui->tabWidget->currentIndex() == 1) {  // 视频抽帧标签页
        ui->frameInputFileEdit->setText(fileName);
        // 自动生成默认的输出目录路径
        QFileInfo fileInfo(fileName);
        const QString outputDir = fileInfo.path() + "/" + fileInfo.completeBaseName() + "_frames";
        ui->frameOutputDirEdit->setText(outputDir);
    }
}

/**
 * @brief 选择输出文件的事件处理
 */
void MainWindow::onSelectOutputFile()
{
    // 打开文件对话框，选择输出文件路径
    const QString fileName = QFileDialog::getSaveFileName(
        this,
        "Select output file",
        ui->outputFileEdit->text(),
        "Video Files (*.mp4 *.avi *.mkv *.mov *.wmv *.flv *.webm);;All Files (*.*)");

    if (!fileName.isEmpty()) {
        ui->outputFileEdit->setText(fileName);
    }
}

/**
 * @brief 选择输出目录的事件处理
 */
void MainWindow::onSelectOutputDirectory()
{
    // 打开文件对话框，选择输出目录
    const QString dir = QFileDialog::getExistingDirectory(
        this, "Select output directory", ui->frameOutputDirEdit->text());

    if (!dir.isEmpty()) {
        ui->frameOutputDirEdit->setText(dir);
    }
}

/**
 * @brief 视频转码的事件处理
 */
void MainWindow::onConvertVideo()
{
    QString inputFile = ui->inputFileEdit->text();  // 获取输入文件路径
    QString outputFile = ui->outputFileEdit->text(); // 获取输出文件路径
    QString format = ui->formatComboBox->currentText(); // 获取输出格式

    // 验证输入
    if (inputFile.isEmpty() || outputFile.isEmpty()) {
        QMessageBox::warning(this, "Warning", "Select input and output files.");
        return;
    }
    if (!inputFile.toLower().endsWith(".mp4")) {
        QMessageBox::warning(this, "Warning", "Only MP4 input is supported.");
        return;
    }

    // 确保输出文件有正确的扩展名
    if (!outputFile.endsWith("." + format)) {
        outputFile += "." + format;
        ui->outputFileEdit->setText(outputFile);
    }

    // 更新UI状态为处理中
    updateUIState(true);
    ui->logTextEdit->append("Converting: " + inputFile + " -> " + outputFile);

    // 执行视频转码
    m_videoConverter->convertVideo(inputFile, outputFile, format);
}

/**
 * @brief 视频抽帧的事件处理
 */
void MainWindow::onExtractFrames()
{
    QString inputFile = ui->frameInputFileEdit->text();    // 获取输入文件路径
    QString outputDir = ui->frameOutputDirEdit->text();    // 获取输出目录路径
    int fps = ui->fpsSpinBox->value();                    // 获取每秒抽取的帧数
    QString imageFormat = ui->imageFormatComboBox->currentText(); // 获取输出图片格式

    // 验证输入
    if (inputFile.isEmpty() || outputDir.isEmpty()) {
        QMessageBox::warning(this, "Warning", "Select input file and output directory.");
        return;
    }
    if (!inputFile.toLower().endsWith(".mp4")) {
        QMessageBox::warning(this, "Warning", "Only MP4 input is supported.");
        return;
    }

    // 更新UI状态为处理中
    updateUIState(true);
    ui->logTextEdit->append("Extracting frames: " + inputFile + " -> " + outputDir +
                            " (format: " + imageFormat + ")");

    // 执行视频抽帧
    m_frameExtractor->extractFrames(inputFile, outputDir, fps, imageFormat);
}

void MainWindow::onOperationFinished(bool success, const QString &message)
{
    ui->logTextEdit->append(message);

    if (message.contains("camera", Qt::CaseInsensitive)) {
        return;
    }

    updateUIState(false);

    if (success) {
        QMessageBox::information(this, "Done", message);
    } else {
        QMessageBox::warning(this, "Error", message);
    }
}

void MainWindow::onProgressChanged(int progress)
{
    ui->progressBar->setValue(progress);
}

void MainWindow::onErrorOccurred(const QString &errorMessage)
{
    ui->logTextEdit->append("Error: " + errorMessage);
    QMessageBox::critical(this, "Error", errorMessage);
}

void MainWindow::onStartCamera()
{
    ui->logTextEdit->append("Starting camera...");

    ui->startCameraButton->setEnabled(false);
    ui->stopCameraButton->setEnabled(true);

    m_cameraHandler->startCamera(0, 640, 480);
}

void MainWindow::onStopCamera()
{
    ui->logTextEdit->append("Stopping camera...");

    ui->stopCameraButton->setEnabled(false);
    ui->startCameraButton->setEnabled(true);

    m_hasDetections = false;
    m_lastDetections.clear();

    m_cameraHandler->stopCamera();
}

void MainWindow::onCameraFrameReady(const QImage &frame)
{
    const QSize targetSize = ui->cameraLabel->contentsRect().size();
    QImage displayImage = frame;

    if (m_hasDetections) {
        // 将“最近一次检测结果”叠加在当前显示帧上（检测结果可能略滞后）
        displayImage = frame.copy();
        QPainter painter(&displayImage);
        painter.setRenderHint(QPainter::Antialiasing);

        for (const auto &result : m_lastDetections) {
        QColor color = QColor::fromHsl((result.classId * 137) % 360, 255, 128, 200);
        QPen pen(color, 3);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(QRectF(result.x, result.y, result.width, result.height));

            QString label = QString("%1: %2%")
                                .arg(result.className)
                                .arg(QString::number(result.confidence * 100, 'f', 1));
            QFont font("Arial", 12, QFont::Bold);
            QFontMetrics fm(font);
            QSize labelSize = fm.size(Qt::TextSingleLine, label);
            QRectF labelRect(result.x, result.y - labelSize.height() - 5,
                             labelSize.width() + 10, labelSize.height() + 5);
        QColor labelBg = color;
        labelBg.setAlpha(160);
        painter.setBrush(QBrush(labelBg));
        painter.drawRoundedRect(labelRect, 3, 3);

            painter.setPen(QPen(Qt::white));
            painter.setFont(font);
            painter.drawText(QPointF(result.x + 5, result.y - 5), label);
        }
        painter.end();
    }

    if (targetSize.width() <= 0 || targetSize.height() <= 0) {
        ui->cameraLabel->setPixmap(QPixmap::fromImage(displayImage));
        return;
    }

    QImage scaledImage = displayImage.scaled(targetSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    ui->cameraLabel->setPixmap(QPixmap::fromImage(scaledImage));
}

void MainWindow::onDetectionResultReady(const QImage &frame, const std::vector<DetectionResult> &results)
{
    // 这里只缓存检测结果，不直接绘制；绘制发生在下一帧 onCameraFrameReady 中
    m_lastDetections = results;
    m_hasDetections = !m_lastDetections.empty();
}

void MainWindow::updateUIState(bool processing)
{
    if (processing) {
        ui->progressBar->setVisible(true);
        ui->progressBar->setValue(0);

        ui->convertButton->setEnabled(false);
        ui->extractButton->setEnabled(false);
        ui->inputFileButton->setEnabled(false);
        ui->outputFileButton->setEnabled(false);
        ui->frameInputFileButton->setEnabled(false);
        ui->frameOutputDirButton->setEnabled(false);
        ui->startCameraButton->setEnabled(false);
        ui->stopCameraButton->setEnabled(false);
    } else {
        ui->progressBar->setVisible(false);

        ui->convertButton->setEnabled(true);
        ui->extractButton->setEnabled(true);
        ui->inputFileButton->setEnabled(true);
        ui->outputFileButton->setEnabled(true);
        ui->frameInputFileButton->setEnabled(true);
        ui->frameOutputDirButton->setEnabled(true);
        ui->startCameraButton->setEnabled(true);
        ui->stopCameraButton->setEnabled(false);
    }
}
