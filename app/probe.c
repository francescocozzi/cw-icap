/*
 * PROBE.APP - primo test SULLA RADIO, prima di CW.APP. SDK ufficiale ICAP v1rc.
 * Solo lettura: nessuna chiamata icap_tx_*, nessun file virtuale legato al TX; l'unica
 * scrittura è PROBE.TXT nella cartella delle app.
 *
 * L'ABI ora è quella dell'SDK: qui si misura ciò che l'SDK non dice.
 *   1. servizi presenti (ICAP_HAS) e versione: abi, radio_id, fw_build, size
 *   2. memoria (icap_mem_info, heap), stato radio (frequenza, CODICE DEL MODO: lancia PROBE
 *      con la radio in CW e annota il codice), modo decoder di avvio (icap_mode)
 *   3. griglia: righello sulle 8 righe della finestra -> caratteri visibili per riga
 *      (fotografa lo schermo); una riga righello su DECODE se quella schermata è visibile
 *   4. audio: frequenza, campioni per chiamata, cadenza, RMS/picco, overrun (ICAP_E_OVERRUN)
 *   5. orologio e file virtuali @FREQ / @CLOCK
 * Esce da sola dopo ~25 s; STOP la ferma in qualsiasi momento (controllo ogni ≤100 ms).
 */
#include "icap.h"

static char out[4096];
static int olen;

static void o_s(const char *s) { while (*s && olen < (int)sizeof out - 1) out[olen++] = *s++; }
static void o_u(uint32_t v) {
    char t[11]; int n = 0;
    do { t[n++] = (char)('0' + v % 10u); v /= 10u; } while (v);
    while (n) { char c[2] = { t[--n], 0 }; o_s(c); }
}
static void o_x(uint32_t v) {
    char t[11] = "0x00000000";
    for (int i = 0; i < 8; i++) t[9 - i] = "0123456789abcdef"[(v >> (4 * i)) & 15];
    o_s(t);
}
static void o_i(int v) { if (v < 0) { o_s("-"); o_u((uint32_t)(-v)); } else o_u((uint32_t)v); }
static void o_kv(const char *k, uint32_t v) { o_s(k); o_s("="); o_x(v); o_s("\n"); }

/* attesa interrompibile: il loader esige icap_should_stop() almeno ogni 500 ms */
static int wait_ms(uint32_t ms) {
    uint32_t t0 = icap_ticks();
    while (icap_ticks() - t0 < ms) {
        if (icap_should_stop()) return 1;
        icap_sleep_ms(100);
    }
    return icap_should_stop();
}

static void vfile(const char *name) {
    char b[96];
    int fd = icap_open(name, ICAP_O_READ);
    o_s(name); o_s(": ");
    if (fd < 0) { o_s("error "); o_i(fd); o_s("\n"); return; }
    int n = icap_read(fd, b, sizeof b - 1);
    icap_close(fd);
    if (n < 0) { o_s("read "); o_i(n); o_s("\n"); return; }
    b[n] = 0;
    o_s(b);
    if (n == 0 || b[n - 1] != '\n') o_s("\n");
}

static void save(void) {
    int fd = icap_open("PROBE.TXT", ICAP_O_WRITE);
    if (fd >= 0) { icap_write(fd, out, (uint32_t)olen); icap_close(fd); }
}

#define HAS(f) do { o_s(#f); o_s(ICAP_HAS(f) ? "=yes " : "=no "); } while (0)

