#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"

#include "lcd_driver.h"

static const char *TAG = "minimal_watch";

#ifndef APP_PI
#define APP_PI 3.14159265358979323846f
#define APP_HALF_PI 1.57079632679489661923f
#endif

#define FRAME_DELAY_MS           16
#define FACE_CENTER_X            (LCD_WIDTH / 2)
#define FACE_CENTER_Y            (LCD_HEIGHT / 2)
#define FACE_RADIUS              106

#define COLOR_SCREEN_BG          0x0000
#define COLOR_FACE_BG            0x0841
#define COLOR_RING_OUTER         0x2104
#define COLOR_RING_MID           0x39E7
#define COLOR_RING_INNER         0x632C
#define COLOR_TICK_MINOR         0x6B4D
#define COLOR_TICK_MAJOR         0xEF7D
#define COLOR_HAND_HOUR          0xFFFF
#define COLOR_HAND_MINUTE        0xD69A
#define COLOR_HAND_SECOND        0xFA69
#define COLOR_HAND_SHADOW        0x1082
#define COLOR_CENTER_OUTER       0x7BEF
#define COLOR_CENTER_INNER       0xFFFF

typedef struct {
    lcd_handle_t lcd;
} app_state_t;

typedef struct {
    float hour_value;
    float minute_value;
    float second_value;
} clock_snapshot_t;

static app_state_t s_app;

static int month_from_build(const char *month)
{
    static const char *months[12] = {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec",
    };

    for (int i = 0; i < 12; i++) {
        if (strncmp(month, months[i], 3) == 0) {
            return i;
        }
    }

    return 0;
}

static void init_clock_time(void)
{
    setenv("TZ", "CST-8", 1);
    tzset();

    struct timeval now = {0};
    gettimeofday(&now, NULL);
    if (now.tv_sec >= 1704067200) {
        return;
    }

    struct tm build_tm = {
        .tm_year = ((__DATE__[7] - '0') * 1000 + (__DATE__[8] - '0') * 100 + (__DATE__[9] - '0') * 10 + (__DATE__[10] - '0')) - 1900,
        .tm_mon = month_from_build(__DATE__),
        .tm_mday = (__DATE__[4] == ' ') ? (__DATE__[5] - '0') : ((__DATE__[4] - '0') * 10 + (__DATE__[5] - '0')),
        .tm_hour = (__TIME__[0] - '0') * 10 + (__TIME__[1] - '0'),
        .tm_min = (__TIME__[3] - '0') * 10 + (__TIME__[4] - '0'),
        .tm_sec = (__TIME__[6] - '0') * 10 + (__TIME__[7] - '0'),
        .tm_isdst = -1,
    };

    const time_t build_time = mktime(&build_tm);
    if (build_time > 0) {
        const struct timeval build_tv = {
            .tv_sec = build_time,
            .tv_usec = 0,
        };
        settimeofday(&build_tv, NULL);
        ESP_LOGI(TAG, "clock initialized from build time %s %s", __DATE__, __TIME__);
    }
}

static clock_snapshot_t sample_clock(void)
{
    struct timeval tv = {0};
    gettimeofday(&tv, NULL);

    time_t now_seconds = tv.tv_sec;
    struct tm tm_info = {0};
    localtime_r(&now_seconds, &tm_info);

    const float second_fraction = (float)tv.tv_usec / 1000000.0f;
    const float seconds = (float)tm_info.tm_sec + second_fraction;
    const float minutes = (float)tm_info.tm_min + seconds / 60.0f;
    const float hours = (float)(tm_info.tm_hour % 12) + minutes / 60.0f;

    const clock_snapshot_t snapshot = {
        .hour_value = hours,
        .minute_value = minutes,
        .second_value = seconds,
    };

    return snapshot;
}

static float clock_angle(float value, float scale)
{
    return (value / scale) * 2.0f * APP_PI - APP_HALF_PI;
}

static void draw_ring(lcd_handle_t lcd, int cx, int cy, int outer_radius, int thickness, uint16_t color)
{
    for (int i = 0; i < thickness; i++) {
        lcd_draw_circle(lcd, cx, cy, outer_radius - i, color);
    }
}

static void draw_wide_line(lcd_handle_t lcd, int x0, int y0, int x1, int y1, int half_width, uint16_t color)
{
    const float vx = (float)(x1 - x0);
    const float vy = (float)(y1 - y0);
    const float length = sqrtf(vx * vx + vy * vy);

    if (length < 1.0f) {
        lcd_draw_pixel(lcd, x0, y0, color);
        return;
    }

    const float px = -vy / length;
    const float py = vx / length;

    for (int offset = -half_width; offset <= half_width; offset++) {
        const int ox = (int)lroundf(px * (float)offset);
        const int oy = (int)lroundf(py * (float)offset);
        lcd_draw_line(lcd, x0 + ox, y0 + oy, x1 + ox, y1 + oy, color);
    }
}

static void draw_tick(lcd_handle_t lcd, int cx, int cy, float angle, int inner_radius, int outer_radius, int half_width, uint16_t color)
{
    const int x0 = cx + (int)lroundf(cosf(angle) * (float)inner_radius);
    const int y0 = cy + (int)lroundf(sinf(angle) * (float)inner_radius);
    const int x1 = cx + (int)lroundf(cosf(angle) * (float)outer_radius);
    const int y1 = cy + (int)lroundf(sinf(angle) * (float)outer_radius);
    draw_wide_line(lcd, x0, y0, x1, y1, half_width, color);
}

