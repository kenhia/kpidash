/**
 * weather_card.c — The weather filler (WI #4576). See the header.
 *
 * Coordinates are the mock-up's (sprints/planning/weather-mockups.html,
 * `vC`, `icon`, `pal`), relative to the card's top-left, at real panel
 * pixels. Text y values are the SVG's baselines, which text_at() converts to
 * LVGL's top-of-line.
 */
#include "weather_card.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "wxstate.h"

/* ---- Geometry (mock-up `vC`) ---- */
#define WX_CX (WX_COL_W / 2) /* the "now" block's centre line */
#define WX_BAR_TOP 156       /* the forecast scale: tmax at the top… */
#define WX_BAR_BOT 330       /* …tmin at the bottom */
#define WX_SUN_Y 506         /* the day bar */
#define WX_SUN_H 20

/* The host columns' raised fill, mixed once and drawn opaque (sprint 030). */
#define WX_RAISE lv_color_mix(WS_COLOR_SURFACE0, WS_COLOR_BG, 66)
/* Mocha accents the palette does not carry yet. */
#define WX_OVERLAY1 lv_color_hex(0x7F849C)
#define WX_SUBTEXT0 lv_color_hex(0xA6ADC8)
/* Twilight on the day bar: the mock-up's pre-mixed dim yellow, opaque. */
#define WX_TWILIGHT lv_color_hex(0x857C6C)

#define FONT_BIG (&lv_font_montserrat_bold_96_digits)
#define FONT_COND (&lv_font_montserrat_bold_28)
#define FONT_TODAY (&lv_font_montserrat_bold_36)
#define FONT_HILO (&lv_font_montserrat_bold_24)
#define FONT_LABEL (&lv_font_montserrat_20)
#define FONT_LABEL_B (&lv_font_montserrat_bold_20)

/* Everything that, when it changes, repaints the whole card. */
typedef struct {
    wx_record_t rec;
    int ncols;
    bool stale;
} body_t;

typedef struct {
    body_t body;
    char age[32];
    int now_min; /* minute of the local day: the day bar's marker */
    bool valid;
} view_t;

typedef struct {
    view_t cur;
    view_t next; /* scratch, zeroed each update so memcmp sees only content */
} wx_widget_t;

/* The mock-up's `pal`: stale greys everything but the age, which turns red. */
typedef struct {
    bool s;
    lv_color_t fg, sub, hi, lo, sun, age;
    const lv_font_t *age_font;
} pal_t;

static pal_t palette(bool stale) {
    pal_t p = {
        .s = stale,
        .fg = stale ? WS_COLOR_MUTED : WS_COLOR_FG,
        .sub = stale ? WS_COLOR_MUTED : WX_SUBTEXT0,
        .hi = stale ? WS_COLOR_MUTED : WS_COLOR_ORANGE,
        .lo = stale ? WS_COLOR_MUTED : WS_COLOR_BLUE,
        .sun = stale ? WS_COLOR_MUTED : WS_COLOR_YELLOW,
        .age = stale ? WS_COLOR_RED : WS_COLOR_MUTED,
        .age_font = stale ? FONT_LABEL_B : FONT_LABEL,
    };
    return p;
}

/* ---- Drawing helpers ---- */

static void rect(lv_layer_t *layer, int32_t x, int32_t y, int32_t w, int32_t h, lv_color_t c,
                 int32_t radius) {
    if (w <= 0 || h <= 0)
        return;
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.bg_color = c;
    d.bg_opa = LV_OPA_COVER;
    d.radius = radius;
    lv_area_t a = {x, y, x + w - 1, y + h - 1};
    lv_draw_rect(layer, &d, &a);
}

static void circle(lv_layer_t *layer, int32_t cx, int32_t cy, int32_t r, lv_color_t c) {
    rect(layer, cx - r, cy - r, 2 * r + 1, 2 * r + 1, c, LV_RADIUS_CIRCLE);
}

