/* services/core/ghal_html.c — VFS HTML/CSS Parser & Rendering Engine
 *
 * Implements bidirectional HTML/CSS layout mapping into the NEXS Registry VFS
 * and recursive, platform-independent 2D layout rendering.
 */

#include "nexs_registry.h"
#include "nexs_value.h"
#include "../../include/ghal.h"
#include "../../include/ghal_2d.h"
#include "../../include/ghal_surface.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── HTML/CSS VFS Parser (Recursive Descent) ────────────────── */

static const char *parse_node(int win_id, const char *p, char *curr_path, int *child_count) {
    (void)win_id;
    (void)child_count;
    // Skip initial whitespace and comments
    while (*p && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) p++;
    if (!*p) return p;

    // We expect a tag start '<'
    if (*p == '<') {
        p++;
        if (*p == '/') {
            // Closing tag of parent, back out
            while (*p && *p != '>') p++;
            if (*p == '>') p++;
            return p;
        }

        // Parse tag name
        char tag[64];
        int tag_len = 0;
        while (*p && *p != ' ' && *p != '\t' && *p != '>' && *p != '/') {
            if (tag_len < 63) tag[tag_len++] = *p;
            p++;
        }
        tag[tag_len] = '\0';

        // Write tag to registry
        char path_tag[256];
        snprintf(path_tag, sizeof(path_tag), "%s/tag", curr_path);
        reg_set(path_tag, VAL_STR(tag), RK_READ | RK_WRITE);

        // Parse attributes (e.g. style="...")
        char style_path[256];
        snprintf(style_path, sizeof(style_path), "%s/style", curr_path);

        while (*p && *p != '>' && *p != '/') {
            if (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') {
                p++;
                continue;
            }

            // Parse attribute name
            char attr_name[64];
            int attr_len = 0;
            while (*p && *p != '=' && *p != ' ' && *p != '\t' && *p != '>') {
                if (attr_len < 63) attr_name[attr_len++] = *p;
                p++;
            }
            attr_name[attr_len] = '\0';

            if (*p == '=') {
                p++;
                char quote = '\0';
                if (*p == '"' || *p == '\'') {
                    quote = *p;
                    p++;
                }

                char attr_val[512];
                int val_len = 0;
                while (*p && (quote ? *p != quote : (*p != ' ' && *p != '\t' && *p != '>'))) {
                    if (val_len < 511) attr_val[val_len++] = *p;
                    p++;
                }
                attr_val[val_len] = '\0';
                if (quote && *p == quote) p++;

                // If attribute is "style", parse individual CSS properties
                if (strcmp(attr_name, "style") == 0) {
                    printf("[HTML DEBUG] style attr_val = '%s'\n", attr_val);
                    const char *sp = attr_val;
                    while (*sp) {
                        while (*sp && (*sp == ' ' || *sp == '\t' || *sp == ';')) sp++;
                        if (!*sp) break;

                        char prop_name[64];
                        int prop_len = 0;
                        while (*sp && *sp != ':' && *sp != ';') {
                            if (prop_len < 63) prop_name[prop_len++] = *sp;
                            sp++;
                        }
                        prop_name[prop_len] = '\0';

                        // Trim spaces from prop_name
                        while (prop_len > 0 && (prop_name[prop_len - 1] == ' ' || prop_name[prop_len - 1] == '\t')) {
                            prop_name[--prop_len] = '\0';
                        }

                        if (*sp == ':') {
                            sp++;
                            while (*sp && (*sp == ' ' || *sp == '\t')) sp++;
                            char prop_val[64];
                            int prop_val_len = 0;
                            while (*sp && *sp != ';') {
                                if (prop_val_len < 63) prop_val[prop_val_len++] = *sp;
                                sp++;
                            }
                            prop_val[prop_val_len] = '\0';

                            // Trim spaces from prop_val
                            while (prop_val_len > 0 && (prop_val[prop_val_len - 1] == ' ' || prop_val[prop_val_len - 1] == '\t')) {
                                prop_val[--prop_val_len] = '\0';
                            }

                            printf("[HTML DEBUG]   prop_name = '%s', prop_val = '%s'\n", prop_name, prop_val);

                            char path_prop[256];
                            snprintf(path_prop, sizeof(path_prop), "%s/%s", style_path, prop_name);
                            reg_set(path_prop, VAL_STR(prop_val), RK_READ | RK_WRITE);
                        }
                    }
                } else {
                    // Store other attributes (e.g. onclick) directly
                    char path_attr[256];
                    snprintf(path_attr, sizeof(path_attr), "%s/%s", curr_path, attr_name);
                    reg_set(path_attr, VAL_STR(attr_val), RK_READ | RK_WRITE);
                }
            } else {
                // Boolean attribute
                char path_attr[256];
                snprintf(path_attr, sizeof(path_attr), "%s/%s", curr_path, attr_name);
                reg_set(path_attr, VAL_INT(1), RK_READ | RK_WRITE);
            }
        }

        int self_closing = 0;
        if (*p == '/') {
            self_closing = 1;
            p++;
        }
        if (*p == '>') p++;

        if (self_closing) {
            char path_cc[256];
            snprintf(path_cc, sizeof(path_cc), "%s/child_count", curr_path);
            reg_set(path_cc, VAL_INT(0), RK_READ | RK_WRITE);
            return p;
        }

        // Parse child nodes or text content
        int c_count = 0;
        while (*p) {
            while (*p && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) p++;
            if (!*p) break;

            if (*p == '<') {
                if (*(p + 1) == '/') {
                    // Closing tag of current node
                    p++; // '<'
                    p++; // '/'
                    while (*p && *p != '>') p++;
                    if (*p == '>') p++;
                    break;
                }

                // Recursively parse nested child
                char child_path[256];
                snprintf(child_path, sizeof(child_path), "%s/child/%d", curr_path, c_count);
                p = parse_node(win_id, p, child_path, &c_count);
                c_count++;
            } else {
                // Parse text content
                char text[1024];
                int text_len = 0;
                while (*p && *p != '<') {
                    if (text_len < 1023) text[text_len++] = *p;
                    p++;
                }
                text[text_len] = '\0';

                // Trim trailing whitespaces
                while (text_len > 0 && (text[text_len - 1] == ' ' || text[text_len - 1] == '\t' ||
                                        text[text_len - 1] == '\r' || text[text_len - 1] == '\n')) {
                    text[--text_len] = '\0';
                }

                if (text_len > 0) {
                    char path_text[256];
                    snprintf(path_text, sizeof(path_text), "%s/text", curr_path);
                    reg_set(path_text, VAL_STR(text), RK_READ | RK_WRITE);
                }
            }
        }

        char path_cc[256];
        snprintf(path_cc, sizeof(path_cc), "%s/child_count", curr_path);
        reg_set(path_cc, VAL_INT(c_count), RK_READ | RK_WRITE);
    }
    return p;
}

