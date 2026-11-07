#pragma once

#define FD_NUM_AXES 6

// Feature positions for acceleration and gyroscope magnitudes.
// The full feature order is preserved in features.cpp.
#define FD_IDX_ACC_SMV_MEAN 24
#define FD_IDX_ACC_SMV_STD 25
#define FD_IDX_ACC_SMV_MIN 26
#define FD_IDX_ACC_SMV_MAX 27
#define FD_IDX_GYR_SMV_MEAN 28
#define FD_IDX_GYR_SMV_STD 29

#ifdef __cplusplus
extern "C" {
#endif

// Convert a window of sensor samples into 32 model features.
// Each sample is ordered: ax, ay, az, gx, gy, gz.
void fd_extract_features(const float *samples, int n_samples, float *out);

// Return the highest acceleration magnitude in the window, in g.
float fd_peak_acc_g(const float *samples, int n_samples);

#ifdef __cplusplus
}
#endif