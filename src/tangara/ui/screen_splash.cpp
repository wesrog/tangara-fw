/*
 * Copyright 2023 jacqueline <me@jacqueline.id.au>
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "ui/screen_splash.hpp"

#include "core/lv_obj.h"
#include "core/lv_obj_pos.h"
#include "core/lv_obj_style.h"
#include "lvgl.h"
#include "misc/lv_area.h"
#include "misc/lv_color.h"
#include "misc/lv_style.h"

#include <cstdio>

#include "esp_app_desc.h"

LV_IMG_DECLARE(splash_word);
LV_IMG_DECLARE(splash_stripe);

namespace ui {
namespace screens {

namespace {

struct SplashColours {
  lv_color_t bg;
  lv_color_t grad;
  lv_color_t text;
};

// The splash is shown before Lua has loaded the user's theme, so it uses the
// theme colours that were cached in NVS the last time a theme was loaded.
// Falls back to the original white-on-black look.
auto loadColours(drivers::NvsStorage& nvs) -> SplashColours {
  SplashColours colours{
      .bg = lv_color_black(),
      .grad = lv_color_black(),
      .text = lv_color_white(),
  };
  auto saved = nvs.InterfaceSplashColours();
  if (!saved) {
    return colours;
  }
  unsigned int bg, grad, text;
  if (std::sscanf(saved->c_str(), "%6x %6x %6x", &bg, &grad, &text) == 3) {
    colours.bg = lv_color_hex(bg);
    colours.grad = lv_color_hex(grad);
    colours.text = lv_color_hex(text);
  }
  return colours;
}

}  // namespace

Splash::Splash(drivers::NvsStorage& nvs) {
  auto colours = loadColours(nvs);

  // A full-screen backdrop, covering the screen's own (unstyled) children.
  lv_obj_t* backdrop = lv_obj_create(root_);
  lv_obj_remove_style_all(backdrop);
  lv_obj_set_size(backdrop, lv_pct(100), lv_pct(100));
  lv_obj_center(backdrop);
  lv_obj_set_style_bg_opa(backdrop, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(backdrop, colours.bg, 0);
  if (!lv_color_eq(colours.bg, colours.grad)) {
    lv_obj_set_style_bg_grad_color(backdrop, colours.grad, 0);
    lv_obj_set_style_bg_grad_dir(backdrop, LV_GRAD_DIR_VER, 0);
  }

  // The coloured stripe, with the wordmark drawn over it as an alpha mask in
  // the theme's text colour (the 'g' descender overlaps the stripe).
  lv_obj_t* stripe = lv_image_create(root_);
  lv_image_set_src(stripe, &splash_stripe);
  lv_obj_center(stripe);

  lv_obj_t* word = lv_image_create(root_);
  lv_image_set_src(word, &splash_word);
  lv_obj_set_style_image_recolor(word, colours.text, 0);
  lv_obj_set_style_image_recolor_opa(word, LV_OPA_COVER, 0);
  lv_obj_center(word);

  lv_obj_t* version_label = lv_label_create(root_);
  lv_label_set_text(version_label, esp_app_get_description()->version);
  lv_obj_set_style_text_color(version_label, colours.text, 0);
  lv_obj_align(version_label, LV_ALIGN_CENTER, 0, 30);
}

Splash::~Splash() {}

}  // namespace screens
}  // namespace ui
