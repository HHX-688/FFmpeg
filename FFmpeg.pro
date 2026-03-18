QT       += core gui concurrent

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++17

# FFmpeg libraries
win32 {
    # 请根据您的FFmpeg安装路径修改以下路径
    INCLUDEPATH += "D:/cppsoft/ffmpeg/include"
    LIBS += -L"D:/cppsoft/ffmpeg/lib"
    LIBS += -lavcodec -lavformat -lavutil -lswscale -lswresample -lavdevice
    
    # ONNX Runtime libraries
    # 请根据您的ONNX Runtime安装路径修改以下路径
    INCLUDEPATH += "D:/cppsoft/onnxruntime-win-x64-gpu-1.23.2/include"
    LIBS += -L"D:/cppsoft/onnxruntime-win-x64-gpu-1.23.2/lib"
    LIBS += -lonnxruntime
}

# You can make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

SOURCES += \
    main.cpp \
    mainwindow.cpp \
    videoconverter.cpp \
    frameextractor.cpp \
    camerahandler.cpp \
    yolodetect.cpp

HEADERS += \
    mainwindow.h \
    videoconverter.h \
    frameextractor.h \
    camerahandler.h \
    yolodetect.h

FORMS += \
    mainwindow.ui

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target
