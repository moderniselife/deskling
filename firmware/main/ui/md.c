// Markdown for chat replies (see md.h). A small line-by-line parser: enough of CommonMark for
// what agents write, drawn with LVGL span groups so bold, code and links sit inside wrapped text.
#include "md.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "theme.h"

LV_FONT_DECLARE(deskling_font_19b)
LV_FONT_DECLARE(deskling_font_mono_17)

#define MD_MAX_LINES 800
#define MD_MAX_TABLE_ROWS 40
#define MD_MAX_COLS 6

typedef struct {
    int32_t max_w;
    bool wide;
    const md_style_t *st;
} ctx_t;

enum { F_BOLD = 1, F_CODE = 2, F_EM = 4, F_LINK = 8, F_STRIKE = 16 };

typedef struct {
    const char *p;
    size_t n;
} line_t;

// ------------------------------------------------------------------ inline

static void add_span(lv_obj_t *sg, const char *s, size_t n, int flags, const ctx_t *c, const lv_font_t *base, lv_color_t color)
{
    if (!n) return;
    char *t = malloc(n + 1);
    if (!t) return;
    memcpy(t, s, n);
    t[n] = '\0';
    lv_span_t *sp = lv_spangroup_add_span(sg);
    if (!sp) {
        free(t);
        return;
    }
    lv_span_set_text(sp, t); // copies
    free(t);
    lv_style_t *style = lv_span_get_style(sp);
    const lv_font_t *f = base;
    if (flags & F_CODE) f = &deskling_font_mono_17;
    else if ((flags & F_BOLD) && base == DK_FONT_M) f = &deskling_font_19b;
    lv_style_set_text_font(style, f);
    lv_style_set_text_color(style, (flags & (F_CODE | F_LINK)) ? c->st->accent : color);
    if (flags & F_LINK) lv_style_set_text_decor(style, LV_TEXT_DECOR_UNDERLINE);
    else if (flags & F_STRIKE) lv_style_set_text_decor(style, LV_TEXT_DECOR_STRIKETHROUGH);
}

static const char *find2(const char *s, size_t n, char a, char b)
{
    for (size_t i = 0; i + 1 < n; i++)
        if (s[i] == a && s[i + 1] == b) return s + i;
    return NULL;
}

static void inline_spans(lv_obj_t *sg, const char *s, size_t n, const ctx_t *c, const lv_font_t *base, lv_color_t color, int flags)
{
    char *buf = malloc(n + 1);
    if (!buf) return;
    size_t bl = 0;
#define FLUSH()                                                                                                        \
    do {                                                                                                               \
        add_span(sg, buf, bl, flags, c, base, color);                                                                  \
        bl = 0;                                                                                                        \
    } while (0)
    for (size_t i = 0; i < n;) {
        char ch = s[i];
        if (ch == '\\' && i + 1 < n && strchr("\\`*_[]()#+-.!~|>", s[i + 1])) {
            buf[bl++] = s[i + 1];
            i += 2;
            continue;
        }
        if (ch == '`') {
            const char *e = memchr(s + i + 1, '`', n - i - 1);
            if (e) {
                FLUSH();
                add_span(sg, s + i + 1, (size_t)(e - (s + i + 1)), flags | F_CODE, c, base, color);
                i = (size_t)(e - s) + 1;
                continue;
            }
        }
        if ((ch == '*' || ch == '_') && i + 1 < n && s[i + 1] == ch) {
            if (flags & F_BOLD) {
                FLUSH();
                flags &= ~F_BOLD;
                i += 2;
                continue;
            }
            if (i + 2 < n && !isspace((unsigned char)s[i + 2]) && find2(s + i + 2, n - i - 2, ch, ch)) {
                FLUSH();
                flags |= F_BOLD;
                i += 2;
                continue;
            }
        }
        if (ch == '~' && i + 1 < n && s[i + 1] == '~') {
            if ((flags & F_STRIKE) || find2(s + i + 2, n - i - 2, '~', '~')) {
                FLUSH();
                flags ^= F_STRIKE;
                i += 2;
                continue;
            }
        }
        if (ch == '*' || ch == '_') {
            // Emphasis: drop the markers (one weight of italics isn't in the fonts). An underscore
            // inside a word (snake_case) is just an underscore.
            bool word_before = i > 0 && isalnum((unsigned char)s[i - 1]);
            bool word_after = i + 1 < n && isalnum((unsigned char)s[i + 1]);
            if (flags & F_EM) {
                if (!(ch == '_' && word_after)) {
                    FLUSH();
                    flags &= ~F_EM;
                    i++;
                    continue;
                }
            } else if (i + 1 < n && !isspace((unsigned char)s[i + 1]) && !(ch == '_' && word_before) && memchr(s + i + 1, ch, n - i - 1)) {
                FLUSH();
                flags |= F_EM;
                i++;
                continue;
            }
        }
        if (ch == '[') {
            const char *rb = find2(s + i + 1, n - i - 1, ']', '(');
            const char *rp = rb ? memchr(rb + 2, ')', (size_t)(s + n - (rb + 2))) : NULL;
            if (rb && rp) {
                FLUSH();
                add_span(sg, s + i + 1, (size_t)(rb - (s + i + 1)), flags | F_LINK, c, base, color);
                i = (size_t)(rp - s) + 1;
                continue;
            }
        }
        buf[bl++] = ch;
        i++;
    }
    FLUSH();
#undef FLUSH
    free(buf);
}

