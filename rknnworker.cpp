#include "rknnworker.h"
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <cerrno>
#include <QCoreApplication>
#include <cmath>
#include <algorithm>
#include <chrono>
#include <fstream>
#include <sstream>
#include <unistd.h>
rknnworker::rknnworker(QObject *parent) : QObject(parent) {
    // m_encoder = new MPPEncoder(416, 480);
    m_encoder = nullptr;
}

rknnworker::~rknnworker() {
    stopPipeline();

    if (ctx) {
        rknn_destroy(ctx);
        ctx = 0;
    }

    if (model_data) {
        free(model_data);
        model_data = nullptr;
    }

    if (m_encoder) {
        delete m_encoder;
        m_encoder = nullptr;
    }
}
void rknnworker::stopPipeline() {
    m_stop.store(true);
    capToInferQueue.stop();
    capToPostQueue.stop();

    if(m_capThread.joinable()) m_capThread.join();
    if(m_inferThread.joinable()) m_inferThread.join();
    if(m_postThread.joinable()) m_postThread.join();
}


static unsigned char* load_model(const char* filename, int* model_size) {
    if (model_size) *model_size = 0;

    FILE* fp = fopen(filename, "rb");
    if (fp == nullptr) {
        fprintf(stderr, "fopen failed: %s errno=%d\n", filename, errno);
        fflush(stderr);
        return nullptr;
    }

    if (fseek(fp, 0, SEEK_END) != 0) {
        fprintf(stderr, "fseek end failed errno=%d\n", errno);
        fflush(stderr);
        fclose(fp);
        return nullptr;
    }

    long size = ftell(fp);
    fprintf(stderr, "model file size = %ld\n", size);
    fflush(stderr);

    if (size <= 0) {
        fclose(fp);
        return nullptr;
    }

    if (fseek(fp, 0, SEEK_SET) != 0) {
        fprintf(stderr, "fseek set failed errno=%d\n", errno);
        fflush(stderr);
        fclose(fp);
        return nullptr;
    }

    unsigned char* data = (unsigned char*)malloc(size);
    fprintf(stderr, "malloc model ptr=%p size=%ld\n", data, size);
    fflush(stderr);

    if (!data) {
        fclose(fp);
        return nullptr;
    }

    size_t total = 0;
    while (total < (size_t)size) {
        size_t n = fread(data + total, 1, std::min((size_t)1024 * 1024, (size_t)size - total), fp);
        if (n == 0) {
            fprintf(stderr, "fread stopped total=%zu errno=%d\n", total, errno);
            fflush(stderr);
            break;
        }

        total += n;
        fprintf(stderr, "read model progress: %zu / %ld\n", total, size);
        fflush(stderr);
    }

    fclose(fp);

    if (total != (size_t)size) {
        free(data);
        return nullptr;
    }

    if (model_size) *model_size = (int)size;
    return data;
}
static inline int64_t nowMs()
{
    using namespace std::chrono;
    return duration_cast<milliseconds>(
               steady_clock::now().time_since_epoch()
               ).count();
}

struct PerfInfo {
    double capture_ms = 0.0;
    double preprocess_ms = 0.0;
    double rknn_ms = 0.0;
    double postprocess_ms = 0.0;
    double mpp_ms = 0.0;
    double stream_ms = 0.0;
    double total_latency_ms = 0.0;
    double fps = 0.0;
    double cpu_percent = 0.0;
};

static PerfInfo g_perf;
static int g_perf_frame_count = 0;

// 读取本进程 CPU 时间：utime + stime
static long long readProcessCpuTicks()
{
    std::ifstream file("/proc/self/stat");
    std::string line;
    std::getline(file, line);

    // /proc/self/stat 第二列 comm 可能带括号，所以先找到最后一个 ')'
    size_t pos = line.rfind(')');
    if (pos == std::string::npos) return 0;

    std::string rest = line.substr(pos + 2);
    std::istringstream iss(rest);

    std::string token;
    long long utime = 0;
    long long stime = 0;

    // rest 从字段3开始，utime是字段14，stime是字段15
    // 所以从字段3数到字段13，跳过11个字段
    for (int i = 3; i <= 13; i++) {
        iss >> token;
    }

    iss >> utime;  // field 14
    iss >> stime;  // field 15

    return utime + stime;
}

