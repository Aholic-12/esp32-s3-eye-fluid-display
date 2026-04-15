#include <math.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"

#include "fluid_simulation.h"

static const char *TAG = "fluid_sim";

#ifndef FLUID_PI
#define FLUID_PI 3.14159265358979323846f
#endif

#define SIM_DT                  0.85f
#define PRESSURE_ITERS          14
#define FLIP_BLEND              0.92f
#define BOUNDARY_DAMPING        0.55f
#define GRID_MARGIN_PIXELS      26.0f
#define MAX_PARTICLE_SPEED      1.15f

typedef struct {
    float x;
    float y;
    float vx;
    float vy;
} fluid_particle_t;

struct fluid_simulator_t {
    fluid_particle_t particles[FLUID_MAX_PARTICLES];
    int particle_count;
    float gravity_x;
    float gravity_y;
    uint8_t theme_id;
    uint32_t update_count;
    float grid_vx[FLUID_GRID_HEIGHT][FLUID_GRID_WIDTH];
    float grid_vy[FLUID_GRID_HEIGHT][FLUID_GRID_WIDTH];
    float prev_vx[FLUID_GRID_HEIGHT][FLUID_GRID_WIDTH];
    float prev_vy[FLUID_GRID_HEIGHT][FLUID_GRID_WIDTH];
    float weight[FLUID_GRID_HEIGHT][FLUID_GRID_WIDTH];
    float pressure[FLUID_GRID_HEIGHT][FLUID_GRID_WIDTH];
    float divergence[FLUID_GRID_HEIGHT][FLUID_GRID_WIDTH];
};

static fluid_simulator_t s_sim;
static uint32_t s_rand_state = 0x12345678;

static const uint16_t s_theme_palette[FLUID_THEME_COUNT][4] = {
    {0x041F, 0x0AFF, 0x7DFF, 0xFFFF},
    {0x7800, 0xF9C0, 0xFD20, 0xFFE0},
    {0x0200, 0x07E0, 0x07FF, 0xFFFF},
};

static float randf(void)
{
    s_rand_state = s_rand_state * 1664525u + 1013904223u;
    return (float)(s_rand_state & 0x00FFFFFFu) / 16777215.0f;
}

static float clampf(float value, float min_value, float max_value)
{
    if (value < min_value) {
        return min_value;
    }
    if (value > max_value) {
        return max_value;
    }
    return value;
}

static void clamp_particle(fluid_particle_t *p)
{
    if (p->x < 0.5f) {
        p->x = 0.5f;
        p->vx = -p->vx * BOUNDARY_DAMPING;
    }
    if (p->x > (FLUID_GRID_WIDTH - 1.5f)) {
        p->x = FLUID_GRID_WIDTH - 1.5f;
        p->vx = -p->vx * BOUNDARY_DAMPING;
    }
    if (p->y < 0.5f) {
        p->y = 0.5f;
        p->vy = -p->vy * BOUNDARY_DAMPING;
    }
    if (p->y > (FLUID_GRID_HEIGHT - 1.5f)) {
        p->y = FLUID_GRID_HEIGHT - 1.5f;
        p->vy = -p->vy * BOUNDARY_DAMPING;
    }
}

static void seed_particles(fluid_simulator_t *sim)
{
    const float cx = FLUID_GRID_WIDTH * 0.5f;
    const float cy = FLUID_GRID_HEIGHT * 0.55f;
    const float max_radius = FLUID_GRID_WIDTH * 0.24f;

    sim->particle_count = FLUID_MAX_PARTICLES;
    for (int i = 0; i < sim->particle_count; i++) {
        const float angle = randf() * 2.0f * FLUID_PI;
        const float radius = sqrtf(randf()) * max_radius;
        fluid_particle_t *p = &sim->particles[i];
        p->x = cx + cosf(angle) * radius;
        p->y = cy + sinf(angle) * radius * 0.72f;
        p->vx = (randf() - 0.5f) * 0.08f;
        p->vy = (randf() - 0.5f) * 0.08f;
        clamp_particle(p);
    }
}

static void clear_grid(fluid_simulator_t *sim)
{
    memset(sim->grid_vx, 0, sizeof(sim->grid_vx));
    memset(sim->grid_vy, 0, sizeof(sim->grid_vy));
    memset(sim->prev_vx, 0, sizeof(sim->prev_vx));
    memset(sim->prev_vy, 0, sizeof(sim->prev_vy));
    memset(sim->weight, 0, sizeof(sim->weight));
    memset(sim->pressure, 0, sizeof(sim->pressure));
    memset(sim->divergence, 0, sizeof(sim->divergence));
}