void ghal_html_parse(int win_id, const char *html_str) {
    printf("[HTML DEBUG] html_str = '%s'\n", html_str);
    char curr_path[256];
    snprintf(curr_path, sizeof(curr_path), "/dev/win/%u/layout", win_id);
    reg_delete(curr_path); // clear previous structure

    int child_count = 0;
    parse_node(win_id, html_str, curr_path, &child_count);
}

/* ── HTML/CSS VFS Serializer (Bidirectional Dump) ───────────── */

static void dump_node(const char *curr_path, char *buf, size_t max_len) {
    char path_tag[256];
    snprintf(path_tag, sizeof(path_tag), "%s/tag", curr_path);
    Value tag_val = reg_get(path_tag);
    if (tag_val.type != TYPE_STR || !tag_val.data) {
        val_free(&tag_val);
        return;
    }
    const char *tag = (char *)tag_val.data;

    strncat(buf, "<", max_len - strlen(buf) - 1);
    strncat(buf, tag, max_len - strlen(buf) - 1);

    // Read CSS styles from Registry style/ child folder
    char style_path[256];
    snprintf(style_path, sizeof(style_path), "%s/style", curr_path);
    RegKey *style_key = reg_lookup(style_path);
    if (style_key && style_key->children) {
        strncat(buf, " style=\"", max_len - strlen(buf) - 1);
        RegKey *prop = style_key->children;
        while (prop) {
            if (prop->val.type == TYPE_STR && prop->val.data) {
                strncat(buf, prop->name, max_len - strlen(buf) - 1);
                strncat(buf, ": ", max_len - strlen(buf) - 1);
                strncat(buf, (char *)prop->val.data, max_len - strlen(buf) - 1);
                strncat(buf, ";", max_len - strlen(buf) - 1);
                if (prop->next) {
                    strncat(buf, " ", max_len - strlen(buf) - 1);
                }
            }
            prop = prop->next;
        }
        strncat(buf, "\"", max_len - strlen(buf) - 1);
    }

    // Read other custom attributes (like onclick)
    RegKey *node_key = reg_lookup(curr_path);
    if (node_key && node_key->children) {
        RegKey *child = node_key->children;
        while (child) {
            if (strcmp(child->name, "tag") != 0 &&
                strcmp(child->name, "text") != 0 &&
                strcmp(child->name, "style") != 0 &&
                strcmp(child->name, "child") != 0 &&
                strcmp(child->name, "child_count") != 0) {

                strncat(buf, " ", max_len - strlen(buf) - 1);
                strncat(buf, child->name, max_len - strlen(buf) - 1);
                strncat(buf, "=\"", max_len - strlen(buf) - 1);
                if (child->val.type == TYPE_STR && child->val.data) {
                    strncat(buf, (char *)child->val.data, max_len - strlen(buf) - 1);
                } else if (child->val.type == TYPE_INT) {
                    char val_buf[32];
                    snprintf(val_buf, sizeof(val_buf), "%lld", (long long)child->val.ival);
                    strncat(buf, val_buf, max_len - strlen(buf) - 1);
                }
                strncat(buf, "\"", max_len - strlen(buf) - 1);
            }
            child = child->next;
        }
    }

    strncat(buf, ">", max_len - strlen(buf) - 1);

    // Text content
    char path_text[256];
    snprintf(path_text, sizeof(path_text), "%s/text", curr_path);
    Value text_val = reg_get(path_text);
    if (text_val.type == TYPE_STR && text_val.data) {
        strncat(buf, (char *)text_val.data, max_len - strlen(buf) - 1);
    }
    val_free(&text_val);

    // Recursively serialize children
    char path_cc[256];
    snprintf(path_cc, sizeof(path_cc), "%s/child_count", curr_path);
    Value cc_val = reg_get(path_cc);
    int cc = (cc_val.type == TYPE_INT) ? (int)cc_val.ival : 0;
    val_free(&cc_val);

    for (int i = 0; i < cc; i++) {
        char child_path[256];
        snprintf(child_path, sizeof(child_path), "%s/child/%d", curr_path, i);
        dump_node(child_path, buf, max_len);
    }

    strncat(buf, "</", max_len - strlen(buf) - 1);
    strncat(buf, tag, max_len - strlen(buf) - 1);
    strncat(buf, ">", max_len - strlen(buf) - 1);

    val_free(&tag_val);
}

