#include "pwm_led.h"
#include "sdkconfig.h"

#include <stdbool.h>
#include <inttypes.h>

#include "esp_log.h"
#include "esp_check.h"
#include "esp_err.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "pwm_led";

#define PWM_MIN_FREQ_HZ        1U
#define PWM_MAX_FREQ_HZ        20000U
#define PWM_MAX_DUTY_PERCENT   100U
#define PWM_FALLBACK_MAX_FREQ  100U

static struct {
    bool initialized;
    bool ledc_available;
    pwm_led_config_t cfg;
    pwm_led_state_t state;
    uint32_t max_duty_raw;
    TaskHandle_t fallback_task;
    bool fallback_running;
    TickType_t period_ticks;
    TickType_t on_ticks;
} s_pwm = {0};

static uint32_t clamp_u32(uint32_t value, uint32_t min, uint32_t max)
{
    if (value < min) return min;
    if (value > max) return max;
    return value;
}

static void fallback_task(void *arg)
{
    (void)arg;
    while (s_pwm.fallback_running) {
        if (s_pwm.state.duty_percent == 0) {
            gpio_set_level(s_pwm.cfg.gpio_num, 0);
            vTaskDelay(s_pwm.period_ticks);
            continue;
        }
        if (s_pwm.state.duty_percent >= 100) {
            gpio_set_level(s_pwm.cfg.gpio_num, 1);
            vTaskDelay(s_pwm.period_ticks);
            continue;
        }

        gpio_set_level(s_pwm.cfg.gpio_num, 1);
        vTaskDelay(s_pwm.on_ticks);
        gpio_set_level(s_pwm.cfg.gpio_num, 0);
        TickType_t off_ticks = s_pwm.period_ticks > s_pwm.on_ticks ? s_pwm.period_ticks - s_pwm.on_ticks : 1;
        vTaskDelay(off_ticks);
    }

    s_pwm.fallback_task = NULL;
    vTaskDelete(NULL);
}

static void fallback_start(uint32_t freq_hz, uint32_t duty_percent)
{
    if (!s_pwm.fallback_running) {
        gpio_reset_pin(s_pwm.cfg.gpio_num);
        gpio_set_direction(s_pwm.cfg.gpio_num, GPIO_MODE_OUTPUT);
    }

    uint32_t freq = freq_hz;
    if (freq > PWM_FALLBACK_MAX_FREQ) {
        ESP_LOGW(TAG, "Requested %" PRIu32 " Hz too high for fallback, clamping to %u Hz", freq, PWM_FALLBACK_MAX_FREQ);
        freq = PWM_FALLBACK_MAX_FREQ;
    }

    uint32_t period_ms = (1000U + freq / 2U) / freq;
    if (period_ms == 0) period_ms = 1;

    s_pwm.state.frequency_hz = freq;
    s_pwm.state.duty_percent = duty_percent;
    s_pwm.period_ticks = pdMS_TO_TICKS(period_ms);
    if (s_pwm.period_ticks == 0) s_pwm.period_ticks = 1;
    s_pwm.on_ticks = (s_pwm.period_ticks * duty_percent) / 100U;
    if (s_pwm.on_ticks == 0 && duty_percent > 0) s_pwm.on_ticks = 1;

    s_pwm.fallback_running = true;
    if (!s_pwm.fallback_task) {
        BaseType_t ok = xTaskCreate(fallback_task, "pwm_fallback", 2048, NULL, 5, &s_pwm.fallback_task);
        if (ok != pdPASS) {
            ESP_LOGE(TAG, "Failed to start fallback task");
            s_pwm.fallback_running = false;
        } else {
            ESP_LOGW(TAG, "Using software fallback blinking at %" PRIu32 " Hz, %" PRIu32 "%%", freq, duty_percent);
        }
    }
}

