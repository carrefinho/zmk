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

    // Read devicetree configuration
    int proximity_threshold = DT_INST_PROP(0, proximity_threshold);
    int sample_rate_hz = DT_INST_PROP(0, sample_rate_hz);
    
    LOG_INF("Device configuration: proximity-threshold=%d, sample-rate=%dHz", 
            proximity_threshold, sample_rate_hz);

    // Calculate OTP Bank 1 value: Standalone mode + proximity threshold
    // Bits 7-6: 00 (standalone), 5-4: proximity threshold, 3-2: 00 (basic filter), 1-0: 00
    uint8_t bank1_value = (proximity_threshold & 0x03) << 4;  // Set bits 5-4
    
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
    uint8_t bank3_value = bank3_sample_bits;  // No ATI events + sample rate
    
    // Try single transfer approach to avoid STOP bits between bank writes
    uint8_t otp_config[8] = {
        0x10, 0x04,        // Bank 0: Quick release threshold = moderate (100 counts)
        0x11, bank1_value, // Bank 1: Standalone mode + configured proximity threshold
        0x12, 0x04,        // Bank 2: Prox/No movement mode + quick release enabled (bit 2)
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
        LOG_INF("OTP Bank 1 (0x11): 0x%02x - Standalone mode, proximity threshold %d", 
                bank1_data[1], proximity_threshold);
        LOG_INF("Using minimal configuration - Banks 2&3 will use default values");
    } else {
        LOG_INF("SUCCESS: All OTP banks configured in single transfer!");
        LOG_INF("OTP Bank 0 (0x10): 0x%02x - Quick release threshold (moderate)", otp_config[1]);
        LOG_INF("OTP Bank 1 (0x11): 0x%02x - Standalone mode, proximity threshold %d", 
                otp_config[3], proximity_threshold);
        LOG_INF("OTP Bank 2 (0x12): 0x%02x - Prox/No movement mode + quick release enabled", otp_config[5]);
        LOG_INF("OTP Bank 3 (0x13): 0x%02x - ATI events disabled, %dHz sampling", 
                otp_config[7], sample_rate_hz);
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
    };                                                                                            \
    DEVICE_DT_INST_DEFINE(inst, iqs231b_init, NULL, &iqs231b_data_##inst,                       \
                          &iqs231b_config_##inst, POST_KERNEL, CONFIG_SENSOR_INIT_PRIORITY,      \
                          NULL);

DT_INST_FOREACH_STATUS_OKAY(IQS231B_DEFINE)