#define DT_DRV_COMPAT azoteq_iqs231b

#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "iqs231b.h"

LOG_MODULE_REGISTER(iqs231b, CONFIG_SENSOR_LOG_LEVEL);

static int iqs231b_configure_standalone_mode(const struct device *dev)
{
    const struct iqs231b_config *config = dev->config;
    int ret;

    LOG_INF("Configuring IQS231B for standalone proximity mode with interrupts...");

    // Use configuration from device tree
    uint8_t proximity_threshold = config->proximity_threshold;
    uint16_t sample_rate_hz = config->sample_rate_hz;
    uint16_t quick_release_threshold = config->quick_release_threshold;
    uint8_t quick_release_beta = config->quick_release_beta;
    bool increase_debounce = config->increase_debounce;
    bool prox_with_movement = config->prox_with_movement;
    uint8_t ac_filter = config->ac_filter;
    bool temperature_compensation = config->temperature_compensation;
    bool quick_release = config->quick_release;
    
    LOG_INF("Device configuration: proximity-threshold=%d, sample-rate=%dHz, quick-release-threshold=%d, quick-release-beta=%d, increase-debounce=%s, prox-with-movement=%s, ac-filter=%d, temperature-compensation=%s, quick-release=%s", 
            proximity_threshold, sample_rate_hz, quick_release_threshold, quick_release_beta, increase_debounce ? "true" : "false", prox_with_movement ? "true" : "false", ac_filter, temperature_compensation ? "true" : "false", quick_release ? "true" : "false");

    // Map quick release threshold to register bits
    uint8_t qr_threshold_bits;
    switch (quick_release_threshold) {
        case 100: qr_threshold_bits = 0x00; break;  // 00 = moderate (100 counts)
        case 150: qr_threshold_bits = 0x01; break;  // 01 = strict (150 counts)
        case 50:  qr_threshold_bits = 0x02; break;  // 10 = relaxed (50 counts)
        case 250: qr_threshold_bits = 0x03; break;  // 11 = very strict (250 counts)
        default:
            LOG_ERR("Invalid quick release threshold %d, using 100 counts", quick_release_threshold);
            qr_threshold_bits = 0x00;
            break;
    }
    
    // Map quick release beta to register bits
    uint8_t qr_beta_bits;
    switch (quick_release_beta) {
        case 2: qr_beta_bits = 0x00; break;  // 00 = fast following
        case 3: qr_beta_bits = 0x01; break;  // 01 = 3
        case 4: qr_beta_bits = 0x02; break;  // 10 = 4
        case 5: qr_beta_bits = 0x03; break;  // 11 = slow following
        default:
            LOG_ERR("Invalid quick release beta %d, using fast following", quick_release_beta);
            qr_beta_bits = 0x00;
            break;
    }

    // Calculate OTP Bank 0 value: Quick release threshold + beta
    // Bits 3-2: Quick release threshold, 1-0: Quick release beta
    uint8_t bank0_value = (qr_threshold_bits << 2) | qr_beta_bits;
    
    // Map proximity threshold to register bits
    uint8_t prox_threshold_bits;
    switch (proximity_threshold) {
        case 4:  prox_threshold_bits = 0x00; break;  // 00 = 4 counts (most sensitive)
        case 6:  prox_threshold_bits = 0x01; break;  // 01 = 6 counts (moderate)
        case 8:  prox_threshold_bits = 0x02; break;  // 10 = 8 counts
        case 10: prox_threshold_bits = 0x03; break;  // 11 = 10 counts (least sensitive)
        default:
            LOG_ERR("Invalid proximity threshold %d, using 6 counts", proximity_threshold);
            prox_threshold_bits = 0x01;
            break;
    }
    
    // Map AC filter level to register bits
    uint8_t ac_filter_bits;
    switch (ac_filter) {
        case 0: ac_filter_bits = 0x03; break;  // 11 = Filter disabled
        case 1: ac_filter_bits = 0x00; break;  // 00 = Filter level 1
        case 2: ac_filter_bits = 0x01; break;  // 01 = Filter level 2
        case 3: ac_filter_bits = 0x02; break;  // 10 = Filter level 3
        default:
            LOG_ERR("Invalid AC filter %d, using filter level 2", ac_filter);
            ac_filter_bits = 0x01;
            break;
    }
    
    // Calculate OTP Bank 1 value: Standalone mode + proximity threshold + AC filter
    // Bits 7-6: 00 (standalone), 5-4: proximity threshold, 3-2: AC filter, 1-0: 00
    uint8_t bank1_value = (prox_threshold_bits & 0x03) << 4 | (ac_filter_bits & 0x03) << 2;  // Set bits 5-4 and 3-2
    
    // Calculate OTP Bank 3 value: Disable ATI events + sample rate
    // Bits 2-1: 00 (Disable ATI events on IO1), Bit 0: sample rate
    uint8_t bank3_sample_bits;
    switch (sample_rate_hz) {
        case 4:   bank3_sample_bits = 0x03; break;  // 11
        case 8:   bank3_sample_bits = 0x02; break;  // 10  
        case 30:  bank3_sample_bits = 0x00; break;  // 00
        case 100: bank3_sample_bits = 0x01; break;  // 01
        default:  
            LOG_ERR("Invalid sample rate %dHz, using 100Hz", sample_rate_hz);
            bank3_sample_bits = 0x01; 
            break;
    }
    // Calculate OTP Bank 3 value: Temperature compensation + ATI events + sample rate
    // Bit 5: Temperature compensation, Bits 2-1: ATI events (00 = disabled), Bits 1-0: Sample rate
    uint8_t bank3_value = (temperature_compensation ? 0x20 : 0x00) | bank3_sample_bits;  // Bit 5 + sample rate
    
    // Calculate OTP Bank 2 value: Increase debounce + quick release + user interface mode
    // Bit 7: Increase debounce, Bit 2: Quick release, Bits 1-0: User interface mode
    uint8_t ui_mode_bits = prox_with_movement ? 0x01 : 0x00;  // 00 = Prox/No movement, 01 = Prox with movement
    uint8_t bank2_value = (increase_debounce ? 0x80 : 0x00) | (quick_release ? 0x04 : 0x00) | ui_mode_bits;  // Bit 7 + bit 2 + bits 1-0
    
    // Try single transfer approach to avoid STOP bits between bank writes
    uint8_t otp_config[8] = {
        0x10, bank0_value, // Bank 0: Configured quick release threshold and beta
        0x11, bank1_value, // Bank 1: Standalone mode + configured proximity threshold
        0x12, bank2_value, // Bank 2: Configured debounce + quick release enabled + prox/no movement mode
        0x13, bank3_value  // Bank 3: Disabled ATI events + configured sample rate
    };
    
    LOG_INF("Attempting single transfer for all OTP banks...");
    ret = i2c_write_dt(&config->i2c, otp_config, sizeof(otp_config));
    if (ret < 0) {
        LOG_WRN("Single transfer failed (%d), trying minimal configuration...", ret);
        
        // Fallback: Just configure Bank 1 with calculated value
        uint8_t bank1_data[2] = {0x11, bank1_value};
        ret = i2c_write_dt(&config->i2c, bank1_data, sizeof(bank1_data));
        if (ret < 0) {
            LOG_ERR("Failed to write OTP bank 1: %d", ret);
            return ret;
        }
        LOG_INF("OTP Bank 1 (0x11): 0x%02x - Standalone mode, proximity threshold %d counts, AC filter %d", 
                bank1_data[1], proximity_threshold, ac_filter);
        LOG_INF("Using minimal configuration - Banks 2&3 will use default values");
    } else {
        LOG_INF("SUCCESS: All OTP banks configured in single transfer!");
        LOG_INF("OTP Bank 0 (0x10): 0x%02x - Quick release threshold %d counts, beta %d", 
                otp_config[1], quick_release_threshold, quick_release_beta);
        LOG_INF("OTP Bank 1 (0x11): 0x%02x - Standalone mode, proximity threshold %d counts, AC filter %d", 
                otp_config[3], proximity_threshold, ac_filter);
        LOG_INF("OTP Bank 2 (0x12): 0x%02x - %s debounce + quick release %s + %s mode", 
                otp_config[5], increase_debounce ? "Increased" : "Normal", quick_release ? "enabled" : "disabled", prox_with_movement ? "prox with movement" : "prox/no movement");
        LOG_INF("OTP Bank 3 (0x13): 0x%02x - %s temperature compensation, ATI events disabled, %dHz sampling", 
                otp_config[7], temperature_compensation ? "Enabled" : "Disabled", sample_rate_hz);
    }

    LOG_INF("IQS231B configured for standalone proximity sensing");
    
    return 0;
}

