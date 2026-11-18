// SPDX-FileCopyrightText: 2023 ailurux <ailuruxx@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <cstdio>
#include <map>
#include <optional>
#include <string>
#include <vector>
#include "lvgl.h"

namespace ui {
namespace themes {

enum class Style {
  kMenuItem,
  kMenuSubheadFirst,
  kMenuSubhead,
  kTopBar,
  kPopup,
  kTab,
  kButtonPrimary,
};

class Theme {
 public:
  void Apply(void);
  void Callback(lv_obj_t* obj);
  void ApplyStyle(lv_obj_t* obj, std::string style_key);

  void AddStyle(std::string key, int selector, lv_style_t* style);

  void Reset();

  // The root background, gradient and text colours of the loaded theme,
  // encoded as "bg grad text" hex triplets for NvsStorage. Empty if the
  // theme has no root style.
  auto RootColours() -> std::optional<std::string>;

  static auto instance() -> Theme*;

 private:
  Theme();
  std::map<std::string, std::vector<std::pair<int, lv_style_t*>>> style_map;
  lv_theme_t theme_;
  std::optional<std::string> filename_;
};
}  // namespace themes
}  // namespace ui