int app_main(const struct icap_svc *svc) {
    static int16_t buf[1024];
    icap_use(svc);
    icap_clear();

    /* 1. versione e servizi */
    o_s("PROBE v2 (SDK v1rc)\n");
    o_kv("abi", svc->abi_version); o_kv("radio_id", svc->radio_id);
    o_kv("fw_build", svc->fw_build); o_kv("size", svc->size);
    HAS(clock_now); HAS(audio_read); HAS(audio_rate); HAS(decode_out); o_s("\n");
    HAS(net_status); HAS(popup); HAS(remove); HAS(mode); o_s("\n");

    /* 2. memoria, radio, modo */
    struct icap_meminfo mi;
    if (icap_mem_info(&mi) == 0) {
        o_kv("app_base", mi.app_base); o_kv("app_limit", mi.app_limit);
        o_kv("image_end", mi.image_end); o_kv("bss_end", mi.bss_end);
        o_kv("stack_lo", mi.stack_lo); o_kv("stack_hi", mi.stack_hi);
        o_kv("free_lo", mi.free_lo); o_kv("free_hi", mi.free_hi);
    }
    o_kv("heap_base", (uint32_t)(uintptr_t)icap_heap()); o_kv("heap_size", icap_heap_size());
    struct icap_radio rs;
    if (icap_radio_state(&rs) == 0) {
        o_s("radio: freq="); o_u(rs.freq_hz); o_s(" sub="); o_u(rs.sub_freq_hz);
        o_s(" mode_code="); o_x(rs.mode); o_s(" is_tx="); o_u(rs.is_tx); o_s("\n");
    }
    char m[8];
    int ml = icap_mode(m, (int)sizeof m);
    o_s("icap_mode="); o_i(ml);
    if (ml > 0) { m[ml < 7 ? ml : 7] = 0; o_s(" "); o_s(m); }
    o_s("\n");

    /* 3. griglia: 8 righe della finestra + una riga su DECODE */
    static const char ruler[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuv";
    for (int r = 0; r < 8; r++) {
        char line[64];
        line[0] = (char)('0' + r); line[1] = ':';
        int i = 0;
        for (; ruler[i] && i < 60; i++) line[2 + i] = ruler[i];
        line[2 + i] = 0;
        icap_set_line(r, line);
    }
    o_s("decode_out="); o_i(icap_decode_out(ruler)); o_s("\n");
    if (wait_ms(8000)) { save(); return 0; }       /* tempo per fotografare finestra e DECODE */
    icap_clear();

    /* 4. audio */
    int rate = icap_audio_rate();
    o_s("audio_rate="); o_i(rate); o_s("\n");
    if (rate > 0) {
        icap_set_line(0, "PROBE: measuring audio 10 s...");
        uint32_t t0 = icap_ticks(), calls = 0, zero = 0, gaps = 0, errs = 0, total = 0, maxn = 0, peak = 0;
        uint32_t last_poll = t0, max_wait = 0;
        float sq = 0.0f;
        while (icap_ticks() - t0 < 10000u) {
            uint32_t now = icap_ticks();
            if (now - last_poll > max_wait) max_wait = now - last_poll;
            last_poll = now;
            if (icap_should_stop()) break;
            int n = icap_audio_read(buf, 1024);
            calls++;
            if (n == ICAP_E_OVERRUN) gaps++;
            else if (n < 0) { errs++; icap_sleep_ms(5); }
            else if (n == 0) { zero++; icap_sleep_ms(5); }
            else {
                total += (uint32_t)n; if ((uint32_t)n > maxn) maxn = (uint32_t)n;
                for (int i = 0; i < n; i++) {
                    int32_t s = buf[i]; uint32_t a = (uint32_t)(s < 0 ? -s : s);
                    if (a > peak) peak = a;
                    sq += (float)s * (float)s;
                }
            }
        }
        uint32_t dt = icap_ticks() - t0;
        o_s("audio: calls="); o_u(calls); o_s(" samples="); o_u(total); o_s(" ms="); o_u(dt);
        o_s(" max_per_call="); o_u(maxn); o_s(" empty="); o_u(zero); o_s(" overrun="); o_u(gaps);
        o_s(" err="); o_u(errs); o_s(" peak="); o_u(peak);
        o_s(" mean_sq="); o_u(total ? (uint32_t)(sq / (float)total) : 0u);
        o_s(" max_loop_ms="); o_u(max_wait); o_s("\n");
    }

    /* 5. orologio e file virtuali di sola lettura (niente @TX: regola 1) */
    struct icap_time c;
    int r = icap_clock_now(&c);
    o_s("clock_now="); o_i(r);
    if (r == 0) {
        o_s(" "); o_u(c.year); o_s("-"); o_u(c.mon); o_s("-"); o_u(c.day); o_s(" ");
        o_u(c.hour); o_s(":"); o_u(c.min); o_s(":"); o_u(c.sec); o_s(" wday="); o_u(c.wday);
    }
    o_s("\n");
    vfile("@FREQ");
    vfile("@CLOCK");

    save();
    icap_set_line(0, "PROBE: PROBE.TXT written");
    wait_ms(3000);
    return 0;
}
