#include "postprocess.h"
#include <math.h>
#include <algorithm>
#include <vector>
#include <string.h>

const char* labels[] = {"reflective_clothes", "other_clothes"};

static float sigmoid(float x) { return 1.0f / (1.0f + expf(-x)); }

static float calculate_iou(detect_result_t res1, detect_result_t res2) {
    int x1 = std::max(res1.x, res2.x);
    int y1 = std::max(res1.y, res2.y);
    int x2 = std::min(res1.x + res1.w, res2.x + res2.w);
    int y2 = std::min(res1.y + res1.h, res2.y + res2.h);
    int intersection_area = std::max(0, x2 - x1) * std::max(0, y2 - y1);
    int union_area = res1.w * res1.h + res2.w * res2.h - intersection_area;
    return union_area <= 0 ? 0 : (float)intersection_area / union_area;
}

int post_process(rknn_output* outputs, float conf_threshold, float nms_threshold,
                 detect_result_group_t* group, int img_w, int img_h) {
    group->count = 0;
    std::vector<detect_result_t> detect_list;

    // 恢复正常阈值
    conf_threshold = 0.50f;
    nms_threshold = 0.30f;

    float scale_w = (float)img_w / 640.0f;
    float scale_h = (float)img_h / 640.0f;

    for (int i = 0; i < 3; i++) {
        // 对齐 Python 跑通的特征图顺序
        int grid_h = (i == 0) ? 20 : (i == 1 ? 40 : 80);
        int grid_w = grid_h;
        int stride = 640 / grid_h;
        int area = grid_h * grid_w; // 单个通道的面积

        // 动态分配对应的 Anchors
        int cur_anchors[3][2];
        if (grid_h == 20) {
            cur_anchors[0][0] = 116; cur_anchors[0][1] = 90;
            cur_anchors[1][0] = 156; cur_anchors[1][1] = 198;
            cur_anchors[2][0] = 373; cur_anchors[2][1] = 326;
        } else if (grid_h == 40) {
            cur_anchors[0][0] = 30;  cur_anchors[0][1] = 61;
            cur_anchors[1][0] = 62;  cur_anchors[1][1] = 45;
            cur_anchors[2][0] = 59;  cur_anchors[2][1] = 119;
        } else { // 80x80
            cur_anchors[0][0] = 10;  cur_anchors[0][1] = 13;
            cur_anchors[1][0] = 16;  cur_anchors[1][1] = 30;
            cur_anchors[2][0] = 33;  cur_anchors[2][1] = 23;
        }

        float* data = (float*)outputs[i].buf;
        int prop_len = 5 + OBJ_CLASS_NUM; // 7

        for (int a = 0; a < 3; a++) {
            for (int h = 0; h < grid_h; h++) {
                for (int w = 0; w < grid_w; w++) {
                    // 核心修正：匹配 Python 的 [Anchor][Channel][H][W] 内存排布
                    int base_idx = a * (prop_len * area) + h * grid_w + w;

                    // 取置信度 (Channel 4)
                    float box_conf = sigmoid(data[base_idx + 4 * area]);

                    if (box_conf > conf_threshold) {
                        float max_class_score = 0;
                        int max_class_id = -1;

                        // 取类别得分 (Channel 5 和 6)
                        for (int c = 0; c < OBJ_CLASS_NUM; c++) {
                            float s = sigmoid(data[base_idx + (5 + c) * area]);
                            if (s > max_class_score) {
                                max_class_score = s;
                                max_class_id = c;
                            }
                        }

                        if (max_class_score * box_conf > conf_threshold) {
                            // 取 X, Y, W, H (Channel 0, 1, 2, 3)
                            float bcx = (sigmoid(data[base_idx + 0 * area]) * 2.0f - 0.5f + w) * stride;
                            float bcy = (sigmoid(data[base_idx + 1 * area]) * 2.0f - 0.5f + h) * stride;
                            float bw = powf(sigmoid(data[base_idx + 2 * area]) * 2.0f, 2.0f) * cur_anchors[a][0];
                            float bh = powf(sigmoid(data[base_idx + 3 * area]) * 2.0f, 2.0f) * cur_anchors[a][1];

                            detect_result_t res;
                            res.x = (int)((bcx - bw / 2.0f) * scale_w);
                            res.y = (int)((bcy - bh / 2.0f) * scale_h);
                            res.w = (int)(bw * scale_w);
                            res.h = (int)(bh * scale_h);
                            res.score = box_conf * max_class_score;
                            res.class_id = max_class_id;
                            strncpy(res.name, labels[max_class_id], OBJ_NAME_MAX_SIZE);
                            detect_list.push_back(res);
                        }
                    }
                }
            }
        }
    }

    // --- 执行跨类别 NMS 算法 ---
    std::sort(detect_list.begin(), detect_list.end(), [](const detect_result_t& a, const detect_result_t& b) {
        return a.score > b.score;
    });

    std::vector<bool> is_suppressed(detect_list.size(), false);
    for (size_t i = 0; i < detect_list.size(); i++) {
        if (is_suppressed[i]) continue;

        if (group->count < OBJ_NUM_MAX_SIZE) {
            group->results[group->count++] = detect_list[i];
        }

        for (size_t j = i + 1; j < detect_list.size(); j++) {
            if (is_suppressed[j]) continue;
            // 只要重叠度高，统统抑制
            if (calculate_iou(detect_list[i], detect_list[j]) > nms_threshold) {
                is_suppressed[j] = true;
            }
        }
    }
    return 0;
}
