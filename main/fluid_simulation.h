#ifndef FLUID_SIMULATION_H
#define FLUID_SIMULATION_H

#include <stdint.h>

#include "sdkconfig.h"
#include "lcd_driver.h"

#define FLUID_GRID_WIDTH    32
#define FLUID_GRID_HEIGHT   32
#define FLUID_THEME_COUNT   3

#define FLUID_MAX_PARTICLES 360

typedef struct fluid_simulator_t fluid_simulator_t;

fluid_simulator_t *fluid_init(void);
fluid_simulator_t *fluid_get_simulator(void);
void fluid_set_gravity(fluid_simulator_t *sim, float gx, float gy);
void fluid_set_theme(fluid_simulator_t *sim, uint8_t theme_id);
void fluid_update(fluid_simulator_t *sim);
void fluid_render(fluid_simulator_t *sim, lcd_handle_t lcd);
void fluid_clear(fluid_simulator_t *sim);
int fluid_get_particle_count(const fluid_simulator_t *sim);

#endif // FLUID_SIMULATION_H
