#ifndef MPP_ENCODER_H
#define MPP_ENCODER_H

// 引入瑞芯微 MPP 核心头文件
#include <rockchip/rk_mpi.h>      // MPP 接口定义，包含 mpp_create, mpp_init 等
#include <rockchip/mpp_frame.h>   // 视频帧定义（输入端）
#include <rockchip/mpp_packet.h>  // 数据包定义（输出端）
#include <opencv2/opencv.hpp>     // 用于处理输入图像矩阵
#include <vector>

/**
 * @brief MPP 硬件编码器包装类
 * 专门负责利用 RK3568 的 VPU 硬件单元将原始图像压缩为 JPEG 格式
 */
class MPPEncoder {
public:
    // 构造函数：初始化硬件环境，设定编码分辨率
    MPPEncoder(int width, int height);

    // 析构函数：负责释放 MPP 上下文和硬件缓冲区，防止内存泄漏
    ~MPPEncoder();

    /**
     * @brief 执行编码
     * @param bgr_frame OpenCV 捕获的原始 BGR 格式图像
     * @return 编码后的 JPEG 数据二进制流
     */
    std::vector<unsigned char> encode(const cv::Mat& bgr_frame);

private:
    MppCtx m_ctx = nullptr;       // MPP 任务上下文句柄，存储编码器的所有状态
    MppApi* m_api = nullptr;      // MPP 函数 Api 指针集合，通过它调用底层硬件
    MppBufferGroup m_buf_group = nullptr; // 硬件内存池，用于管理物理连续内存
    int m_width;                  // 图像宽度
    int m_height;                 // 图像高度
    size_t m_frame_size;          // 单帧图像占用的内存大小
    bool m_ready = false;
};

#endif
