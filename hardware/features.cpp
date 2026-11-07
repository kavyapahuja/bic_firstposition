#include "features.h"

#include <math.h>

// Calculate mean, population standard deviation, minimum, and maximum
// for one sensor axis.
static void fd_stats(const float *signal, int n, int stride, float *out) {
    float sum = 0.0f;
    float min_value = signal[0];
    float max_value = signal[0];

    for (int i = 0; i < n; i++) {
        const float value = signal[i * stride];
        sum += value;

        if (value < min_value) {
            min_value = value;
        }

        if (value > max_value) {
            max_value = value;
        }
    }

    const float mean = sum / (float)n;

    float sum_sq = 0.0f;
    for (int i = 0; i < n; i++) {
        const float diff = signal[i * stride] - mean;
        sum_sq += diff * diff;
    }

    out[0] = mean;
    out[1] = sqrtf(sum_sq / (float)n);
    out[2] = min_value;
    out[3] = max_value;
}

// Calculate the 3D magnitude from three values in one sensor sample.
static float fd_magnitude(const float *row, int offset) {
    const float x = row[offset];
    const float y = row[offset + 1];
    const float z = row[offset + 2];

    return sqrtf(x * x + y * y + z * z);
}

// Extract 32 features from a window of sensor samples.
void fd_extract_features(const float *samples, int n_samples, float *out) {
    int index = 0;

    // First 24 features: four statistics for each of the six axes.
    for (int axis = 0; axis < FD_NUM_AXES; axis++) {
        fd_stats(samples + axis, n_samples, FD_NUM_AXES, out + index);
        index += 4;
    }

    // Next 4 features: acceleration magnitude statistics.
    // Final 4 features: gyroscope magnitude statistics.
    for (int block = 0; block < 2; block++) {
        const int offset = block * 3;
        float sum = 0.0f;
        float min_value = 0.0f;
        float max_value = 0.0f;

        for (int i = 0; i < n_samples; i++) {
            const float value =
                fd_magnitude(samples + i * FD_NUM_AXES, offset);

            sum += value;

            if (i == 0 || value < min_value) {
                min_value = value;
            }

            if (i == 0 || value > max_value) {
                max_value = value;
            }
        }

        const float mean = sum / (float)n_samples;

        float sum_sq = 0.0f;
        for (int i = 0; i < n_samples; i++) {
            const float diff =
                fd_magnitude(samples + i * FD_NUM_AXES, offset) - mean;
            sum_sq += diff * diff;
        }

        out[index++] = mean;
        out[index++] = sqrtf(sum_sq / (float)n_samples);
        out[index++] = min_value;
        out[index++] = max_value;
    }
}

// Return the highest acceleration magnitude in the window, in g.
float fd_peak_acc_g(const float *samples, int n_samples) {
    float peak = 0.0f;

    for (int i = 0; i < n_samples; i++) {
        const float value =
            fd_magnitude(samples + i * FD_NUM_AXES, 0);

        if (i == 0 || value > peak) {
            peak = value;
        }
    }

    return peak;
}