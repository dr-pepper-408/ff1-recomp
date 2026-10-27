/**
 * @file apu.c
 * @brief NES 2A03 APU: pulse x2, triangle, noise, frame sequencer, mixer.
 *
 * The chip is stepped a CPU cycle at a time (1.789773 MHz, NTSC) and each
 * output sample is the average of the mixer output over its cycles, run
 * through the NES's two high-pass filters (90 Hz and 440 Hz).  Register
 * writes take effect at once; the game writes them once per frame, as the
 * original's music and sound-effect code does.
 */

#include <string.h>
#include "apu.h"

#define CPU_HZ 1789773.0

static const uint8_t length_table[32] = {
    10, 254, 20, 2, 40, 4, 80, 6, 160, 8, 60, 10, 14, 12, 26, 14,
    12, 16, 24, 18, 48, 20, 96, 22, 192, 24, 72, 26, 16, 28, 32, 30,
};
static const uint8_t duty_table[4][8] = {
    { 0, 1, 0, 0, 0, 0, 0, 0 }, { 0, 1, 1, 0, 0, 0, 0, 0 },
    { 0, 1, 1, 1, 1, 0, 0, 0 }, { 1, 0, 0, 1, 1, 1, 1, 1 },
};
static const uint8_t tri_table[32] = {
    15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0,
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
};
static const uint16_t noise_periods[16] = {
    4, 8, 16, 32, 64, 96, 128, 160, 202, 254, 380, 508, 762, 1016, 2034, 4068,
};

typedef struct {               /* envelope generator (pulse, noise) */
    int start, divider, decay;
    int loop, constant, volume;
} Envelope;

typedef struct {
    int enabled;
    int duty, seq;
    Envelope env;
    int sweep_on, sweep_period, sweep_neg, sweep_shift, sweep_reload, sweep_div;
    int period, timer;
    int length;
    int ones_complement;        /* pulse 1 negates with one's complement */
} Pulse;

typedef struct {
    int enabled;
    int control, linear_reload, linear, reload_flag;
    int period, timer, seq;
    int length;
} Triangle;

typedef struct {
    int enabled;
    Envelope env;
    int mode, period, timer;
    uint16_t lfsr;
    int length;
} Noise;

/* The nonlinear mixer (0..1). */
static double mix(int p1, int p2, int t, int n) {
    double pulse_mix = (p1 + p2) ? 95.88 / (8128.0 / (p1 + p2) + 100.0) : 0.0;
    double tnd_in = t / 8227.0 + n / 12241.0;
    return pulse_mix + (tnd_in > 0 ? 159.79 / (1.0 / tnd_in + 100.0) : 0.0);
}

static Pulse pulse[2];
static Triangle tri;
static Noise noise;
static int five_step, frame_cycle;
static double cycle_frac;
static double hp1_prev_in, hp1_prev_out, hp2_prev_in, hp2_prev_out;

void Apu_Reset(void) {
    memset(pulse, 0, sizeof(pulse));
    memset(&tri, 0, sizeof(tri));
    memset(&noise, 0, sizeof(noise));
    pulse[0].ones_complement = 1;
    noise.lfsr = 1;
    five_step = 0;
    frame_cycle = 0;
    cycle_frac = 0;
    /* the filters start settled: the idle triangle rests at 15 (a DC
     * level the high-pass removes), so no pop at power-on */
    hp1_prev_in = mix(0, 0, tri_table[0], 0);
    hp1_prev_out = hp2_prev_in = hp2_prev_out = 0;
}

static void env_clock(Envelope* e) {
    if (e->start) {
        e->start = 0;
        e->decay = 15;
        e->divider = e->volume;
    } else if (e->divider) {
        e->divider--;
    } else {
        e->divider = e->volume;
        if (e->decay) e->decay--;
        else if (e->loop) e->decay = 15;
    }
}

static int env_output(const Envelope* e) { return e->constant ? e->volume : e->decay; }

static int sweep_target(const Pulse* p) {
    int change = p->period >> p->sweep_shift;
    if (p->sweep_neg) return p->period - change - p->ones_complement;
    return p->period + change;
}

static int pulse_muted(const Pulse* p) {
    return p->period < 8 || sweep_target(p) > 0x7FF;
}

static void sweep_clock(Pulse* p) {
    if (p->sweep_div == 0 && p->sweep_on && p->sweep_shift && !pulse_muted(p)) {
        int t = sweep_target(p);
        p->period = t < 0 ? 0 : t;
    }
    if (p->sweep_div == 0 || p->sweep_reload) {
        p->sweep_div = p->sweep_period;
        p->sweep_reload = 0;
    } else {
        p->sweep_div--;
    }
}

static void quarter_frame(void) {
    env_clock(&pulse[0].env);
    env_clock(&pulse[1].env);
    env_clock(&noise.env);
    if (tri.reload_flag) tri.linear = tri.linear_reload;
    else if (tri.linear) tri.linear--;
    if (!tri.control) tri.reload_flag = 0;
}

static void half_frame(void) {
    for (int i = 0; i < 2; i++) {
        if (pulse[i].length && !pulse[i].env.loop) pulse[i].length--;
        sweep_clock(&pulse[i]);
    }
    if (tri.length && !tri.control) tri.length--;
    if (noise.length && !noise.env.loop) noise.length--;
}

