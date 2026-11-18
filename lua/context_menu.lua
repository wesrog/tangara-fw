
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

    -- Actions for the whole list the item is in, so they're reachable from
    -- anywhere in a long list.
    if self.list then
      local heading = menu_items:add_text(self.list_title or "Whole list")
      heading:set { text_font = font.fusion_10 }

      local play_list = function(shuffle)
        queue.clear()
        queue.random:set(shuffle)
        queue.add(self.list)
        playback.playing:set(true)
        backstack.pop()
        require("playing"):push_if_not_shown()
      end

      local play_all_btn = menu_items:add_btn(nil, "Play All")
      play_all_btn:onClicked(function() play_list(false) end)
      play_all_btn:add_style(styles.list_item)

      local shuffle_all_btn = menu_items:add_btn(nil, "Shuffle All")
      shuffle_all_btn:onClicked(function() play_list(true) end)
      shuffle_all_btn:add_style(styles.list_item)

      local queue_all_btn = menu_items:add_btn(nil, "Queue All")
      queue_all_btn:onClicked(function()
        widgets.PopUp("Added to Queue")
        queue.add(self.list)
        backstack.pop()
      end)
      queue_all_btn:add_style(styles.list_item)
    end
  end
}
