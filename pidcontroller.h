#ifndef PIDCONTROLLER_H
#define PIDCONTROLLER_H

struct SimplePid {
    float Kp = 15.0f;  //比例系数
    float Kd = 5.0f;  // 微分系数
    float Ki = 1.0f;   //积分系数
    
    float integral = 0.0f;
    float prev_error = 0.0f;
    
    float max_integral = 2000.0f;
    long calculate(float error){
        
        //看多远，踩多深
        float Pout = Kp * error;
        
        //微分项 D (Derivative) —— “提前预判，踩刹车”
        float derivative = error - prev_error;
        float Dout = derivative * Kd;
        
        // 积分项 I (Integral) —— “死磕静态误差”
        integral += error;
        if(integral > max_integral) integral = max_integral;
        if(integral < -max_integral) integral = -max_integral;
        float Iout = Ki * integral;
        
        prev_error = error;
        
        return static_cast<long>(Pout + Iout + Dout);
    }
    void reset() {
        integral = 0.0f;
        prev_error = 0.0f;
    }
};


#endif // PIDCONTROLLER_H
