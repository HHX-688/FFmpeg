/**
 * @file main.cpp
 * @brief 应用程序入口测试
 */

#include "mainwindow.h"  // 主窗口类头文件
#include "yolodetect.h"  // YOLO检测类头文件

#include <QApplication>  // Qt应用程序类头文件

/**
 * @brief 应用程序入口函数
 * @param argc 命令行参数数量
 * @param argv 命令行参数数组
 * @return 应用程序退出码
 */
int main(int argc, char *argv[])
{
    // 创建Qt应用程序对象，管理应用程序的资源和事件循环
    QApplication a(argc, argv);
    
    // 注册自定义元类型，使跨线程信号能传递std::vector<DetectionResult>类型的数据
    qRegisterMetaType<std::vector<DetectionResult>>("std::vector<DetectionResult>");
    
    // 创建主窗口对象
    MainWindow w;
    
    // 显示主窗口
    w.show();
    
    // 进入应用程序的事件循环，处理用户交互和系统事件
    return a.exec();
}
