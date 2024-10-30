#include <lvgl.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>

#include "widgets/layer_roller.h"
#include "widgets/battery_bar.h"
#include "widgets/caps_word_indicator.h"

#include <fonts.h>
#include <sf_symbols.h>

#include <zmk/keymap.h>

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

static struct zmk_widget_layer_roller layer_roller_widget;
static struct zmk_widget_battery_bar battery_bar_widget;
static struct zmk_widget_caps_word_indicator caps_word_indicator_widget;

lv_obj_t *zmk_display_status_screen() {
    lv_obj_t *screen;
    screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen, 255, LV_PART_MAIN);

    // lv_obj_t *mod_cmd = lv_label_create(screen);
    // lv_label_set_text(mod_cmd, SF_SYMBOL_COMMAND);
    // lv_obj_set_style_text_color(mod_cmd, lv_color_hex(0x20ffff), LV_PART_MAIN);
    // lv_obj_set_style_text_font(mod_cmd, &SF_Compact_Text_Bold_32, LV_PART_MAIN);
    // lv_obj_align(mod_cmd, LV_ALIGN_TOP_LEFT, 10, 50);

    // lv_obj_t *mod_opt = lv_label_create(screen);
    // lv_label_set_text(mod_opt, SF_SYMBOL_OPTION);
    // lv_obj_set_style_text_color(mod_opt, lv_color_hex(0x20ffff), LV_PART_MAIN);
    // lv_obj_set_style_text_font(mod_opt, &SF_Compact_Text_Bold_32, LV_PART_MAIN);
    // lv_obj_align(mod_opt, LV_ALIGN_TOP_LEFT, 10, 90);

    // lv_obj_t *mod_ctr = lv_label_create(screen);
    // lv_label_set_text(mod_ctr, SF_SYMBOL_CONTROL);
    // lv_obj_set_style_text_color(mod_ctr, lv_color_hex(0x20ffff), LV_PART_MAIN);
    // lv_obj_set_style_text_font(mod_ctr, &SF_Compact_Text_Bold_32, LV_PART_MAIN);
    // lv_obj_align(mod_ctr, LV_ALIGN_TOP_LEFT, 10, 130);

    // lv_obj_t *mod_sft = lv_label_create(screen);
    // lv_label_set_text(mod_sft, SF_SYMBOL_SHIFT);
    // lv_obj_set_style_text_color(mod_sft, lv_color_hex(0x20ffff), LV_PART_MAIN);
    // lv_obj_set_style_text_font(mod_sft, &SF_Compact_Text_Bold_32, LV_PART_MAIN);
    // lv_obj_align(mod_sft, LV_ALIGN_TOP_LEFT, 10, 170);

    // zmk_widget_caps_word_indicator_init(&caps_word_indicator_widget, screen);
    // lv_obj_align(zmk_widget_caps_word_indicator_obj(&caps_word_indicator_widget), LV_ALIGN_TOP_LEFT, 10, 10);

    zmk_widget_battery_bar_init(&battery_bar_widget, screen);
    // lv_obj_set_width(zmk_widget_battery_bar_obj(&battery_bar_widget), lv_pct(100));
    lv_obj_set_size(zmk_widget_battery_bar_obj(&battery_bar_widget), lv_pct(100), 48);
    lv_obj_align(zmk_widget_battery_bar_obj(&battery_bar_widget), LV_ALIGN_BOTTOM_MID, 0, 0);

    zmk_widget_layer_roller_init(&layer_roller_widget, screen);
    lv_obj_set_size(zmk_widget_layer_roller_obj(&layer_roller_widget), 256, 160);
    lv_obj_align(zmk_widget_layer_roller_obj(&layer_roller_widget), LV_ALIGN_CENTER, 0, -20);


    return screen;
}