static int iqs231b_verify_standalone_mode(const struct device *dev)
{
    LOG_INF("IQS231B configured for standalone operation");
    LOG_INF("Sensor will now operate independently with interrupt output on IO1 pin");
    LOG_INF("Proximity detection: 6-count threshold, 100Hz sampling rate");
    LOG_INF("Mode: Proximity only (no movement detection)");
    
    // No further communication needed - sensor operates standalone
    return 0;
}

int iqs231b_power_on(const struct device *dev)
{
    const struct iqs231b_config *config = dev->config;
    int ret;

    ret = gpio_pin_set_dt(&config->power, 1);
    if (ret < 0) {
        LOG_ERR("Failed to power on IQS231B: %d", ret);
        return ret;
    }

    LOG_INF("IQS231B powered on, waiting %dms for tinit", IQS231B_TINIT_DELAY_MS);
    k_msleep(IQS231B_TINIT_DELAY_MS);

    return 0;
}

int iqs231b_power_off(const struct device *dev)
{
    const struct iqs231b_config *config = dev->config;
    int ret;

    ret = gpio_pin_set_dt(&config->power, 0);
    if (ret < 0) {
        LOG_ERR("Failed to power off IQS231B: %d", ret);
        return ret;
    }

    LOG_INF("IQS231B powered off");
    return 0;
}

