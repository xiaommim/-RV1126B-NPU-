#include "postprocess_yolov8.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

static const char* labels[OBJ_CLASS_NUM] = {
    "Hardhat", "Mask", "NO-Hardhat", "NO-Mask", "NO-Safety Vest",
    "Person", "Safety Cone", "Safety Vest", "machinery", "vehicle"
};

static float calculate_iou(const detect_result_t& a, const detect_result_t& b)
{
    int x1 = std::max(a.x, b.x);
    int y1 = std::max(a.y, b.y);
    int x2 = std::min(a.x + a.w, b.x + b.w);
    int y2 = std::min(a.y + a.h, b.y + b.h);

    int inter_w = std::max(0, x2 - x1);
    int inter_h = std::max(0, y2 - y1);
    int inter_area = inter_w * inter_h;

    int area_a = a.w * a.h;
    int area_b = b.w * b.h;
    int union_area = area_a + area_b - inter_area;

    return union_area <= 0 ? 0.0f : (float)inter_area / (float)union_area;
}

static float dfl_value(const float* box_data, int grid_len, int offset, int side)
{
    const int DFL_LEN = 16;
    float max_v = -1e30f;

    for (int k = 0; k < DFL_LEN; k++) {
        float v = box_data[(side * DFL_LEN + k) * grid_len + offset];
        if (v > max_v) max_v = v;
    }

    float exp_sum = 0.0f;
    float acc = 0.0f;

    for (int k = 0; k < DFL_LEN; k++) {
        float v = std::exp(box_data[(side * DFL_LEN + k) * grid_len + offset] - max_v);
        exp_sum += v;
        acc += v * k;
    }

    if (exp_sum <= 0.0f) return 0.0f;
    return acc / exp_sum;
}

static void process_branch(float* box_data,
                           float* cls_data,
                           float* score_sum_data,
                           int grid_h,
                           int grid_w,
                           int stride,
                           float conf_threshold,
                           int img_w,
                           int img_h,
                           std::vector<detect_result_t>& detect_list)
{
    const int grid_len = grid_h * grid_w;

    float r = std::min((float)MODEL_INPUT_SIZE / img_w,
                       (float)MODEL_INPUT_SIZE / img_h);
    float new_w = img_w * r;
    float new_h = img_h * r;
    float pad_x = ((float)MODEL_INPUT_SIZE - new_w) / 2.0f;
    float pad_y = ((float)MODEL_INPUT_SIZE - new_h) / 2.0f;

    for (int y = 0; y < grid_h; y++) {
        for (int x = 0; x < grid_w; x++) {
            int offset = y * grid_w + x;

            if (score_sum_data && score_sum_data[offset] < conf_threshold) {
                continue;
            }

            float max_score = -1e30f;
            int class_id = -1;

            for (int c = 0; c < OBJ_CLASS_NUM; c++) {
                float score = cls_data[c * grid_len + offset];
                if (score > max_score) {
                    max_score = score;
                    class_id = c;
                }
            }

            if (class_id < 0 || max_score < conf_threshold) {
                continue;
            }

            float l = dfl_value(box_data, grid_len, offset, 0);
            float t = dfl_value(box_data, grid_len, offset, 1);
            float rgt = dfl_value(box_data, grid_len, offset, 2);
            float b = dfl_value(box_data, grid_len, offset, 3);

            float x1f = (x + 0.5f - l) * stride;
            float y1f = (y + 0.5f - t) * stride;
            float x2f = (x + 0.5f + rgt) * stride;
            float y2f = (y + 0.5f + b) * stride;

            x1f = (x1f - pad_x) / r;
            y1f = (y1f - pad_y) / r;
            x2f = (x2f - pad_x) / r;
            y2f = (y2f - pad_y) / r;

            if (x2f <= x1f || y2f <= y1f) continue;
            if (x2f <= 0 || y2f <= 0 || x1f >= img_w || y1f >= img_h) continue;

            int x1 = std::max(0, std::min((int)x1f, img_w - 1));
            int y1i = std::max(0, std::min((int)y1f, img_h - 1));
            int x2 = std::max(0, std::min((int)x2f, img_w - 1));
            int y2 = std::max(0, std::min((int)y2f, img_h - 1));

            int w = x2 - x1;
            int h = y2 - y1i;

            if (w <= 10 || h <= 10) {
                continue;
            }

            detect_result_t res;
            memset(&res, 0, sizeof(res));
            res.x = x1;
            res.y = y1i;
            res.w = w;
            res.h = h;
            res.score = max_score;
            res.class_id = class_id;

            strncpy(res.name, labels[class_id], OBJ_NAME_MAX_SIZE - 1);
            res.name[OBJ_NAME_MAX_SIZE - 1] = '\0';

            detect_list.push_back(res);
        }
    }
}

int post_process_yolov8(rknn_output* outputs,
                        float conf_threshold,
                        float nms_threshold,
                        detect_result_group_t* group,
                        int img_w,
                        int img_h)
{
    group->count = 0;
    std::vector<detect_result_t> detect_list;

    // RKOPT YOLOv8 416 input:
    // 0 box [1,64,52,52], 1 cls [1,10,52,52], 2 score_sum [1,1,52,52]
    // 3 box [1,64,26,26], 4 cls [1,10,26,26], 5 score_sum [1,1,26,26]
    // 6 box [1,64,13,13], 7 cls [1,10,13,13], 8 score_sum [1,1,13,13]
    process_branch((float*)outputs[0].buf, (float*)outputs[1].buf, (float*)outputs[2].buf,
                   52, 52, 8, conf_threshold, img_w, img_h, detect_list);

    process_branch((float*)outputs[3].buf, (float*)outputs[4].buf, (float*)outputs[5].buf,
                   26, 26, 16, conf_threshold, img_w, img_h, detect_list);

    process_branch((float*)outputs[6].buf, (float*)outputs[7].buf, (float*)outputs[8].buf,
                   13, 13, 32, conf_threshold, img_w, img_h, detect_list);

    std::sort(detect_list.begin(), detect_list.end(),
              [](const detect_result_t& a, const detect_result_t& b) {
                  return a.score > b.score;
              });

    std::vector<bool> suppressed(detect_list.size(), false);

    for (size_t i = 0; i < detect_list.size(); i++) {
        if (suppressed[i]) continue;

        if (group->count < OBJ_NUM_MAX_SIZE) {
            group->results[group->count++] = detect_list[i];
        }

        for (size_t j = i + 1; j < detect_list.size(); j++) {
            if (suppressed[j]) continue;
            if (detect_list[i].class_id != detect_list[j].class_id) continue;

            if (calculate_iou(detect_list[i], detect_list[j]) > nms_threshold) {
                suppressed[j] = true;
            }
        }
    }

    return 0;
}
