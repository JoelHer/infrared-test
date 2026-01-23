#include <unistd.h>
#include "esp_log.h"
#include "esp_err.h"
#include "vigilant.h"
#include "status_led.h"
#include "pwm_led.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "app_main";

void app_main(void)
{
    VigilantConfig VgConfig = {
        .unique_component_name = "Vigliant ESP Test",
        .network_mode = NW_MODE_AP
    };
    ESP_ERROR_CHECK(vigilant_init(VgConfig));

    pwm_led_config_t pwm_cfg = {
        .gpio_num = 2,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .timer = LEDC_TIMER_0,
        .channel = LEDC_CHANNEL_0,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .default_frequency_hz = 0,
        .default_duty_percent = 0,
    };
    esp_err_t pwm_err = pwm_led_init(&pwm_cfg);
    if (pwm_err == ESP_ERR_NOT_SUPPORTED) {
        ESP_LOGW(TAG, "PWM LED disabled via config");
    } else {
        ESP_ERROR_CHECK(pwm_err);
    }

    while (1) {
        ESP_ERROR_CHECK(status_led_set_rgb(100, 100, 100));
        vTaskDelay(pdMS_TO_TICKS(300));

        ESP_ERROR_CHECK(status_led_off());
        vTaskDelay(pdMS_TO_TICKS(300));
        
    }
}
