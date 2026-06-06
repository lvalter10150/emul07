/*
 * wav2cas_x07.c - Convert Canon X-07 cassette WAV <-> CAS/raw bytes.
 *
 * Direction 1, WAV -> CAS:
 *   - reads a RIFF/WAVE PCM file,
 *   - detects high-level pulses,
 *   - converts pulse lengths to serial bits,
 *   - finds the Canon X-07 D3 sync,
 *   - skips sync/name/leader,
 *   - writes decoded payload bytes.
 *
 * Direction 2, CAS -> WAV:
 *   - reads raw payload bytes,
 *   - builds a Canon-like cassette bit stream:
 *       leader 1 bits,
 *       ten D3 sync frames,
 *       six filename frames,
 *       leader 1 bits,
 *       payload frames,
 *       final leader 1 bits,
 *   - writes an unsigned 8-bit PCM WAV using the usual cassette FSK idea:
 *       bit 0 = one 1200 Hz square cycle,
 *       bit 1 = two 2400 Hz square cycles.
 *
 * Build:
 *   gcc -O2 -Wall -Wextra -o x07cas wav2cas_x07.c
 *
 * Examples:
 *   ./x07cas --wav2cas input.wav output.cas
 *   ./x07cas --cas2wav input.cas output.wav --name PROG1
 *   ./x07cas --bits input.wav bits.txt
 */

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define DEFAULT_THRESHOLD_RATIO  (2.0 / 3.0)
#define DEFAULT_BAUD             2400.0
#define DEFAULT_SAMPLE_RATE      44100u
#define DEFAULT_LEADER_BITS      512u
#define DEFAULT_GAP_BITS         128u
#define DEFAULT_TAIL_BITS        256u
#define SYNC_BYTE                0xD3
#define FRAME_BITS               12
#define STOP_BITS_AFTER_DATA     3

#define WAV_HIGH                 230u
#define WAV_LOW                  25u

typedef enum {
    MODE_WAV2CAS,
    MODE_CAS2WAV,
    MODE_BITS
} Mode;

typedef struct {
    uint16_t audio_format;
    uint16_t channels;
    uint32_t sample_rate;
    uint16_t bits_per_sample;
    uint16_t block_align;
    long data_offset;
    uint32_t data_size;
} WavInfo;

typedef struct {
    unsigned char *p;
    size_t n;
    size_t cap;
} BitVec;

typedef struct {
    unsigned char *p;
    size_t n;
} ByteBuf;

static uint16_t rd16le(const unsigned char *p) {
    return (uint16_t)(p[0] | (p[1] << 8));
}

