/*
 * CW.APP - decoder CW come app ICAP (IC-705 / IC-9700, firmware custom SQ6EMM, SDK v1rc).
 * Solo ricezione: nessuna chiamata icap_tx_*.
 *
 * Uscite:
 *   - testo decodificato a righe nello stile delle app digi: "HHMM WPM  Hz : TESTO". Una riga
 *     si chiude quando la parola successiva non ci sta, dopo FLUSH_IDLE_MS senza caratteri
 *     (fine del passaggio) e all'uscita. Le ultime 4 righe restano sempre nella finestra.
 *     icap_decode_out() solo se l'app è partita da un modo decoder: avviata da Apps, sulla radio
 *     decode_out si comporta come print() e fa scorrere la finestra (provato su IC-705, v14).
 *   - stato -> icap_set_line() sulle 8 righe della finestra dell'app (vedi W_* sotto).
 *
 * Avvio: dal modo decoder "CW" registrato nell'header (RTTY → RTTY-R → CW, radio_mode keep)
 * oppure da MENU > SET > Apps. Configurazione da CW.CFG nella cartella delle app:
 *   pitch=600   (0 = ricerca automatica)    bw=80    wpm=20    squelch=12
 */
#include "icap.h"
#include "../cwdec/cwdec.h"

#define AUDIO_CHUNK   160       /* 20 ms a 8 kHz */
#define DRAW_MS       200       /* refresh finestra: 5 Hz, non saturare la UI */
#define FLUSH_IDLE_MS 2500      /* silenzio dopo cui la riga DECODE parziale viene emessa */
/* Misurato con PROBE (IC-705, foto del righello): font proporzionale, circa 39 caratteri
 * maiuscoli/cifre per riga, oltre la riga viene tagliata (niente a capo). Prefisso DECODE
 * "HHMM WPM  Hz : " = 16 caratteri + 22 di testo = 38, come la riga d'esempio dell'SDK. */
#define DEC_TEXT_W    22        /* caratteri di testo per riga DECODE (dopo il prefisso) */
#define WIN_W         39        /* larghezza visibile di una riga della finestra */
#define RMODE_CW      0x22      /* codice modo di CW in icap_radio_state (misurato con PROBE) */

/* righe della finestra (0..7); il titolo ("CW - running") lo disegna il firmware */
#define W_DEC     0             /* WPM, pitch, SNR */
#define W_RADIO   1             /* frequenza, modo della radio (solo lettura), configurazione */
#define W_MSG     2             /* origine dell'avvio, avvisi: buchi audio, errori */
#define W_HIST    3             /* righe 3..6: ultime N_HIST righe decodificate */
#define N_HIST    4
#define W_LIVE    7             /* riga in ricezione */

static cwdec_t dec;
static uint32_t now_ms;

/* ---------- utilità senza libc (divisioni solo per costanti: niente __aeabi_*) ---------- */
static char *put_s(char *p, const char *s) { while (*s) *p++ = *s++; *p = 0; return p; }
static char *put_u(char *p, uint32_t v) {
    char t[11]; int n = 0;
    do { t[n++] = (char)('0' + v % 10u); v /= 10u; } while (v);
    while (n) *p++ = t[--n];
    *p = 0;
    return p;
}
static char *put_u_w(char *p, uint32_t v, int w) {     /* allineato a destra su w colonne */
    char t[12]; char *e = put_u(t, v);
    for (int n = (int)(e - t); n < w; n++) *p++ = ' ';
    return put_s(p, t);
}
static char *put_2(char *p, uint32_t v) { *p++ = (char)('0' + v / 10u % 10u); *p++ = (char)('0' + v % 10u); *p = 0; return p; }
static int parse_u(const char *s, uint32_t *out) {
    uint32_t v = 0; int any = 0;
    while (*s >= '0' && *s <= '9') { v = v * 10u + (uint32_t)(*s++ - '0'); any = 1; }
    if (any) *out = v;
    return any;
}
static int key_is(const char *l, const char *k) {    /* "k=" all'inizio della riga */
    while (*k) if (*l++ != *k++) return 0;
    return *l == '=';
}

/* ---------- righe per la schermata DECODE ---------- */
static char cur[DEC_TEXT_W + 1];  /* testo della riga in costruzione */
static int  cur_len, word_start;  /* word_start: inizio della parola in corso (0 = nessuno spazio) */
static char cur_hhmm[5] = "----"; /* ora del primo carattere della riga */
static uint32_t last_char_ms;
static char hist[N_HIST][64];     /* storico delle righe chiuse, hist[N_HIST-1] = la più recente */
static int  hist_dirty, live_dirty;
static int  use_decode_out;       /* 1 se avviata da un modo decoder */
static char cfg_txt[24];          /* riassunto della configurazione per la riga W_RADIO */

