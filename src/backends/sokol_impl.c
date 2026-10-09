#define SOKOL_IMPL
#define SOKOL_GFX_IMPL
#define SOKOL_GLUE_IMPL
#define SOKOL_LOG_IMPL
#define SOKOL_NO_ENTRY

/* Backend is now defined by CMake (e.g. -DSOKOL_D3D11, -DSOKOL_GLCORE) */

#include "sokol_app.h"
#include "sokol_gfx.h"
#include "sokol_glue.h"
#include "sokol_log.h"
#define SOKOL_TIME_IMPL
#include "sokol_time.h"
