/* bc/ghal_compiler.c — GHAL high-performance .g.nx bytecode compiler
 *
 * Translates a graphics-oriented .g.nx script directly into platform-independent
 * GALB bytecode for execution via the high-performance GALB VM.
 */

#include "../include/ghal_bc.h"
#include "../include/ghal.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>

/* ── Growable buffer for bytecode ────────────────────────────── */

typedef struct {
    uint8_t *data;
    uint32_t len;
    uint32_t cap;
} GalbBuf;

static int galb_buf_init(GalbBuf *b) {
    b->cap  = 256;
    b->len  = 0;
    b->data = (uint8_t *)malloc(b->cap);
    return b->data ? 0 : -1;
}

static int galb_buf_ensure(GalbBuf *b, uint32_t need) {
    if (b->len + need <= b->cap) return 0;
    uint32_t newcap = b->cap * 2 + need;
    uint8_t *p = (uint8_t *)realloc(b->data, newcap);
    if (!p) return -1;
    b->data = p;
    b->cap  = newcap;
    return 0;
}

static void emit_u8(GalbBuf *b, uint8_t v) {
    if (galb_buf_ensure(b, 1) == 0)
        b->data[b->len++] = v;
}

static void emit_u32(GalbBuf *b, uint32_t v) {
    if (galb_buf_ensure(b, 4) != 0) return;
    memcpy(b->data + b->len, &v, 4);
    b->len += 4;
}

static void emit_str(GalbBuf *b, const char *s) {
    size_t n = strlen(s) + 1;
    if (galb_buf_ensure(b, (uint32_t)n) != 0) return;
    memcpy(b->data + b->len, s, n);
    b->len += (uint32_t)n;
}

/* ── Tokenizer ───────────────────────────────────────────────── */

typedef struct {
    const char *src;
    size_t pos;
    size_t len;
} Tokenizer;

static void tok_init(Tokenizer *t, const char *src) {
    t->src = src;
    t->pos = 0;
    t->len = strlen(src);
}

static void skip_whitespace(Tokenizer *t) {
    while (t->pos < t->len) {
        char c = t->src[t->pos];
        if (isspace(c)) {
            t->pos++;
        } else if (c == '#') {
            while (t->pos < t->len && t->src[t->pos] != '\n') {
                t->pos++;
            }
        } else {
            break;
        }
    }
}

static int next_token(Tokenizer *t, char *buf, size_t max_len) {
    skip_whitespace(t);
    if (t->pos >= t->len) return 0;

    char c = t->src[t->pos];

    if (c == '"') {
        t->pos++; /* skip " */
        size_t len = 0;
        while (t->pos < t->len && t->src[t->pos] != '"') {
            if (len < max_len - 1) {
                buf[len++] = t->src[t->pos];
            }
            t->pos++;
        }
        if (t->pos < t->len) t->pos++; /* skip closing " */
        buf[len] = '\0';
        return 1;
    }

    if (c == '(' || c == ')' || c == ',' || c == '{' || c == '}') {
        buf[0] = c;
        buf[1] = '\0';
        t->pos++;
        return 1;
    }

    size_t len = 0;
    while (t->pos < t->len) {
        char next_c = t->src[t->pos];
        if (isspace(next_c) || next_c == '(' || next_c == ')' || next_c == ',' || next_c == '{' || next_c == '}' || next_c == '#') {
            break;
        }
        if (len < max_len - 1) {
            buf[len++] = next_c;
        }
        t->pos++;
    }
    buf[len] = '\0';
    return (len > 0);
}

static uint32_t parse_num(const char *s) {
    if (strncmp(s, "0x", 2) == 0 || strncmp(s, "0X", 2) == 0) {
        return (uint32_t)strtoul(s + 2, NULL, 16);
    }
    return (uint32_t)strtoul(s, NULL, 10);
}

/* ── Loop stack ──────────────────────────────────────────────── */

typedef struct {
    uint32_t start_pc;
    uint8_t  loop_reg;
} LoopInfo;

#define MAX_NESTED_LOOPS 8

/* ── Main compiler ───────────────────────────────────────────── */

