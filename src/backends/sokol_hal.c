
#include "sokol_app.h"
#include "sokol_log.h"
#include "chitra_hal.h"
#include "chitra_rhi.h"
#include "chitra_cmd.h"
#include "sokol_time.h"
#include <stdio.h>

#ifdef _WIN32
#include <windows.h>
#include <dwmapi.h>
#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#else
#include <errno.h>
#include <time.h>
#endif

static sapp_desc      s_desc            = {0};
static Chitra_Config  s_config          = {0};
static int            s_last_w          = 0;
static int            s_last_h          = 0;
static int            s_dirty_countdown = 3;
static double         s_target_ms       = 0.0;
static uint64_t       s_last_frame_time = 0;

#ifdef _WIN32
static HANDLE s_waitable_timer = NULL;
#endif

extern void chitra_internal_init(void);
extern void chitra_internal_prepare(void);
extern void chitra_internal_term(void);

/* -------------------------------------------------------------------------
 * chitra_sleep_ms
 * Sleeps for exactly `ms` milliseconds using the highest-resolution
 * mechanism available on the platform.
 *
 * Windows: High-resolution WaitableTimer (100ns units). Falls back to
 *          regular Sleep() if the timer is not available.
 * POSIX:   nanosleep(), restarted automatically on EINTR.
 * ------------------------------------------------------------------------- */
static void chitra_sleep_ms(double ms) {
    if (ms <= 0.0) return;

#ifdef _WIN32
    if (s_waitable_timer) {
        /* Negative 100-nanosecond ticks = relative wait */
        LONGLONG ticks = (LONGLONG)(ms * 10000.0);
        if (ticks < 1) ticks = 1;
        LARGE_INTEGER due = { .QuadPart = -ticks };
        SetWaitableTimer(s_waitable_timer, &due, 0, NULL, NULL, FALSE);
        WaitForSingleObject(s_waitable_timer, INFINITE);
    } else {
        DWORD wait = (DWORD)(ms + 0.5);
        Sleep(wait > 0 ? wait : 1);
    }
#else
    struct timespec req = {
        .tv_sec  = (time_t)(ms / 1000.0),
        .tv_nsec = (long)(((long long)(ms * 1000000.0)) % 1000000000)
    };
    while (nanosleep(&req, &req) != 0 && errno == EINTR) {}
#endif
}

/* -------------------------------------------------------------------------
 * chitra_idle_wait
 * Suspends the engine until the OS delivers any window message (input,
 * resize, paint, etc.). Gives true 0% CPU/GPU when the scene is unchanged.
 *
 * Windows: MsgWaitForMultipleObjectsEx(INFINITE) — wakes the instant a
 *          message is queued, no periodic polling.
 * POSIX:   nanosleep(16ms). A true event-driven wait on Linux/macOS needs
 *          platform-specific APIs (poll/epoll on X11/Wayland) — out of scope.
 * ------------------------------------------------------------------------- */