static double calcProcessCpuPercent()
{
    static int64_t last_wall_ms = nowMs();
    static long long last_cpu_ticks = readProcessCpuTicks();

    int64_t cur_wall_ms = nowMs();
    long long cur_cpu_ticks = readProcessCpuTicks();

    int64_t wall_delta_ms = cur_wall_ms - last_wall_ms;
    long long cpu_delta_ticks = cur_cpu_ticks - last_cpu_ticks;

    last_wall_ms = cur_wall_ms;
    last_cpu_ticks = cur_cpu_ticks;

    if (wall_delta_ms <= 0) return 0.0;

    long ticks_per_sec = sysconf(_SC_CLK_TCK);
    int cpu_cores = sysconf(_SC_NPROCESSORS_ONLN);

    double cpu_time_sec = (double)cpu_delta_ticks / (double)ticks_per_sec;
    double wall_time_sec = (double)wall_delta_ms / 1000.0;

    // 这里返回的是进程 CPU 占用百分比，按整机总核数归一化
    return (cpu_time_sec / wall_time_sec) * 100.0 / cpu_cores;
}

static void printPerfEveryNFrames(int n = 30)
{
    g_perf_frame_count++;

    if (g_perf_frame_count % n != 0) {
        return;
    }

    g_perf.cpu_percent = calcProcessCpuPercent();

    qDebug() << "========== 性能统计 ==========";
    qDebug() << "采集耗时:" << g_perf.capture_ms << "ms";
                                   qDebug() << "预处理耗时:" << g_perf.preprocess_ms << "ms";
                                          qDebug() << "RKNN推理耗时:" << g_perf.rknn_ms << "ms";
                                           qDebug() << "后处理耗时:" << g_perf.postprocess_ms << "ms";
                                          qDebug() << "MPP编码耗时:" << g_perf.mpp_ms << "ms";
                                          qDebug() << "推流发送耗时:" << g_perf.stream_ms << "ms";
                                             qDebug() << "总延迟:" << g_perf.total_latency_ms << "ms";
        qDebug() << "FPS:" << g_perf.fps;
    qDebug() << "CPU占用:" << g_perf.cpu_percent << "%";
        qDebug() << "==============================";
}
struct GimbalTarget{
    bool found = false;
    float x = 0.0f;
    float y = 0.0f;
    int class_id = -1;
    float score = 0.0f;
};
// 云台追踪优先级：数值越小，优先级越高
static int getGimbalPriority(int class_id)
{
    if (class_id == 3) return 1;  // NO-Mask，最适合追脸
    if (class_id == 2) return 2;  // NO-Hardhat
    if (class_id == 4) return 3;  // NO-Safety Vest
    return 999;                  // Person / Hardhat / Mask 不参与云台追踪
}
// 判断这个框适不适合控制云台
static bool isUsableGimbalBox(const detect_result_t& r, int frame_w, int frame_h) {
    if (r.w < 20 || r.h < 20) {
        return false;
    }

    if (r.score < 0.60f) {
        return false;
    }

    float area_ratio = (float)(r.w * r.h) / (float)(frame_w * frame_h);
    if (area_ratio > 0.65f) {
        return false;
    }

    return getGimbalPriority(r.class_id) != 999;
}
// 从所有检测框里选一个云台目标
static GimbalTarget selectGimbalTarget(const detect_result_group_t& results,
                                       int frame_w,
                                       int frame_h)
{
    GimbalTarget best;
    int best_priority = 999;

    for (int i = 0; i < results.count; i++) {
        const detect_result_t& res = results.results[i];

        qDebug() << "当前检测类别:" << res.class_id
                 << "名称:" << res.name
                 << "置信度:" << res.score;

                                    if (!isUsableGimbalBox(res, frame_w, frame_h)) {
            continue;
        }

        int priority = getGimbalPriority(res.class_id);

        // 优先级更高，或者同优先级但置信度更高
        if (!best.found ||
            priority < best_priority ||
            (priority == best_priority && res.score > best.score))
        {
            best.found = true;
            best.x = res.x + res.w * 0.5f;
            best.y = res.y + res.h * 0.5f;
            best.class_id = res.class_id;
            best.score = res.score;
            best_priority = priority;
        }
    }

    return best;
}
// 限制云台每帧最大移动量，防止一次转太猛
static long clampGimbalDelta(long value, long max_abs) {
    if (value > max_abs) return max_abs;
    if (value < -max_abs) return -max_abs;
    return value;
}