static void line(lv_layer_t *layer, int32_t x1, int32_t y1, int32_t x2, int32_t y2, lv_color_t c,
                 int32_t w) {
    lv_draw_line_dsc_t d;
    lv_draw_line_dsc_init(&d);
    d.color = c;
    d.width = w < 1 ? 1 : w;
    d.opa = LV_OPA_COVER;
    d.round_start = 1;
    d.round_end = 1;
    d.p1.x = x1;
    d.p1.y = y1;
    d.p2.x = x2;
    d.p2.y = y2;
    lv_draw_line(layer, &d);
}

static void tri(lv_layer_t *layer, const int32_t *p, lv_color_t c) {
    lv_draw_triangle_dsc_t d;
    lv_draw_triangle_dsc_init(&d);
    d.bg_color = c;
    d.bg_opa = LV_OPA_COVER;
    for (int i = 0; i < 3; i++) {
        d.p[i].x = p[2 * i];
        d.p[i].y = p[2 * i + 1];
    }
    lv_draw_triangle(layer, &d);
}

/* The regular 20 px font has no "·" or "—" (they are not in LVGL's built-in
 * Montserrat), so a regular line draws those two in bold 20, which has both:
 * the 10G card's spangroup trick, done here as runs. Other fonts draw whole. */
static bool is_alt(const char *s) {
    return (s[0] == '\xC2' && s[1] == '\xB7') || (s[0] == '\xE2' && s[1] == '\x80' && s[2] == '\x94');
}

typedef struct {
    const char *s;
    uint32_t len;
    const lv_font_t *f;
} run_t;

static int split_runs(const char *s, const lv_font_t *font, run_t *out, int max) {
    int n = 0;
    bool split = font == FONT_LABEL;
    while (*s && n < max) {
        if (split && is_alt(s)) {
            uint32_t l = s[0] == '\xC2' ? 2 : 3;
            out[n++] = (run_t){s, l, FONT_LABEL_B};
            s += l;
            continue;
        }
        const char *e = s;
        while (*e && !(split && is_alt(e)))
            e++;
        out[n++] = (run_t){s, (uint32_t)(e - s), font};
        s = e;
    }
    return n;
}

static int32_t text_w(const lv_font_t *font, const char *s) {
    run_t r[12];
    int n = split_runs(s, font, r, 12);
    int32_t w = 0;
    for (int i = 0; i < n; i++)
        w += lv_text_get_width(r[i].s, r[i].len, r[i].f, 0);
    return w;
}

/* Text whose BASELINE is at y. `x` is the left edge, the centre or the right
 * edge by `align`. */
static void text_at(lv_layer_t *layer, const lv_font_t *font, int32_t x, int32_t baseline,
                    const char *s, lv_color_t c, lv_text_align_t align) {
    if (!s || !s[0])
        return;
    run_t r[12];
    int n = split_runs(s, font, r, 12);
    int32_t w = text_w(font, s);
    if (align == LV_TEXT_ALIGN_CENTER)
        x -= w / 2;
    else if (align == LV_TEXT_ALIGN_RIGHT)
        x -= w;
    for (int i = 0; i < n; i++) {
        char buf[96];
        uint32_t len = r[i].len < sizeof(buf) - 1 ? r[i].len : (uint32_t)sizeof(buf) - 1;
        memcpy(buf, r[i].s, len);
        buf[len] = '\0';
        const lv_font_t *f = r[i].f;
        int32_t rw = lv_text_get_width(buf, len, f, 0);
        lv_draw_label_dsc_t d;
        lv_draw_label_dsc_init(&d);
        d.text = buf;
        d.text_local = 1;
        d.font = f;
        d.color = c;
        d.opa = LV_OPA_COVER;
        int32_t top = baseline - (f->line_height - f->base_line);
        lv_area_t a = {x, top, x + rw, top + f->line_height - 1};
        lv_draw_label(layer, &d, &a);
        x += rw;
    }
}

