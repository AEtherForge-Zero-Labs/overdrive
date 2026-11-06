/* The minimal consumer (specs/003-packaging, R209): public API only.
 * Creates a headless terminal, draws one frame, and exits 0 only if the
 * output sink received the frame. */
#include <overdrive/overdrive.h>

#include <stdio.h>

static bool count_output(void *context, const char *bytes, size_t length)
{
    size_t *calls = context;
    if (bytes != nullptr && length > 0) {
        *calls += 1;
    }
    return true;
}

int main(void)
{
    size_t calls = 0;
    OvdTerminalConfig config = ovd_terminal_config_default();
    config.backend = OVD_BACKEND_HEADLESS;
    config.headless_size = (OvdSize){ .cols = 20, .rows = 2 };
    config.sink = count_output;
    config.sink_context = &calls;

    OvdTerminal *terminal = nullptr;
    OvdResult result = ovd_terminal_create(&config, &terminal);
    if (result != OVD_OK) {
        fprintf(stderr, "consumer: create failed: %s\n", ovd_result_name(result));
        return 1;
    }
    OvdCanvas *canvas = nullptr;
    result = ovd_frame_begin(terminal, &canvas);
    if (result == OVD_OK) {
        static const char text[] = "consumer";
        ovd_canvas_text(canvas, 0, 0, text, sizeof text - 1, OVD_STYLE_DEFAULT);
        result = ovd_frame_end(terminal);
    }
    ovd_terminal_destroy(terminal);
    if (result != OVD_OK) {
        fprintf(stderr, "consumer: frame failed: %s\n", ovd_result_name(result));
        return 1;
    }
    if (calls == 0) {
        fprintf(stderr, "consumer: the sink was never called\n");
        return 1;
    }
    return 0;
}