static void draw_ticks(lcd_handle_t lcd, int cx, int cy)
{
    const int outer_radius = FACE_RADIUS - 12;

    for (int i = 0; i < 60; i++) {
        const float angle = clock_angle((float)i, 60.0f);
        const bool major = (i % 5) == 0;
        const bool quarter = (i % 15) == 0;
        const int inner_radius = quarter ? outer_radius - 18 : (major ? outer_radius - 14 : outer_radius - 7);
        const int half_width = quarter ? 2 : (major ? 1 : 0);
        const uint16_t color = major ? COLOR_TICK_MAJOR : COLOR_TICK_MINOR;
        draw_tick(lcd, cx, cy, angle, inner_radius, outer_radius, half_width, color);
    }
}

static void draw_hand(lcd_handle_t lcd, int cx, int cy, float angle, int length, int tail, int half_width, uint16_t color)
{
    const float dx = cosf(angle);
    const float dy = sinf(angle);
    const int sx = cx - (int)lroundf(dx * (float)tail);
    const int sy = cy - (int)lroundf(dy * (float)tail);
    const int ex = cx + (int)lroundf(dx * (float)length);
    const int ey = cy + (int)lroundf(dy * (float)length);

    draw_wide_line(lcd, sx, sy, ex, ey, half_width, color);
    lcd_fill_circle(lcd, ex, ey, half_width > 0 ? half_width : 1, color);
}

static void draw_hand_with_shadow(lcd_handle_t lcd, int cx, int cy, float angle, int length, int tail, int half_width, uint16_t shadow_color, uint16_t color)
{
    draw_hand(lcd, cx + 2, cy + 2, angle, length, tail, half_width, shadow_color);
    draw_hand(lcd, cx, cy, angle, length, tail, half_width, color);
}

static void draw_watch_face(lcd_handle_t lcd, const clock_snapshot_t *clock)
{
    const int cx = FACE_CENTER_X;
    const int cy = FACE_CENTER_Y;
    const float hour_angle = clock_angle(clock->hour_value, 12.0f);
    const float minute_angle = clock_angle(clock->minute_value, 60.0f);
    const float second_angle = clock_angle(clock->second_value, 60.0f);
    const float second_dx = cosf(second_angle);
    const float second_dy = sinf(second_angle);
    const int second_counter_x = cx - (int)lroundf(second_dx * 20.0f);
    const int second_counter_y = cy - (int)lroundf(second_dy * 20.0f);
    const int second_tip_x = cx + (int)lroundf(second_dx * 84.0f);
    const int second_tip_y = cy + (int)lroundf(second_dy * 84.0f);

    lcd_clear(lcd, COLOR_SCREEN_BG);
    lcd_fill_circle(lcd, cx, cy, FACE_RADIUS, COLOR_FACE_BG);

    draw_ring(lcd, cx, cy, FACE_RADIUS + 4, 2, COLOR_RING_OUTER);
    draw_ring(lcd, cx, cy, FACE_RADIUS + 1, 1, COLOR_RING_MID);
    draw_ring(lcd, cx, cy, FACE_RADIUS - 7, 1, COLOR_RING_INNER);
    draw_ring(lcd, cx, cy, FACE_RADIUS - 36, 1, COLOR_RING_OUTER);

    draw_ticks(lcd, cx, cy);

    draw_hand_with_shadow(lcd, cx, cy, hour_angle, 50, 10, 3, COLOR_HAND_SHADOW, COLOR_HAND_HOUR);
    draw_hand_with_shadow(lcd, cx, cy, minute_angle, 72, 14, 2, COLOR_HAND_SHADOW, COLOR_HAND_MINUTE);
    draw_hand_with_shadow(lcd, cx, cy, second_angle, 84, 22, 1, COLOR_HAND_SHADOW, COLOR_HAND_SECOND);

    lcd_fill_circle(lcd, second_counter_x, second_counter_y, 4, COLOR_HAND_SECOND);
    lcd_fill_circle(lcd, second_tip_x, second_tip_y, 2, COLOR_HAND_SECOND);
    lcd_fill_circle(lcd, cx, cy, 8, COLOR_CENTER_OUTER);
    lcd_fill_circle(lcd, cx, cy, 4, COLOR_CENTER_INNER);
}

static esp_err_t init_app(app_state_t *app)
{
    memset(app, 0, sizeof(*app));
    init_clock_time();

    app->lcd = lcd_init();
    if (app->lcd == NULL) {
        ESP_LOGE(TAG, "lcd init failed");
        return ESP_FAIL;
    }

    return ESP_OK;
}

void app_main(void)
{
    ESP_LOGI(TAG, "start minimal analog watch face");

    if (init_app(&s_app) != ESP_OK) {
        ESP_LOGE(TAG, "init failed, restarting");
        vTaskDelay(pdMS_TO_TICKS(500));
        esp_restart();
    }

    while (true) {
        const clock_snapshot_t clock = sample_clock();
        draw_watch_face(s_app.lcd, &clock);
        lcd_swap_buffers(s_app.lcd);
        vTaskDelay(pdMS_TO_TICKS(FRAME_DELAY_MS));
    }
}
