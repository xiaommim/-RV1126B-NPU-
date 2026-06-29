#ifndef RKNNWORKER_H
#define RKNNWORKER_H

#include <QObject>
#include <QImage>
#include <QThread>
#include <QRect>
#include <QDebug>
#include <QPoint>
#include <atomic>
#include <mutex>
#include <queue>
#include <condition_variable>
#include <thread>
#include <opencv2/opencv.hpp>
#include "rknn_api.h"
#include "im2d.h"
#include "rga.h"
#include "MPPEncoder.h"
#include "pidcontroller.h"
#include "gimbalcontroller.h"
#include "postprocess_yolov8.h"
// ==========================================
// 1. 流水线数据包结构体
// ==========================================
struct FrameData {
    cv::Mat raw_frame;         // 原始帧
    cv::Mat bgra_frame;        // 用于显示的帧
    detect_result_group_t group; // 识别结果
    bool has_detect = false;   // 本帧是否经过了检测
    int safe_count = 0;
    int alarm_count = 0;
    int64_t capture_ts_ms = 0;  //计算“总延迟”
    int64_t infer_done_ts_ms = 0;
};

// ==========================================
// 2. 线程安全队列 (模板类必须在头文件实现)
// ==========================================
template<typename T>
class DropQueue {
public:
    DropQueue(size_t max_len) : max_length(max_len), m_stop(false) {}

    void push(const T& item) {
        std::unique_lock<std::mutex> lock(m_mutex);
        if (m_queue.size() >= max_length) {
            m_queue.pop();
        }
        m_queue.push(item);
        m_cond.notify_one();
    }

    bool pop(T& item) {
        std::unique_lock<std::mutex> lock(m_mutex);
        while (m_queue.empty() && !m_stop) {
            m_cond.wait(lock);
        }
        if (m_queue.empty() && m_stop) return false;
        item = m_queue.front();
        m_queue.pop();
        return true;
    }

    void stop() {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_stop = true;
        m_cond.notify_all();
    }

private:
    std::queue<T> m_queue;
    std::mutex m_mutex;
    std::condition_variable m_cond;
    size_t max_length;
    bool m_stop;
};

// ==========================================
// 3. rknnworker 类声明
// ==========================================
class rknnworker : public QObject
{
    Q_OBJECT
public:
    explicit rknnworker(QObject *parent = nullptr);
    ~rknnworker();
    bool isCtxValid() const { return m_ctxValid; }
    void setTargetFilter(bool helmet, bool mask, bool vest) {
        m_check_helmet.store(helmet);
        m_check_mask.store(mask);
        m_check_vest.store(vest);
    }
    bool isCheckHelmet() const { return m_check_helmet.load(); }
    bool isCheckMask() const { return m_check_mask.load(); }
    bool isCheckVest() const { return m_check_vest.load(); }

signals:
    void dataResult(int safe, int alarm);
    void alarmLog(QString msg);
    void frameReady(QImage img);
    void streamFrameReady(QByteArray jpegData);
    void initComplete();

    void requestGimbalMove(long pan_delta, long tilt_delta);

public slots:
    void initRknn();
    void process();
    void setDetecting(bool allowed) { m_isDetecting.store(allowed); }
    void setAlarmRect(QRect rect); // 仅声明，在 cpp 中实现
    void stopPipeline();

private:
    void captureThreadFunc();
    void inferThreadFunc();
    void postThreadFunc();

    rknn_context ctx = 0;
    bool m_ctxValid = false;
    unsigned char *model_data = nullptr; // ✅ 补回缺失的 model_data

    std::atomic<bool> m_check_helmet{true};
    std::atomic<bool> m_check_mask{true};
    std::atomic<bool> m_check_vest{true};

    //新增：零拷贝内存指针与属性描述符
    rknn_tensor_mem* m_input_mem = nullptr;
    rknn_tensor_attr m_input_attr;

    rknn_tensor_mem* m_output_mems[3] = {nullptr,nullptr,nullptr};
    rknn_tensor_attr m_output_attrs[3];

    std::atomic<bool> m_stop{false};
    std::atomic<bool> m_isDetecting{false};

    std::mutex m_rectMutex; // 保护 m_alarmRect 的锁
    QRect m_alarmRect;

    std::thread m_capThread;
    std::thread m_inferThread;
    std::thread m_postThread;

    DropQueue<FrameData> capToInferQueue{2};
    //DropQueue<FrameData> inferToPostQueue{3};
    DropQueue<FrameData> capToPostQueue{3};  // 队列 B：直接发给 UI 和推流
    MPPEncoder *m_encoder = nullptr;

    // 1. 新增：保护 AI 识别结果的全局锁和变量
    std::mutex m_detectedMutex;
    detect_result_group_t m_latest_results; // 存放最新的一次 AI 识别结果

    // 新增：用于平滑处理的持久变量
    detect_result_group_t m_smooth_results; // 存储平滑后的结果

    SimplePid m_panPid;
    SimplePid m_tiltPid;


};

#endif // RKNNWORKER_H