static void scatter_to_grid(fluid_simulator_t *sim)
{
    for (int i = 0; i < sim->particle_count; i++) {
        const fluid_particle_t *p = &sim->particles[i];
        const float gx = clampf(p->x, 0.0f, FLUID_GRID_WIDTH - 1.001f);
        const float gy = clampf(p->y, 0.0f, FLUID_GRID_HEIGHT - 1.001f);
        const int x0 = (int)floorf(gx);
        const int y0 = (int)floorf(gy);
        const int x1 = x0 + 1 < FLUID_GRID_WIDTH ? x0 + 1 : x0;
        const int y1 = y0 + 1 < FLUID_GRID_HEIGHT ? y0 + 1 : y0;
        const float fx = gx - x0;
        const float fy = gy - y0;
        const float w00 = (1.0f - fx) * (1.0f - fy);
        const float w10 = fx * (1.0f - fy);
        const float w01 = (1.0f - fx) * fy;
        const float w11 = fx * fy;

        sim->grid_vx[y0][x0] += p->vx * w00;
        sim->grid_vy[y0][x0] += p->vy * w00;
        sim->weight[y0][x0] += w00;

        sim->grid_vx[y0][x1] += p->vx * w10;
        sim->grid_vy[y0][x1] += p->vy * w10;
        sim->weight[y0][x1] += w10;

        sim->grid_vx[y1][x0] += p->vx * w01;
        sim->grid_vy[y1][x0] += p->vy * w01;
        sim->weight[y1][x0] += w01;

        sim->grid_vx[y1][x1] += p->vx * w11;
        sim->grid_vy[y1][x1] += p->vy * w11;
        sim->weight[y1][x1] += w11;
    }

    for (int y = 0; y < FLUID_GRID_HEIGHT; y++) {
        for (int x = 0; x < FLUID_GRID_WIDTH; x++) {
            if (sim->weight[y][x] > 0.0f) {
                const float inv_weight = 1.0f / sim->weight[y][x];
                sim->grid_vx[y][x] *= inv_weight;
                sim->grid_vy[y][x] *= inv_weight;
            }
            sim->prev_vx[y][x] = sim->grid_vx[y][x];
            sim->prev_vy[y][x] = sim->grid_vy[y][x];
        }
    }
}

static void enforce_boundaries(fluid_simulator_t *sim)
{
    for (int x = 0; x < FLUID_GRID_WIDTH; x++) {
        sim->grid_vx[0][x] = 0.0f;
        sim->grid_vx[FLUID_GRID_HEIGHT - 1][x] = 0.0f;
        sim->grid_vy[0][x] = 0.0f;
        sim->grid_vy[FLUID_GRID_HEIGHT - 1][x] = 0.0f;
    }

    for (int y = 0; y < FLUID_GRID_HEIGHT; y++) {
        sim->grid_vx[y][0] = 0.0f;
        sim->grid_vx[y][FLUID_GRID_WIDTH - 1] = 0.0f;
        sim->grid_vy[y][0] = 0.0f;
        sim->grid_vy[y][FLUID_GRID_WIDTH - 1] = 0.0f;
    }
}

static void apply_forces(fluid_simulator_t *sim)
{
    const float force_x = sim->gravity_x * 0.12f;
    const float force_y = 0.035f + sim->gravity_y * 0.12f;

    for (int y = 1; y < FLUID_GRID_HEIGHT - 1; y++) {
        for (int x = 1; x < FLUID_GRID_WIDTH - 1; x++) {
            if (sim->weight[y][x] > 0.0f) {
                sim->grid_vx[y][x] += force_x;
                sim->grid_vy[y][x] += force_y;
            }
        }
    }
}

static void project_pressure(fluid_simulator_t *sim)
{
    for (int y = 1; y < FLUID_GRID_HEIGHT - 1; y++) {
        for (int x = 1; x < FLUID_GRID_WIDTH - 1; x++) {
            sim->divergence[y][x] = -0.5f * ((sim->grid_vx[y][x + 1] - sim->grid_vx[y][x - 1]) +
                                             (sim->grid_vy[y + 1][x] - sim->grid_vy[y - 1][x]));
        }
    }

    for (int iter = 0; iter < PRESSURE_ITERS; iter++) {
        for (int y = 1; y < FLUID_GRID_HEIGHT - 1; y++) {
            for (int x = 1; x < FLUID_GRID_WIDTH - 1; x++) {
                sim->pressure[y][x] = (sim->pressure[y][x - 1] + sim->pressure[y][x + 1] +
                                       sim->pressure[y - 1][x] + sim->pressure[y + 1][x] -
                                       sim->divergence[y][x]) * 0.25f;
            }
        }
    }

    for (int y = 1; y < FLUID_GRID_HEIGHT - 1; y++) {
        for (int x = 1; x < FLUID_GRID_WIDTH - 1; x++) {
            sim->grid_vx[y][x] -= 0.5f * (sim->pressure[y][x + 1] - sim->pressure[y][x - 1]);
            sim->grid_vy[y][x] -= 0.5f * (sim->pressure[y + 1][x] - sim->pressure[y - 1][x]);
        }
    }
}