/* ---- Icons (mock-up `icon`): circles, lines and one polygon. `s` is the
 * icon's size; (cx, cy) its centre. ---- */

typedef struct {
    lv_color_t sun, moon, cl, cl2, bl, wh;
} icon_pal_t;

static icon_pal_t icon_palette(bool st) {
    icon_pal_t p = {
        .sun = st ? WS_COLOR_MUTED : WS_COLOR_YELLOW,
        .moon = st ? WS_COLOR_MUTED : WS_COLOR_LAVENDER,
        .cl = st ? WS_COLOR_SURFACE1 : WX_SUBTEXT0,
        .cl2 = st ? WS_COLOR_SURFACE0 : WX_OVERLAY1,
        .bl = st ? WS_COLOR_MUTED : WS_COLOR_BLUE,
        .wh = st ? WS_COLOR_MUTED : WS_COLOR_FG,
    };
    return p;
}

#define I(v) ((int32_t)lround(v))

static void ic_sun(lv_layer_t *l, double x, double y, double r, const icon_pal_t *p) {
    circle(l, I(x), I(y), I(r), p->sun);
    for (int k = 0; k < 8; k++) {
        double a = k * M_PI / 4, c = cos(a), s = sin(a);
        line(l, I(x + c * r * 1.4), I(y + s * r * 1.4), I(x + c * r * 1.85), I(y + s * r * 1.85),
             p->sun, I(r * .24));
    }
}

/* A crescent: the disc, with a disc of the card's own fill cut from its upper
 * right. Both opaque; the cut lands only on the card background, because the
 * sky is always drawn before anything that overlaps it. */
static void ic_moon(lv_layer_t *l, double x, double y, double r, const icon_pal_t *p) {
    circle(l, I(x), I(y), I(r), p->moon);
    circle(l, I(x + .979 * r), I(y - .459 * r), I(.8 * r), WX_RAISE);
}

static void ic_cloud(lv_layer_t *l, double x, double y, double w, lv_color_t f) {
    circle(l, I(x - .22 * w), I(y + .05 * w), I(.2 * w), f);
    circle(l, I(x), I(y - .08 * w), I(.27 * w), f);
    circle(l, I(x + .24 * w), I(y + .04 * w), I(.21 * w), f);
    rect(l, I(x - .42 * w), I(y + .05 * w), I(.87 * w), I(.2 * w), f, I(.1 * w));
}