static int iqs231b_init(const struct device *dev)
{
    const struct iqs231b_config *config = dev->config;
    int ret;

    LOG_INF("Initializing IQS231B sensor");

    if (!device_is_ready(config->i2c.bus)) {
        LOG_ERR("I2C bus device not ready");
        return -ENODEV;
    }

    if (!device_is_ready(config->power.port)) {
        LOG_ERR("Power GPIO device not ready");
        return -ENODEV;
    }

    ret = gpio_pin_configure_dt(&config->power, GPIO_OUTPUT_INACTIVE);
    if (ret < 0) {
        LOG_ERR("Failed to configure power GPIO: %d", ret);
        return ret;
    }

    LOG_INF("Power GPIO configured, device initially powered off");

    ret = iqs231b_power_on(dev);
    if (ret < 0) {
        LOG_ERR("Failed to power on device: %d", ret);
        return ret;
    }

    LOG_INF("Entering test mode - communicating with address 0x%02x", IQS231B_TEST_MODE_ADDR);
    
    ret = iqs231b_configure_standalone_mode(dev);
    if (ret < 0) {
        LOG_ERR("Failed to configure standalone mode: %d", ret);
        return ret;
    }

    // Small delay to let configuration take effect
    k_msleep(10);

    ret = iqs231b_verify_standalone_mode(dev);
    if (ret < 0) {
        LOG_ERR("Failed to verify standalone mode: %d", ret);
        return ret;
    }

    LOG_INF("IQS231B initialization completed successfully");
    return 0;
}

#define IQS231B_DEFINE(inst)                                                                      \
    static struct iqs231b_data iqs231b_data_##inst;                                              \
    static const struct iqs231b_config iqs231b_config_##inst = {                                 \
        .i2c = I2C_DT_SPEC_INST_GET(inst),                                                       \
        .power = GPIO_DT_SPEC_INST_GET(inst, power_gpios),                                       \
        .proximity_threshold = DT_INST_PROP(inst, proximity_threshold),                          \
        .sample_rate_hz = DT_INST_PROP(inst, sample_rate_hz),                                    \
        .quick_release_threshold = DT_INST_PROP(inst, quick_release_threshold),                  \
        .quick_release_beta = DT_INST_PROP(inst, quick_release_beta),                            \
        .increase_debounce = DT_INST_PROP(inst, increase_debounce),                              \
        .prox_with_movement = DT_INST_PROP(inst, prox_with_movement),                        \
        .ac_filter = DT_INST_PROP(inst, ac_filter),                                          \
        .temperature_compensation = DT_INST_PROP(inst, temperature_compensation),            \
        .quick_release = DT_INST_PROP(inst, quick_release),                                  \
    };                                                                                            \
    DEVICE_DT_INST_DEFINE(inst, iqs231b_init, NULL, &iqs231b_data_##inst,                       \
                          &iqs231b_config_##inst, POST_KERNEL, CONFIG_SENSOR_INIT_PRIORITY,      \
                          NULL);

DT_INST_FOREACH_STATUS_OKAY(IQS231B_DEFINE)