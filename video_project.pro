#-------------------------------------------------
# Industrial Safety Monitoring System (RV1126B)
# Project updated: 2026-05-31 (Migrated to RV1126B)
#-------------------------------------------------

QT += core gui network widgets
greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

TARGET = video_project
TEMPLATE = app

DEFINES += QT_DEPRECATED_WARNINGS
DEFINES += _GLIBCXX_USE_C99
DEFINES += __attr_dealloc_free=

# ==============================================================================
# --- 0. 系统路径净化与强制绑定 ---
# ==============================================================================
# 强制清空旧板子的头文件和库路径
INCLUDEPATH -= /opt/sysroot/usr/include
INCLUDEPATH -= /opt/sysroot/usr/include/aarch64-linux-gnu

# 清理各种模式下的旧 sysroot
QMAKE_CFLAGS_RELEASE -= --sysroot=/opt/sysroot
QMAKE_CXXFLAGS_RELEASE -= --sysroot=/opt/sysroot
QMAKE_LFLAGS_RELEASE -= --sysroot=/opt/sysroot
QMAKE_CFLAGS_DEBUG -= --sysroot=/opt/sysroot
QMAKE_CXXFLAGS_DEBUG -= --sysroot=/opt/sysroot
QMAKE_LFLAGS_DEBUG -= --sysroot=/opt/sysroot

# 编译和链接都强行指明新 sysroot
QMAKE_CFLAGS   += --sysroot=/opt/rv1126b_sysroot
QMAKE_CXXFLAGS += --sysroot=/opt/rv1126b_sysroot
QMAKE_LFLAGS   += --sysroot=/opt/rv1126b_sysroot

# ==============================================================================
# --- 1. 解决 GCC 8.3 编译新标准库的语法冲突 ---
# ==============================================================================
QMAKE_CFLAGS += -include $$PWD/sysroot_fix.h
QMAKE_CXXFLAGS += -include $$PWD/sysroot_fix.h

QMAKE_CFLAGS += -D\"__attr_dealloc(a,b)=\"
QMAKE_CFLAGS += -D\"__attribute_alloc_align__(a)=\"
QMAKE_CFLAGS += -D\"__attr_access(a)=\"
QMAKE_CXXFLAGS += -D\"__attr_dealloc(a,b)=\"
QMAKE_CXXFLAGS += -D\"__attribute_alloc_align__(a)=\"
QMAKE_CXXFLAGS += -D\"__attr_access(a)=\"

# ==============================================================================
# --- 2. OpenCV 交叉编译配置 ---
# ==============================================================================
INCLUDEPATH += /opt/rv1126b_sysroot/usr/include/opencv4
LIBS += -L/opt/rv1126b_sysroot/usr/lib/aarch64-linux-gnu \
        -lopencv_highgui -lopencv_videoio -lopencv_imgcodecs -lopencv_imgproc -lopencv_core

# ==============================================================================
# --- 3. RKNN NPU 库配置 ---
# ==============================================================================
INCLUDEPATH += /home/lubancat/rknn-toolkit2-2.3.2/rknpu2/runtime/Linux/librknn_api/include
LIBS += -L/opt/rv1126b_sysroot/usr/lib -lrknnrt

# ==============================================================================
# --- 4. RGA 硬件加速与 MPP 视频编解码库配置 ---
# ==============================================================================
INCLUDEPATH += /opt/rv1126b_sysroot/usr/include/rga
LIBS += -L/opt/rv1126b_sysroot/usr/lib/aarch64-linux-gnu -lrga -lrockchip_mpp

# ==============================================================================
# --- 5. 链接器与标准库配置 ---
# ==============================================================================
QMAKE_CXXFLAGS += -O3 -march=armv8-a

# 避免把 libstdc++ / libgcc 静态塞进程序，防止 CXXABI 符号被错误版本化为 Qt_5
QMAKE_LFLAGS -= -static-libstdc++
QMAKE_LFLAGS -= -static-libgcc
QMAKE_LFLAGS += -Wl,--allow-shlib-undefined

# 强制动态链接 C++ 标准库
LIBS += -lstdc++

# 强制优先找新 sysroot 库
QMAKE_LFLAGS += -Wl,-rpath-link,/opt/rv1126b_sysroot/usr/lib/aarch64-linux-gnu
QMAKE_LFLAGS += -Wl,-rpath-link,/opt/rv1126b_sysroot/lib/aarch64-linux-gnu

# ==============================================================================
# --- 6. 项目源码文件列表 ---
# ==============================================================================
SOURCES += \
        MPPEncoder.cpp clientworker.cpp gimbalcontroller.cpp main.cpp mainwindow.cpp \
        postprocess.cpp postprocess_yolov8.cpp rknnworker.cpp streamserver.cpp

HEADERS += \
        MPPEncoder.h clientworker.h gimbalcontroller.h mainwindow.h pidcontroller.h \
        postprocess.h postprocess_yolov8.h rknnworker.h streamserver.h sysroot_fix.h

FORMS += mainwindow.ui

# ==============================================================================
# --- 7. 启动代码 crt1.o / crti.o 寻路补丁 ---
# ==============================================================================
QMAKE_LFLAGS += -B/opt/rv1126b_sysroot/usr/lib/aarch64-linux-gnu/
QMAKE_LFLAGS += -B/opt/rv1126b_sysroot/usr/lib/
QMAKE_LFLAGS += -B/opt/rv1126b_sysroot/lib/aarch64-linux-gnu/