static void icon(lv_layer_t *l, wx_cond_t cond, bool is_day, double cx, double cy, double s,
                 bool stale) {
    icon_pal_t p = icon_palette(stale);
    const double cw = .66 * s, cyc = cy - .1 * s, py = cy + .2 * s;
    const double xs[3] = {cx - .2 * s, cx, cx + .2 * s};
#define DROP(x, y) line(l, I((x) + .04 * s), I(y), I((x) - .04 * s), I((y) + .14 * s), p.bl, I(s * .05))
#define DOT(x, y, c) circle(l, I(x), I(y), I(s * .04), c)
    switch (cond) {
    case WX_COND_CLEAR:
        if (is_day)
            ic_sun(l, cx, cy, .22 * s, &p);
        else
            ic_moon(l, cx, cy, .34 * s, &p);
        break;
    case WX_COND_PARTLY:
        if (is_day)
            ic_sun(l, cx + .17 * s, cy - .14 * s, .15 * s, &p);
        else
            ic_moon(l, cx + .17 * s, cy - .14 * s, .225 * s, &p);
        ic_cloud(l, cx - .05 * s, cy + .08 * s, .6 * s, p.cl);
        break;
    case WX_COND_CLOUDY:
        ic_cloud(l, cx + .13 * s, cy - .12 * s, .5 * s, p.cl2);
        ic_cloud(l, cx - .05 * s, cy + .06 * s, .64 * s, p.cl);
        break;
    case WX_COND_FOG:
        ic_cloud(l, cx, cy - .14 * s, .6 * s, p.cl);
        for (int k = 0; k < 3; k++) {
            double y = cy + .2 * s + k * .11 * s;
            line(l, I(cx - .3 * s + (k % 2) * .06 * s), I(y),
                 I(cx + .3 * s - ((k + 1) % 2) * .06 * s), I(y), p.cl2, I(s * .05));
        }
        break;
    case WX_COND_DRIZZLE:
        ic_cloud(l, cx, cyc, cw, p.cl);
        for (int k = 0; k < 3; k++)
            DOT(xs[k], py + (k % 2) * .1 * s + .04 * s, p.bl);
        break;
    case WX_COND_RAIN:
        ic_cloud(l, cx, cyc, cw, p.cl);
        for (int k = 0; k < 3; k++)
            DROP(xs[k], py);
        break;
    case WX_COND_SLEET:
        ic_cloud(l, cx, cyc, cw, p.cl);
        DROP(xs[0], py);
        DOT(xs[1], py + .08 * s, p.wh);
        DROP(xs[2], py);
        break;
    case WX_COND_SNOW:
        ic_cloud(l, cx, cyc, cw, p.cl);
        for (int k = 0; k < 3; k++)
            DOT(xs[k], py + .03 * s + (k % 2) * .1 * s, p.wh);
        DOT(cx - .1 * s, py + .2 * s, p.wh);
        DOT(cx + .1 * s, py + .2 * s, p.wh);
        break;
    case WX_COND_THUNDER: {
        ic_cloud(l, cx, cyc, cw, p.cl2);
        /* The bolt, split into four triangles. */
        const int32_t A[2] = {I(cx + .04 * s), I(py - .04 * s)}, B[2] = {I(cx - .1 * s), I(py + .14 * s)},
                      C[2] = {I(cx - .01 * s), I(py + .14 * s)}, D[2] = {I(cx - .06 * s), I(py + .3 * s)},
                      E[2] = {I(cx + .12 * s), I(py + .08 * s)}, F[2] = {I(cx + .02 * s), I(py + .08 * s)};
        const int32_t t1[6] = {A[0], A[1], B[0], B[1], C[0], C[1]};
        const int32_t t2[6] = {A[0], A[1], C[0], C[1], F[0], F[1]};
        const int32_t t3[6] = {C[0], C[1], D[0], D[1], E[0], E[1]};
        const int32_t t4[6] = {C[0], C[1], E[0], E[1], F[0], F[1]};
        tri(l, t1, p.sun);
        tri(l, t2, p.sun);
        tri(l, t3, p.sun);
        tri(l, t4, p.sun);
        break;
    }
    case WX_COND_NONE:
    default:
        break; /* no classification: the temperature's own dash says it */
    }
#undef DROP
#undef DOT
}

/* ---- The card ---- */

static int32_t sun_x(int32_t bx, int32_t bw, double frac) {
    return bx + (int32_t)lround(bw * frac);
}

