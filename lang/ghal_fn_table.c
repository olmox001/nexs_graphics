/* lang/ghal_fn_table.c — Register GHAL builtins in NEXS function table
 *
 * Called once at runtime startup (from reimplementation/hal/hal_hosted.c
 * or from the seL4/baremetal init path).
 */

#include "nexs_fn.h"

/* Forward declarations (defined in ghal_builtins.c) */
extern Value bi_win_open(Value *args, int n);
extern Value bi_win_close(Value *args, int n);
extern Value bi_draw_rect(Value *args, int n);
extern Value bi_surface_blit(Value *args, int n);
extern Value bi_draw_text(Value *args, int n);
extern Value bi_draw_clear(Value *args, int n);
extern Value bi_gl_clear(Value *args, int n);
extern Value bi_compute_run(Value *args, int n);
extern Value bi_vsync(Value *args, int n);

void ghal_register_builtins(void) {
    fn_register_builtin_sig("win_open",
        bi_win_open,
        "win_open(title str, w int, h int) -> int");

    fn_register_builtin_sig("win_close",
        bi_win_close,
        "win_close(id int) -> int");

    fn_register_builtin_sig("draw_rect",
        bi_draw_rect,
        "draw_rect(win int, x int, y int, w int, h int, color int) -> int");

    fn_register_builtin_sig("surface_blit",
        bi_surface_blit,
        "surface_blit(win int) -> int");

    fn_register_builtin_sig("draw_text",
        bi_draw_text,
        "draw_text(win int, x int, y int, text str, color int) -> int");

    fn_register_builtin_sig("draw_clear",
        bi_draw_clear,
        "draw_clear(win int, color int) -> int");

    fn_register_builtin_sig("gl_clear",
        bi_gl_clear,
        "gl_clear(r float, g float, b float, a float) -> int");

    fn_register_builtin_sig("compute_run",
        bi_compute_run,
        "compute_run(kernel str) -> int");

    fn_register_builtin_sig("vsync",
        bi_vsync,
        "vsync() -> int");
}
