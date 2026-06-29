#ifndef POSTPROCESS_H
#define POSTPROCESS_H

#include <vector>
#include <opencv2/opencv.hpp>
#include "rknn_api.h"

#define OBJ_NAME_MAX_SIZE 32
#define OBJ_NUM_MAX_SIZE  64
#define OBJ_CLASS_NUM     2     // 修正为 2 类：reflective_clothes, other_clothes

typedef struct {
    int x, y, w, h;
    float score;
    int class_id;
    char name[OBJ_NAME_MAX_SIZE];
} detect_result_t;

typedef struct {
    int count;
    detect_result_t results[OBJ_NUM_MAX_SIZE];
} detect_result_group_t;

// 后处理主函数声明
int post_process(rknn_output* outputs, float conf_threshold, float nms_threshold,
                 detect_result_group_t* group,int img_w, int img_h);

#endif
