#include <math.h>
#include <stdint.h>

// code adopted from miniaudio
// https://raw.githubusercontent.com/mackron/miniaudio/master/miniaudio.h
// ma_hpf1_process_pcm_frame_s16, ma_lpf1_process_pcm_frame_s16, ma_hpf1_reinit

#ifndef MA_BIQUAD_FIXED_POINT_SHIFT
#define MA_BIQUAD_FIXED_POINT_SHIFT 14
#endif

int32_t filter_get_alpha(uint32_t sampling_freq, uint32_t cutoff_freq) {
    double a = exp(-2 * M_PI * cutoff_freq / sampling_freq);
    return (int32_t)(a * (1 << MA_BIQUAD_FIXED_POINT_SHIFT));
}

int16_t filter_highpass(int32_t alpha, int32_t *cap, int16_t sample) {
    const int32_t a = ((1 << MA_BIQUAD_FIXED_POINT_SHIFT) - alpha);
    const int32_t b = ((1 << MA_BIQUAD_FIXED_POINT_SHIFT) - a);
    int32_t x, y;

    x = sample;
    y = (b * x - a * *cap) >> MA_BIQUAD_FIXED_POINT_SHIFT;
    *cap = (int32_t)y;
    return (int16_t)y;
}

int16_t filter_lowpass(int32_t alpha, int32_t *cap, int16_t sample) {
    const int32_t a = alpha;
    const int32_t b = ((1 << MA_BIQUAD_FIXED_POINT_SHIFT) - a);
    int32_t x, y;

    x = sample;
    y = (b * x + a * *cap) >> MA_BIQUAD_FIXED_POINT_SHIFT;
    *cap = (int32_t)y;
    return (int16_t)y;
}
