/*
 * cwdec.c - vedi cwdec.h per vincoli e interfaccia.
 *
 * Catena di elaborazione (per campione a 8 kHz):
 *   PCM -> mixer complesso a f0 (pitch) -> 2 x IIR passa-basso su I/Q
 *       -> ampiezza ogni 2 ms (frame) -> tracker segnale/rumore con isteresi
 *       -> macchina a stati mark/space con filtro anti-glitch
 *       -> classificazione punto/linea con stima adattiva della velocità
 *       -> albero Morse -> callback carattere
 * In parallelo (se pitch_hz == 0) un banco Goertzel cerca il tono più forte.
 */
#include "cwdec.h"

#define PI_F 3.14159265358979f

/* ---------- matematica minima senza libm ---------- */
static float wrap_pi(float x) {
    while (x >  PI_F) x -= 2.0f * PI_F;
    while (x < -PI_F) x += 2.0f * PI_F;
    return x;
}
static float sin_f(float x) {          /* Taylor fino a x^11, |err| < 1e-6 su [-pi, pi] */
    x = wrap_pi(x);
    float x2 = x * x, t = x, s = x;
    for (int k = 1; k <= 5; k++) { t *= -x2 / (float)((2 * k) * (2 * k + 1)); s += t; }
    return s;
}
static float cos_f(float x) { return sin_f(x + 0.5f * PI_F); }
static float sqrt_f(float x) { return x > 0.0f ? __builtin_sqrtf(x) : 0.0f; }
static float log2_f(float x) {         /* sufficiente per mostrare l'SNR */
    union { float f; uint32_t u; } v = { x };
    float e = (float)((int)((v.u >> 23) & 0xff) - 127);
    v.u = (v.u & 0x007fffff) | 0x3f800000;  /* mantissa in [1,2) */
    float m = v.f;
    return e + (-0.34484843f * m + 2.02466578f) * m - 0.67487759f;
}
static float clampf(float x, float lo, float hi) { return x < lo ? lo : (x > hi ? hi : x); }

/* ---------- tabella Morse ---------- */
static const struct { const char *code; const char *txt; } MORSE[] = {
    {".-","A"},{"-...","B"},{"-.-.","C"},{"-..","D"},{".","E"},{"..-.","F"},{"--.","G"},
    {"....","H"},{"..","I"},{".---","J"},{"-.-","K"},{".-..","L"},{"--","M"},{"-.","N"},
    {"---","O"},{".--.","P"},{"--.-","Q"},{".-.","R"},{"...","S"},{"-","T"},{"..-","U"},
    {"...-","V"},{".--","W"},{"-..-","X"},{"-.--","Y"},{"--..","Z"},
    {"-----","0"},{".----","1"},{"..---","2"},{"...--","3"},{"....-","4"},
    {".....","5"},{"-....","6"},{"--...","7"},{"---..","8"},{"----.","9"},
    {".-.-.-","."},{"--..--",","},{"..--..","?"},{"-..-.","/"},{"-...-","="},
    {".-.-.","+"},{"-....-","-"},{"---...",":"},{".----.","'"},{".--.-.","@"},
    {"-.--.","<KN>"},{"...-.-","<SK>"},{".-...","<AS>"},{"-.-.-","<KA>"},
    {"...-.","<SN>"},{"........","<HH>"},
};

const char *cwdec_lookup(uint16_t sym) {
    for (unsigned i = 0; i < sizeof MORSE / sizeof MORSE[0]; i++) {
        uint16_t s = 1;
        for (const char *p = MORSE[i].code; *p; p++) s = (uint16_t)(s * 2 + (*p == '-'));
        if (s == sym) return MORSE[i].txt;
    }
    return "*";
}

/* ---------- configurazione ---------- */
void cwdec_default_config(cwdec_config_t *c) {
    c->pitch_hz = 0.0f;
    c->pitch_min_hz = 300.0f;
    c->pitch_max_hz = 1300.0f;
    c->bw_hz = 80.0f;
    c->init_wpm = 20.0f;
    c->squelch_db = 12.0f;
    c->emit = 0;
    c->user = 0;
}

static void set_pitch(cwdec_t *d, float hz) {
    float w = 2.0f * PI_F * hz / (float)CWDEC_FS;
    d->rot_c = cos_f(w);
    d->rot_s = sin_f(w);
    d->cur_hz = hz;
}

