// Verify the two halves of the shared tone renderer in main.c
// (`audio_play_notes`): the magic-circle oscillator that replaced sinf(), and
// the per-note envelope clamp that lets a short chime ask for a tail longer
// than its own note.
//
// Only the `s` state variable is emitted as audio, so that is what gets
// measured: its pitch, and whether its peak amplitude drifts over the longest
// note any melody contains.
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
    // The alert tones reach higher than any melody does: the two-tone alarm
    // sits where the Tab5 speaker is actually efficient.
    698.46f, 880.00f,
};

// Exactly the clamp `audio_play_notes()` applies before rendering a note.
static void env_clamp(int note_samples, int attack_ms, int release_ms,
                      int *attack, int *release)
{
    *attack = SR * attack_ms / 1000;
    *release = SR * release_ms / 1000;
    if (*attack > note_samples / 4) *attack = note_samples / 4;
    if (*release > note_samples / 2) *release = note_samples / 2;
    if (*release < 2) *release = 2;
}

// ...and the envelope it then evaluates per sample.
static float env_at(int i, int note_samples, int attack, int release)
{
    if (i < attack) return (float)i / attack;
    if (i >= note_samples - release) {
        return (float)(note_samples - 1 - i) / (release - 1);
    }
    return 1.0f;
}

// Checks that hold for any note the renderer is asked to play.
static void check_envelope(const char *what, int note_ms, int attack_ms,
                           int release_ms, float gain)
{
    const int n = SR * note_ms / 1000;
    int attack = 0, release = 0;
    env_clamp(n, attack_ms, release_ms, &attack, &release);

    // The two ramps must never overlap: the release branch is an `else if`, so
    // an overlap would drop the note straight from the attack ramp onto the
    // decay and click.
    CHECK(attack + release <= n, "%s: attack %d + release %d exceeds %d samples",
          what, attack, release, n);
    CHECK(release >= 2, "%s: release collapsed to %d samples", what, release);

    float prev = 0.0f;
    for (int i = 0; i < n; i++) {
        float e = env_at(i, n, attack, release);
        CHECK(e >= 0.0f && e <= 1.0f, "%s: env %.4f out of range at %d", what, e, i);
        if (i < attack) {
            CHECK(e >= prev, "%s: attack ramp fell at %d", what, i);
        } else if (i >= n - release) {
            CHECK(e <= prev + 1e-6f, "%s: release ramp rose at %d", what, i);
        }
        prev = e;
        // The int16 conversion the firmware performs, at this call site's gain.
        double scaled = 1.001 * (double)e * gain * 32767.0;
        CHECK(scaled <= 32767.0 && scaled >= -32768.0,
              "%s: sample %d would wrap int16", what, i);
    }

    CHECK(env_at(0, n, attack, release) == 0.0f, "%s: does not start from silence", what);
    CHECK(env_at(n - 1, n, attack, release) == 0.0f, "%s: does not end in silence", what);
    // Handover point: the release ramp starts at exactly 1.0, so the sustain
    // and the decay meet without a step.
    CHECK(fabsf(env_at(n - release, n, attack, release) - 1.0f) < 1e-6f,
          "%s: release starts at %.6f, not 1.0", what,
          (double)env_at(n - release, n, attack, release));
}

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

    // --- envelope: every note length the boot melodies use, with the boot
    //     call site's 5 ms / 15 ms ramps. None of them is short enough to be
    //     clamped, so the melody sounds exactly as it did before the renderer
    //     was shared with the chime.
    const int boot_note_ms[] = { 100, 110, 120, 125, 150, 250, 280, 380, 500, 680 };
    for (size_t i = 0; i < sizeof(boot_note_ms) / sizeof(boot_note_ms[0]); i++) {
        int attack = 0, release = 0;
        env_clamp(SR * boot_note_ms[i] / 1000, 5, 15, &attack, &release);
        CHECK(attack == SR * 5 / 1000 && release == SR * 15 / 1000,
              "boot note %d ms: ramps clamped to %d/%d, melody would change",
              boot_note_ms[i], attack, release);
        check_envelope("boot melody note", boot_note_ms[i], 5, 15, 0.85f);
    }

    // --- and every note of the four alert tones, each with its own ramps.
    //     These are the ones that exercise the clamp: the join and win tones
    //     ask for a release twice as long as their opening note.
    check_envelope("join note 1", 70, 12, 140, 0.75f);
    check_envelope("join note 2", 190, 12, 140, 0.75f);
    check_envelope("win note 1", 70, 10, 130, 0.80f);
    check_envelope("win note 3", 230, 10, 130, 0.80f);
    check_envelope("warn note 1", 110, 14, 200, 0.80f);
    check_envelope("warn note 2", 260, 14, 200, 0.80f);
    check_envelope("alarm pulse", 110, 8, 60, 0.85f);
    check_envelope("alarm last", 150, 8, 60, 0.85f);

    if (failures == 0) {
        printf("oscillator: pitch within 1 cent, amplitude stable, no clipping\n");
        printf("envelope: ramps never overlap, boot melody notes unclamped\n");
        return 0;
    }
    printf("%d failure(s)\n", failures);
    return 1;
}
