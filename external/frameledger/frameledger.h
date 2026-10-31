/* frameledger.h — an optional writer for frameledger run files.
 *
 * The format is docs/specs/FLED-0001-run-file-format.md; § 4.6 is this
 * header's contract. An engine may write run files without it.
 *
 * The writer formats what the engine hands it and times nothing. It never
 * allocates, does no file I/O and never touches the locale: the engine
 * hands it a buffer at fled_open, rows go into that buffer, and the engine
 * takes the bytes with fled_pending and fled_consume and writes them out
 * when it chooses. When fewer than FLED_ROW_MAX bytes are free, a row is
 * dropped and counted, and the count goes into the close line.
 *
 * Sizing: a buffer of the opening lines + FLED_CLOSE_MAX + N * FLED_ROW_MAX
 * guarantees N rows of any size. Rows are written at their true size,
 * typically about 40 bytes, so N * 40 + FLED_ROW_MAX is enough in practice.
 *
 *     static char buf[1 << 20];
 *     fled_writer w;
 *     fled_open(&w, buf, sizeof buf, "run-42", header_json, NULL);
 *     fled_row(&w, "view:1", 0, "frame", "cpu_span", 0, 8.25);
 *     ...
 *     fled_close(&w, frames);
 *     n = fled_pending(&w, &bytes);
 *     fwrite(bytes, 1, n, file);
 *     fled_consume(&w, n);
 *
 * One thread: the caller's. Every function is static inline, so one
 * writer may be shared by C and C++ files of the same program. Compiles
 * as C99 and as C++17.
 */
#ifndef FRAMELEDGER_H
#define FRAMELEDGER_H

#include <math.h>   /* llround */
#include <stddef.h> /* size_t */
#include <string.h> /* memchr, memcmp, memcpy, memmove, strcmp, strlen */

#define FLED_SCHEMA     1
#define FLED_NAME_MAX   64 /* name */
#define FLED_PART_MAX   32 /* category; segment kind; segment id */
#define FLED_RUN_ID_MAX 64

/* The largest row in bytes. A run_id or segment holding a comma or a
   quote is quoted with each quote doubled, so takes at most twice its
   length plus two. Then frame (19 digits), category, name, depth (10
   digits), value (14 bytes, "-999999999.999"), six commas and the
   newline. */
#define FLED_ROW_MAX                                                       \
    ((2 * FLED_RUN_ID_MAX + 2) + (2 * (2 * FLED_PART_MAX + 1) + 2) + 19 + \
     FLED_PART_MAX + FLED_NAME_MAX + 10 + 14 + 6 + 1)

/* The close line at its largest, two 19-digit counts. fled_open keeps
   this much free, so the close line is never dropped. */
#define FLED_CLOSE_MAX                                                     \
    (sizeof "# frameledger-close {\"rows_dropped\":,\"frames\":}\n" - 1 + 19 + 19)

enum { FLED_OK = 0, FLED_DROPPED = 1, FLED_BAD_ARG = 2 };

/* The fields are visible so the engine can place a writer anywhere; their
   contents are the writer's. */
typedef struct fled_writer {
    char *buf;
    size_t cap;
    size_t len;          /* filled bytes not yet consumed */
    long long dropped;
    int state;           /* 0 not open, 1 open, 2 closed */
    size_t run_id_len;
    char run_id[2 * FLED_RUN_ID_MAX + 2]; /* as a row writes it, quoted if need be */
} fled_writer;

#ifdef __cplusplus
extern "C" {
#endif

/* Helpers; the fled__ prefix marks them as not part of the interface. */

static inline int fled__is_name_char(char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
           c == '_' || c == '.' || c == '/' || c == '-';
}

/* 1 when src holds no byte below 0x20 and no DEL. A CR or LF would end
   the line early, and the reader would take the rest as a new line. */
static inline int fled__is_line_safe(const char *src, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++) {
        unsigned char c = (unsigned char)src[i];
        if (c < 0x20 || c == 0x7f)
            return 0;
    }
    return 1;
}

