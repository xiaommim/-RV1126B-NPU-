#include "MPPEncoder.h"
#include "im2d.h"
#include "rga.h"

#include <cstdio>

MPPEncoder::MPPEncoder(int width, int height)
    : m_width(width), m_height(height)
{
    MPP_RET ret = MPP_OK;

    ret = mpp_create(&m_ctx, &m_api);
    if (ret != MPP_OK || !m_ctx || !m_api) {
        fprintf(stderr, "mpp_create failed: %d\n", ret);
        return;
    }

    ret = mpp_init(m_ctx, MPP_CTX_ENC, MPP_VIDEO_CodingMJPEG);
    if (ret != MPP_OK) {
        fprintf(stderr, "mpp_init failed: %d\n", ret);
        return;
    }

    MppPollType timeout = MPP_POLL_BLOCK;
    ret = m_api->control(m_ctx, MPP_SET_OUTPUT_TIMEOUT, &timeout);
    if (ret != MPP_OK) {
        fprintf(stderr, "MPP_SET_OUTPUT_TIMEOUT failed: %d\n", ret);
        return;
    }

    MppEncCfg cfg = nullptr;
    ret = mpp_enc_cfg_init(&cfg);
    if (ret != MPP_OK || !cfg) {
        fprintf(stderr, "mpp_enc_cfg_init failed: %d\n", ret);
        return;
    }

    ret = m_api->control(m_ctx, MPP_ENC_GET_CFG, cfg);
    if (ret != MPP_OK) {
        fprintf(stderr, "MPP_ENC_GET_CFG failed: %d\n", ret);
        mpp_enc_cfg_deinit(cfg);
        return;
    }

    mpp_enc_cfg_set_s32(cfg, "prep:width", m_width);
    mpp_enc_cfg_set_s32(cfg, "prep:height", m_height);
    mpp_enc_cfg_set_s32(cfg, "prep:hor_stride", m_width);
    mpp_enc_cfg_set_s32(cfg, "prep:ver_stride", m_height);
    mpp_enc_cfg_set_s32(cfg, "prep:format", MPP_FMT_YUV420P);

    mpp_enc_cfg_set_s32(cfg, "codec:type", MPP_VIDEO_CodingMJPEG);

    mpp_enc_cfg_set_s32(cfg, "rc:mode", MPP_ENC_RC_MODE_CBR);
    mpp_enc_cfg_set_s32(cfg, "rc:bps_target", m_width * m_height * 8);
    mpp_enc_cfg_set_s32(cfg, "rc:bps_max", m_width * m_height * 8 * 17 / 16);
    mpp_enc_cfg_set_s32(cfg, "rc:bps_min", m_width * m_height * 8 * 15 / 16);
    mpp_enc_cfg_set_s32(cfg, "rc:fps_in_flex", 0);
    mpp_enc_cfg_set_s32(cfg, "rc:fps_in_num", 30);
    mpp_enc_cfg_set_s32(cfg, "rc:fps_in_denorm", 1);
    mpp_enc_cfg_set_s32(cfg, "rc:fps_out_num", 30);
    mpp_enc_cfg_set_s32(cfg, "rc:fps_out_denorm", 1);
    mpp_enc_cfg_set_s32(cfg, "rc:gop", 30);

    ret = m_api->control(m_ctx, MPP_ENC_SET_CFG, cfg);
    mpp_enc_cfg_deinit(cfg);

    if (ret != MPP_OK) {
        fprintf(stderr, "MPP_ENC_SET_CFG failed: %d\n", ret);
        return;
    }

    ret = mpp_buffer_group_get_internal(&m_buf_group, MPP_BUFFER_TYPE_ION);
    if (ret != MPP_OK || !m_buf_group) {
        fprintf(stderr, "mpp_buffer_group_get_internal failed: %d\n", ret);
        return;
    }

    m_ready = true;
    fprintf(stderr, "MPPEncoder init success: %dx%d MJPEG\n", m_width, m_height);
}

MPPEncoder::~MPPEncoder()
{
    if (m_buf_group) {
        mpp_buffer_group_put(m_buf_group);
        m_buf_group = nullptr;
    }

    if (m_ctx) {
        mpp_destroy(m_ctx);
        m_ctx = nullptr;
    }
}

std::vector<unsigned char> MPPEncoder::encode(const cv::Mat& bgr_frame)
{
    if (!m_ready || !m_ctx || !m_api || !m_buf_group) {
        fprintf(stderr, "MPPEncoder is not ready, skip encode\n");
        return {};
    }

    if (bgr_frame.empty()) {
        return {};
    }

    cv::Mat input;
    if (bgr_frame.cols != m_width || bgr_frame.rows != m_height) {
        cv::resize(bgr_frame, input, cv::Size(m_width, m_height));
    } else {
        input = bgr_frame;
    }

    MppFrame frame = nullptr;
    MPP_RET ret = mpp_frame_init(&frame);
    if (ret != MPP_OK || !frame) {
        fprintf(stderr, "mpp_frame_init failed: %d\n", ret);
        return {};
    }

    mpp_frame_set_width(frame, m_width);
    mpp_frame_set_height(frame, m_height);
    mpp_frame_set_hor_stride(frame, m_width);
    mpp_frame_set_ver_stride(frame, m_height);
    mpp_frame_set_fmt(frame, MPP_FMT_YUV420P);

    MppBuffer buffer = nullptr;
    size_t data_size = m_width * m_height * 3 / 2;

    ret = mpp_buffer_get(m_buf_group, &buffer, data_size);
    if (ret != MPP_OK || !buffer) {
        fprintf(stderr, "mpp_buffer_get failed: %d\n", ret);
        mpp_frame_deinit(&frame);
        return {};
    }

    void* hardware_ptr = mpp_buffer_get_ptr(buffer);
    if (!hardware_ptr) {
        fprintf(stderr, "mpp_buffer_get_ptr failed\n");
        mpp_buffer_put(buffer);
        mpp_frame_deinit(&frame);
        return {};
    }

    cv::Mat yuv_hardware_mat(m_height * 3 / 2, m_width, CV_8UC1, hardware_ptr);
    cv::cvtColor(input, yuv_hardware_mat, cv::COLOR_BGR2YUV_I420);

    mpp_frame_set_buffer(frame, buffer);

    ret = m_api->encode_put_frame(m_ctx, frame);
    if (ret != MPP_OK) {
        fprintf(stderr, "encode_put_frame failed: %d\n", ret);
        mpp_buffer_put(buffer);
        mpp_frame_deinit(&frame);
        return {};
    }

    MppPacket packet = nullptr;
    ret = m_api->encode_get_packet(m_ctx, &packet);
    if (ret != MPP_OK) {
        fprintf(stderr, "encode_get_packet failed: %d\n", ret);
        mpp_buffer_put(buffer);
        mpp_frame_deinit(&frame);
        return {};
    }

    std::vector<unsigned char> result;
    if (packet) {
        unsigned char* data = static_cast<unsigned char*>(mpp_packet_get_pos(packet));
        size_t len = mpp_packet_get_length(packet);

        if (data && len > 0) {
            result.assign(data, data + len);
        }

        mpp_packet_deinit(&packet);
    }

    mpp_buffer_put(buffer);
    mpp_frame_deinit(&frame);

    return result;
}
