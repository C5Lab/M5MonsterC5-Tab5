// Verify the magic-circle oscillator is a safe replacement for sinf() in the
// boot melody. Only the `s` state variable is emitted as audio, so that is what
// gets measured: its pitch, and whether its peak amplitude drifts over the
// longest note any melody contains.
#define _USE_MATH_DEFINES
#include <math.h>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#include <stdio.h>
#include <stdlib.h>

#define SR 48000

static int failures = 0;
#define CHECK(c, ...) do { if(!(c)){ printf("FAIL: "); printf(__VA_ARGS__); \
                           printf("\n"); failures++; } } while (0)

// Every distinct frequency used by the three melodies.
static const float freqs[] = {
    277.18f, 293.66f, 329.63f, 349.23f, 369.99f, 415.30f, 440.00f, 466.16f,
    493.88f, 523.25f, 554.37f, 587.33f, 622.25f, 659.25f,
};

int main(void)
{
    // The longest note in any melody is 680 ms; check past it.
    const int n = SR * 700 / 1000;
    float *out = malloc((size_t)n * sizeof(float));
    if (!out) return 2;

    for (size_t f = 0; f < sizeof(freqs) / sizeof(freqs[0]); f++) {
        const float freq = freqs[f];

        // Exactly the recurrence used in play_startup_beep().
        const float k = 2.0f * sinf((float)M_PI * freq / SR);
        float c = 1.0f, s = 0.0f;
        for (int i = 0; i < n; i++) {
            c -= k * s;
            s += k * c;
            out[i] = s;
        }

        // --- pitch: sub-sample interpolated rising zero crossings ---
        double first_x = -1.0, last_x = -1.0;
        int crossings = 0;
        for (int i = 1; i < n; i++) {
            if (out[i - 1] < 0.0f && out[i] >= 0.0f) {
                double frac = (double)(-out[i - 1]) / (double)(out[i] - out[i - 1]);
                double x = (i - 1) + frac;
                if (first_x < 0.0) first_x = x;
                last_x = x;
                crossings++;
            }
        }
        CHECK(crossings > 100, "%.2f Hz: only %d crossings", freq, crossings);
        if (crossings > 1) {
            double period = (last_x - first_x) / (crossings - 1);
            double measured = (double)SR / period;
            double cents = 1200.0 * log2(measured / freq);
            CHECK(fabs(cents) < 1.0,
                  "%.2f Hz: measured %.4f Hz (%.3f cents off)", freq, measured, cents);
        }

        // --- stability: peak amplitude at the start vs at the end ---
        int window = SR / 20;                 // 50 ms
        double peak_head = 0.0, peak_tail = 0.0;
        for (int i = 0; i < window; i++) {
            double a = fabs((double)out[i]);
            if (a > peak_head) peak_head = a;
        }
        for (int i = n - window; i < n; i++) {
            double a = fabs((double)out[i]);
            if (a > peak_tail) peak_tail = a;
        }
        double drift = fabs(peak_tail - peak_head) / peak_head;
        CHECK(drift < 0.005,
              "%.2f Hz: amplitude drifted %.3f%% over 700 ms (%.5f -> %.5f)",
              freq, drift * 100.0, peak_head, peak_tail);

        // --- headroom: reproduce the exact int16 conversion the firmware does,
        //     including its 0.85 gain, and require that nothing wraps. The
        //     recurrence overshoots unity by <0.1%, which the gain absorbs.
        int wrapped = 0;
        double peak_all = 0.0;
        for (int i = 0; i < n; i++) {
            double a = fabs((double)out[i]);
            if (a > peak_all) peak_all = a;
            double scaled = (double)out[i] * 0.85 * 32767.0;
            if (scaled > 32767.0 || scaled < -32768.0) wrapped++;
        }
        CHECK(wrapped == 0, "%.2f Hz: %d samples would wrap int16", freq, wrapped);
        CHECK(peak_all < 1.01, "%.2f Hz: peak %.5f drifted too far above unity",
              freq, peak_all);
    }

    free(out);
    if (failures == 0) {
        printf("oscillator: pitch within 1 cent, amplitude stable, no clipping\n");
        return 0;
    }
    printf("%d failure(s)\n", failures);
    return 1;
}