/* 1 when src is 1 to max bytes of § 4.4's name set. */
static inline int fled__is_name(const char *src, size_t max)
{
    size_t n = strlen(src), i;
    if (n == 0 || n > max)
        return 0;
    for (i = 0; i < n; i++)
        if (!fled__is_name_char(src[i]))
            return 0;
    return 1;
}

/* § 4.4: frame, cpu, gpu, mem, event, or ext.<engine>, at most 32 bytes. */
static inline int fled__is_category(const char *src)
{
    size_t n = strlen(src);
    if (n > FLED_PART_MAX)
        return 0;
    if (strcmp(src, "frame") == 0 || strcmp(src, "cpu") == 0 || strcmp(src, "gpu") == 0 ||
        strcmp(src, "mem") == 0 || strcmp(src, "event") == 0)
        return 1;
    return n > 4 && memcmp(src, "ext.", 4) == 0 && fled__is_name(src + 4, FLED_PART_MAX - 4);
}

/* § 4.4: empty, or <kind>:<id> with each part 1 to 32 bytes. The kind
   ends at the first colon, as the analyser reads it. */
static inline int fled__is_segment(const char *src)
{
    size_t n = strlen(src), kind;
    const char *colon;
    if (n == 0)
        return 1;
    if (!fled__is_line_safe(src, n))
        return 0;
    colon = (const char *)memchr(src, ':', n);
    if (colon == NULL)
        return 0;
    kind = (size_t)(colon - src);
    return kind >= 1 && kind <= FLED_PART_MAX && n - kind - 1 >= 1 && n - kind - 1 <= FLED_PART_MAX;
}

/* Writes src as one CSV field, quoted when it holds a comma or a quote
   (RFC 4180), and returns the bytes written: at most 2n + 2. */
static inline size_t fled__put_field(char *dst, const char *src, size_t n)
{
    size_t i, out = 0;
    if (memchr(src, ',', n) == NULL && memchr(src, '"', n) == NULL) {
        memcpy(dst, src, n);
        return n;
    }
    dst[out++] = '"';
    for (i = 0; i < n; i++) {
        if (src[i] == '"')
            dst[out++] = '"';
        dst[out++] = src[i];
    }
    dst[out++] = '"';
    return out;
}

/* Writes v in decimal, without the locale, and returns the bytes written:
   at most 20. */
static inline size_t fled__put_unsigned(char *dst, unsigned long long v)
{
    char digits[20];
    size_t n = 0, i;
    do {
        digits[n++] = (char)('0' + (int)(v % 10));
        v /= 10;
    } while (v != 0);
    for (i = 0; i < n; i++)
        dst[i] = digits[n - 1 - i];
    return n;
}

/* Writes a count of thousandths as a § 4.4 value, "-12.345", and returns
   the bytes written. The sign is the rounded number's, so a value that
   rounds to zero is "0.000", never "-0.000". */
static inline size_t fled__put_value(char *dst, long long thousandths)
{
    unsigned long long magnitude;
    size_t out = 0;
    if (thousandths < 0) {
        dst[out++] = '-';
        magnitude = (unsigned long long)(-thousandths);
    } else {
        magnitude = (unsigned long long)thousandths;
    }
    out += fled__put_unsigned(dst + out, magnitude / 1000);
    dst[out++] = '.';
    dst[out++] = (char)('0' + (int)(magnitude / 100 % 10));
    dst[out++] = (char)('0' + (int)(magnitude / 10 % 10));
    dst[out++] = (char)('0' + (int)(magnitude % 10));
    return out;
}

static inline size_t fled__put_text(char *dst, const char *src)
{
    size_t n = strlen(src);
    memcpy(dst, src, n);
    return n;
}

/* Copies src into dst, which holds cap bytes, with every quote, backslash,
   control character and DEL escaped for a JSON string, so fled_open
   always takes the result. It adds no surrounding
   quotes, and NUL-terminates dst when cap is above zero. Other bytes,
   UTF-8 included, pass through. Returns the length the whole escaped text
   needs, not counting the NUL, so a result of cap or more means dst was
   cut short; never inside an escape. */
