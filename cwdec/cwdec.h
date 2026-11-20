/*
 * cwdec - decoder CW (Morse) portabile, pensato per girare dentro una app ICAP
 * sull'IC-705 / IC-9700 (ARMv7-A Cortex-A9 con VFP/NEON) e, identico, su PC.
 *
 * Vincoli di progetto (validi anche sulla radio):
 *   - nessuna allocazione dinamica: tutto lo stato sta in cwdec_t
 *   - nessuna dipendenza da libc oltre a <stdint.h>/<stddef.h> (niente math.h)
 *   - input: PCM int16 mono a 8000 Hz (il loader ICAP fornisce audio a 8 kHz)
 *   - output: un carattere alla volta tramite callback
 *   - costo: poche decine di operazioni per campione + banco Goertzel (pitch e presenza tono)
 */
#ifndef CWDEC_H
#define CWDEC_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CWDEC_FS            8000    /* frequenza di campionamento attesa (Hz)      */
#define CWDEC_FRAME         16      /* campioni per frame di envelope (2 ms)        */
#define CWDEC_NBINS         41      /* bin del banco Goertzel per la ricerca pitch  */
#define CWDEC_DELAY         1024    /* linea di ritardo davanti al rivelatore (128 ms, potenza di 2) */
#define CWDEC_MAXEL         10      /* elementi massimi per carattere (oltre -> '*') */

typedef void (*cwdec_emit_fn)(void *user, char c);

typedef struct {
    float pitch_hz;      /* pitch fisso (es. CW PITCH della radio). 0 = ricerca automatica */
    float pitch_min_hz;  /* range ricerca automatica (default 300)                          */
    float pitch_max_hz;  /* (default 1300)                                                  */
    float bw_hz;         /* banda del filtro di rivelazione (default 80 Hz)                 */
    float init_wpm;      /* velocità di partenza per la stima del punto (default 20)        */
    float squelch_db;    /* SNR minimo (nella banda del filtro) per decodificare, default 12 dB */
    cwdec_emit_fn emit;  /* callback carattere (' ' = spazio tra parole)                    */
    void *user;
} cwdec_config_t;

typedef struct {
    /* configurazione */
    cwdec_config_t cfg;

    /* mixer + filtro passa-basso a 2 poli su I/Q */
    float osc_c, osc_s;          /* oscillatore ricorsivo (cos, sin) */
    float rot_c, rot_s;          /* rotazione per campione */
    float cur_hz;                /* pitch in uso */
    float lp_a;                  /* coefficiente IIR */
    float i1, q1, i2, q2;        /* stati dei due stadi */
    int   frame_n;
    float frame_acc;

    /* tracker di livello */
    float sig_lvl, noise_lvl;
    int   key;                   /* stato tasto (1 = mark) */
    float run_frames;            /* durata della corsa corrente (mark o space), in frame */
    float prev_space;            /* durata dello space precedente (per assorbire mark spuri) */
    int   mark_pending;          /* mark terminato ma non ancora confermato (anti-dropout) */
    float mark_len;

    /* timing */
    float dit;                   /* stima durata punto, in frame */
    float dah;                   /* stima durata linea, in frame */
    float cgap;                  /* stima spazio tra caratteri, in unità di punto (Farnsworth) */
    float hist[16];              /* ultimi mark, per il clustering punto/linea */
    int   hist_n, hist_i;
    float el[CWDEC_MAXEL];       /* durate dei mark del carattere in corso: classificati alla chiusura */
    int   n_el;
    int   char_open;
    int   gap_closed_char;       /* lo space corrente ha chiuso un carattere */
    int   word_emitted;

    /* ricerca pitch automatica (Goertzel) */
    float g_coef[CWDEC_NBINS];
    float g_s1[CWDEC_NBINS], g_s2[CWDEC_NBINS];
    float g_pow[CWDEC_NBINS];    /* potenze mediate */
    int   g_n;
    int   g_cur;                 /* bin selezionato */
    int   g_cand, g_cand_cnt;
    int   g_locked;              /* il bin scelto è un picco vero sopra il fondo del banco */
    int   g_fix;                 /* bin del pitch fisso (-1 = pitch automatico o fuori banco) */
    int   g_present;             /* tono presente: abilita lo squelch */
    int   g_pres_next;           /* presenza dell'ultimo blocco, in attesa di un blocco */

    /* linea di ritardo: il rivelatore lavora 128 ms indietro rispetto al Goertzel,
     * così il pitch è già agganciato quando il primo mark arriva al filtro */
    int16_t dl[CWDEC_DELAY];
    int   dl_i;

    /* statistiche */
    uint32_t chars_out;
    uint32_t frames;
} cwdec_t;

void  cwdec_default_config(cwdec_config_t *cfg);
void  cwdec_init(cwdec_t *d, const cwdec_config_t *cfg);
void  cwdec_process(cwdec_t *d, const int16_t *pcm, size_t n);
void  cwdec_flush(cwdec_t *d);          /* SOLO a fine flusso: svuota la linea di ritardo e chiude il carattere */

float cwdec_wpm(const cwdec_t *d);
float cwdec_pitch(const cwdec_t *d);
float cwdec_snr_db(const cwdec_t *d);
int   cwdec_tone_present(const cwdec_t *d);   /* 1 = tono vero nel banco: squelch abilitato */

/* utility: decodifica di un simbolo (albero binario: parte da 1, punto=0, linea=1).
 * Ritorna una stringa (un carattere, o un prosegno tipo "<SK>"), "*" se sconosciuto. */
const char *cwdec_lookup(uint16_t sym);

#ifdef __cplusplus
}
#endif
#endif
