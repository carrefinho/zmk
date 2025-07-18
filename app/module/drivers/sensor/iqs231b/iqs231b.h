#ifndef ZEPHYR_DRIVERS_SENSOR_IQS231B_H_
#define ZEPHYR_DRIVERS_SENSOR_IQS231B_H_

#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/gpio.h>

#define IQS231B_TEST_MODE_ADDR 0x45
#define IQS231B_REG_SOFTWARE_VERSION 0x01

#define IQS231A_VERSION 0x06
#define IQS231B_VERSION 0x07

#define IQS231B_TINIT_DELAY_MS 15
#define IQS231B_TEST_MODE_WINDOW_MS 340

struct iqs231b_config {
    struct i2c_dt_spec i2c;
    struct gpio_dt_spec power;
};

struct iqs231b_data {
    uint8_t software_version;
};

int iqs231b_power_on(const struct device *dev);
int iqs231b_power_off(const struct device *dev);

#endif /* ZEPHYR_DRIVERS_SENSOR_IQS231B_H_ */