static uint32_t rd32le(const unsigned char *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void wr16le(unsigned char *p, uint16_t v) {
    p[0] = (unsigned char)(v & 0xffu);
    p[1] = (unsigned char)((v >> 8) & 0xffu);
}

static void wr32le(unsigned char *p, uint32_t v) {
    p[0] = (unsigned char)(v & 0xffu);
    p[1] = (unsigned char)((v >> 8) & 0xffu);
    p[2] = (unsigned char)((v >> 16) & 0xffu);
    p[3] = (unsigned char)((v >> 24) & 0xffu);
}

static int read_exact(FILE *f, void *buf, size_t n) {
    return fread(buf, 1, n, f) == n ? 0 : -1;
}

static int bitvec_push(BitVec *v, unsigned bit) {
    if (v->n == v->cap) {
        size_t nc = v->cap ? v->cap * 2 : 65536;
        unsigned char *np = (unsigned char *)realloc(v->p, nc);
        if (!np) return -1;
        v->p = np;
        v->cap = nc;
    }
    v->p[v->n++] = (unsigned char)(bit ? 1 : 0);
    return 0;
}

static int bitvec_push_many(BitVec *v, unsigned bit, unsigned count) {
    for (unsigned i = 0; i < count; i++) {
        if (bitvec_push(v, bit) != 0) return -1;
    }
    return 0;
}

static int bitvec_push_byte_frame(BitVec *v, uint8_t b) {
    /* Serial frame compatible with the decoder:
     * start bit 0, 8 data bits LSB first, 3 stop/mark bits 1.
     */
    if (bitvec_push(v, 0) != 0) return -1;
    for (int i = 0; i < 8; i++) {
        if (bitvec_push(v, (b >> i) & 1u) != 0) return -1;
    }
    return bitvec_push_many(v, 1, STOP_BITS_AFTER_DATA);
}

static int parse_wav(FILE *f, WavInfo *w) {
    unsigned char hdr[12];
    memset(w, 0, sizeof(*w));

    if (read_exact(f, hdr, sizeof(hdr)) != 0) {
        fprintf(stderr, "Erreur: fichier WAV trop court\n");
        return -1;
    }
    if (memcmp(hdr, "RIFF", 4) != 0 || memcmp(hdr + 8, "WAVE", 4) != 0) {
        fprintf(stderr, "Erreur: ce n'est pas un WAV RIFF/WAVE\n");
        return -1;
    }

    int have_fmt = 0, have_data = 0;
    while (!have_data) {
        unsigned char ch[8];
        if (read_exact(f, ch, sizeof(ch)) != 0) break;

        uint32_t size = rd32le(ch + 4);
        long payload = ftell(f);
        if (payload < 0) return -1;

        if (memcmp(ch, "fmt ", 4) == 0) {
            unsigned char fmt[64];
            size_t to_read = size < sizeof(fmt) ? size : sizeof(fmt);
            if (read_exact(f, fmt, to_read) != 0 || to_read < 16) {
                fprintf(stderr, "Erreur: chunk fmt WAV invalide\n");
                return -1;
            }
            w->audio_format    = rd16le(fmt + 0);
            w->channels        = rd16le(fmt + 2);
            w->sample_rate     = rd32le(fmt + 4);
            w->block_align     = rd16le(fmt + 12);
            w->bits_per_sample = rd16le(fmt + 14);
            have_fmt = 1;
        } else if (memcmp(ch, "data", 4) == 0) {
            w->data_offset = payload;
            w->data_size = size;
            have_data = 1;
        }

        long next = payload + (long)size + (size & 1u);
        if (fseek(f, next, SEEK_SET) != 0) {
            fprintf(stderr, "Erreur: seek WAV impossible\n");
            return -1;
        }
    }

    if (!have_fmt || !have_data) {
        fprintf(stderr, "Erreur: chunk fmt ou data introuvable\n");
        return -1;
    }
    if (w->audio_format != 1) {
        fprintf(stderr, "Erreur: WAV non PCM format=%u\n", w->audio_format);
        return -1;
    }
    if (w->channels < 1 || w->block_align == 0) {
        fprintf(stderr, "Erreur: format WAV invalide\n");
        return -1;
    }
    if (w->bits_per_sample != 8 && w->bits_per_sample != 16) {
        fprintf(stderr, "Erreur: seuls les WAV PCM 8 ou 16 bits sont geres\n");
        return -1;
    }
    return 0;
}

static int sample_is_high(const unsigned char *frame, const WavInfo *w, double threshold_ratio) {
    long sum = 0;

    for (uint16_t c = 0; c < w->channels; c++) {
        if (w->bits_per_sample == 8) {
            sum += frame[c];
        } else {
            const unsigned char *p = frame + c * 2;
            int16_t s = (int16_t)rd16le(p);
            sum += ((long)s + 32768L) >> 8;
        }
    }

    double avg = (double)sum / (double)w->channels;
    return avg > (255.0 * threshold_ratio);
}

static int wav_to_bits(FILE *f, const WavInfo *w, double threshold_ratio, BitVec *bits) {
    if (fseek(f, w->data_offset, SEEK_SET) != 0) return -1;

    unsigned char *frame = (unsigned char *)malloc(w->block_align);
    if (!frame) return -1;

    const unsigned long long frames = w->data_size / w->block_align;
    const double long_limit = (double)w->sample_rate / (2.0 * DEFAULT_BAUD);

    int old_short_state = 11;
    unsigned long long i = 0;

    while (i < frames) {
        if (read_exact(f, frame, w->block_align) != 0) break;
        i++;

        if (!sample_is_high(frame, w, threshold_ratio)) continue;

        unsigned long pulse_len = 1;
        while (i < frames) {
            if (read_exact(f, frame, w->block_align) != 0) break;
            i++;
            if (!sample_is_high(frame, w, threshold_ratio)) break;
            pulse_len++;
        }

        if ((double)pulse_len > long_limit) {
            if (bitvec_push(bits, 0) != 0) { free(frame); return -1; }
        } else {
            old_short_state = (old_short_state == 11) ? 1 : 11;
            if (old_short_state == 1) {
                if (bitvec_push(bits, 1) != 0) { free(frame); return -1; }
            }
        }
    }

    free(frame);
    return 0;
}

static int match_byte_lsb(const BitVec *bits, size_t pos, uint8_t value) {
    if (pos + 8 > bits->n) return 0;
    for (int i = 0; i < 8; i++) {
        unsigned b = (value >> i) & 1u;
        if (bits->p[pos + (size_t)i] != b) return 0;
    }
    return 1;
}

static long find_sync_d3(const BitVec *bits) {
    for (size_t i = 0; i + 8 <= bits->n; i++) {
        if (match_byte_lsb(bits, i, SYNC_BYTE)) return (long)i;
    }
    return -1;
}

static int write_bits_text(const char *path, const BitVec *bits) {
    FILE *out = fopen(path, "wb");
    if (!out) return -1;
    for (size_t i = 0; i < bits->n; i++) fputc(bits->p[i] ? '1' : '0', out);
    int err = ferror(out);
    fclose(out);
    return err ? -1 : 0;
}

static int decode_payload_to_cas(const char *path, const BitVec *bits, int verbose) {
    long sync = find_sync_d3(bits);
    if (sync < 0) {
        fprintf(stderr, "Erreur: synchro D3 introuvable\n");
        return -1;
    }

    size_t pos = (size_t)sync + 8;
    pos += STOP_BITS_AFTER_DATA;
    pos += 9 * FRAME_BITS;
    pos += 6 * FRAME_BITS;

    while (pos < bits->n && bits->p[pos] == 1) pos++;
    if (pos >= bits->n) {
        fprintf(stderr, "Erreur: debut donnees introuvable apres l'entete\n");
        return -1;
    }

    FILE *out = fopen(path, "wb");
    if (!out) return -1;

    size_t nbytes = 0;
    while (pos + 1 + 8 <= bits->n) {
        if (bits->p[pos] != 0 && verbose) {
            fprintf(stderr, "Attention: start bit inattendu a bit %zu\n", pos);
        }
        pos++;

        uint8_t code = 0;
        for (int i = 0; i < 8; i++) {
            code |= (uint8_t)(bits->p[pos++] << i);
        }
        fputc(code, out);
        nbytes++;

        for (int i = 0; i < STOP_BITS_AFTER_DATA && pos < bits->n; i++) pos++;

        size_t look = pos;
        while (look < bits->n && bits->p[look] == 1) look++;
        if (look >= bits->n) break;
    }

    int err = ferror(out);
    fclose(out);
    if (err) return -1;

    if (verbose) fprintf(stderr, "CAS: %zu octets ecrits\n", nbytes);
    return 0;
}

static int read_whole_file(const char *path, ByteBuf *b) {
    memset(b, 0, sizeof(*b));
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return -1; }
    long sz = ftell(f);
    if (sz < 0) { fclose(f); return -1; }
    rewind(f);

    b->p = (unsigned char *)malloc((size_t)sz ? (size_t)sz : 1u);
    if (!b->p) { fclose(f); return -1; }
    b->n = (size_t)sz;
    if (b->n && fread(b->p, 1, b->n, f) != b->n) {
        free(b->p);
        b->p = NULL;
        b->n = 0;
        fclose(f);
        return -1;
    }
    fclose(f);
    return 0;
}