void Apu_Write(uint16_t addr, uint8_t v) {
    if (addr >= 0x4000 && addr <= 0x4007) {
        Pulse* p = &pulse[(addr - 0x4000) >> 2];
        switch (addr & 3) {
        case 0:
            p->duty = v >> 6;
            p->env.loop = (v >> 5) & 1;
            p->env.constant = (v >> 4) & 1;
            p->env.volume = v & 0x0F;
            break;
        case 1:
            p->sweep_on = v >> 7;
            p->sweep_period = (v >> 4) & 7;
            p->sweep_neg = (v >> 3) & 1;
            p->sweep_shift = v & 7;
            p->sweep_reload = 1;
            break;
        case 2:
            p->period = (p->period & 0x700) | v;
            break;
        case 3:
            p->period = (p->period & 0xFF) | ((v & 7) << 8);
            if (p->enabled) p->length = length_table[v >> 3];
            p->seq = 0;
            p->env.start = 1;
            break;
        }
        return;
    }
    switch (addr) {
    case 0x4008:
        tri.control = v >> 7;
        tri.linear_reload = v & 0x7F;
        break;
    case 0x400A:
        tri.period = (tri.period & 0x700) | v;
        break;
    case 0x400B:
        tri.period = (tri.period & 0xFF) | ((v & 7) << 8);
        if (tri.enabled) tri.length = length_table[v >> 3];
        tri.reload_flag = 1;
        break;
    case 0x400C:
        noise.env.loop = (v >> 5) & 1;
        noise.env.constant = (v >> 4) & 1;
        noise.env.volume = v & 0x0F;
        break;
    case 0x400E:
        noise.mode = v >> 7;
        noise.period = noise_periods[v & 0x0F];
        break;
    case 0x400F:
        if (noise.enabled) noise.length = length_table[v >> 3];
        noise.env.start = 1;
        break;
    case 0x4015:
        pulse[0].enabled = v & 1;
        pulse[1].enabled = (v >> 1) & 1;
        tri.enabled = (v >> 2) & 1;
        noise.enabled = (v >> 3) & 1;
        if (!pulse[0].enabled) pulse[0].length = 0;
        if (!pulse[1].enabled) pulse[1].length = 0;
        if (!tri.enabled) tri.length = 0;
        if (!noise.enabled) noise.length = 0;
        break;
    case 0x4017:
        five_step = v >> 7;
        frame_cycle = 0;
        if (five_step) { quarter_frame(); half_frame(); }
        break;
    }
}

/* One CPU cycle of every channel and the frame sequencer; the mixer's
 * output (0..1). */
static double step(void) {
    /* frame sequencer (4-step: quarter at 1-4, half at 2 and 4) */
    frame_cycle++;
    if (!five_step) {
        if (frame_cycle == 7457 || frame_cycle == 22371) quarter_frame();
        else if (frame_cycle == 14913) { quarter_frame(); half_frame(); }
        else if (frame_cycle >= 29829) { quarter_frame(); half_frame(); frame_cycle = 0; }
    } else {
        if (frame_cycle == 7457 || frame_cycle == 22371) quarter_frame();
        else if (frame_cycle == 14913 || frame_cycle == 37281) { quarter_frame(); half_frame(); }
        if (frame_cycle >= 37281) frame_cycle = 0;
    }

    /* pulses: the timer counts APU cycles (every other CPU cycle) */
    int p_out[2];
    for (int i = 0; i < 2; i++) {
        Pulse* p = &pulse[i];
        if (frame_cycle & 1) {
            if (p->timer) p->timer--;
            else { p->timer = p->period; p->seq = (p->seq + 1) & 7; }
        }
        p_out[i] = (p->length && !pulse_muted(p) && duty_table[p->duty][p->seq]) ? env_output(&p->env) : 0;
    }

    /* triangle: every CPU cycle, only while both counters run */
    if (tri.timer) tri.timer--;
    else {
        tri.timer = tri.period;
        if (tri.length && tri.linear) tri.seq = (tri.seq + 1) & 31;
    }
    int t_out = tri_table[tri.seq];

    /* noise */
    if (noise.timer) noise.timer--;
    else {
        noise.timer = noise.period;
        int bit = (noise.lfsr ^ (noise.lfsr >> (noise.mode ? 6 : 1))) & 1;
        noise.lfsr = (uint16_t)((noise.lfsr >> 1) | (bit << 14));
    }
    int n_out = (noise.length && !(noise.lfsr & 1)) ? env_output(&noise.env) : 0;

    return mix(p_out[0], p_out[1], t_out, n_out);
}

void Apu_Render(int16_t* out, int count, int sample_rate) {
    double per_sample = CPU_HZ / sample_rate;
    /* first-order high-pass filters at 90 Hz and 440 Hz */
    double a1 = 1.0 / (1.0 + 2 * 3.14159265 * 90.0 / sample_rate);
    double a2 = 1.0 / (1.0 + 2 * 3.14159265 * 440.0 / sample_rate);
    for (int i = 0; i < count; i++) {
        cycle_frac += per_sample;
        int n = (int)cycle_frac;
        cycle_frac -= n;
        double sum = 0;
        for (int c = 0; c < n; c++) sum += step();
        double x = n ? sum / n : 0;
        double y1 = a1 * (hp1_prev_out + x - hp1_prev_in);
        hp1_prev_in = x; hp1_prev_out = y1;
        double y2 = a2 * (hp2_prev_out + y1 - hp2_prev_in);
        hp2_prev_in = y1; hp2_prev_out = y2;
        double s = y2 * 40000.0;
        out[i] = (int16_t)(s > 32767 ? 32767 : s < -32768 ? -32768 : s);
    }
}