static float bin_hz(const cwdec_t *d, float k) {
    return d->cfg.pitch_min_hz + k * (d->cfg.pitch_max_hz - d->cfg.pitch_min_hz) / (float)(CWDEC_NBINS - 1);
}

void cwdec_init(cwdec_t *d, const cwdec_config_t *cfg) {
    unsigned char *p = (unsigned char *)d;
    for (size_t i = 0; i < sizeof *d; i++) p[i] = 0;
    d->cfg = *cfg;
    int auto_pitch = (cfg->pitch_hz <= 0.0f);

    d->osc_c = 1.0f; d->osc_s = 0.0f;
    /* passa-basso: due poli identici, frequenza di taglio ~ bw/2 */
    d->lp_a = clampf(2.0f * PI_F * (cfg->bw_hz * 0.5f) / (float)CWDEC_FS, 0.001f, 0.5f);

    d->sig_lvl = 0.0f;     /* viene tenuto >= rumore */
    d->noise_lvl = 1.0f;   /* partenza alta: il tracker del rumore scende in fretta */
    d->dit = 600.0f / clampf(cfg->init_wpm, 5.0f, 60.0f);   /* frame da 2 ms */
    d->dah = 3.0f * d->dit;
    d->cgap = 3.0f;        /* in unità di punto */
    d->word_emitted = 1;

    for (int k = 0; k < CWDEC_NBINS; k++)
        d->g_coef[k] = 2.0f * cos_f(2.0f * PI_F * bin_hz(d, (float)k) / (float)CWDEC_FS);
    d->g_cur = -1;
    d->g_fix = -1;
    if (!auto_pitch) {
        float k = (cfg->pitch_hz - cfg->pitch_min_hz) * (float)(CWDEC_NBINS - 1) / (cfg->pitch_max_hz - cfg->pitch_min_hz);
        if (k >= -0.5f && k <= (float)CWDEC_NBINS - 0.5f) d->g_fix = (int)(k + 0.5f);
        else d->g_present = d->g_pres_next = 1;   /* fuori dal banco: nessun gate di presenza */
    }

    set_pitch(d, auto_pitch ? 700.0f : cfg->pitch_hz);   /* cfg.pitch_hz == 0 -> modalità auto */
}

/* ---------- uscita ---------- */
static void emit_str(cwdec_t *d, const char *s) {
    for (; *s; s++) {
        if (d->cfg.emit) d->cfg.emit(d->cfg.user, *s);
        d->chars_out++;
    }
}

/* Punto/linea si decide qui e non a ogni mark: la stima di velocità a fine carattere
 * include anche i mark successivi (decisivo a freddo, quando dit/dah sono ancora
 * quelli di init_wpm). */
static void close_char(cwdec_t *d) {
    if (!d->char_open) return;
    const char *txt = "*";
    if (d->n_el <= CWDEC_MAXEL) {
        float mid = 0.5f * (d->dit + d->dah);
        uint16_t sym = 1;
        for (int i = 0; i < d->n_el; i++) sym = (uint16_t)(sym * 2 + (d->el[i] > mid));
        txt = cwdec_lookup(sym);
    }
    emit_str(d, txt);
    d->n_el = 0;
    d->char_open = 0;
    d->word_emitted = 0;
}

/* ---------- timing ---------- */
/* Clustering a 2 classi (Otsu) sugli ultimi mark: aggancia subito velocità molto
 * diverse da quella iniziale, dove una sola media mobile divergerebbe. */
static void recluster(cwdec_t *d) {
    float v[16];
    int n = d->hist_n;
    for (int i = 0; i < n; i++) v[i] = d->hist[i];
    for (int i = 1; i < n; i++) {               /* insertion sort, n <= 16 */
        float x = v[i]; int j = i - 1;
        while (j >= 0 && v[j] > x) { v[j + 1] = v[j]; j--; }
        v[j + 1] = x;
    }
    float tot = 0.0f;
    for (int i = 0; i < n; i++) tot += v[i];
    float best = 0.0f, lo_m = 0.0f, hi_m = 0.0f, lo_s = 0.0f;
    for (int k = 1; k < n; k++) {
        lo_s += v[k - 1];
        float ml = lo_s / (float)k, mh = (tot - lo_s) / (float)(n - k);
        float score = (float)k * (float)(n - k) * (mh - ml) * (mh - ml);
        if (score > best) { best = score; lo_m = ml; hi_m = mh; }
    }
    if (best > 0.0f && hi_m > 2.0f * lo_m && hi_m < 5.0f * lo_m) {
        d->dit += 0.5f * (lo_m - d->dit);
        d->dah += 0.5f * (hi_m - d->dah);
    }
}

