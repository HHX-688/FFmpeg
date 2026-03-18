#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QString>
#include "yolodetect.h"
#include "camerahandler.h"
#include "frameextractor.h"
#include "videoconverter.h"

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

// 主窗口：负责 UI 交互、显示摄像头画面、展示检测结果与日志
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    // 文件/目录选择
    void onSelectInputFile();
    void onSelectOutputFile();
    void onSelectOutputDirectory();
    // 功能按钮入口
    void onConvertVideo();
    void onExtractFrames();
    void onStartCamera();
    void onStopCamera();
    // 摄像头帧与检测结果回调
    void onCameraFrameReady(const QImage &frame);
    void onDetectionResultReady(const QImage &frame, const std::vector<DetectionResult> &results);
    // 统一的状态/进度/错误处理
    void onOperationFinished(bool success, const QString &message);
    void onProgressChanged(int progress);
    void onErrorOccurred(const QString &errorMessage);

private:
    Ui::MainWindow *ui;
    // 三个业务模块：转码、抽帧、摄像头采集 + 检测
    VideoConverter *m_videoConverter;
    FrameExtractor *m_frameExtractor;
    CameraHandler *m_cameraHandler;
    // 最近一次检测结果缓存，用于在显示帧时叠加绘制
    std::vector<DetectionResult> m_lastDetections;
    bool m_hasDetections = false;
    
    // UI 初始化与状态控制
    void setupUI();
    void setupConnections();
    void updateUIState(bool processing);
};
#endif // MAINWINDOW_H