static void normalize_name6(const char *name, unsigned char out[6]) {
    memset(out, ' ', 6);
    if (!name) name = "CAS";
    for (int i = 0; i < 6 && name[i]; i++) {
        unsigned char c = (unsigned char)name[i];
        out[i] = (unsigned char)toupper(c);
    }
}

static int cas_to_bitstream(const ByteBuf *cas, const char *name, BitVec *bits) {
    unsigned char fname[6];
    normalize_name6(name, fname);

    if (bitvec_push_many(bits, 1, DEFAULT_LEADER_BITS) != 0) return -1;

    for (int i = 0; i < 10; i++) {
        if (bitvec_push_byte_frame(bits, SYNC_BYTE) != 0) return -1;
    }

    for (int i = 0; i < 6; i++) {
        if (bitvec_push_byte_frame(bits, fname[i]) != 0) return -1;
    }

    if (bitvec_push_many(bits, 1, DEFAULT_GAP_BITS) != 0) return -1;

    for (size_t i = 0; i < cas->n; i++) {
        if (bitvec_push_byte_frame(bits, cas->p[i]) != 0) return -1;
    }

    return bitvec_push_many(bits, 1, DEFAULT_TAIL_BITS);
}

static int wav_write_header(FILE *f, uint32_t sample_rate, uint32_t data_size) {
    unsigned char h[44];
    memset(h, 0, sizeof(h));
    memcpy(h + 0, "RIFF", 4);
    wr32le(h + 4, 36u + data_size);
    memcpy(h + 8, "WAVE", 4);
    memcpy(h + 12, "fmt ", 4);
    wr32le(h + 16, 16);
    wr16le(h + 20, 1);          /* PCM */
    wr16le(h + 22, 1);          /* mono */
    wr32le(h + 24, sample_rate);
    wr32le(h + 28, sample_rate);/* byte rate, 8-bit mono */
    wr16le(h + 32, 1);          /* block align */
    wr16le(h + 34, 8);          /* bits/sample */
    memcpy(h + 36, "data", 4);
    wr32le(h + 40, data_size);
    return fwrite(h, 1, sizeof(h), f) == sizeof(h) ? 0 : -1;
}