static void stamp_now(char *hhmm) {
    struct icap_time t;
    if (icap_clock_now(&t) == 0) { put_2(put_2(hhmm, t.hour), t.min); }
    else put_s(hhmm, "----");
}

/* emette i primi n caratteri di cur come riga DECODE e tiene il resto */
static void commit(int n) {
    while (n > 0 && cur[n - 1] == ' ') n--;
    if (n > 0) {
        char line[64], *p = line;
        p = put_s(p, cur_hhmm); *p++ = ' ';
        p = put_u_w(p, (uint32_t)(cwdec_wpm(&dec) + 0.5f), 3); *p++ = ' ';
        p = put_u_w(p, (uint32_t)(cwdec_pitch(&dec) + 0.5f), 4);
        p = put_s(p, " : ");
        for (int i = 0; i < n; i++) *p++ = cur[i];
        *p = 0;
        if (use_decode_out) icap_decode_out(line);
        for (int i = 0; i < N_HIST - 1; i++) put_s(hist[i], hist[i + 1]);
        put_s(hist[N_HIST - 1], line);
        hist_dirty = 1;
    }
    int k = 0;                                        /* sposta in testa la parte non emessa */
    for (int i = n; i < cur_len; i++) if (k || cur[i] != ' ') cur[k++] = cur[i];
    cur_len = k; cur[k] = 0;
    word_start = 0;
    if (cur_len) stamp_now(cur_hhmm);
    live_dirty = 1;
}

static void on_char(void *user, char c) {
    (void)user;
    last_char_ms = now_ms;
    if (c == ' ') {
        if (cur_len == 0 || cur[cur_len - 1] == ' ') return;
        if (cur_len >= DEC_TEXT_W) { commit(cur_len); return; }
        cur[cur_len++] = ' '; cur[cur_len] = 0;
        word_start = cur_len;
        live_dirty = 1;
        return;
    }
    if (cur_len == 0) stamp_now(cur_hhmm);
    if (cur_len >= DEC_TEXT_W)                        /* a capo sulla parola, o di netto se è una sola */
        commit(word_start > 0 ? word_start : cur_len);
    cur[cur_len++] = c; cur[cur_len] = 0;
    live_dirty = 1;
}

/* ---------- finestra ---------- */
static void draw_status(void) {
    char s[WIN_W + 24], *p = s;
    p = put_u(p, (uint32_t)(cwdec_wpm(&dec) + 0.5f)); p = put_s(p, " WPM  ");
    p = put_u(p, (uint32_t)(cwdec_pitch(&dec) + 0.5f)); p = put_s(p, " Hz  SNR ");
    float snr = cwdec_snr_db(&dec);
    p = put_u(p, snr > 0.0f ? (uint32_t)snr : 0u); p = put_s(p, " dB");
    if (cwdec_tone_present(&dec)) put_s(p, "  *");               /* tono presente: squelch aperto */
    icap_set_line(W_DEC, s);
    if (hist_dirty) {
        for (int i = 0; i < N_HIST; i++) icap_set_line(W_HIST + i, hist[i]);
        hist_dirty = 0;
    }
    if (live_dirty) {
        p = put_s(s, "> ");
        put_s(p, cur);
        icap_set_line(W_LIVE, s);
        live_dirty = 0;
    }
}

static void draw_radio(void) {
    struct icap_radio rs;
    char s[WIN_W + 24], *p = s;
    if (icap_radio_state(&rs) != 0) return;
    /* kHz con due decimali; il codice modo è interno alla radio: si legge soltanto */
    p = put_u(p, rs.freq_hz / 1000u); *p++ = '.';
    p = put_2(p, rs.freq_hz / 10u % 100u);
    if (rs.mode == RMODE_CW) { p = put_s(p, " kHz CW  "); p = put_s(p, cfg_txt); }
    else {                                            /* avviso: la radio non è in CW */
        p = put_s(p, " kHz modo 0x");
        *p++ = "0123456789abcdef"[(rs.mode >> 4) & 15]; *p++ = "0123456789abcdef"[rs.mode & 15];
        p = put_s(p, ": premi CW");               /* es. RTTY-R dopo il ciclo del tasto RTTY */
    }
    if (rs.is_tx) put_s(p, "  TX");
    icap_set_line(W_RADIO, s);
}

