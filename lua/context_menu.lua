
-- SPDX-FileCopyrightText: 2025 ailurux <ailuruxx@gmail.com>
--
-- SPDX-License-Identifier: GPL-3.0-only

local lvgl = require("lvgl")
local widgets = require("widgets")
local backstack = require("backstack")
local font = require("font")
local queue = require("queue")
local playback = require("playback")
local screen = require("screen")
local theme = require("theme")
local styles = require("styles")

return screen:new {
  create_ui = function(self)
    self.root = lvgl.Object(nil, {
      flex = {
        flex_direction = "column",
        flex_wrap = "wrap",
        justify_content = "center",
        align_items = "center",
        align_content = "center",
      },
      w = lvgl.HOR_RES(),
      h = lvgl.VER_RES(),
    })
    self.root:center()

    self.status_bar = widgets.StatusBar(self, {
      back_cb = backstack.pop,
      transparent_bg = true,
    })


    local menu_items = lvgl.List(self.root, {
      w = lvgl.PCT(100),
      h = lvgl.PCT(100),
      flex_grow = 1,
    })

    local play = function(shuffle)
      queue.clear()
      queue.random:set(shuffle)
      queue.add(self.item:contents())
      playback.playing:set(true)
      backstack.pop()
      require("playing"):push_if_not_shown()
    end

    local play_btn = menu_items:add_btn(nil, "Play")
    play_btn:onClicked(function() play(false) end)
    play_btn:add_style(styles.list_item)

    local shuffle_btn = menu_items:add_btn(nil, "Shuffle")
    shuffle_btn:onClicked(function() play(true) end)
    shuffle_btn:add_style(styles.list_item)

    local queue_btn = menu_items:add_btn(nil, "Add to Queue")
    queue_btn:onClicked(function()
      contents = self.item:contents()
      widgets.PopUp("Added to Queue")
      queue.add(contents)
      backstack.pop()
    end)
    queue_btn:add_style(styles.list_item)
  end
}
