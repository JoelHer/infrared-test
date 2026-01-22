#pragma once

#include <stdint.h>
#include "esp_err.h"
#include "driver/ledc.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef CONFIG_VE_PWM_GPIO
#define CONFIG_VE_PWM_GPIO 2
#endif

#ifndef CONFIG_VE_PWM_DEFAULT_FREQ
#define CONFIG_VE_PWM_DEFAULT_FREQ 1000
#endif

#ifndef CONFIG_VE_PWM_DEFAULT_DUTY
#define CONFIG_VE_PWM_DEFAULT_DUTY 50
#endif

typedef struct {
    int gpio_num;
    ledc_mode_t speed_mode;
    ledc_timer_t timer;
    ledc_channel_t channel;
    ledc_timer_bit_t duty_resolution;
    uint32_t default_frequency_hz;
    uint32_t default_duty_percent;
} pwm_led_config_t;

typedef struct {
    uint32_t frequency_hz;
    uint32_t duty_percent;
} pwm_led_state_t;

esp_err_t pwm_led_init(const pwm_led_config_t *cfg);
esp_err_t pwm_led_set(uint32_t frequency_hz, uint32_t duty_percent);
pwm_led_state_t pwm_led_get_state(void);

#ifdef __cplusplus
}
#endif
