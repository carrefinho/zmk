#pragma once

#include <lvgl.h>
#include <zephyr/kernel.h>

struct zmk_widget_caps_word_indicator {
    sys_snode_t node;
    lv_obj_t *obj;
};

int zmk_widget_caps_word_indicator_init(struct zmk_widget_caps_word_indicator *widget, lv_obj_t *parent);
lv_obj_t *zmk_widget_caps_word_indicator_obj(struct zmk_widget_caps_word_indicator *widget);