static inline size_t fled_json_escape(char *dst, size_t cap, const char *src)
{
    static const char hex[] = "0123456789abcdef";
    size_t need = 0, out = 0;
    for (; *src != '\0'; src++) {
        unsigned char c = (unsigned char)*src;
        char escape[6];
        size_t n = 0;
        if (c == '"' || c == '\\') {
            escape[n++] = '\\';
            escape[n++] = (char)c;
        } else if (c == '\n') {
            escape[n++] = '\\';
            escape[n++] = 'n';
        } else if (c == '\r') {
            escape[n++] = '\\';
            escape[n++] = 'r';
        } else if (c == '\t') {
            escape[n++] = '\\';
            escape[n++] = 't';
        } else if (c < 0x20 || c == 0x7f) {
            escape[n++] = '\\';
            escape[n++] = 'u';
            escape[n++] = '0';
            escape[n++] = '0';
            escape[n++] = hex[c >> 4];
            escape[n++] = hex[c & 0xf];
        } else {
            escape[n++] = (char)c;
        }
        /* Once one escape does not fit, nothing after it is written. */
        if (dst != NULL && out == need && out + n < cap) {
            memcpy(dst + out, escape, n);
            out += n;
        }
        need += n;
    }
    if (dst != NULL && cap > 0)
        dst[out] = '\0';
    return need;
}

/* Writes the header, static and column lines into buf. header_json, and
   static_json unless it is NULL, are complete one-line JSON objects the
   engine built; fled_json_escape helps build them. Returns FLED_BAD_ARG,
   writing nothing, for a NULL w, buf, run_id or header_json; a run_id of
   0 or over FLED_RUN_ID_MAX bytes; an empty JSON text; a line break or
   other control character in any of them; or a buf too small for those
   lines and the close line. A refused writer has nothing pending, so an
   engine's flush path may still call fled_pending. */
static inline int fled_open(fled_writer *w, char *buf, size_t cap, const char *run_id,
                            const char *header_json, const char *static_json)
{
    static const char header[] = "# frameledger ";
    static const char stat[] = "# frameledger-static ";
    static const char columns[] = "run_id,segment,frame,category,name,depth,value\n";
    size_t id_len, header_len, static_len = 0, need;
    if (w == NULL)
        return FLED_BAD_ARG;
    w->buf = NULL;
    w->cap = 0;
    w->len = 0;
    w->dropped = 0;
    w->state = 0;
    if (buf == NULL || run_id == NULL || header_json == NULL)
        return FLED_BAD_ARG;
    id_len = strlen(run_id);
    header_len = strlen(header_json);
    if (static_json != NULL)
        static_len = strlen(static_json);
    if (id_len == 0 || id_len > FLED_RUN_ID_MAX || !fled__is_line_safe(run_id, id_len) ||
        header_len == 0 || !fled__is_line_safe(header_json, header_len) ||
        (static_json != NULL && (static_len == 0 || !fled__is_line_safe(static_json, static_len))))
        return FLED_BAD_ARG;
    need = sizeof header - 1 + header_len + 1 + sizeof columns - 1 + FLED_CLOSE_MAX;
    if (static_json != NULL)
        need += sizeof stat - 1 + static_len + 1;
    if (cap < need)
        return FLED_BAD_ARG;

    w->buf = buf;
    w->cap = cap;
    w->run_id_len = fled__put_field(w->run_id, run_id, id_len);
    w->len += fled__put_text(buf + w->len, header);
    memcpy(buf + w->len, header_json, header_len);
    w->len += header_len;
    buf[w->len++] = '\n';
    if (static_json != NULL) {
        w->len += fled__put_text(buf + w->len, stat);
        memcpy(buf + w->len, static_json, static_len);
        w->len += static_len;
        buf[w->len++] = '\n';
    }
    w->len += fled__put_text(buf + w->len, columns);
    w->state = 1;
    return FLED_OK;
}