uint8_t *ghal_compile_g_nx(const char *source, uint32_t *out_len) {
    if (!source || !out_len) return NULL;

    GalbBuf b;
    if (galb_buf_init(&b) != 0) return NULL;

    Tokenizer tok;
    tok_init(&tok, source);

    LoopInfo loop_stack[MAX_NESTED_LOOPS];
    int loop_depth = 0;

    char t[256];
    while (next_token(&tok, t, sizeof(t))) {
        if (strcmp(t, "win_open") == 0) {
            char args[6][128];
            int arg_count = 0;

            if (next_token(&tok, t, sizeof(t)) && strcmp(t, "(") == 0) {
                while (arg_count < 6) {
                    if (!next_token(&tok, args[arg_count], sizeof(args[arg_count]))) break;
                    if (strcmp(args[arg_count], ")") == 0) break;
                    arg_count++;

                    if (next_token(&tok, t, sizeof(t))) {
                        if (strcmp(t, ")") == 0) break;
                    } else {
                        break;
                    }
                }
            }

            char title[128] = "GHAL Window";
            uint32_t w = 800, h = 600;
            if (arg_count >= 1) strcpy(title, args[0]);
            if (arg_count >= 2) w = parse_num(args[1]);
            if (arg_count >= 3) h = parse_num(args[2]);

            /* Emit GALO_WIN_OPEN */
            emit_u8(&b, (uint8_t)GALO_WIN_OPEN);
            emit_u8(&b, 0); /* reg[0] = win */
            emit_str(&b, title);
            emit_u32(&b, w);
            emit_u32(&b, h);

            /* Emit GALO_SURF_CREATE */
            emit_u8(&b, (uint8_t)GALO_SURF_CREATE);
            emit_u8(&b, 0); /* surf[0] */
            emit_u32(&b, w);
            emit_u32(&b, h);
            emit_u32(&b, GHAL_FMT_BGRA8);

            /* Emit GALO_SURF_LOCK */
            emit_u8(&b, (uint8_t)GALO_SURF_LOCK);
            emit_u8(&b, 0);
        }
        else if (strcmp(t, "draw_clear") == 0) {
            char args[6][128];
            int arg_count = 0;

            if (next_token(&tok, t, sizeof(t)) && strcmp(t, "(") == 0) {
                while (arg_count < 6) {
                    if (!next_token(&tok, args[arg_count], sizeof(args[arg_count]))) break;
                    if (strcmp(args[arg_count], ")") == 0) break;
                    arg_count++;

                    if (next_token(&tok, t, sizeof(t))) {
                        if (strcmp(t, ")") == 0) break;
                    } else {
                        break;
                    }
                }
            }

            uint32_t color = 0xFFFFFFFF;
            if (arg_count == 2) {
                color = parse_num(args[1]);
            } else if (arg_count == 1) {
                color = parse_num(args[0]);
            }

            emit_u8(&b, (uint8_t)GALO_DRAW_CLEAR);
            emit_u8(&b, 0); /* surf[0] */
            emit_u32(&b, color);
        }
        else if (strcmp(t, "draw_rect") == 0) {
            char args[6][128];
            int arg_count = 0;

            if (next_token(&tok, t, sizeof(t)) && strcmp(t, "(") == 0) {
                while (arg_count < 6) {
                    if (!next_token(&tok, args[arg_count], sizeof(args[arg_count]))) break;
                    if (strcmp(args[arg_count], ")") == 0) break;
                    arg_count++;

                    if (next_token(&tok, t, sizeof(t))) {
                        if (strcmp(t, ")") == 0) break;
                    } else {
                        break;
                    }
                }
            }

            uint32_t x = 0, y = 0, w = 0, h = 0, color = 0xFFFFFFFF;
            if (arg_count == 6) {
                x     = parse_num(args[1]);
                y     = parse_num(args[2]);
                w     = parse_num(args[3]);
                h     = parse_num(args[4]);
                color = parse_num(args[5]);
            } else if (arg_count == 5) {
                x     = parse_num(args[0]);
                y     = parse_num(args[1]);
                w     = parse_num(args[2]);
                h     = parse_num(args[3]);
                color = parse_num(args[4]);
            }

            emit_u8(&b, (uint8_t)GALO_DRAW_RECT);
            emit_u8(&b, 0); /* surf[0] */
            emit_u32(&b, x);
            emit_u32(&b, y);
            emit_u32(&b, w);
            emit_u32(&b, h);
            emit_u32(&b, color);
        }
        else if (strcmp(t, "draw_text") == 0) {
            char args[6][128];
            int arg_count = 0;

            if (next_token(&tok, t, sizeof(t)) && strcmp(t, "(") == 0) {
                while (arg_count < 6) {
                    if (!next_token(&tok, args[arg_count], sizeof(args[arg_count]))) break;
                    if (strcmp(args[arg_count], ")") == 0) break;
                    arg_count++;

                    if (next_token(&tok, t, sizeof(t))) {
                        if (strcmp(t, ")") == 0) break;
                    } else {
                        break;
                    }
                }
            }

            uint32_t x = 0, y = 0, color = 0xFFFFFFFF;
            char text[128] = "";

            if (arg_count == 5) {
                x     = parse_num(args[1]);
                y     = parse_num(args[2]);
                strcpy(text, args[3]);
                color = parse_num(args[4]);
            } else if (arg_count == 4) {
                x     = parse_num(args[0]);
                y     = parse_num(args[1]);
                strcpy(text, args[2]);
                color = parse_num(args[3]);
            }

            emit_u8(&b, (uint8_t)GALO_DRAW_TEXT);
            emit_u8(&b, 0); /* surf[0] */
            emit_u32(&b, x);
            emit_u32(&b, y);
            emit_str(&b, text);
            emit_u32(&b, color);
        }
        else if (strcmp(t, "surface_blit") == 0) {
            if (next_token(&tok, t, sizeof(t)) && strcmp(t, "(") == 0) {
                next_token(&tok, t, sizeof(t)); /* consume 'win' or ')' */
                if (strcmp(t, ")") != 0) {
                    next_token(&tok, t, sizeof(t)); /* consume ')' */
                }
            }

            emit_u8(&b, (uint8_t)GALO_SURF_UNLOCK);
            emit_u8(&b, 0);

            emit_u8(&b, (uint8_t)GALO_SURF_PRESENT);
            emit_u8(&b, 0); /* win[0] */
            emit_u8(&b, 0); /* surf[0] */

            emit_u8(&b, (uint8_t)GALO_SURF_LOCK);
            emit_u8(&b, 0);
        }
        else if (strcmp(t, "vsync") == 0) {
            if (next_token(&tok, t, sizeof(t)) && strcmp(t, "(") == 0) {
                next_token(&tok, t, sizeof(t)); /* consume ')' */
            }
            emit_u8(&b, (uint8_t)GALO_GPU_VSYNC);
        }
        else if (strcmp(t, "win_close") == 0) {
            if (next_token(&tok, t, sizeof(t)) && strcmp(t, "(") == 0) {
                next_token(&tok, t, sizeof(t)); /* consume 'win' or ')' */
                if (strcmp(t, ")") != 0) {
                    next_token(&tok, t, sizeof(t)); /* consume ')' */
                }
            }

            emit_u8(&b, (uint8_t)GALO_SURF_UNLOCK);
            emit_u8(&b, 0);

            emit_u8(&b, (uint8_t)GALO_SURF_DESTROY);
            emit_u8(&b, 0);

            emit_u8(&b, (uint8_t)GALO_WIN_CLOSE);
            emit_u8(&b, 0);
        }
        else if (strcmp(t, "loop") == 0) {
            uint32_t count = 1000000;
            if (next_token(&tok, t, sizeof(t))) {
                if (isdigit(t[0])) {
                    count = parse_num(t);
                    next_token(&tok, t, sizeof(t)); /* consume '{' */
                }
            }

            if (loop_depth < MAX_NESTED_LOOPS) {
                uint8_t loop_reg = (uint8_t)(loop_depth + 1);
                
                emit_u8(&b, (uint8_t)GALO_LOOP_START);
                emit_u8(&b, loop_reg);
                emit_u32(&b, count);

                loop_stack[loop_depth].start_pc = b.len;
                loop_stack[loop_depth].loop_reg = loop_reg;
                loop_depth++;
            }
        }
        else if (strcmp(t, "}") == 0) {
            if (loop_depth > 0) {
                loop_depth--;
                uint32_t target_pc = loop_stack[loop_depth].start_pc;
                uint8_t  loop_reg  = loop_stack[loop_depth].loop_reg;

                emit_u8(&b, (uint8_t)GALO_LOOP_END);
                emit_u8(&b, loop_reg);
                emit_u32(&b, target_pc);
            }
        }
    }

    emit_u8(&b, 0xFF);

    *out_len = b.len;
    return b.data;
}