static uint32_t samples_for_bit(unsigned bit, uint32_t sample_rate) {
    unsigned half1 = (unsigned)((sample_rate + (uint32_t)DEFAULT_BAUD) / (uint32_t)(2.0 * DEFAULT_BAUD));
    unsigned half0 = (unsigned)((sample_rate + (uint32_t)(DEFAULT_BAUD / 2.0)) / (uint32_t)DEFAULT_BAUD);
    if (half1 < 1) half1 = 1;
    if (half0 < 2) half0 = 2;
    return bit ? (uint32_t)(4u * half1) : (uint32_t)(2u * half0);
}

static int write_run(FILE *f, unsigned char value, unsigned count) {
    for (unsigned i = 0; i < count; i++) {
        if (fputc(value, f) == EOF) return -1;
    }
    return 0;
}

static int write_bit_as_fsk(FILE *f, unsigned bit, uint32_t sample_rate) {
    unsigned half1 = (unsigned)((sample_rate + (uint32_t)DEFAULT_BAUD) / (uint32_t)(2.0 * DEFAULT_BAUD));
    unsigned half0 = (unsigned)((sample_rate + (uint32_t)(DEFAULT_BAUD / 2.0)) / (uint32_t)DEFAULT_BAUD);
    if (half1 < 1) half1 = 1;
    if (half0 < 2) half0 = 2;

    if (bit) {
        /* 2 cycles at 2400 Hz */
        if (write_run(f, WAV_HIGH, half1) != 0) return -1;
        if (write_run(f, WAV_LOW,  half1) != 0) return -1;
        if (write_run(f, WAV_HIGH, half1) != 0) return -1;
        if (write_run(f, WAV_LOW,  half1) != 0) return -1;
    } else {
        /* 1 cycle at 1200 Hz */
        if (write_run(f, WAV_HIGH, half0) != 0) return -1;
        if (write_run(f, WAV_LOW,  half0) != 0) return -1;
    }
    return 0;
}

static int write_wav_from_bits(const char *path, const BitVec *bits, uint32_t sample_rate) {
    uint64_t total = 0;
    for (size_t i = 0; i < bits->n; i++) total += samples_for_bit(bits->p[i], sample_rate);
    if (total > 0xffffffffu) {
        fprintf(stderr, "Erreur: WAV trop gros\n");
        return -1;
    }

    FILE *out = fopen(path, "wb");
    if (!out) return -1;
    if (wav_write_header(out, sample_rate, (uint32_t)total) != 0) {
        fclose(out);
        return -1;
    }

    for (size_t i = 0; i < bits->n; i++) {
        if (write_bit_as_fsk(out, bits->p[i], sample_rate) != 0) {
            fclose(out);
            return -1;
        }
    }

    int err = ferror(out);
    fclose(out);
    return err ? -1 : 0;
}

static int cas_to_wav_file(const char *inpath, const char *outpath, const char *name, uint32_t sample_rate, int verbose) {
    ByteBuf cas;
    if (read_whole_file(inpath, &cas) != 0) {
        fprintf(stderr, "Erreur lecture %s: %s\n", inpath, strerror(errno));
        return -1;
    }

    BitVec bits = {0};
    if (cas_to_bitstream(&cas, name, &bits) != 0) {
        fprintf(stderr, "Erreur construction bitstream CAS\n");
        free(cas.p);
        free(bits.p);
        return -1;
    }

    int rc = write_wav_from_bits(outpath, &bits, sample_rate);
    if (rc == 0 && verbose) {
        fprintf(stderr, "WAV: %zu octets CAS, %zu bits, %u Hz, nom %.6s\n",
                cas.n, bits.n, sample_rate, name ? name : "CAS   ");
    }

    free(cas.p);
    free(bits.p);
    return rc;
}