/* Appends one row; segment may be NULL or "" for none. The value is
   rounded to thousandths: llround of value * 1000 as a double, so ties go
   away from zero, but 8.2505 is 8250.4999... and writes 8.250.
   Returns FLED_BAD_ARG, writing nothing, for a writer not open, a field
   breaking § 4.4, a negative frame or depth, or a value that is not
   finite or rounds to 1 000 000 000 or more in size. That is checked
   before the space, so a bad row is never counted as dropped.
   Returns FLED_DROPPED, and counts the row, when fewer than FLED_ROW_MAX
   bytes are free. */
static inline int fled_row(fled_writer *w, const char *segment, long long frame,
                           const char *category, const char *name, int depth, double value)
{
    long long thousandths;
    char *out;
    if (w == NULL || w->state != 1 || category == NULL || name == NULL)
        return FLED_BAD_ARG;
    if (segment == NULL)
        segment = "";
    if (!fled__is_segment(segment) || frame < 0 || !fled__is_category(category) ||
        !fled__is_name(name, FLED_NAME_MAX) || depth < 0)
        return FLED_BAD_ARG;
    /* NaN fails both comparisons; the bound also keeps llround in range. */
    if (!(value > -1e9 - 1 && value < 1e9 + 1))
        return FLED_BAD_ARG;
    thousandths = llround(value * 1000.0);
    if (thousandths <= -1000000000000LL || thousandths >= 1000000000000LL)
        return FLED_BAD_ARG;
    /* Cannot wrap: fled_open left FLED_CLOSE_MAX free, and every row keeps
       it free, since a row is written only with FLED_ROW_MAX more spare. */
    if (w->cap - w->len - FLED_CLOSE_MAX < FLED_ROW_MAX) {
        w->dropped++;
        return FLED_DROPPED;
    }

    out = w->buf + w->len;
    memcpy(out, w->run_id, w->run_id_len);
    out += w->run_id_len;
    *out++ = ',';
    out += fled__put_field(out, segment, strlen(segment));
    *out++ = ',';
    out += fled__put_unsigned(out, (unsigned long long)frame);
    *out++ = ',';
    out += fled__put_text(out, category);
    *out++ = ',';
    out += fled__put_text(out, name);
    *out++ = ',';
    out += fled__put_unsigned(out, (unsigned long long)depth);
    *out++ = ',';
    out += fled__put_value(out, thousandths);
    *out++ = '\n';
    w->len = (size_t)(out - w->buf);
    return FLED_OK;
}

/* Points *bytes at the filled bytes the engine has not consumed, and
   returns how many there are. */
static inline size_t fled_pending(const fled_writer *w, const char **bytes)
{
    if (bytes != NULL)
        *bytes = w->buf;
    return w->len;
}

/* Frees the first n pending bytes, once the engine has written them out,
   and moves the rest to the buffer's start, so the space is reused. */
static inline void fled_consume(fled_writer *w, size_t n)
{
    if (n > w->len)
        n = w->len;
    if (n == 0)
        return; /* also a refused writer, whose buf is NULL */
    memmove(w->buf, w->buf + n, w->len - n);
    w->len -= n;
}

/* Appends the close line, with the drop count and the engine's frame
   count. Never dropped: fled_open kept room for it. Returns FLED_BAD_ARG
   for a writer not open or a negative frames. After it, fled_row and
   fled_close refuse. */
static inline int fled_close(fled_writer *w, long long frames)
{
    char *out;
    if (w == NULL || w->state != 1 || frames < 0)
        return FLED_BAD_ARG;
    out = w->buf + w->len;
    out += fled__put_text(out, "# frameledger-close {\"rows_dropped\":");
    out += fled__put_unsigned(out, (unsigned long long)w->dropped);
    out += fled__put_text(out, ",\"frames\":");
    out += fled__put_unsigned(out, (unsigned long long)frames);
    *out++ = '}';
    *out++ = '\n';
    w->len = (size_t)(out - w->buf);
    w->state = 2;
    return FLED_OK;
}

#ifdef __cplusplus
}
#endif

#endif /* FRAMELEDGER_H */