/* A paragraph of styled text, as narrow as it can be (or exactly `w` in the reader and tables). */
static lv_obj_t *para(lv_obj_t *parent, const char *s, size_t n, const ctx_t *c, const lv_font_t *font, lv_color_t color, int flags, int32_t w, bool fixed)
{
    lv_obj_t *sg = lv_spangroup_create(parent);
    lv_obj_remove_flag(sg, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_line_space(sg, 3, 0);
    inline_spans(sg, s, n, c, font, color, flags);
    lv_spangroup_refresh(sg);
    int32_t width = w;
    if (!fixed) {
        width = (int32_t)lv_spangroup_get_expand_width(sg, (uint32_t)w);
        if (width > w) width = w;
        if (width < 1) width = 1;
    }
    lv_obj_set_width(sg, width);
    lv_obj_set_height(sg, LV_SIZE_CONTENT);
    return sg;
}

// ------------------------------------------------------------------ blocks

static size_t indent_of(const line_t *l)
{
    size_t i = 0;
    while (i < l->n && (l->p[i] == ' ' || l->p[i] == '\t')) i++;
    return i;
}

static bool blank(const line_t *l)
{
    return indent_of(l) == l->n;
}

static bool fence(const line_t *l)
{
    size_t i = indent_of(l);
    return i <= 3 && l->n - i >= 3 && !strncmp(l->p + i, "```", 3);
}

static bool table_row(const line_t *l)
{
    size_t i = indent_of(l);
    return i < l->n && l->p[i] == '|';
}

/* "- item", "* item", "+ item", "1. item", "2) item": returns the content start, or 0. */
static size_t list_item(const line_t *l, bool *numbered, int *number)
{
    size_t i = indent_of(l);
    if (i >= l->n) return 0;
    char ch = l->p[i];
    if ((ch == '-' || ch == '*' || ch == '+') && i + 1 < l->n && l->p[i + 1] == ' ') {
        *numbered = false;
        return i + 2;
    }
    size_t j = i;
    int v = 0;
    while (j < l->n && j - i < 3 && isdigit((unsigned char)l->p[j])) v = v * 10 + (l->p[j++] - '0');
    if (j > i && j + 1 < l->n && (l->p[j] == '.' || l->p[j] == ')') && l->p[j + 1] == ' ') {
        *numbered = true;
        *number = v;
        return j + 2;
    }
    return 0;
}

static bool rule(const line_t *l)
{
    size_t i = indent_of(l);
    if (i >= l->n) return false;
    char ch = l->p[i];
    if (ch != '-' && ch != '*' && ch != '_') return false;
    int count = 0;
    for (size_t k = i; k < l->n; k++) {
        if (l->p[k] == ch) count++;
        else if (l->p[k] != ' ') return false;
    }
    return count >= 3;
}

/* Join lines into one paragraph: soft line breaks become spaces. */
typedef struct {
    char *s;
    size_t n, cap;
} sbuf_t;

static void sb_add(sbuf_t *b, const char *s, size_t n)
{
    if (b->n + n + 2 > b->cap) {
        size_t cap = (b->n + n + 2) * 2;
        char *p = realloc(b->s, cap);
        if (!p) return;
        b->s = p;
        b->cap = cap;
    }
    memcpy(b->s + b->n, s, n);
    b->n += n;
    b->s[b->n] = '\0';
}

static void sb_space(sbuf_t *b)
{
    if (b->n && b->s[b->n - 1] != ' ' && b->s[b->n - 1] != '\n') sb_add(b, " ", 1);
}

static void emit_para(lv_obj_t *box, sbuf_t *b, const ctx_t *c)
{
    if (!b->n) return;
    para(box, b->s, b->n, c, DK_FONT_M, c->st->ink, 0, c->max_w, c->wide);
    b->n = 0;
}

static void emit_code(lv_obj_t *box, sbuf_t *b, const ctx_t *c)
{
    while (b->n && b->s[b->n - 1] == '\n') b->s[--b->n] = '\0';
    lv_obj_t *card = dk_card(box, c->st->code_bg, 12);
    lv_obj_set_style_pad_hor(card, 14, 0);
    lv_obj_set_style_pad_ver(card, 10, 0);
    if (c->wide) lv_obj_set_width(card, c->max_w);
    lv_obj_t *l = dk_label(card, &deskling_font_mono_17, c->st->ink, b->n ? b->s : " ");
    lv_obj_set_style_max_width(l, c->max_w - 28, 0);
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_WRAP);
    b->n = 0;
}