void rknnworker::setAlarmRect(QRect rect) {
    // ✅ 修正：锁住的是 m_rectMutex 这把锁，而不是数据本身
    std::lock_guard<std::mutex> lock(m_rectMutex);
    m_alarmRect = rect;
    qDebug() << "Worker 收到警戒区坐标:" << rect.x() << rect.y() << rect.width() << rect.height();
}

void rknnworker::initRknn() {
    int ret = 0;
    int model_len = 0;
    const char *model_path = "/userdata/ywy_test/best_rkopt_416_rv1126b_i8.rknn"; // 先用 /tmp，排除磁盘读取影响
    //yolov8n_rv1126b_i8.rknn
    //const char *model_path = "/userdata/ywy_test/yolov8n_rv1126b_i8.rknn";
    fprintf(stderr, "========== initRknn enter ==========\n");
    fflush(stderr);

    fprintf(stderr, "model_path: %s\n", model_path);
    fflush(stderr);

    model_data = load_model(model_path, &model_len);

    fprintf(stderr, "load_model done, ptr=%p size=%d\n", model_data, model_len);
    fflush(stderr);

    if (!model_data || model_len <= 0) {
        fprintf(stderr, "load_model failed\n");
        fflush(stderr);
        m_ctxValid = false;
        return;
    }

    fprintf(stderr, "rknn_init start\n");
    fflush(stderr);

    ret = rknn_init(&ctx, model_data, model_len, 0, NULL);

    fprintf(stderr, "rknn_init done, ret=%d ctx=%p\n", ret, ctx);
    fflush(stderr);

    if (ret < 0) {
        fprintf(stderr, "rknn_init failed, ret=%d\n", ret);
        fflush(stderr);
        m_ctxValid = false;
        return;
    }

    m_ctxValid = true;

    rknn_input_output_num io_num;
    memset(&io_num, 0, sizeof(io_num));

    fprintf(stderr, "rknn_query RKNN_QUERY_IN_OUT_NUM start\n");
    fflush(stderr);

    ret = rknn_query(ctx, RKNN_QUERY_IN_OUT_NUM, &io_num, sizeof(io_num));

    fprintf(stderr, "rknn_query RKNN_QUERY_IN_OUT_NUM done, ret=%d input=%d output=%d\n",
            ret, io_num.n_input, io_num.n_output);
    fflush(stderr);

    for (int i = 0; i < io_num.n_input; i++) {
        rknn_tensor_attr attr;
        memset(&attr, 0, sizeof(attr));
        attr.index = i;

        fprintf(stderr, "rknn_query input attr %d start\n", i);
        fflush(stderr);

        ret = rknn_query(ctx, RKNN_QUERY_INPUT_ATTR, &attr, sizeof(attr));

        fprintf(stderr, "rknn_query input attr %d done, ret=%d\n", i, ret);
        fprintf(stderr, "input name=%s n_dims=%d dims=%d %d %d %d type=%d fmt=%d qnt_type=%d scale=%f zp=%d\n",
                attr.name,
                attr.n_dims,
                attr.dims[0], attr.dims[1], attr.dims[2], attr.dims[3],
                attr.type,
                attr.fmt,
                attr.qnt_type,
                attr.scale,
                attr.zp);
        fflush(stderr);
    }


    fprintf(stderr, "NPU init success, stop here for isolate test\n");
    fflush(stderr);

    emit initComplete();


}
void rknnworker::process() {
    m_stop.store(false);
    m_isDetecting.store(false);
    // 启动三个独立线程
    qDebug() << "CAMERA ONLY TEST";
    m_capThread = std::thread(&rknnworker::captureThreadFunc, this);
    m_inferThread = std::thread(&rknnworker::inferThreadFunc, this);
    m_postThread = std::thread(&rknnworker::postThreadFunc, this);
}