static void commit_mark(cwdec_t *d, float m) {
    d->hist[d->hist_i] = m;
    d->hist_i = (d->hist_i + 1) & 15;
    if (d->hist_n < 16) d->hist_n++;
    if (d->hist_n >= 3) recluster(d);

    float mid = 0.5f * (d->dit + d->dah);
    int is_dah = m > mid;
    if (is_dah) {
        d->dah += 0.25f * (m - d->dah);
        d->dit = clampf(d->dit, d->dah / 4.0f, d->dah / 2.2f);
    } else {
        d->dit += 0.25f * (m - d->dit);
        d->dah = clampf(d->dah, 2.2f * d->dit, 4.0f * d->dit);
    }
    d->dit = clampf(d->dit, 8.0f, 150.0f);       /* ~75 .. 4 WPM: oltre è rumore, non CW manuale */
    d->dah = clampf(d->dah, 2.2f * d->dit, 4.0f * d->dit);
    if (d->n_el < CWDEC_MAXEL) d->el[d->n_el] = m;
    if (d->n_el <= CWDEC_MAXEL) d->n_el++;
    d->char_open = 1;
}

static float word_threshold(const cwdec_t *d) {
    float u = 1.9f * d->cgap;
    return (u > 5.0f ? u : 5.0f) * d->dit;
}

static void on_frame(cwdec_t *d, float amp) {
    /* tracker livelli: attacco veloce / rilascio lento */
    /* nei primi ~300 ms il livello segnale scende in fretta (partenza a freddo) */
    float sig_decay = d->frames < 150 ? 0.05f : 0.0015f;
    d->frames++;
    if (amp > d->sig_lvl) d->sig_lvl += 0.15f * (amp - d->sig_lvl);
    else                  d->sig_lvl += sig_decay * (amp - d->sig_lvl);
    /* rumore ~ media dell'ampiezza durante gli space (non il minimo: con il rumore
     * di Rayleigh il minimo è ~0 e lo squelch si aprirebbe sul solo rumore) */
    /* asimmetrico: sottostima un po' il rumore puro, ma non si fa trascinare dal segnale se
     * il decoder parte a trasmissione in corso. I falsi mark iniziali li ferma il gate di presenza. */
    if (amp < d->noise_lvl) d->noise_lvl += (d->frames < 150 ? 0.10f : 0.02f) * (amp - d->noise_lvl);
    else d->noise_lvl += (d->key ? 0.0003f : 0.005f) * (amp - d->noise_lvl);
    if (d->sig_lvl < d->noise_lvl) d->sig_lvl = d->noise_lvl;

    float span = d->sig_lvl - d->noise_lvl;
    float thr = d->noise_lvl + (d->key ? 0.40f : 0.55f) * span;   /* isteresi */
    if (thr < 3.0f * d->noise_lvl) thr = 3.0f * d->noise_lvl;      /* mai dentro al rumore (Rayleigh: P(>3x media) ~ 0.1%) */
    /* lo squelch resta chiuso finché il banco non vede un tono vero (al pitch fisso, se
     * impostato): niente mark dal rumore né frammenti presi al pitch sbagliato a freddo */
    int open = cwdec_snr_db(d) >= d->cfg.squelch_db && d->g_present;
    int k = open && amp > thr;

    float glitch = clampf(0.3f * d->dit, 1.5f, 12.0f);

    if (k == d->key) {
        d->run_frames += 1.0f;
    } else if (d->key) {                       /* fine di un mark */
        if (d->run_frames < glitch) {          /* mark spurio: torna nello space precedente */
            d->key = 0;
            d->run_frames = d->prev_space + d->run_frames + 1.0f;
            return;
        }
        d->mark_pending = 1;
        d->mark_len = d->run_frames;
        d->key = 0;
        d->run_frames = 1.0f;
    } else {                                   /* inizio di un mark */
        if (d->mark_pending && d->run_frames < glitch) {   /* buco dentro un mark: unisci */
            d->key = 1;
            d->run_frames = d->mark_len + d->run_frames + 1.0f;
            d->mark_pending = 0;
            return;
        }
        /* impara lo spazio tra caratteri (gestisce la spaziatura Farnsworth) */
        float s = d->run_frames;
        if (d->gap_closed_char && s < word_threshold(d)) {
            d->cgap += 0.2f * (s / d->dit - d->cgap);
            d->cgap = clampf(d->cgap, 2.5f, 6.0f);
        }
        d->gap_closed_char = 0;
        d->key = 1;
        d->prev_space = s;
        d->run_frames = 1.0f;
    }

    if (!d->key) {
        if (d->mark_pending && d->run_frames >= glitch) {
            commit_mark(d, d->mark_len);
            d->mark_pending = 0;
        }
        if (!d->mark_pending) {
            if (d->char_open && d->run_frames > 2.0f * d->dit) {
                close_char(d);
                d->gap_closed_char = 1;
            }
            if (!d->word_emitted && d->run_frames > word_threshold(d)) {
                emit_str(d, " ");
                d->word_emitted = 1;
                d->gap_closed_char = 0;   /* spazio tra parole: non usarlo per cgap */
            }
        }
    }
}