static int split_cells(const line_t *l, line_t *cells, int max)
{
    const char *p = l->p + indent_of(l), *end = l->p + l->n;
    while (end > p && (end[-1] == ' ' || end[-1] == '\r')) end--;
    if (p < end && *p == '|') p++;
    if (end > p && end[-1] == '|') end--;
    int n = 0;
    while (p <= end && n < max) {
        const char *q = p;
        while (q < end && *q != '|') {
            if (*q == '\\' && q + 1 < end) q++;
            q++;
        }
        const char *a = p, *b = q;
        while (a < b && *a == ' ') a++;
        while (b > a && b[-1] == ' ') b--;
        cells[n].p = a;
        cells[n].n = (size_t)(b - a);
        n++;
        if (q >= end) break;
        p = q + 1;
    }
    return n;
}

static bool separator_row(const line_t *cells, int n)
{
    for (int i = 0; i < n; i++) {
        size_t dashes = 0;
        for (size_t k = 0; k < cells[i].n; k++) {
            char ch = cells[i].p[k];
            if (ch == '-') dashes++;
            else if (ch != ':' && ch != ' ') return false;
        }
        if (!dashes) return false;
    }
    return n > 0;
}

static void emit_table(lv_obj_t *box, const line_t *rows, int nrows, const ctx_t *c)
{
    line_t cells[MD_MAX_COLS];
    int cols = 0;
    for (int r = 0; r < nrows; r++) {
        int n = split_cells(&rows[r], cells, MD_MAX_COLS);
        if (n > cols) cols = n;
    }
    if (!cols) return;
    lv_obj_t *card = dk_card(box, c->st->code_bg, 12);
    lv_obj_set_style_pad_all(card, 10, 0);
    dk_col(card, 0);
    int32_t colw = (c->max_w - 20 - (cols - 1) * 12) / cols;
    if (colw < 60) colw = 60;
    bool header = true;
    for (int r = 0; r < nrows && r < MD_MAX_TABLE_ROWS; r++) {
        int n = split_cells(&rows[r], cells, MD_MAX_COLS);
        if (separator_row(cells, n)) continue;
        lv_obj_t *row = dk_box(card);
        dk_row(row, 12);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        lv_obj_set_style_pad_ver(row, 6, 0);
        lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, 0);
        lv_obj_set_style_border_width(row, r + 1 < nrows ? 1 : 0, 0);
        lv_obj_set_style_border_color(row, c->st->soft, 0);
        lv_obj_set_style_border_opa(row, LV_OPA_30, 0);
        for (int k = 0; k < cols; k++) {
            const char *s = k < n ? cells[k].p : "";
            size_t len = k < n ? cells[k].n : 0;
            para(row, len ? s : " ", len ? len : 1, c, header ? DK_FONT_M : DK_FONT_S, c->st->ink, header ? F_BOLD : 0, colw, true);
        }
        header = false;
    }
}

