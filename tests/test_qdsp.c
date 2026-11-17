/*
 * qdsp — unit tests (host).
 *
 * Библиотека header-only, поэтому проверяется просто компиляцией этого файла:
 *   cc -O2 -std=gnu99 -Wall -Wextra -I include tests/test_qdsp.c -lm -o test_qdsp
 *   ./test_qdsp
 * Или через CMake: cmake -S . -B build && cmake --build build && ctest --test-dir build
 *
 * Всё считается в Q15: 1.0 == 32767 (Q15_ONE), значения в диапазоне [-32768, 32767].
 */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "qdsp.h"

static int failures;

#define CHECK(cond, msg)                                                        \
    do {                                                                        \
        if (!(cond)) {                                                          \
            printf("  FAIL  %s  (%s:%d)\n", (msg), __FILE__, __LINE__);         \
            failures++;                                                         \
        }                                                                       \
    } while (0)

#define CHECK_NEAR(actual, expected, tol, msg)                                  \
    do {                                                                        \
        long _a = (long)(actual), _e = (long)(expected);                        \
        if (labs(_a - _e) > (long)(tol)) {                                      \
            printf("  FAIL  %s: получено %ld, ожидалось %ld (допуск %ld)  (%s:%d)\n", \
                   (msg), _a, _e, (long)(tol), __FILE__, __LINE__);             \
            failures++;                                                         \
        }                                                                       \
    } while (0)

/* ── скалярная арифметика ─────────────────────────────────────────── */

static void test_saturation(void)
{
    CHECK(q15_sat(40000) == Q15_ONE, "q15_sat выше 1.0 обрезается до Q15_ONE");
    CHECK(q15_sat(-40000) == ((q15_t)-32768), "q15_sat ниже -1.0 обрезается до ((q15_t)-32768)");
    CHECK(q15_sat(1234) == 1234, "q15_sat не трогает значения в диапазоне");
    CHECK_NEAR(q15_add(30000, 30000), Q15_ONE, 1, "q15_add насыщается сверху");
    CHECK_NEAR(q15_sub(-30000, 30000), ((q15_t)-32768), 1, "q15_sub насыщается снизу");
}

static void test_multiplication(void)
{
    /* 0.5 * 0.5 = 0.25 */
    CHECK_NEAR(q15_mul(16384, 16384), 8192, 2, "0.5 × 0.5 = 0.25");
    /* 1.0 * 1.0 ≈ 1.0 */
    CHECK_NEAR(q15_mul(Q15_ONE, Q15_ONE), Q15_ONE, 2, "1.0 × 1.0 ≈ 1.0");
    /* знаки */
    CHECK_NEAR(q15_mul(-16384, 16384), -8192, 2, "-0.5 × 0.5 = -0.25");
    CHECK_NEAR(q15_mul(-16384, -16384), 8192, 2, "-0.5 × -0.5 = 0.25");
    CHECK(q15_mul(0, 32767) == 0, "0 × x = 0");
}

static void test_abs(void)
{
    CHECK(q15_abs(-16384) == 16384, "|−0.5| = 0.5");
    CHECK(q15_abs(16384) == 16384, "|0.5| = 0.5");
    CHECK(q15_abs(0) == 0, "|0| = 0");
    CHECK(q15_abs(((q15_t)-32768)) == Q15_ONE, "|−1.0| насыщается до 1.0 (не переполняется)");
}

/* ── биквад ───────────────────────────────────────────────────────── */

static void test_biquad_passthrough(void)
{
    /* b0 = 1.0, остальные нули: фильтр не должен менять сигнал */
    qdsp_biquad_t f = {Q15_ONE, 0, 0, 0, 0, 0, 0};
    static const q15_t input[] = {1000, -2000, 3000, -4000, 5000};
    for (int i = 0; i < 5; i++) {
        q15_t y = qdsp_biquad_process(&f, input[i]);
        CHECK_NEAR(y, input[i], 2, "биквад с b0=1.0 не меняет сигнал");
    }
}

static void test_biquad_averaging(void)
{
    /* b0 = b1 = b2 = 1/3: скользящее среднее трёх отсчётов */
    const q15_t third = (q15_t)(Q15_ONE / 3);
    qdsp_biquad_t f = {third, third, third, 0, 0, 0, 0};
    qdsp_biquad_reset(&f);
    for (int i = 0; i < 10; i++) qdsp_biquad_process(&f, 3000);
    q15_t y = qdsp_biquad_process(&f, 3000);
    CHECK_NEAR(y, 3000, 12, "на постоянном входе среднее равно входу");
}

/* ── one-pole ─────────────────────────────────────────────────────── */

static void test_onepole_converges(void)
{
    qdsp_onepole_t f = {16384, 0}; /* coef = 0.5 */
    q15_t prev = 0;
    for (int i = 0; i < 40; i++) {
        q15_t y = qdsp_onepole_process(&f, Q15_ONE / 2);
        CHECK(y >= prev, "реакция на ступеньку монотонно растёт");
        prev = y;
    }
    CHECK_NEAR(prev, Q15_ONE / 2, 120, "one-pole доходит до уровня ступеньки");
}

static void test_onepole_step_response(void)
{
    qdsp_onepole_t f = {16384, 0};
    q15_t y1 = qdsp_onepole_process(&f, 10000);
    CHECK_NEAR(y1, 5000, 60, "первый отсчёт: половина ступеньки при coef=0.5");
    q15_t y2 = qdsp_onepole_process(&f, 10000);
    CHECK_NEAR(y2, 7500, 60, "второй отсчёт: три четверти");
}