static void fallback_stop(void)
{
    if (s_pwm.fallback_task) {
        s_pwm.fallback_running = false;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

esp_err_t pwm_led_init(const pwm_led_config_t *cfg)
{
    if (!cfg) return ESP_ERR_INVALID_ARG;
    if (s_pwm.initialized) return ESP_OK;

    pwm_led_config_t config = *cfg;
    config.default_frequency_hz = clamp_u32(
        config.default_frequency_hz ? config.default_frequency_hz : CONFIG_VE_PWM_DEFAULT_FREQ,
        PWM_MIN_FREQ_HZ,
        PWM_MAX_FREQ_HZ);
    config.default_duty_percent = clamp_u32(config.default_duty_percent,
                                            0, PWM_MAX_DUTY_PERCENT);

    s_pwm.cfg = config;
    s_pwm.state.frequency_hz = config.default_frequency_hz;
    s_pwm.state.duty_percent = config.default_duty_percent;
    s_pwm.max_duty_raw = (1UL << config.duty_resolution) - 1;
    s_pwm.initialized = true;
    s_pwm.ledc_available = CONFIG_VE_ENABLE_PWM_LED;

    if (s_pwm.ledc_available) {
        ledc_timer_config_t timer_cfg = {
            .speed_mode = config.speed_mode,
            .duty_resolution = config.duty_resolution,
            .timer_num = config.timer,
            .freq_hz = config.default_frequency_hz,
            .clk_cfg = LEDC_AUTO_CLK,
        };
        esp_err_t err = ledc_timer_config(&timer_cfg);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "LEDC timer config failed (%s), switching to fallback", esp_err_to_name(err));
            s_pwm.ledc_available = false;
        }
    }

    if (s_pwm.ledc_available) {
        ledc_channel_config_t channel_cfg = {
            .gpio_num = config.gpio_num,
            .speed_mode = config.speed_mode,
            .channel = config.channel,
            .timer_sel = config.timer,
            .duty = 0,
            .hpoint = 0,
            .intr_type = LEDC_INTR_DISABLE,
        };
        esp_err_t err = ledc_channel_config(&channel_cfg);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "LEDC channel config failed (%s), switching to fallback", esp_err_to_name(err));
            s_pwm.ledc_available = false;
        }
    }

    return pwm_led_set(config.default_frequency_hz, config.default_duty_percent);
}

esp_err_t pwm_led_set(uint32_t frequency_hz, uint32_t duty_percent)
{
    if (!s_pwm.initialized) return ESP_ERR_INVALID_STATE;

    uint32_t freq = clamp_u32(frequency_hz, PWM_MIN_FREQ_HZ, PWM_MAX_FREQ_HZ);
    uint32_t duty = clamp_u32(duty_percent, 0, PWM_MAX_DUTY_PERCENT);

    if (s_pwm.ledc_available) {
        fallback_stop();

        esp_err_t err = ledc_set_freq(s_pwm.cfg.speed_mode, s_pwm.cfg.timer, freq);
        if (err == ESP_OK) {
            uint32_t duty_raw = (s_pwm.max_duty_raw * duty) / 100U;
            err = ledc_set_duty(s_pwm.cfg.speed_mode, s_pwm.cfg.channel, duty_raw);
            if (err == ESP_OK) {
                err = ledc_update_duty(s_pwm.cfg.speed_mode, s_pwm.cfg.channel);
            }
            if (err == ESP_OK) {
                s_pwm.state.frequency_hz = freq;
                s_pwm.state.duty_percent = duty;
                ESP_LOGI(TAG, "PWM updated -> freq: %" PRIu32 " Hz, duty: %" PRIu32 "%% (raw %" PRIu32 ")", freq, duty, (s_pwm.max_duty_raw * duty) / 100U);
                return ESP_OK;
            }
        }

        ESP_LOGW(TAG, "LEDC update failed (%s), switching to fallback", esp_err_to_name(err));
        s_pwm.ledc_available = false;
    }

    fallback_start(freq, duty);
    return ESP_OK;
}

pwm_led_state_t pwm_led_get_state(void)
{
    return s_pwm.state;
}
