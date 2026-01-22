#include "pwm_led.h"
#include "sdkconfig.h"

#include <stdbool.h>
#include <inttypes.h>

#include "esp_log.h"
#include "esp_check.h"
#include "esp_err.h"

static const char *TAG = "pwm_led";

#define PWM_MIN_FREQ_HZ        10U
#define PWM_MAX_FREQ_HZ        20000U
#define PWM_MAX_DUTY_PERCENT   100U

static struct {
    bool initialized;
    pwm_led_config_t cfg;
    pwm_led_state_t state;
    uint32_t max_duty_raw;
} s_pwm = {0};

static uint32_t clamp_u32(uint32_t value, uint32_t min, uint32_t max)
{
    if (value < min) return min;
    if (value > max) return max;
    return value;
}

esp_err_t pwm_led_init(const pwm_led_config_t *cfg)
{
#if !CONFIG_VE_ENABLE_PWM_LED
    ESP_LOGW(TAG, "PWM LED disabled via configuration");
    return ESP_ERR_NOT_SUPPORTED;
#endif

    if (!cfg) return ESP_ERR_INVALID_ARG;
    if (s_pwm.initialized) return ESP_OK;

    pwm_led_config_t config = *cfg;
    config.default_frequency_hz = clamp_u32(
        config.default_frequency_hz ? config.default_frequency_hz : CONFIG_VE_PWM_DEFAULT_FREQ,
        PWM_MIN_FREQ_HZ,
        PWM_MAX_FREQ_HZ);
    config.default_duty_percent = clamp_u32(config.default_duty_percent,
                                            0, PWM_MAX_DUTY_PERCENT);

    ledc_timer_config_t timer_cfg = {
        .speed_mode = config.speed_mode,
        .duty_resolution = config.duty_resolution,
        .timer_num = config.timer,
        .freq_hz = config.default_frequency_hz,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_RETURN_ON_ERROR(ledc_timer_config(&timer_cfg), TAG, "timer config failed");

    ledc_channel_config_t channel_cfg = {
        .gpio_num = config.gpio_num,
        .speed_mode = config.speed_mode,
        .channel = config.channel,
        .timer_sel = config.timer,
        .duty = 0,
        .hpoint = 0,
        .intr_type = LEDC_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(ledc_channel_config(&channel_cfg), TAG, "channel config failed");

    s_pwm.cfg = config;
    s_pwm.state.frequency_hz = config.default_frequency_hz;
    s_pwm.state.duty_percent = config.default_duty_percent;
    s_pwm.max_duty_raw = (1UL << config.duty_resolution) - 1;
    s_pwm.initialized = true;

    return pwm_led_set(config.default_frequency_hz, config.default_duty_percent);
}

esp_err_t pwm_led_set(uint32_t frequency_hz, uint32_t duty_percent)
{
#if !CONFIG_VE_ENABLE_PWM_LED
    (void)frequency_hz;
    (void)duty_percent;
    return ESP_ERR_NOT_SUPPORTED;
#endif

    if (!s_pwm.initialized) return ESP_ERR_INVALID_STATE;

    uint32_t freq = clamp_u32(frequency_hz, PWM_MIN_FREQ_HZ, PWM_MAX_FREQ_HZ);
    uint32_t duty = clamp_u32(duty_percent, 0, PWM_MAX_DUTY_PERCENT);

    esp_err_t err = ledc_set_freq(s_pwm.cfg.speed_mode, s_pwm.cfg.timer, freq);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set PWM freq %u Hz: %s", freq, esp_err_to_name(err));
        return err;
    }

    uint32_t duty_raw = (s_pwm.max_duty_raw * duty) / 100U;
    err = ledc_set_duty(s_pwm.cfg.speed_mode, s_pwm.cfg.channel, duty_raw);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set PWM duty raw=%" PRIu32 ": %s", duty_raw, esp_err_to_name(err));
        return err;
    }

    err = ledc_update_duty(s_pwm.cfg.speed_mode, s_pwm.cfg.channel);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to update PWM duty: %s", esp_err_to_name(err));
        return err;
    }

    s_pwm.state.frequency_hz = freq;
    s_pwm.state.duty_percent = duty;

    ESP_LOGI(TAG, "PWM updated -> freq: %u Hz, duty: %u%% (raw %" PRIu32 ")", freq, duty, duty_raw);
    return ESP_OK;
}

pwm_led_state_t pwm_led_get_state(void)
{
    return s_pwm.state;
}