/* es. "auto bw80 sq12" oppure "p600 bw80 sq12" (max ~16 caratteri) */
static void make_cfg_txt(const cwdec_config_t *c) {
    char *p = cfg_txt;
    if (c->pitch_hz > 0.0f) { p = put_s(p, "p"); p = put_u(p, (uint32_t)c->pitch_hz); }
    else p = put_s(p, "auto");
    p = put_s(p, " bw"); p = put_u(p, (uint32_t)c->bw_hz);
    p = put_s(p, " sq"); put_u(p, (uint32_t)c->squelch_db);
}

/* ---------- configurazione ---------- */
static void load_cfg(cwdec_config_t *cfg) {
    static char buf[256];
    int fd = icap_open("CW.CFG", ICAP_O_READ);
    if (fd < 0) return;
    int n = icap_read(fd, buf, sizeof buf - 1);
    icap_close(fd);
    if (n <= 0) return;
    buf[n] = 0;
    for (char *l = buf; *l; ) {
        char *e = l;
        while (*e && *e != '\n' && *e != '\r') e++;
        char save = *e; *e = 0;
        uint32_t v;
        if      (key_is(l, "pitch")   && parse_u(l + 6, &v)) cfg->pitch_hz = (float)v;
        else if (key_is(l, "bw")      && parse_u(l + 3, &v)) cfg->bw_hz = (float)v;
        else if (key_is(l, "wpm")     && parse_u(l + 4, &v)) cfg->init_wpm = (float)v;
        else if (key_is(l, "squelch") && parse_u(l + 8, &v)) cfg->squelch_db = (float)v;
        *e = save;
        l = *e ? e + 1 : e;
    }
}

/* attende STOP mostrando un errore: l'app non deve uscire da sola senza farsi leggere */
static int fail(const char *msg) {
    icap_set_line(W_MSG, msg);
    while (!icap_should_stop()) icap_sleep_ms(200);
    return 1;
}

/* ---------- main ---------- */
int app_main(const struct icap_svc *svc) {
    static int16_t pcm[AUDIO_CHUNK];
    icap_use(svc);
    icap_clear();

    char mode[8];
    int ml = icap_mode(mode, (int)sizeof mode);
    use_decode_out = ml > 0;
    char t[WIN_W + 24], *p = t;
    if (ml > 0) { mode[ml < (int)sizeof mode ? ml : (int)sizeof mode - 1] = 0; p = put_s(p, "modo "); p = put_s(p, mode); put_s(p, ": testo anche su DECODE"); }
    else put_s(p, "da Apps: testo qui sotto");
    icap_set_line(W_MSG, t);

    int rate = icap_audio_rate();
    if (rate == ICAP_E_NOSYS) return fail("CW: firmware senza audio (serve v1rc)");
    if (rate != CWDEC_FS)     return fail("CW: serve audio a 8 kHz");

    cwdec_config_t cfg;
    cwdec_default_config(&cfg);
    load_cfg(&cfg);
    cfg.emit = on_char;
    cwdec_init(&dec, &cfg);
    make_cfg_txt(&cfg);
    draw_radio();

    uint32_t gaps = 0, last_draw = icap_ticks(), last_radio = last_draw;
    now_ms = last_draw;

    /* ogni giro: al massimo 160 campioni (~0,1 ms di calcolo) o 10 ms di attesa,
     * quindi icap_should_stop() viene controllato ben più spesso dei 500 ms richiesti */
    while (!icap_should_stop()) {
        int n = icap_audio_read(pcm, AUDIO_CHUNK);
        now_ms = icap_ticks();
        if (n > 0) {
            cwdec_process(&dec, pcm, (size_t)n);
        } else if (n == ICAP_E_OVERRUN) {
            /* campioni persi: si prosegue senza toccare il decoder. Un buco di qualche decina di
             * ms accorcia un mark o uno space e il filtro anti-glitch lo assorbe; cwdec_flush()
             * qui farebbe danni (spinge 128 ms di silenzio e azzera la stima del rumore). */
            gaps++;
            char m[WIN_W + 24];
            put_u(put_s(m, "buchi audio: "), gaps);
            icap_set_line(W_MSG, m);
        } else if (n < 0) {
            icap_set_line(W_MSG, "CW: errore audio");
            icap_sleep_ms(100);
        } else {
            icap_sleep_ms(10);                       /* nessun campione pronto */
        }
        if (cur_len > 0 && now_ms - last_char_ms >= FLUSH_IDLE_MS) commit(cur_len);
        if (now_ms - last_draw >= DRAW_MS) { draw_status(); last_draw = now_ms; }
        if (now_ms - last_radio >= 1000u) { draw_radio(); last_radio = now_ms; }
    }
    cwdec_flush(&dec);
    commit(cur_len);
    draw_status();                                   /* l'ultima riga anche nella finestra */
    return 0;
}