static void draw_card(lv_layer_t *layer, const lv_area_t *a, const view_t *v) {
    const wx_record_t *r = &v->body.rec;
    const int n = v->body.ncols;
    const int32_t x0 = a->x1, y0 = a->y1, w = wx_width(n);
    const pal_t p = palette(v->body.stale);
    char buf[64], b2[32];

    /* Frame: the host columns' raised fill and border, a lavender top line. */
    {
        lv_draw_rect_dsc_t d;
        lv_draw_rect_dsc_init(&d);
        d.radius = 6;
        d.bg_color = WX_RAISE;
        d.bg_opa = LV_OPA_COVER;
        d.border_color = WS_COLOR_SURFACE0;
        d.border_width = 1;
        d.border_opa = LV_OPA_COVER;
        lv_draw_rect(layer, &d, a);
        rect(layer, x0, y0, w, 4, WS_COLOR_LAVENDER, 2);
    }
    text_at(layer, p.age_font, x0 + 20, y0 + 40, v->age, p.age, LV_TEXT_ALIGN_LEFT);

    /* Now: icon, temperature, condition, feels. */
    const int32_t cx = x0 + WX_CX;
    icon(layer, r->now.cond, r->now.is_day, cx, y0 + 130, 170, p.s);
    wx_fmt_deg(r->now.has_temp, r->now.temp_f, buf, sizeof(buf));
    text_at(layer, FONT_BIG, cx, y0 + 300, buf, p.fg, LV_TEXT_ALIGN_CENTER);
    text_at(layer, FONT_COND, cx, y0 + 342, wx_cond_name(r->now.cond), p.fg, LV_TEXT_ALIGN_CENTER);
    wx_fmt_feels(&r->now, buf, sizeof(buf));
    text_at(layer, FONT_LABEL, cx, y0 + 374, buf, p.sub, LV_TEXT_ALIGN_CENTER);

    if (n == 1 && r->ndays > 0) {
        /* "today 61° 52°", centred as one run. */
        const wx_day_t *t = &r->days[0];
        char hi[16], lo[16];
        wx_fmt_deg(t->has_hi, t->hi_f, hi, sizeof(hi));
        wx_fmt_deg(t->has_lo, t->lo_f, lo, sizeof(lo));
        int32_t tw = text_w(FONT_LABEL, "today") + 14 + text_w(FONT_TODAY, hi) + 14 +
                     text_w(FONT_TODAY, lo);
        int32_t x = cx - tw / 2;
        text_at(layer, FONT_LABEL, x, y0 + 426, "today", p.sub, LV_TEXT_ALIGN_LEFT);
        x += text_w(FONT_LABEL, "today") + 14;
        text_at(layer, FONT_TODAY, x, y0 + 426, hi, p.hi, LV_TEXT_ALIGN_LEFT);
        x += text_w(FONT_TODAY, hi) + 14;
        text_at(layer, FONT_TODAY, x, y0 + 426, lo, p.lo, LV_TEXT_ALIGN_LEFT);
    }

    /* Forecast: a bar per day from low to high on one shared scale. */
    const int k = wx_days_shown(n, r->ndays);
    if (n > 1) {
        const int32_t fx = x0 + WX_PITCH, rw = w - 20 - WX_PITCH;
        rect(layer, fx - 9, y0 + 24, 2, 416, WS_COLOR_SURFACE0, 0);
        int tmin = 0, tmax = 0;
        bool any = wx_temp_scale(r, k, &tmin, &tmax);
        const int per = (int)(n == 2 ? 3 : n == 3 ? 6 : 8); /* the pitch is by width, not count */
        for (int i = 0; i < k; i++) {
            const wx_day_t *d = &r->days[i];
            const int32_t x = fx + rw * (2 * i + 1) / (2 * per);
            icon(layer, d->cond, true, x, y0 + 54, 70, p.s);
            if (any && (d->has_hi || d->has_lo)) {
                int32_t yh = d->has_hi ? y0 + wx_scale_y(d->hi_f, tmin, tmax, WX_BAR_TOP, WX_BAR_BOT) : 0;
                int32_t yl = d->has_lo ? y0 + wx_scale_y(d->lo_f, tmin, tmax, WX_BAR_TOP, WX_BAR_BOT) : 0;
                if (d->has_hi && d->has_lo) {
                    lv_draw_rect_dsc_t g;
                    lv_draw_rect_dsc_init(&g);
                    g.radius = 15;
                    g.bg_opa = LV_OPA_COVER;
                    if (p.s) {
                        g.bg_color = WS_COLOR_SURFACE1;
                    } else {
                        /* An opaque LVGL gradient, peach to blue: no alpha. */
                        g.bg_color = WS_COLOR_ORANGE;
                        g.bg_grad.dir = LV_GRAD_DIR_VER;
                        g.bg_grad.stops_count = 2;
                        g.bg_grad.stops[0] = (lv_gradient_stop_t){WS_COLOR_ORANGE, LV_OPA_COVER, 0};
                        g.bg_grad.stops[1] = (lv_gradient_stop_t){WS_COLOR_BLUE, LV_OPA_COVER, 255};
                    }
                    lv_area_t ba = {x - 15, yh - 15, x + 14, yl + 14};
                    lv_draw_rect(layer, &g, &ba);
                } else {
                    /* Only one of hi/lo: a dot, not a bar. */
                    lv_color_t c = p.s ? WS_COLOR_SURFACE1 : d->has_hi ? WS_COLOR_ORANGE : WS_COLOR_BLUE;
                    circle(layer, x, d->has_hi ? yh : yl, 15, c);
                }
                if (d->has_hi) {
                    wx_fmt_deg(true, d->hi_f, buf, sizeof(buf));
                    text_at(layer, FONT_HILO, x, yh - 26, buf, p.hi, LV_TEXT_ALIGN_CENTER);
                }
                if (d->has_lo) {
                    wx_fmt_deg(true, d->lo_f, buf, sizeof(buf));
                    text_at(layer, FONT_HILO, x, yl + 44, buf, p.lo, LV_TEXT_ALIGN_CENTER);
                }
            }
            wx_fmt_weekday(d, i, b2, sizeof(b2));
            text_at(layer, FONT_LABEL_B, x, y0 + 410, b2, p.fg, LV_TEXT_ALIGN_CENTER);
            if (d->pop_pct >= 0) {
                bool wet = d->pop_pct >= 50;
                lv_snprintf(b2, sizeof(b2), "%d%%", d->pop_pct);
                text_at(layer, wet ? FONT_LABEL_B : FONT_LABEL, x, y0 + 438, b2,
                        wet ? p.lo : WS_COLOR_MUTED, LV_TEXT_ALIGN_CENTER);
            }
        }
    }

    /* The local day, midnight to midnight: twilight dim, daylight yellow. */
    const wx_sun_t *u = &r->sun;
    const int32_t bx = x0 + 20, bw = w - 40, by = y0 + WX_SUN_Y;
    rect(layer, bx, by, bw, WX_SUN_H, WS_COLOR_SURFACE0, 10);
    if (u->first_light && u->last_light) {
        int32_t xa = sun_x(bx, bw, wx_day_frac((double)u->first_light));
        int32_t xe = sun_x(bx, bw, wx_day_frac((double)u->last_light));
        rect(layer, xa, by, xe - xa, WX_SUN_H, p.s ? WS_COLOR_SURFACE1 : WX_TWILIGHT, 10);
        if (n > 1) {
            wx_fmt_time(u->first_light, b2, sizeof(b2));
            lv_snprintf(buf, sizeof(buf), "first light %s", b2);
            text_at(layer, FONT_LABEL, xa, by + 52, buf, WS_COLOR_MUTED, LV_TEXT_ALIGN_CENTER);
            wx_fmt_time(u->last_light, b2, sizeof(b2));
            lv_snprintf(buf, sizeof(buf), "last light %s", b2);
            text_at(layer, FONT_LABEL, xe, by + 52, buf, WS_COLOR_MUTED, LV_TEXT_ALIGN_CENTER);
        }
    }
    if (u->sunrise && u->sunset) {
        int32_t xb = sun_x(bx, bw, wx_day_frac((double)u->sunrise));
        int32_t xc = sun_x(bx, bw, wx_day_frac((double)u->sunset));
        rect(layer, xb, by, xc - xb, WX_SUN_H, p.sun, 10);
        wx_fmt_time(u->sunrise, b2, sizeof(b2));
        text_at(layer, FONT_LABEL_B, xb, by - 14, b2, p.sun, LV_TEXT_ALIGN_CENTER);
        wx_fmt_time(u->sunset, b2, sizeof(b2));
        text_at(layer, FONT_LABEL_B, xc, by - 14, b2, p.sun, LV_TEXT_ALIGN_CENTER);
        if (n == 1) {
            wx_fmt_daylight(u, buf, sizeof(buf));
            text_at(layer, FONT_LABEL, x0 + w / 2, by + 52, buf, WS_COLOR_MUTED,
                    LV_TEXT_ALIGN_CENTER);
        }
    } else {
        text_at(layer, FONT_LABEL, x0 + w / 2, by - 14, "no sunrise today", p.sub,
                LV_TEXT_ALIGN_CENTER);
    }
    /* Now, as a marker ringed in the card's fill. */
    int32_t mx = bx + (int32_t)lround(bw * (v->now_min / 1440.0));
    circle(layer, mx, by + WX_SUN_H / 2, 18, WX_RAISE);
    circle(layer, mx, by + WX_SUN_H / 2, 13, p.fg);
}