static void chitra_idle_wait(void) {
#ifdef _WIN32
    MsgWaitForMultipleObjectsEx(0, NULL, INFINITE, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
#else
    struct timespec req = { .tv_sec = 0, .tv_nsec = 16000000 };
    while (nanosleep(&req, &req) != 0 && errno == EINTR) {}
#endif
}

/* -------------------------------------------------------------------------
 * Sokol callbacks
 * ------------------------------------------------------------------------- */

static void sokol_init(void) {
    stm_setup();
    s_last_frame_time = stm_now();

#ifdef _WIN32
    /* High-resolution waitable timer for frame pacing.
     * Try Win10 1803+ high-res variant first, fall back to standard. */
    if (s_target_ms > 0.0) {
        s_waitable_timer = CreateWaitableTimerExW(
            NULL, NULL, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
            TIMER_MODIFY_STATE | SYNCHRONIZE);
        if (!s_waitable_timer) {
            s_waitable_timer = CreateWaitableTimerW(NULL, FALSE, NULL);
        }
    }

    /* Dark titlebar on Windows 11 */
    HWND hwnd = (HWND)sapp_win32_get_hwnd();
    if (hwnd) {
        BOOL dark = TRUE;
        DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
    }
#endif

    chitra_rhi_get_api()->init();
    chitra_internal_init();
    if (s_config.on_init) s_config.on_init();
}

static void sokol_frame(void) {
    int w = sapp_width();
    int h = sapp_height();
    if (w == 0 || h == 0) return;

    /* Detect resize and mark dirty */
    if (w != s_last_w || h != s_last_h) {
        chitra_cmd_set_dirty();
        s_last_w = w;
        s_last_h = h;
    }

    /* Let the app update its command list */
    if (s_config.on_frame) s_config.on_frame();

    /* If commands changed, re-prepare batches and schedule 3 draw frames.
     * Triple-buffering: GPU may still be reading the previous 2 frames. */
    if (chitra_cmd_is_dirty()) {
        chitra_internal_prepare();
        s_dirty_countdown = 3;
    }

    if (s_dirty_countdown > 0) {
        chitra_rhi_get_api()->begin_frame(w, h);
        chitra_cmd_draw();
        chitra_rhi_get_api()->end_frame();
        s_dirty_countdown--;
    } else {
        /* Scene is visually unchanged.
         * On VSync mode: idle-wait for OS events → true 0% CPU/GPU.
         * On framecap mode: the sleep below is sufficient — no extra wait. */
        if (s_target_ms == 0.0) {
            chitra_idle_wait();
        }
    }

    /* Manual frame cap (target_fps > 0, VSync disabled).
     * Sleep precisely until the next frame deadline. */
    if (s_target_ms > 0.0) {
        double elapsed_ms = stm_ms(stm_diff(stm_now(), s_last_frame_time));
        chitra_sleep_ms(s_target_ms - elapsed_ms);
        s_last_frame_time = stm_now();
    }
}

static void sokol_cleanup(void) {
    chitra_internal_term();
    chitra_rhi_get_api()->cleanup();
#ifdef _WIN32
    if (s_waitable_timer) {
        CloseHandle(s_waitable_timer);
        s_waitable_timer = NULL;
    }
#endif
}

static void sokol_event(const sapp_event* e) {
    if (e->type == SAPP_EVENTTYPE_RESIZED) {
        chitra_cmd_set_dirty();
        s_dirty_countdown = 3;
    }
    if (s_config.on_event) s_config.on_event(e);
}

/* -------------------------------------------------------------------------
 * HAL API
 * ------------------------------------------------------------------------- */

static void hal_init(const Chitra_Config* config) {
    s_target_ms = 0.0;
    if (config) {
        s_config            = *config;
        s_desc.width        = config->width;
        s_desc.height       = config->height;
        s_desc.window_title = config->title;

        if (config->target_fps > 0) {
            /* Manual framecap: disable driver VSync so Sokol does not cap us;
             * we sleep to the exact target interval ourselves. */
            s_desc.swap_interval = 0;
            s_target_ms = 1000.0 / (double)config->target_fps;
        } else {
            /* VSync: swap_interval = 1 → wait for 1 vertical blank signal. */
            s_desc.swap_interval = 1;
        }
    }
    s_desc.init_cb     = sokol_init;
    s_desc.frame_cb    = sokol_frame;
    s_desc.cleanup_cb  = sokol_cleanup;
    s_desc.event_cb    = sokol_event;
    s_desc.logger.func = slog_func;
}

static void hal_run(void) {
    sapp_run(&s_desc);
}

static void hal_quit(void) {
    sapp_request_quit();
}

static const Chitra_HAL_API s_api = {
    .init = hal_init,
    .run  = hal_run,
    .quit = hal_quit
};

const Chitra_HAL_API* chitra_hal_get_api(void) {
    return &s_api;
}