void ghal_html_dump(int win_id, char *out_buf, size_t max_len) {
    char curr_path[256];
    snprintf(curr_path, sizeof(curr_path), "/dev/win/%u/layout", win_id);
    out_buf[0] = '\0';
    dump_node(curr_path, out_buf, max_len);
}

/* ── 2D Layout Box Rendering ────────────────────────────────── */

static void render_node(GHalSurface *surf, const char *curr_path, int32_t px, int32_t py, int32_t pw, int32_t ph, int32_t *out_h, int draw) {
    char path_tag[256];
    snprintf(path_tag, sizeof(path_tag), "%s/tag", curr_path);
    Value tag_val = reg_get(path_tag);
    if (tag_val.type != TYPE_STR || !tag_val.data) {
        val_free(&tag_val);
        *out_h = 0;
        return;
    }

    uint32_t bg_color = 0;
    int has_bg = 0;
    uint32_t text_color = 0xFFFFFFFF; // default white
    int32_t margin = 0;
    int32_t padding = 0;

    char path_style[256];

    // background-color
    snprintf(path_style, sizeof(path_style), "%s/style/background-color", curr_path);
    Value bg_val = reg_get(path_style);
    if (bg_val.type == TYPE_STR && bg_val.data) {
        const char *bg_str = (char *)bg_val.data;
        if (bg_str[0] == '#') {
            unsigned int r = 0, g = 0, b = 0, a = 255;
            int scanned = sscanf(bg_str + 1, "%02x%02x%02x%02x", &r, &g, &b, &a);
            if (scanned == 3) a = 255;
            bg_color = GHAL_RGBA(r, g, b, a);
            has_bg = 1;
        }
    }
    val_free(&bg_val);

    // color
    snprintf(path_style, sizeof(path_style), "%s/style/color", curr_path);
    Value c_val = reg_get(path_style);
    if (c_val.type == TYPE_STR && c_val.data) {
        const char *c_str = (char *)c_val.data;
        if (c_str[0] == '#') {
            unsigned int r = 255, g = 255, b = 255, a = 255;
            int scanned = sscanf(c_str + 1, "%02x%02x%02x%02x", &r, &g, &b, &a);
            if (scanned == 3) a = 255;
            text_color = GHAL_RGBA(r, g, b, a);
        }
    }
    val_free(&c_val);

    // margin
    snprintf(path_style, sizeof(path_style), "%s/style/margin", curr_path);
    Value m_val = reg_get(path_style);
    if (m_val.type == TYPE_STR && m_val.data) {
        margin = atoi((char *)m_val.data);
    }
    val_free(&m_val);

    // padding
    snprintf(path_style, sizeof(path_style), "%s/style/padding", curr_path);
    Value pad_val = reg_get(path_style);
    if (pad_val.type == TYPE_STR && pad_val.data) {
        padding = atoi((char *)pad_val.data);
    }
    val_free(&pad_val);

    int32_t w = pw - 2 * margin;
    int32_t h = ph;
    int is_height_auto = 1;

    snprintf(path_style, sizeof(path_style), "%s/style/width", curr_path);
    Value w_val = reg_get(path_style);
    if (w_val.type == TYPE_STR && w_val.data) {
        const char *w_str = (char *)w_val.data;
        if (w_str[strlen(w_str) - 1] == '%') {
            int pct = atoi(w_str);
            w = (pw * pct) / 100 - 2 * margin;
        } else {
            w = atoi(w_str);
        }
    }
    val_free(&w_val);

    snprintf(path_style, sizeof(path_style), "%s/style/height", curr_path);
    Value h_val = reg_get(path_style);
    if (h_val.type == TYPE_STR && h_val.data) {
        const char *h_str = (char *)h_val.data;
        if (h_str[strlen(h_str) - 1] == '%') {
            int pct = atoi(h_str);
            h = (ph * pct) / 100 - 2 * margin;
            is_height_auto = 0;
        } else {
            h = atoi(h_str);
            is_height_auto = 0;
        }
    }
    val_free(&h_val);

    int32_t node_x = px + margin;
    int32_t node_y = py + margin;

    // Height of text content
    int32_t text_h = 0;
    char path_text[256];
    snprintf(path_text, sizeof(path_text), "%s/text", curr_path);
    Value text_val = reg_get(path_text);
    if (text_val.type == TYPE_STR && text_val.data) {
        int lines = 1;
        const char *tp = (char *)text_val.data;
        while (*tp) {
            if (*tp == '\n') lines++;
            tp++;
        }
        text_h = lines * 16 + 2 * padding;
    }

    // Children layout
    char path_cc[256];
    snprintf(path_cc, sizeof(path_cc), "%s/child_count", curr_path);
    Value cc_val = reg_get(path_cc);
    int cc = (cc_val.type == TYPE_INT) ? (int)cc_val.ival : 0;
    val_free(&cc_val);

    int32_t children_h = 0;
    int32_t *child_heights = NULL;
    if (cc > 0) {
        child_heights = malloc(sizeof(int32_t) * cc);
        for (int i = 0; i < cc; i++) {
            char child_path[256];
            snprintf(child_path, sizeof(child_path), "%s/child/%d", curr_path, i);
            int32_t ch_h = 0;
            // First pass to measure child heights (always draw=0 for layout measurement)
            render_node(surf, child_path, node_x + padding, node_y + padding + children_h, w - 2 * padding, ph - 2 * padding - children_h, &ch_h, 0);
            child_heights[i] = ch_h;
            children_h += ch_h;
        }
    }

    if (is_height_auto) {
        h = text_h + children_h + 2 * padding;
    }

    if (draw) {
        // 1. Draw element background
        if (has_bg) {
            draw2d_fill_rect(surf, node_x, node_y, w, h, bg_color);
        }

        // 2. Draw element border
        snprintf(path_style, sizeof(path_style), "%s/style/border", curr_path);
        Value b_val = reg_get(path_style);
        if (b_val.type == TYPE_STR && b_val.data) {
            uint32_t border_color = text_color;
            const char *b_str = (char *)b_val.data;
            const char *b_color_str = strchr(b_str, '#');
            if (b_color_str) {
                unsigned int r = 255, g = 255, b = 255, a = 255;
                int scanned = sscanf(b_color_str + 1, "%02x%02x%02x%02x", &r, &g, &b, &a);
                if (scanned == 3) a = 255;
                border_color = GHAL_RGBA(r, g, b, a);
            }
            int border_w = atoi(b_str);
            if (border_w <= 0) border_w = 1;

            for (int i = 0; i < border_w; i++) {
                draw2d_line(surf, node_x + i, node_y + i, node_x + w - 1 - i, node_y + i, border_color);
                draw2d_line(surf, node_x + i, node_y + h - 1 - i, node_x + w - 1 - i, node_y + h - 1 - i, border_color);
                draw2d_line(surf, node_x + i, node_y + i, node_x + i, node_y + h - 1 - i, border_color);
                draw2d_line(surf, node_x + w - 1 - i, node_y + i, node_x + w - 1 - i, node_y + h - 1 - i, border_color);
            }
        }
        val_free(&b_val);

        // 3. Draw text content
        if (text_val.type == TYPE_STR && text_val.data) {
            draw2d_text(surf, node_x + padding, node_y + padding, (char *)text_val.data, text_color);
        }

        // 4. Draw children recursively with draw=1
        if (cc > 0 && child_heights) {
            int32_t cy = node_y + padding;
            for (int i = 0; i < cc; i++) {
                char child_path[256];
                snprintf(child_path, sizeof(child_path), "%s/child/%d", curr_path, i);
                int32_t dummy = 0;
                render_node(surf, child_path, node_x + padding, cy, w - 2 * padding, h - 2 * padding, &dummy, 1);
                cy += child_heights[i];
            }
        }
    }

    val_free(&text_val);
    if (child_heights) {
        free(child_heights);
    }

    *out_h = h + 2 * margin;
    val_free(&tag_val);
}

void ghal_layout_render(GHalWindow *w) {
    if (!w || !w->surface) return;
    char curr_path[256];
    snprintf(curr_path, sizeof(curr_path), "/dev/win/%u/layout", w->id);
    int32_t dummy = 0;
    // Two-pass rendering:
    // Pass 1: Measure exact dimensions (draw = 0)
    render_node(w->surface, curr_path, 0, 0, w->width, w->height, &dummy, 0);
    // Pass 2: Draw components from back to front (draw = 1)
    render_node(w->surface, curr_path, 0, 0, w->width, w->height, &dummy, 1);
}