static void usage(const char *argv0) {
    fprintf(stderr,
        "Usage:\n"
        "  %s --wav2cas [options] input.wav output.cas\n"
        "  %s --cas2wav [options] input.cas output.wav\n"
        "  %s --bits   [options] input.wav output_bits.txt\n\n"
        "Options WAV->CAS:\n"
        "  --threshold R       seuil haut, defaut 0.666666\n\n"
        "Options CAS->WAV:\n"
        "  --name NOM          nom cassette X-07, 6 caracteres max, defaut CAS\n"
        "  --rate HZ           frequence WAV, defaut 44100\n\n"
        "Options communes:\n"
        "  --quiet             moins de messages\n",
        argv0, argv0, argv0);
}

int main(int argc, char **argv) {
    Mode mode = MODE_WAV2CAS;
    int mode_set = 0;
    int verbose = 1;
    double threshold_ratio = DEFAULT_THRESHOLD_RATIO;
    uint32_t sample_rate = DEFAULT_SAMPLE_RATE;
    const char *cas_name = "CAS";
    const char *inpath = NULL;
    const char *outpath = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--wav2cas") == 0) {
            mode = MODE_WAV2CAS; mode_set = 1;
        } else if (strcmp(argv[i], "--cas2wav") == 0) {
            mode = MODE_CAS2WAV; mode_set = 1;
        } else if (strcmp(argv[i], "--bits") == 0) {
            mode = MODE_BITS; mode_set = 1;
        } else if (strcmp(argv[i], "--quiet") == 0) {
            verbose = 0;
        } else if (strcmp(argv[i], "--threshold") == 0) {
            if (++i >= argc) { usage(argv[0]); return 2; }
            threshold_ratio = atof(argv[i]);
            if (threshold_ratio <= 0.0 || threshold_ratio >= 1.0) {
                fprintf(stderr, "Erreur: threshold doit etre entre 0 et 1\n");
                return 2;
            }
        } else if (strcmp(argv[i], "--name") == 0) {
            if (++i >= argc) { usage(argv[0]); return 2; }
            cas_name = argv[i];
        } else if (strcmp(argv[i], "--rate") == 0) {
            if (++i >= argc) { usage(argv[0]); return 2; }
            long r = atol(argv[i]);
            if (r < 8000 || r > 192000) {
                fprintf(stderr, "Erreur: rate doit etre entre 8000 et 192000\n");
                return 2;
            }
            sample_rate = (uint32_t)r;
        } else if (!inpath) {
            inpath = argv[i];
        } else if (!outpath) {
            outpath = argv[i];
        } else {
            usage(argv[0]);
            return 2;
        }
    }

    (void)mode_set;
    if (!inpath || !outpath) {
        usage(argv[0]);
        return 2;
    }

    if (mode == MODE_CAS2WAV) {
        if (cas_to_wav_file(inpath, outpath, cas_name, sample_rate, verbose) != 0) {
            fprintf(stderr, "Erreur conversion CAS->WAV vers %s: %s\n", outpath, strerror(errno));
            return 1;
        }
        return 0;
    }

    FILE *in = fopen(inpath, "rb");
    if (!in) {
        fprintf(stderr, "Erreur ouverture %s: %s\n", inpath, strerror(errno));
        return 1;
    }

    WavInfo w;
    if (parse_wav(in, &w) != 0) {
        fclose(in);
        return 1;
    }

    if (verbose) {
        fprintf(stderr, "WAV: PCM %u canal(aux), %u Hz, %u bits, data=%u octets\n",
                w.channels, w.sample_rate, w.bits_per_sample, w.data_size);
    }

    BitVec bits = {0};
    if (wav_to_bits(in, &w, threshold_ratio, &bits) != 0) {
        fprintf(stderr, "Erreur pendant le decodage WAV\n");
        free(bits.p);
        fclose(in);
        return 1;
    }
    fclose(in);

    if (verbose) fprintf(stderr, "Bits detectes: %zu\n", bits.n);

    int rc = (mode == MODE_BITS) ? write_bits_text(outpath, &bits)
                                 : decode_payload_to_cas(outpath, &bits, verbose);
    free(bits.p);

    if (rc != 0) {
        fprintf(stderr, "Erreur ecriture/decodage vers %s: %s\n", outpath, strerror(errno));
        return 1;
    }

    return 0;
}
