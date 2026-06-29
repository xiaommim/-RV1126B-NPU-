#ifndef POSTPROCESS_YOLOV8_H
#define POSTPROCESS_YOLOV8_H

#include <vector>
#include <opencv2/opencv.hpp>
#include "rknn_api.h"

#define OBJ_NAME_MAX_SIZE 32
#define OBJ_NUM_MAX_SIZE 64
#define OBJ_CLASS_NUM 10       // YOLOv8 PPE 类别
#define MODEL_INPUT_SIZE 416   // 模型输入尺寸

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

// YOLOv8 PPE 后处理函数
int post_process_yolov8(rknn_output* outputs,
                        float conf_threshold,
                        float nms_threshold,
                        detect_result_group_t* group,
                        int img_w,
                        int img_h);

#endif