static void render(lv_obj_t *box, const char *text, const ctx_t *c)
{
    // Split into lines first: lists, tables and quotes look ahead.
    line_t *lines = malloc(sizeof(line_t) * MD_MAX_LINES);
    if (!lines) return;
    int nl = 0;
    for (const char *p = text; *p && nl < MD_MAX_LINES;) {
        const char *e = strchr(p, '\n');
        size_t n = e ? (size_t)(e - p) : strlen(p);
        if (n && p[n - 1] == '\r') n--;
        lines[nl].p = p;
        lines[nl].n = n;
        nl++;
        if (!e) break;
        p = e + 1;
    }

    sbuf_t pb = {0}, cb = {0};
    for (int i = 0; i < nl; i++) {
        const line_t *l = &lines[i];
        if (fence(l)) {
            emit_para(box, &pb, c);
            int j = i + 1;
            cb.n = 0;
            for (; j < nl && !fence(&lines[j]); j++) {
                sb_add(&cb, lines[j].p, lines[j].n);
                sb_add(&cb, "\n", 1);
            }
            emit_code(box, &cb, c);
            i = j; // the closing fence (or the end)
            continue;
        }
        if (blank(l)) {
            emit_para(box, &pb, c);
            continue;
        }
        size_t ind = indent_of(l);
        if (ind <= 3 && l->p[ind] == '#') {
            size_t h = 0;
            while (ind + h < l->n && l->p[ind + h] == '#' && h < 7) h++;
            if (h <= 6 && ind + h < l->n && l->p[ind + h] == ' ') {
                emit_para(box, &pb, c);
                const char *s = l->p + ind + h + 1;
                size_t n = l->n - ind - h - 1;
                while (n && (s[n - 1] == '#' || s[n - 1] == ' ')) n--;
                const lv_font_t *f = h == 1 ? DK_FONT_XL : h == 2 ? DK_FONT_L : DK_FONT_M;
                lv_obj_t *hd = para(box, s, n, c, f, c->st->ink, h >= 3 ? F_BOLD : 0, c->max_w, c->wide);
                lv_obj_set_style_pad_top(hd, 4, 0);
                continue;
            }
        }
        if (rule(l) && !pb.n) {
            lv_obj_t *hr = dk_box(box);
            lv_obj_set_size(hr, c->wide ? c->max_w : 120, 2);
            lv_obj_set_style_bg_color(hr, c->st->soft, 0);
            lv_obj_set_style_bg_opa(hr, LV_OPA_40, 0);
            continue;
        }
        if (table_row(l)) {
            emit_para(box, &pb, c);
            int j = i;
            while (j < nl && table_row(&lines[j])) j++;
            emit_table(box, &lines[i], j - i, c);
            i = j - 1;
            continue;
        }
        if (l->p[ind] == '>') {
            emit_para(box, &pb, c);
            sbuf_t qb = {0};
            int j = i;
            for (; j < nl && !blank(&lines[j]) && lines[j].p[indent_of(&lines[j])] == '>'; j++) {
                size_t k = indent_of(&lines[j]) + 1;
                if (k < lines[j].n && lines[j].p[k] == ' ') k++;
                sb_space(&qb);
                sb_add(&qb, lines[j].p + k, lines[j].n - k);
            }
            lv_obj_t *q = dk_box(box);
            lv_obj_set_style_border_side(q, LV_BORDER_SIDE_LEFT, 0);
            lv_obj_set_style_border_width(q, 4, 0);
            lv_obj_set_style_border_color(q, DK_LILAC, 0);
            lv_obj_set_style_pad_left(q, 12, 0);
            if (qb.n) para(q, qb.s, qb.n, c, DK_FONT_M, c->st->soft, 0, c->max_w - 16, c->wide);
            free(qb.s);
            i = j - 1;
            continue;
        }
        bool numbered = false;
        int number = 0;
        size_t start = list_item(l, &numbered, &number);
        if (start) {
            emit_para(box, &pb, c);
            sbuf_t ib = {0};
            sb_add(&ib, l->p + start, l->n - start);
            // Continuation lines: indented, not blank, not a new item.
            int j = i + 1;
            bool dummy;
            int dn;
            for (; j < nl && !blank(&lines[j]) && indent_of(&lines[j]) > ind && !list_item(&lines[j], &dummy, &dn) && !fence(&lines[j]); j++) {
                size_t k = indent_of(&lines[j]);
                sb_space(&ib);
                sb_add(&ib, lines[j].p + k, lines[j].n - k);
            }
            int level = (int)(ind / 2);
            if (level > 3) level = 3;
            lv_obj_t *row = dk_box(box);
            dk_row(row, 10);
            lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
            lv_obj_set_style_pad_left(row, 4 + level * 22, 0);
            if (numbered) {
                char num[8];
                lv_snprintf(num, sizeof(num), "%d.", number);
                lv_obj_t *nl_ = dk_label(row, DK_FONT_M, c->st->soft, num);
                lv_obj_set_style_min_width(nl_, 24, 0);
            } else {
                lv_obj_t *dot = dk_box(row);
                lv_obj_set_size(dot, 7, 7);
                lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
                lv_obj_set_style_bg_color(dot, c->st->soft, 0);
                lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
                lv_obj_set_style_margin_top(dot, 9, 0);
            }
            int32_t w = c->max_w - (4 + level * 22) - 34;
            if (ib.n) para(row, ib.s, ib.n, c, DK_FONT_M, c->st->ink, 0, w < 80 ? 80 : w, c->wide);
            free(ib.s);
            i = j - 1;
            continue;
        }
        // Ordinary text: lines of one paragraph run together.
        sb_space(&pb);
        sb_add(&pb, l->p + ind, l->n - ind);
    }
    emit_para(box, &pb, c);
    free(pb.s);
    free(cb.s);
    free(lines);
}

lv_obj_t *md_render(lv_obj_t *parent, const char *text, int32_t max_w, const md_style_t *st)
{
    lv_obj_t *box = dk_box(parent);
    dk_col(box, 8);
    ctx_t c = {.max_w = max_w, .wide = false, .st = st};
    render(box, text ? text : "", &c);
    return box;
}

lv_obj_t *md_render_wide(lv_obj_t *parent, const char *text, int32_t max_w, const md_style_t *st)
{
    lv_obj_t *box = dk_box(parent);
    lv_obj_set_width(box, max_w);
    dk_col(box, 10);
    ctx_t c = {.max_w = max_w, .wide = true, .st = st};
    render(box, text ? text : "", &c);
    return box;
}