/* ── Гёрцель: обнаружение бина ────────────────────────────────────── */

static q15_t goertzel_power_for(int N, int k, int freq_bin, int amplitude)
{
    qdsp_goertzel_t g;
    g.coeff = (int32_t)lround(2.0 * cos(2.0 * M_PI * (double)k / (double)N) * (double)Q15_ONE);
    qdsp_goertzel_reset(&g);
    for (int n = 0; n < N; n++) {
        double ph = 2.0 * M_PI * (double)freq_bin * (double)n / (double)N;
        q15_t x = (q15_t)lround((double)amplitude * sin(ph));
        qdsp_goertzel_process(&g, x);
    }
    return qdsp_goertzel_power(&g);
}

static void test_goertzel_detects_its_bin(void)
{
    const int N = 64, k = 8, amplitude = 10000;
    q15_t on_bin = goertzel_power_for(N, k, k, amplitude);
    q15_t off_bin = goertzel_power_for(N, k, 24, amplitude); /* другая частота */

    CHECK(on_bin > 0, "мощность на своём бине должна быть положительной");
    CHECK(on_bin > off_bin, "свой бин мощнее чужого");
    CHECK(on_bin > (q15_t)(off_bin * 3), "разница между бинами не меньше 3x");
}

/* ── огибающая ────────────────────────────────────────────────────── */

static void test_envelope_attack_release(void)
{
    qdsp_envelope_t e = {Q15_ONE, 3277, 0}; /* мгновенная атака, релиз 0.1 */
    q15_t y = qdsp_envelope_process(&e, 10000);
    CHECK_NEAR(y, 10000, 40, "атака выходит на уровень сигнала сразу");

    y = qdsp_envelope_process(&e, 0);
    CHECK(y > 8000, "после первого нуля огибающая ещё высокая");
    for (int i = 0; i < 40; i++) y = qdsp_envelope_process(&e, 0);
    CHECK(y < 1300, "за 40 отсчётов отпускания огибающая почти затухает");
}

static void test_envelope_abs_negative(void)
{
    qdsp_envelope_t e = {Q15_ONE, Q15_ONE, 0};
    q15_t y = qdsp_envelope_process(&e, -12000);
    CHECK_NEAR(y, 12000, 40, "огибающая берёт модуль входного сигнала");
}

/* ── FIR ──────────────────────────────────────────────────────────── */

static void test_fir_single_tap_passthrough(void)
{
    static const q15_t taps[1] = {Q15_ONE};
    q15_t buf[1] = {0};
    qdsp_fir_t f = {taps, buf, 1, 0};
    static const q15_t input[] = {111, -222, 333};
    for (int i = 0; i < 3; i++) {
        q15_t y = qdsp_fir_process(&f, input[i]);
        CHECK_NEAR(y, input[i], 3, "FIR с одним коэффициентом 1.0 не меняет сигнал");
    }
}

static void test_fir_moving_average(void)
{
    static const q15_t taps[2] = {16384, 16384}; /* 0.5, 0.5 */
    q15_t buf[2] = {0, 0};
    qdsp_fir_t f = {taps, buf, 2, 0};
    qdsp_fir_process(&f, 1000);
    q15_t y = qdsp_fir_process(&f, 3000);
    CHECK_NEAR(y, 2000, 10, "FIR из двух 0.5 — среднее двух последних отсчётов");
}

/* ── метрология: peak + RMS ───────────────────────────────────────── */

static void test_peak_tracking(void)
{
    q15_t peak = 0;
    static const q15_t input[] = {1000, -8000, 3000, -2000};
    for (int i = 0; i < 4; i++) peak = qdsp_peak_update(peak, input[i]);
    CHECK(peak == 8000, "пик равен максимальному модулю выборки");
    peak = qdsp_peak_update(peak, 100);
    CHECK(peak == 8000, "пик не уменьшается");
}

static void test_rms_of_constant(void)
{
    int64_t acc = 0;
    const int n = 100;
    for (int i = 0; i < n; i++) acc = qdsp_rms_acc(acc, 16384); /* 0.5 */
    q15_t rms = qdsp_rms_final(acc, n);
    CHECK_NEAR(rms, 16384, 400, "RMS постоянного сигнала 0.5 равен 0.5");
}

static void test_rms_of_zero(void)
{
    int64_t acc = 0;
    for (int i = 0; i < 16; i++) acc = qdsp_rms_acc(acc, 0);
    CHECK(qdsp_rms_final(acc, 16) == 0, "RMS нулевого сигнала равен нулю");
}

int main(void)
{
    printf("qdsp unit tests\n");

    test_saturation();
    test_multiplication();
    test_abs();
    test_biquad_passthrough();
    test_biquad_averaging();
    test_onepole_converges();
    test_onepole_step_response();
    test_goertzel_detects_its_bin();
    test_envelope_attack_release();
    test_envelope_abs_negative();
    test_fir_single_tap_passthrough();
    test_fir_moving_average();
    test_peak_tracking();
    test_rms_of_constant();
    test_rms_of_zero();

    if (failures == 0) {
        printf("OK: все проверки пройдены\n");
        return 0;
    }
    printf("ПРОВАЛЕНО проверок: %d\n", failures);
    return 1;
}
