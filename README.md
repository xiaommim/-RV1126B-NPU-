# 基于 RV1126B 边缘计算平台的智慧安全工厂系统

本项目是一套运行在 RV1126B 边缘计算平台上的智慧安全工厂视觉检测系统。系统以 Qt/C++ 为主程序框架，调用 RKNN Runtime 在板端 NPU 上部署 YOLOv8 INT8 量化模型，对安全帽、口罩、反光衣等劳保用品穿戴状态进行实时检测，并把检测结果同步用于本地界面显示、HTTP MJPEG 远程推流、报警日志统计和二维云台跟踪控制。

项目面向工厂、工地、矿区、车间等高风险作业场景，强调端侧实时推理和现场闭环控制，不依赖云端服务器即可完成“采集-推理-后处理-显示-推流-云台控制”的完整链路。

## 核心功能

- 实时采集 USB/CSI 摄像头画面，当前代码固定打开 `/dev/video52`，输入分辨率为 `640x480`，目标帧率为 `30 FPS`。
- 通过 RKNN Runtime 加载 YOLOv8 INT8 模型，模型输入尺寸为 `416x416`。
- 完成 Letterbox 预处理、NPU 推理、置信度筛选、NMS、坐标还原和类别统计。
- 在 Qt 全屏界面中显示实时画面、检测框、FPS、合规/违规人数和报警日志。
- 支持勾选检测类别，可按安全帽、口罩、反光衣开关过滤检测目标。
- 提供 HTTP MJPEG 推流服务，局域网浏览器可直接访问实时画面，默认端口为 `8000`。
- 根据违规目标中心与画面中心偏差，结合 PID、死区判断和单步限幅，输出 PWM 控制量驱动二维云台。
- 使用短队列丢帧策略和采集、推理、后处理多线程流水线，优先保证实时性，避免旧帧堆积造成延迟。

## 系统架构

```text
摄像头/V4L2
    |
    v
采集线程 captureThreadFunc
    |                         
    |                          -> 显示/推流队列
    v
推理线程 inferThreadFunc
    |
    v
YOLOv8 后处理 postprocess_yolov8
    |
    +--> Qt 本地界面: 视频、检测框、统计、报警日志
    +--> StreamServer/ClientWorker: HTTP MJPEG 推流
    +--> PID + GimbalController: PWM 云台跟踪
```

软件分为四层：界面层负责按钮、画面和统计信息；算法层负责 YOLOv8 推理与后处理；服务层负责 MJPEG 网络推流；控制层负责 PID 计算和 PWM 写入。模块之间主要通过 Qt 信号槽、线程安全队列和共享检测结果传递数据。

## 硬件组成

| 模块 | 作用 | 说明 |
| --- | --- | --- |
| RV1126B 开发板 | 边缘计算核心 | 运行 Linux、Qt 程序和 RKNN 推理环境 |
| USB/CSI 摄像头 | 视频输入 | 通过 V4L2 读取 `640x480` 图像帧 |
| 显示屏 | 本地交互 | 显示检测画面、报警日志和统计数据 |
| 二维舵机云台 | 视角调整 | 水平和垂直两轴跟踪违规目标 |
| 独立舵机电源 | 执行机构供电 | 与开发板共地，降低舵机瞬态电流干扰 |

当前云台 PWM 映射：

| 方向 | GPIO/通道 | sysfs 路径 | 说明 |
| --- | --- | --- | --- |
| 水平轴 Pan | GPIO3_B4 | `/sys/class/pwm/pwmchip2/pwm0` | normal 极性，直接写入目标脉宽 |
| 垂直轴 Tilt | GPIO3_B5 | `/sys/class/pwm/pwmchip3/pwm0` | 实测为反向极性，代码中使用 `20ms - 目标脉宽` 换算 |

舵机周期为 `20ms`，常用脉宽范围为 `0.5ms` 到 `2.5ms`，中位值为 `1.5ms`。

## 软件模块

| 文件/模块 | 主要职责 |
| --- | --- |
| `main.cpp` | 初始化 Qt 应用，加载中文字体，全屏启动主窗口 |
| `mainwindow.*` / `mainwindow.ui` | 本地界面、按钮交互、检测开关、报警区域设置、统计和画面刷新 |
| `rknnworker.*` | RKNN 初始化、摄像头采集、推理线程、后处理线程、性能统计和云台目标选择 |
| `postprocess_yolov8.*` | YOLOv8 输出解析、阈值筛选、NMS 和坐标还原 |
| `streamserver.*` | 基于 `QTcpServer` 的 MJPEG 推流服务 |
| `clientworker.*` | 每个浏览器连接独立发送 multipart JPEG 数据 |
| `gimbalcontroller.*` | 通过 sysfs 写 PWM，占空比限幅并控制云台两轴 |
| `pidcontroller.h` | 简单 PID 控制器，用于把目标偏差转换为云台步进量 |
| `MPPEncoder.*` | Rockchip MPP JPEG 编码封装，用于推流帧编码 |
| `sysroot_fix.h` | 交叉编译兼容补丁，处理部分 sysroot 头文件宏兼容问题 |

## 关键参数

