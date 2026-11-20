/* cwdec_wav - decodifica un file WAV mono 16 bit a 8 kHz con cwdec (banco di prova su PC).
 * Uso: cwdec_wav file.wav [--pitch HZ] [--wpm N] [--bw HZ] [--squelch DB] [--stats]
 * Stampa il testo decodificato su stdout; con --stats aggiunge WPM/pitch/SNR su stderr. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../cwdec.h"

static void emit(void *user, char c) { (void)user; putchar(c); }

static int read_wav(const char *path, int16_t **out, size_t *n) {
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); return -1; }
    unsigned char h[12];
    if (fread(h, 1, 12, f) != 12 || memcmp(h, "RIFF", 4) || memcmp(h + 8, "WAVE", 4)) {
        fprintf(stderr, "%s: non è un WAV\n", path); fclose(f); return -1;
    }
    int ok_fmt = 0;
    for (;;) {
        unsigned char ch[8];
        if (fread(ch, 1, 8, f) != 8) break;
        unsigned sz = ch[4] | ch[5] << 8 | ch[6] << 16 | (unsigned)ch[7] << 24;
        if (!memcmp(ch, "fmt ", 4)) {
            unsigned char fm[16];
            if (sz < 16 || fread(fm, 1, 16, f) != 16) break;
            unsigned fmt = fm[0] | fm[1] << 8, chn = fm[2] | fm[3] << 8;
            unsigned rate = fm[4] | fm[5] << 8 | fm[6] << 16 | (unsigned)fm[7] << 24;
            unsigned bits = fm[14] | fm[15] << 8;
            if (fmt != 1 || chn != 1 || rate != CWDEC_FS || bits != 16) {
                fprintf(stderr, "%s: serve PCM mono 16 bit %d Hz (trovato fmt=%u ch=%u rate=%u bits=%u)\n",
                        path, CWDEC_FS, fmt, chn, rate, bits);
                fclose(f); return -1;
            }
            fseek(f, (long)(sz - 16 + (sz & 1)), SEEK_CUR);
            ok_fmt = 1;
        } else if (!memcmp(ch, "data", 4) && ok_fmt) {
            *n = sz / 2;
            *out = malloc(sz);
            size_t got = fread(*out, 2, *n, f);
            *n = got;
            fclose(f);
            return 0;
        } else {
            fseek(f, (long)(sz + (sz & 1)), SEEK_CUR);
        }
    }
    fprintf(stderr, "%s: chunk mancanti\n", path);
    fclose(f);
    return -1;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "uso: %s file.wav [--pitch HZ] [--wpm N] [--bw HZ] [--squelch DB] [--stats]\n", argv[0]);
        return 2;
    }
    cwdec_config_t cfg;
    cwdec_default_config(&cfg);
    int stats = 0;
    for (int i = 2; i < argc; i++) {
        if (!strcmp(argv[i], "--pitch") && i + 1 < argc) cfg.pitch_hz = (float)atof(argv[++i]);
        else if (!strcmp(argv[i], "--wpm") && i + 1 < argc) cfg.init_wpm = (float)atof(argv[++i]);
        else if (!strcmp(argv[i], "--bw") && i + 1 < argc) cfg.bw_hz = (float)atof(argv[++i]);
        else if (!strcmp(argv[i], "--squelch") && i + 1 < argc) cfg.squelch_db = (float)atof(argv[++i]);
        else if (!strcmp(argv[i], "--stats")) stats = 1;
    }
    cfg.emit = emit;

    int16_t *pcm; size_t n;
    if (read_wav(argv[1], &pcm, &n)) return 1;

    static cwdec_t d;
    cwdec_init(&d, &cfg);
    /* blocchi da 160 campioni (20 ms), come arriverebbero da un ring audio */
    for (size_t off = 0; off < n; off += 160)
        cwdec_process(&d, pcm + off, n - off < 160 ? n - off : 160);
    cwdec_flush(&d);
    putchar('\n');
    if (stats)
        fprintf(stderr, "wpm=%.1f pitch=%.0fHz snr=%.1fdB sizeof(cwdec_t)=%zu\n",
                (double)cwdec_wpm(&d), (double)cwdec_pitch(&d), (double)cwdec_snr_db(&d), sizeof d);
    free(pcm);
    return 0;
}
