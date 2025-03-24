#include <stdio.h>
#include "pico/stdlib.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"
#include "hardware/gpio.h"
#include "hardware/timer.h"
#include "pico/time.h"
#include "ssd1306.h"
#include "gfx.h"

#define TRIGGER_PIN 17
#define ECHO_PIN 16
#define DIST_MAX_CM 100.0f
#define OLED_WIDTH 128
#define BAR_Y 20
#define BAR_HEIGHT 5

QueueHandle_t xQueueTime;
QueueHandle_t xQueueDistance;
SemaphoreHandle_t xSemaphoreTrigger;

ssd1306_t disp;

void draw_distance_bar_gfx(ssd1306_t *disp, float distancia) {
    if (distancia > DIST_MAX_CM){ 
        distancia = DIST_MAX_CM;
    }
    if (distancia < 0) {
        distancia = 0;
    }
    int bar_width = (int)((distancia / DIST_MAX_CM) * OLED_WIDTH);
    for (int y = BAR_Y; y < BAR_Y + BAR_HEIGHT; y++) {
        gfx_draw_line(disp, 0, y, bar_width, y);
    }
}

void pin_callback(uint gpio, uint32_t events) {
    static absolute_time_t t_inicial, t_final;
    if (events & GPIO_IRQ_EDGE_RISE) {
        t_inicial = get_absolute_time();
    } else if (events & GPIO_IRQ_EDGE_FALL) {
        t_final = get_absolute_time();
        uint64_t tempo = absolute_time_diff_us(t_inicial, t_final);
        xQueueSendFromISR(xQueueTime, &tempo, NULL);
    }
}

void trigger_task(void *params) {
    while (1) {
        gpio_put(TRIGGER_PIN, 1);
        vTaskDelay(pdMS_TO_TICKS(1));
        gpio_put(TRIGGER_PIN, 0);
        xSemaphoreGive(xSemaphoreTrigger);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void echo_task(void *params) {
    uint64_t tempo;
    while (1) {
        if (xQueueReceive(xQueueTime, &tempo, portMAX_DELAY)) {
            float distancia = (tempo / 2.0f) / 29.1f;
            xQueueSend(xQueueDistance, &distancia, portMAX_DELAY);
        }
    }
}

void oled_task(void *params) {
    float dist;
    while (1) {
        if (xSemaphoreTake(xSemaphoreTrigger, portMAX_DELAY)) {
            gfx_clear_buffer(&disp);
            if (xQueueReceive(xQueueDistance, &dist, pdMS_TO_TICKS(100))) {
                if (dist>400){
                    gfx_draw_string(&disp, 0, 0, 1, "Falha no sensor!");
                } else{
                    gfx_draw_string(&disp, 0, 0, 1, "Distancia:");
                    char dist_str[20];
                    snprintf(dist_str, sizeof(dist_str), "%.2f cm", dist);
                    gfx_draw_string(&disp, 0, 10, 1, dist_str);
                    draw_distance_bar_gfx(&disp, dist);
                }
            } else {
                gfx_draw_string(&disp, 0, 0, 1, "Falha no sensor!");
            }
            gfx_show(&disp);
        }
    }
}

int main() {
    stdio_init_all();

    gpio_init(TRIGGER_PIN);
    gpio_set_dir(TRIGGER_PIN, GPIO_OUT);
    gpio_put(TRIGGER_PIN, 0);

    gpio_init(ECHO_PIN);
    gpio_set_dir(ECHO_PIN, GPIO_IN);
    gpio_set_irq_enabled_with_callback(ECHO_PIN, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true, &pin_callback);

    xQueueTime = xQueueCreate(10, sizeof(uint64_t));
    xQueueDistance = xQueueCreate(10, sizeof(float));
    xSemaphoreTrigger = xSemaphoreCreateBinary();

    ssd1306_init();
    gfx_init(&disp, 128, 32);

    xTaskCreate(trigger_task, "Trigger", 256, NULL, 2, NULL);
    xTaskCreate(echo_task, "Echo", 256, NULL, 2, NULL);
    xTaskCreate(oled_task, "OLED", 512, NULL, 1, NULL);

    vTaskStartScheduler();

    while (1) {}
    return 0;
}