/* ---------- ricerca pitch ---------- */
#ifndef RING_K
#define RING_K 6.0f
#endif
#define G_BLOCK 400   /* 50 ms -> risoluzione 20 Hz */

/* media dei bin a distanza 3..6 da c (75..150 Hz): il "contorno" di un picco. Un tono vero
 * è stretto (lobo +-1 bin) e sta molto sopra il contorno; il rumore passato dal filtro IF della
 * radio (FIL3 = 250 Hz in CW) riempie anche il contorno, quindi non lo supera. */
static float ring_mean(const cwdec_t *d, int c) {
    float s = 0.0f; int n = 0;
    for (int k = 3; k <= 6; k++) {
        if (c - k >= 0)          { s += d->g_pow[c - k]; n++; }
        if (c + k < CWDEC_NBINS) { s += d->g_pow[c + k]; n++; }
    }
    return n ? s / (float)n : 0.0f;
}

/* tono vero nel bin c: sopra il fondo del banco E sopra il proprio contorno */
static int is_tone(const cwdec_t *d, int c, float floor_) {
    const float LOCK = 8.0f;   /* 9 dB sopra il fondo, misurato in un bin da 20 Hz */
    const float RING = RING_K; /* sopra il contorno (rumore filtrato) */
    return d->g_pow[c] > LOCK * floor_ && d->g_pow[c] > RING * ring_mean(d, c);
}

static void goertzel_block_done(cwdec_t *d) {
    int best = 0;
    float tot = 0.0f;
    for (int k = 0; k < CWDEC_NBINS; k++) {
        float s1 = d->g_s1[k], s2 = d->g_s2[k];
        float p = s1 * s1 + s2 * s2 - d->g_coef[k] * s1 * s2;
        /* attacco veloce (un tono nuovo emerge in un blocco), rilascio lento (regge gli space) */
        d->g_pow[k] += (p > d->g_pow[k] ? 0.6f : 0.2f) * (p - d->g_pow[k]);
        d->g_s1[k] = d->g_s2[k] = 0.0f;
        tot += d->g_pow[k];
        if (d->g_pow[k] > d->g_pow[best]) best = k;
    }
    /* fondo del banco: media dei bin escluso il lobo del picco (best +-2) */
    float lobe = 0.0f;
    for (int k = best - 2; k <= best + 2; k++)
        if (k >= 0 && k < CWDEC_NBINS) lobe += d->g_pow[k];
    float floor_ = (tot - lobe) * (1.0f / (float)(CWDEC_NBINS - 5));

    if (d->g_cur < 0 || !is_tone(d, d->g_cur, floor_)) {
        /* nessun tono agganciato: salta subito al picco, se è un tono vero */
        d->g_cur = best;
        d->g_cand_cnt = 0;
    } else if (best != d->g_cur && d->g_pow[best] > 1.58f * d->g_pow[d->g_cur]) {
        if (best == d->g_cand) d->g_cand_cnt++;
        else { d->g_cand = best; d->g_cand_cnt = 1; }
        if (d->g_cand_cnt >= 4) { d->g_cur = best; d->g_cand_cnt = 0; }
    } else d->g_cand_cnt = 0;
    d->g_locked = is_tone(d, d->g_cur, floor_);
    /* presenza tono, applicata con un blocco di ritardo: il rivelatore è 128 ms indietro,
     * così il gate si apre ~80 ms prima del blocco che ha visto il tono e non 128 ms prima
     * (quella finestra in più lascerebbe passare picchi di rumore) */
    int pres = d->g_locked;
    if (d->g_fix >= 0) {           /* pitch fisso: conta solo un tono vicino a quel pitch */
        int m = d->g_fix;
        for (int k = d->g_fix - 1; k <= d->g_fix + 1; k++)
            if (k >= 0 && k < CWDEC_NBINS && d->g_pow[k] > d->g_pow[m]) m = k;
        pres = is_tone(d, m, floor_);
    }
    d->g_present = d->g_pres_next;
    d->g_pres_next = pres;
    if (d->g_fix >= 0) {
        d->g_n = 0;
        return;                    /* il rivelatore resta sul pitch impostato */
    }

    /* interpolazione parabolica attorno al bin scelto */
    int c = d->g_cur;
    float frac = 0.0f;
    if (c > 0 && c < CWDEC_NBINS - 1) {
        float a = d->g_pow[c - 1], b = d->g_pow[c], e = d->g_pow[c + 1];
        float den = a - 2.0f * b + e;
        if (den < 0.0f) frac = clampf(0.5f * (a - e) / den, -0.5f, 0.5f);
    }
    float hz = bin_hz(d, (float)c + frac);
    set_pitch(d, hz);
    d->g_n = 0;
}