static void draw_cb(lv_event_t *e) {
    lv_obj_t *obj = lv_event_get_target(e);
    wx_widget_t *ww = lv_obj_get_user_data(obj);
    if (!ww || !ww->cur.valid)
        return;
    lv_area_t a;
    lv_obj_get_coords(obj, &a);
    draw_card(lv_event_get_layer(e), &a, &ww->cur);
}

static void delete_cb(lv_event_t *e) {
    free(lv_obj_get_user_data(lv_event_get_target(e)));
}

/* Invalidate a strip of the card, in card-relative coordinates. */
static void invalidate_strip(lv_obj_t *card, int32_t x1, int32_t y1, int32_t x2, int32_t y2) {
    lv_area_t c;
    lv_obj_get_coords(card, &c);
    lv_area_t a = {c.x1 + x1, c.y1 + y1, c.x1 + x2, c.y1 + y2};
    lv_obj_invalidate_area(card, &a);
}

/* ---- Public API ---- */

lv_obj_t *weather_card_create(lv_obj_t *parent) {
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(o, wx_width(1), WX_H);
    wx_widget_t *ww = calloc(1, sizeof(*ww));
    lv_obj_set_user_data(o, ww);
    lv_obj_add_event_cb(o, delete_cb, LV_EVENT_DELETE, NULL);
    lv_obj_add_event_cb(o, draw_cb, LV_EVENT_DRAW_MAIN, NULL);
    return o;
}