static float sample_field(float field[FLUID_GRID_HEIGHT][FLUID_GRID_WIDTH], float x, float y)
{
    const float gx = clampf(x, 0.0f, FLUID_GRID_WIDTH - 1.001f);
    const float gy = clampf(y, 0.0f, FLUID_GRID_HEIGHT - 1.001f);
    const int x0 = (int)floorf(gx);
    const int y0 = (int)floorf(gy);
    const int x1 = x0 + 1 < FLUID_GRID_WIDTH ? x0 + 1 : x0;
    const int y1 = y0 + 1 < FLUID_GRID_HEIGHT ? y0 + 1 : y0;
    const float fx = gx - x0;
    const float fy = gy - y0;

    return field[y0][x0] * (1.0f - fx) * (1.0f - fy) +
           field[y0][x1] * fx * (1.0f - fy) +
           field[y1][x0] * (1.0f - fx) * fy +
           field[y1][x1] * fx * fy;
}

static void update_particles_from_grid(fluid_simulator_t *sim)
{
    for (int i = 0; i < sim->particle_count; i++) {
        fluid_particle_t *p = &sim->particles[i];
        const float pic_vx = sample_field(sim->grid_vx, p->x, p->y);
        const float pic_vy = sample_field(sim->grid_vy, p->x, p->y);
        const float prev_vx = sample_field(sim->prev_vx, p->x, p->y);
        const float prev_vy = sample_field(sim->prev_vy, p->x, p->y);
        const float flip_vx = p->vx + (pic_vx - prev_vx);
        const float flip_vy = p->vy + (pic_vy - prev_vy);

        p->vx = flip_vx * FLIP_BLEND + pic_vx * (1.0f - FLIP_BLEND);
        p->vy = flip_vy * FLIP_BLEND + pic_vy * (1.0f - FLIP_BLEND);

        const float speed = sqrtf(p->vx * p->vx + p->vy * p->vy);
        if (speed > MAX_PARTICLE_SPEED) {
            const float scale = MAX_PARTICLE_SPEED / speed;
            p->vx *= scale;
            p->vy *= scale;
        }

        p->x += p->vx * SIM_DT;
        p->y += p->vy * SIM_DT;
        clamp_particle(p);
    }
}

fluid_simulator_t *fluid_init(void)
{
    memset(&s_sim, 0, sizeof(s_sim));
    s_sim.gravity_y = 0.35f;
    s_sim.theme_id = 0;
    seed_particles(&s_sim);
    ESP_LOGI(TAG, "init ok, particles=%d", s_sim.particle_count);
    return &s_sim;
}

fluid_simulator_t *fluid_get_simulator(void)
{
    return &s_sim;
}

void fluid_set_gravity(fluid_simulator_t *sim, float gx, float gy)
{
    if (sim == NULL) {
        return;
    }
    sim->gravity_x = clampf(gx, -1.2f, 1.2f);
    sim->gravity_y = clampf(gy, -1.2f, 1.2f);
}

void fluid_set_theme(fluid_simulator_t *sim, uint8_t theme_id)
{
    if (sim == NULL) {
        return;
    }
    sim->theme_id = theme_id % FLUID_THEME_COUNT;
}

void fluid_update(fluid_simulator_t *sim)
{
    if (sim == NULL) {
        return;
    }

    sim->update_count++;
    clear_grid(sim);
    scatter_to_grid(sim);
    apply_forces(sim);
    enforce_boundaries(sim);
    project_pressure(sim);
    enforce_boundaries(sim);
    update_particles_from_grid(sim);
}

void fluid_render(fluid_simulator_t *sim, lcd_handle_t lcd)
{
    if (sim == NULL || lcd == NULL) {
        return;
    }

    const float cell_w = (LCD_WIDTH - GRID_MARGIN_PIXELS * 2.0f) / (float)(FLUID_GRID_WIDTH - 1);
    const float cell_h = (LCD_HEIGHT - GRID_MARGIN_PIXELS * 2.0f) / (float)(FLUID_GRID_HEIGHT - 1);
    const uint16_t *palette = s_theme_palette[sim->theme_id % FLUID_THEME_COUNT];

    for (int i = 0; i < sim->particle_count; i++) {
        const fluid_particle_t *p = &sim->particles[i];
        const float speed = sqrtf(p->vx * p->vx + p->vy * p->vy);
        const int color_index = speed > 0.70f ? 3 : speed > 0.40f ? 2 : speed > 0.18f ? 1 : 0;
        const int radius = speed > 0.45f ? 3 : 2;
        const int sx = (int)(GRID_MARGIN_PIXELS + p->x * cell_w);
        const int sy = (int)(GRID_MARGIN_PIXELS + p->y * cell_h);

        lcd_fill_circle(lcd, sx, sy, radius, palette[color_index]);
        if (radius > 2) {
            lcd_draw_pixel(lcd, sx - 1, sy - 1, LCD_COLOR_WHITE);
        }
    }
}

void fluid_clear(fluid_simulator_t *sim)
{
    if (sim == NULL) {
        return;
    }
    seed_particles(sim);
}

int fluid_get_particle_count(const fluid_simulator_t *sim)
{
    return sim ? sim->particle_count : 0;
}