// 线程 1：纯粹的视频采集
void rknnworker::captureThreadFunc() {
    cv::VideoCapture cap;
    bool isOpened = false;

    qDebug() << "FIXED CAMERA TEST: open /dev/video52 only";

    QString devPath = "/dev/video52";
    if (cap.open(devPath.toStdString(), cv::CAP_V4L2)) {
        qDebug() << "camera opened:" << devPath;
        isOpened = true;
    }

    if (!isOpened) {
        qDebug() << "camera open failed:" << devPath;
        return;
    }

    cap.set(cv::CAP_PROP_FOURCC, cv::VideoWriter::fourcc('M','J','P','G'));
    cap.set(cv::CAP_PROP_FRAME_WIDTH, 640);
    cap.set(cv::CAP_PROP_FRAME_HEIGHT, 480);
    cap.set(cv::CAP_PROP_FPS, 30);;

    while(!m_stop.load()) {
        cv::Mat frame;
        int64_t t_cap0 = nowMs();
        cap >> frame;
        int64_t t_cap1 = nowMs();

        g_perf.capture_ms = t_cap1 - t_cap0;

        // 关键修正：检查图像是否合法
        if (frame.empty()) {
            qDebug() << "⚠️ 捕获到空帧";
            QThread::msleep(10);
            continue;
        }

        // 摄像头 MJPG 有时会被 OpenCV 读成单通道压缩数据，这里手动解码
        if (frame.channels() == 1) {
            cv::Mat decoded = cv::imdecode(frame, cv::IMREAD_COLOR);
            if (!decoded.empty()) {
                frame = decoded;
            }
        }
        qDebug() << "camera frame ok:"
                 << frame.cols << "x" << frame.rows
                 << "channels:" << frame.channels();

        if (frame.empty() || frame.channels() != 3) {
            qDebug() << "⚠️ 捕获到无效帧，通道数" << frame.channels()
                     << "size:" << frame.cols << "x" << frame.rows;
            QThread::msleep(10);
            continue;
        }
        // 核心修改：分离内存，防止线程打架 (数据竞争)
        // 1. 发给 AI 的数据包
        FrameData data_infer;
        //data.raw_frame = frame;
        data_infer.raw_frame = frame.clone();
        data_infer.capture_ts_ms = t_cap1;
        capToInferQueue.push(data_infer);

        // 2. 发给 UI 和推流的数据包
        FrameData data_post;
        data_post.raw_frame = frame.clone();
        data_post.capture_ts_ms = t_cap1;
        cv::cvtColor(frame, data_post.bgra_frame, cv::COLOR_BGR2BGRA);
        capToPostQueue.push(data_post);
    }
}
// 线程 2：专注 NPU 推理
// 线程 2：专注 NPU 推理
void rknnworker::inferThreadFunc()
{
    const int MODEL_W = 416;
    const int MODEL_H = 416;
    const int MODEL_C = 3;

    rknn_input inputs[1];
    memset(inputs, 0, sizeof(inputs));
    inputs[0].index = 0;
    inputs[0].type = RKNN_TENSOR_UINT8;
    inputs[0].fmt = RKNN_TENSOR_NHWC;
    inputs[0].size = MODEL_W * MODEL_H * MODEL_C;
    inputs[0].pass_through = 0;

    while (!m_stop.load()) {
        if (!m_ctxValid) {
            qDebug() << "RKNN 未初始化，跳过 infer";
            QThread::msleep(10);
            continue;
        }

        FrameData data;
        if (!capToInferQueue.pop(data)) {
            break;
        }

        if (data.raw_frame.empty() || data.raw_frame.channels() != 3) {
            qDebug() << "无效摄像头帧，跳过";
            continue;
        }

        if (!m_isDetecting.load()) {
            std::lock_guard<std::mutex> lock(m_detectedMutex);
            m_latest_results.count = 0;
            continue;
        }

        cv::Mat det_img;

        try {
            int src_w = data.raw_frame.cols;
            int src_h = data.raw_frame.rows;

            float scale = std::min((float)MODEL_W / src_w, (float)MODEL_H / src_h);
            int new_w = (int)round(src_w * scale);
            int new_h = (int)round(src_h * scale);

            cv::Mat resized;
            cv::resize(data.raw_frame, resized, cv::Size(new_w, new_h));

            cv::Mat letterbox_img(MODEL_H, MODEL_W, CV_8UC3, cv::Scalar(114, 114, 114));

            int pad_x = (MODEL_W - new_w) / 2;
            int pad_y = (MODEL_H - new_h) / 2;

            resized.copyTo(letterbox_img(cv::Rect(pad_x, pad_y, new_w, new_h)));

            cv::cvtColor(letterbox_img, det_img, cv::COLOR_BGR2RGB);
        } catch (...) {
            qDebug() << "预处理异常，跳过本帧";
            continue;
        }

        if (det_img.empty() || det_img.channels() != 3 || !det_img.isContinuous()) {
            qDebug() << "det_img 无效，跳过本帧";
            continue;
        }

        inputs[0].buf = det_img.data;

        int64_t t_rknn0 = nowMs();

        int ret = rknn_inputs_set(ctx, 1, inputs);
        if (ret != 0) {
            qDebug() << "rknn_inputs_set failed, ret =" << ret;
            continue;
        }

        ret = rknn_run(ctx, nullptr);
        if (ret != 0) {
            qDebug() << "rknn_run failed, ret =" << ret;
            continue;
        }

        rknn_output outputs[9];
        memset(outputs, 0, sizeof(outputs));
        for (int i = 0; i < 9; i++) {
            outputs[i].want_float = 1;
        }

        ret = rknn_outputs_get(ctx, 9, outputs, NULL);

        int64_t t_rknn1 = nowMs();
        g_perf.rknn_ms = t_rknn1 - t_rknn0;

        if (ret != 0 || outputs[0].buf == nullptr) {
            qDebug() << "rknn_outputs_get failed, ret =" << ret;
            continue;
        }

        float* debug_data = (float*)outputs[0].buf;
        int float_count = outputs[0].size / sizeof(float);
        int box_num = float_count / 14;
        qDebug() << "output0 float_count:" << outputs[0].size / sizeof(float);
        float max_val = -99999.0f;
        float min_val = 99999.0f;

        for (int i = 0; i < float_count; i++) {
            max_val = std::max(max_val, debug_data[i]);
            min_val = std::min(min_val, debug_data[i]);
        }

        qDebug() << "[NPU输出] bytes:" << outputs[0].size
                 << "float_count:" << float_count
                 << "box_num:" << box_num
                 << "max:" << max_val
                 << "min:" << min_val;

        detect_result_group_t temp_group{};
        try {
            int64_t t_post0 = nowMs();

            post_process_yolov8(outputs, 0.45f, 0.45f, &temp_group,
                                data.raw_frame.cols, data.raw_frame.rows);

            int64_t t_post1 = nowMs();
            g_perf.postprocess_ms = t_post1 - t_post0;
        } catch (...) {
            qDebug() << "post_process_yolov8 异常，跳过本帧";
            rknn_outputs_release(ctx, 9, outputs);
            continue;
        }

        rknn_outputs_release(ctx, 9, outputs);

        {
            std::lock_guard<std::mutex> lock(m_detectedMutex);
            m_latest_results = temp_group;

            if (temp_group.count > 0) {
                qDebug() << "AI 检测到目标，数量:" << temp_group.count;
            }
        }
    }
}// 线程 3：画框、推流与 UI 更新
void rknnworker::postThreadFunc() {
    int frame_cnt = 0;
    // ==========================================
    // 新增：用于计算真实系统吞吐量 (FPS) 的变量
    // ==========================================
    int fps_calc_frames = 0;
    int64 start_time = cv::getTickCount();
    double current_fps = 0.0;

    //初始化一个空的检测结果，防止刚开机时读取报错
    m_latest_results.count = 0;
    m_smooth_results.count = 0;

    while(!m_stop.load()) {
        FrameData data;
        if(!capToPostQueue.pop(data)) break;


        // 获取最新的警戒区域（加锁读取）
        QRect currentAlarmRect;
        {
            std::lock_guard<std::mutex> lock(m_rectMutex);
            currentAlarmRect = m_alarmRect;
        }
        //2.获取后台最新框
        detect_result_group_t current_results;
        {
            std::lock_guard<std::mutex> lock(m_detectedMutex);
            current_results = m_latest_results;
        }
        // 开始平滑处理
        // 如果 NPU 还没出结果，直接沿用上一帧的 m_smooth_results
        if(current_results.count > 0)
        {
            if(current_results.count == m_smooth_results.count)
            {
                for(int i= 0;i < current_results.count;i ++){
                    auto& old_r = m_smooth_results.results[i];
                    auto& new_r = current_results.results[i];
                    if (old_r.class_id != new_r.class_id) {
                        continue;
                    }
                    // 计算重心偏移
                    int dx = std::abs(old_r.x - new_r.x);
                    int dy = std::abs(old_r.y - new_r.y);

                    // 阈值判断：如果偏移极小，忽略抖动；否则执行加权平均逼近
                    if(dx < 2 && dy < 2)
                    {
                        new_r.x = old_r.x;
                        new_r.y = old_r.y;
                        new_r.w = old_r.w;
                        new_r.h = old_r.h;
                    }
                    else {
                        // 线性内插逻辑: Old * 0.7 + New * 0.3
                        new_r.x = old_r.x * 0.7 + new_r.x * 0.3;
                        new_r.y = old_r.y * 0.7 + new_r.y * 0.3;
                        new_r.w = old_r.w * 0.7 + new_r.w * 0.3;
                        new_r.h = old_r.h * 0.7 + new_r.h * 0.3;
                    }


                }
            }
            // 更新平滑状态
            m_smooth_results = current_results;
        }


        // 2. 绘制识别结果并统计

        int safe_cnt = 0;
        int alarm_cnt = 0;

        int frame_w = data.raw_frame.cols;
        int frame_h = data.raw_frame.rows;
        for (int i = 0; i < m_smooth_results.count; i++) {
            detect_result_t *res = &(m_smooth_results.results[i]);

            // 5 = Person, 2 = NO-Hardhat, 4 = NO-Safety Vest
            if (res->class_id == 5) continue;

            // 如果用户在界面上取消了勾选，这里直接跳过对应的类别（不画框、不报警、不记录）
            if ((res->class_id == 0 || res->class_id == 2) && !m_check_helmet.load()) continue;
            if ((res->class_id == 1 || res->class_id == 3) && !m_check_mask.load()) continue;
            if ((res->class_id == 4 || res->class_id == 7) && !m_check_vest.load()) continue;

            // 判断是否为违规
            bool is_violating = (res->class_id == 2 || res->class_id == 3 || res->class_id == 4);
            cv::Scalar color = is_violating ? cv::Scalar(0,0,255) : cv::Scalar(0,255,0);
            cv::Scalar color_bgr = is_violating ? cv::Scalar(0,0,255) : cv::Scalar(0,255,0);

            // 画框
            cv::rectangle(data.bgra_frame, cv::Rect(res->x, res->y, res->w, res->h), color, 4);
            cv::putText(data.bgra_frame, res->name, cv::Point(res->x, res->y - 10), cv::FONT_HERSHEY_SIMPLEX, 0.8, color, 2);
            cv::rectangle(data.raw_frame, cv::Rect(res->x, res->y, res->w, res->h), color_bgr, 4);
            cv::putText(data.raw_frame, res->name, cv::Point(res->x, res->y - 10), cv::FONT_HERSHEY_SIMPLEX, 0.8, color_bgr, 2);

            if(is_violating) {
                alarm_cnt ++;
            }else if(res->class_id == 0 || res->class_id == 7)
            {
                safe_cnt ++;
            }



        }
        // ===============================
        // 云台控制：选目标 + 死区 + 限幅
        // ===============================
        GimbalTarget target = selectGimbalTarget(m_smooth_results,frame_w,frame_h);
        if(target.found){
            float frame_center_x = frame_w / 2.0;
            float frame_center_y = frame_h / 2.0;

            float error_x = target.x - frame_center_x;
            float error_y = target.y - frame_center_y;

            // 死区：目标已经接近中心就不动，避免云台一直抖
            const float DEAD_ZONE_X = 45.0f;
            const float DEAD_ZONE_Y = 35.0f;

            if(std::abs(error_x) < DEAD_ZONE_X){
                error_x = 0.0f;
            }
            if(std::abs(error_y) < DEAD_ZONE_Y){
                error_y = 0.0f;
            }
            // 两个方向都在死区内，直接不发送云台指令
            if (error_x == 0.0f && error_y == 0.0f)
            {
                m_panPid.reset();
                m_tiltPid.reset();
            }else {
                long pan_delta = m_panPid.calculate(-error_x);
                long tilt_delta = m_tiltPid.calculate(error_y);
                // 每帧最大移动量限制，防止云台一次转太猛
                const long MAX_PAN_STEP = 8000;
                const long MAX_TILT_STEP = 5000;
                pan_delta = clampGimbalDelta(pan_delta,MAX_PAN_STEP);
                tilt_delta = clampGimbalDelta(tilt_delta,MAX_TILT_STEP);
                qDebug() << "SEND GIMBAL MOVE pan =" << pan_delta << "tilt =" << tilt_delta;
                emit requestGimbalMove(pan_delta,tilt_delta);
            }
        }else {
            m_panPid.reset();
            m_tiltPid.reset();
        }

        // 发送 UI 统计数据
        emit dataResult(safe_cnt, alarm_cnt);
        if(alarm_cnt > 0 && (frame_cnt++ % 30 == 0)) {
            emit alarmLog(QString("警告：警戒区内发现 %1 名未穿反光衣人员！").arg(alarm_cnt));
        }

        // ==========================================
        // 新增：计算并绘制 FPS
        // ==========================================
        fps_calc_frames++;
        if (fps_calc_frames == 10) {
            int64 end_time = cv::getTickCount();
            current_fps = cv::getTickFrequency() * 10.0 / (end_time - start_time);
            g_perf.fps = current_fps;

            start_time = end_time;
            fps_calc_frames = 0;
        }

        // 在画面的左上角写上黄色的 FPS 文字
        if (current_fps > 0) {
            QString fpsText = QString("FPS: %1").arg(current_fps, 0, 'f', 1);
            cv::putText(data.bgra_frame, fpsText.toStdString(), cv::Point(20, 40),
                        cv::FONT_HERSHEY_SIMPLEX, 1.0, cv::Scalar(0, 255, 255, 255), 2);
            cv::putText(data.raw_frame, fpsText.toStdString(), cv::Point(20, 40),
                        cv::FONT_HERSHEY_SIMPLEX, 1.0, cv::Scalar(0, 255, 255), 2);


        }
        // ==========================================

        // 3. 发送给 Qt UI 显示
        QImage img((const uchar*)data.bgra_frame.data, data.bgra_frame.cols, data.bgra_frame.rows, data.bgra_frame.step, QImage::Format_ARGB32);
        emit frameReady(img.copy());
        /*
        // 4. JPEG 压缩与网络推流
        std::vector<uchar> buffer;
        cv::imencode(".jpg", data.bgra_frame, buffer, {cv::IMWRITE_JPEG_QUALITY, 80});
        QByteArray jpegData((const char*)buffer.data(), buffer.size()); */
        // 注意：这里建议传入绘完框的 data.bgra_frame (需要调整 Encoder 兼容 BGRA)
        // 或者传入 data.raw_frame (速度最快，但推流画面没框)

        // 在 postThreadFunc 循环内部
        if (data.raw_frame.empty() || data.raw_frame.channels() != 3) {
            qDebug() << "传递到 PostThread 的 raw_frame 格式错误！";
            continue;
        }
        if (!data.raw_frame.empty()) {
            int64_t t_mpp0 = nowMs();

            //std::vector<unsigned char> mpp_buffer = m_encoder->encode(data.raw_frame);
            std::vector<uchar> jpg_buffer;
            cv::imencode(".jpg", data.raw_frame, jpg_buffer, {cv::IMWRITE_JPEG_QUALITY, 80});

            int64_t t_mpp1 = nowMs();
            g_perf.mpp_ms = t_mpp1 - t_mpp0;

            if (jpg_buffer.empty()) {
                qDebug() << "JPEG encode failed, skip frame";
                continue;
            }

            QByteArray jpegData((const char*)jpg_buffer.data(), jpg_buffer.size());

            int64_t t_stream0 = nowMs();

            emit streamFrameReady(jpegData);

            if (data.capture_ts_ms > 0) {
                g_perf.total_latency_ms = nowMs() - data.capture_ts_ms;
            }

            printPerfEveryNFrames(30);

            int64_t t_stream1 = nowMs();
            g_perf.stream_ms = t_stream1 - t_stream0;
        }

    }
}