void weather_card_update(lv_obj_t *card, int ncols, double now) {
    wx_widget_t *ww = card ? lv_obj_get_user_data(card) : NULL;
    if (!ww || !wx_has_record() || ncols < 1)
        return;
    if (ncols > WX_MAX_COLS)
        ncols = WX_MAX_COLS;

    view_t *v = &ww->next;
    memset(v, 0, sizeof(*v));
    v->body.rec = *wx_record();
    v->body.ncols = ncols;
    v->body.stale = wx_is_stale(&v->body.rec, now);
    wx_fmt_age(&v->body.rec, now, v->age, sizeof(v->age));
    v->now_min = (int)lround(wx_day_frac(now) * 1440);
    v->valid = true;

    if (!ww->cur.valid || memcmp(&v->body, &ww->cur.body, sizeof(v->body)) != 0) {
        lv_obj_set_width(card, wx_width(ncols));
        ww->cur = *v;
        lv_obj_invalidate(card);
        return;
    }
    const int32_t w = wx_width(ncols);
    if (strcmp(v->age, ww->cur.age) != 0)
        invalidate_strip(card, 4, 8, w / 2, 56);
    if (v->now_min != ww->cur.now_min)
        invalidate_strip(card, 0, WX_SUN_Y - 9, w - 1, WX_SUN_Y + WX_SUN_H + 9);
    ww->cur = *v;
}
