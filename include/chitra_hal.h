#ifndef CHITRA_HAL_H
#define CHITRA_HAL_H

#include "chitra.h"
#include <stdbool.h>

/* Window and Application Lifecycle Interface */
typedef struct {
    void (*init)(const Chitra_Config* config);
    void (*run)(void);
    void (*quit)(void);
} Chitra_HAL_API;

/* Backend registration (implemented by whatever backend is compiled) */
const Chitra_HAL_API* chitra_hal_get_api(void);

#endif // CHITRA_HAL_H