/* ---------- ingresso ---------- */
void cwdec_process(cwdec_t *d, const int16_t *pcm, size_t n) {
    const float a = d->lp_a;
    for (size_t j = 0; j < n; j++) {
        float x = (float)pcm[j] * (1.0f / 32768.0f);

        {   /* il Goertzel vede il campione attuale, il rivelatore quello di 128 ms fa */
            int16_t old = d->dl[d->dl_i];
            d->dl[d->dl_i] = pcm[j];
            d->dl_i = (d->dl_i + 1) & (CWDEC_DELAY - 1);
            for (int k = 0; k < CWDEC_NBINS; k++) {
                float s0 = x + d->g_coef[k] * d->g_s1[k] - d->g_s2[k];
                d->g_s2[k] = d->g_s1[k];
                d->g_s1[k] = s0;
            }
            if (++d->g_n >= G_BLOCK) goertzel_block_done(d);
            x = (float)old * (1.0f / 32768.0f);
        }

        /* oscillatore locale ricorsivo */
        float c = d->osc_c * d->rot_c - d->osc_s * d->rot_s;
        float s = d->osc_s * d->rot_c + d->osc_c * d->rot_s;
        d->osc_c = c; d->osc_s = s;

        float i = x * c, q = x * s;
        d->i1 += a * (i - d->i1);       d->q1 += a * (q - d->q1);
        d->i2 += a * (d->i1 - d->i2);   d->q2 += a * (d->q1 - d->q2);

        if (++d->frame_n >= CWDEC_FRAME) {
            d->frame_n = 0;
            /* rinormalizza l'oscillatore (deriva numerica) */
            float g = 1.5f - 0.5f * (d->osc_c * d->osc_c + d->osc_s * d->osc_s);
            d->osc_c *= g; d->osc_s *= g;
            on_frame(d, 2.0f * sqrt_f(d->i2 * d->i2 + d->q2 * d->q2));
        }
    }
}

void cwdec_flush(cwdec_t *d) {
    /* svuota la linea di ritardo (silenzio in coda), poi chiude il carattere in corso */
    static const int16_t zeros[64];
    for (int i = 0; i < CWDEC_DELAY; i += 64) cwdec_process(d, zeros, 64);
    close_char(d);
}

/* ---------- stato ---------- */
float cwdec_wpm(const cwdec_t *d)   { return 600.0f / d->dit; }
float cwdec_pitch(const cwdec_t *d) { return d->cur_hz; }
int   cwdec_tone_present(const cwdec_t *d) { return d->g_present; }
float cwdec_snr_db(const cwdec_t *d) {
    if (d->noise_lvl <= 1e-9f) return 99.0f;
    return 20.0f * 0.30103f * log2_f(d->sig_lvl / d->noise_lvl);
}