| 指标 | 参数/结果 | 说明 |
| --- | --- | --- |
| 核心平台 | RV1126B | 板端 Linux + RKNN Runtime |
| 检测模型 | YOLOv8 INT8 | RKNN 格式，输入 `416x416` |
| 视频输入 | `640x480 @ 30 FPS` | V4L2 摄像头采集 |
| 稳定帧率 | 约 `30 FPS` | 后处理线程统计 |
| NPU 推理耗时 | 约 `26-31 ms/帧` | 板端实测范围 |
| 推流编码耗时 | 约 `8-9 ms/帧` | JPEG/MJPEG 输出 |
| CPU 占用 | 约 `66%-70%` | 综合运行状态参考 |
| 推流端口 | `8000` | 浏览器访问 `http://<板端IP>:8000/` |

## 依赖环境

板端需要具备以下运行/编译环境：

- Qt Core / Widgets / Network
- OpenCV 4
- RKNN Runtime，链接库 `librknnrt`
- Rockchip RGA，链接库 `librga`
- Rockchip MPP，链接库 `lrockchip_mpp`
- RV1126B 交叉编译 sysroot，当前工程配置为 `/opt/rv1126b_sysroot`
- RKNN API 头文件路径，当前工程配置为 `/home/lubancat/rknn-toolkit2-2.3.2/rknpu2/runtime/Linux/librknn_api/include`

当前 `.pro` 文件中已经配置了主要 include/lib 路径和链接参数。如果你的板端或交叉编译环境路径不同，需要同步修改 `video_project.pro` 中的 `INCLUDEPATH`、`LIBS` 和 sysroot 路径。

## 资源路径

运行前请确认以下资源存在：

| 资源 | 默认路径 | 说明 |
| --- | --- | --- |
| RKNN 模型 | `/userdata/ywy_test/best_rkopt_416_rv1126b_i8.rknn` | `rknnworker.cpp` 中的 `model_path` |
| 中文字体 | `/userdata/ywy_test/fonts/NotoSansCJK-Regular.ttc` | 首选字体 |
| 备用字体 | `/userdata/ywy_test/fonts/msyh.ttc` | 首选字体加载失败时使用 |
| 摄像头节点 | `/dev/video52` | `captureThreadFunc()` 当前固定节点 |
| PWM 节点 | `/sys/class/pwm/pwmchip2/pwm0`、`/sys/class/pwm/pwmchip3/pwm0` | 云台控制输出 |

## 编译部署

在已配置 RV1126B 交叉编译工具链和 Qt 环境的主机上执行：

```bash
qmake video_project.pro
make -j$(nproc)
```

将生成的可执行文件部署到 RV1126B 板端，并确保模型、字体、摄像头节点、PWM 权限和运行库路径正确。若在板端直接编译，也需要保证 Qt、OpenCV、RKNN、RGA、MPP 等库均可被编译器和动态链接器找到。

## 运行方式

1. 接好摄像头、显示屏、二维云台和独立舵机电源，并确保舵机电源与开发板共地。
2. 放置 RKNN 模型和字体文件到默认路径，或修改代码中的路径常量。
3. 确认摄像头节点为 `/dev/video52`，如实际节点不同，需要修改 `rknnworker.cpp`。
4. 确认 PWM 节点已导出且当前用户有写入权限。
5. 启动程序后，Qt 界面会全屏显示，点击“开启监测”开始检测。
6. 局域网内浏览器访问：

```text
http://<RV1126B_IP>:8000/
```

## 检测与控制逻辑

系统会从后处理结果中筛选违规目标，并按类别与置信度选择云台跟踪目标。目标中心点与画面中心点之间的偏差进入 PID 控制器；如果偏差处于死区范围内，则保持当前位置；如果超出死区，则输出经过限幅的 `pan_delta` 和 `tilt_delta`，再由 `GimbalController` 写入 PWM `duty_cycle`。

这种控制方式可以减少检测框轻微抖动造成的频繁摆动，也能降低多人场景下误跟踪的概率。

## 项目亮点

- 端侧闭环：检测结果不仅显示，还直接参与报警、推流和云台执行控制。
- 部署轻量：使用 HTTP MJPEG，浏览器无需插件即可查看实时画面。
- 实时优先：短队列丢帧与三线程流水线结合，避免延迟累积。
- 工程完整：覆盖摄像头采集、NPU 推理、后处理、GUI、网络服务和硬件控制。
- 易扩展：可继续增加烟火、跌倒、禁区闯入、车辆靠近人员等工业安全类别。

## 后续优化方向

- 增加 SQLite 本地报警记录或云端平台上传，形成可追溯安全台账。
- 进一步优化 RGA/MPP 零拷贝链路，减少图像格式转换和 JPEG 编码开销。
- 增加目标保持、轨迹预测和多目标切换策略，提高遮挡场景下的云台稳定性。
- 将摄像头节点、模型路径、推流端口、PWM 节点等参数改为配置文件，减少重新编译成本。
- 增加守护进程、自启动脚本和日志轮转，提升现场长期运行能力。

## 参考

- Rockchip RKNN Toolkit2 / RKNN Runtime 文档
- Rockchip MPP / RGA 开发资料
- Qt Network / Qt Widgets 文档
- OpenCV 视频采集与图像处理接口
- 项目说明文档：《基于 RV1126 边缘计算平台的智慧安全工厂系统》
