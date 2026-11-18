-- SPDX-FileCopyrightText: 2023 jacqueline <me@jacqueline.id.au>
--
-- SPDX-License-Identifier: GPL-3.0-only

local lvgl = require("lvgl")
local widgets = require("widgets")
local backstack = require("backstack")
local queue = require("queue")
local playing = require("playing")
local playback = require("playback")
local theme = require("theme")
local screen = require("screen")
local filesystem = require("filesystem")
local playlist_iterator = require("playlist_iterator")
local img = require("images")


return screen:new {
  create_ui = function(self)
    self.root = lvgl.Object(nil, {
      flex = {
        flex_direction = "column",
        flex_wrap = "wrap",
        justify_content = "flex-start",
        align_items = "flex-start",
        align_content = "flex-start"
      },
      w = lvgl.HOR_RES(),
      h = lvgl.VER_RES()
    })
    self.root:center()

    -- One title bar, showing the most specific name we have: the folder,
    -- album, artist etc. being browsed, or else the name of the list.
    self.status_bar = widgets.StatusBar(self, {
      back_cb = backstack.pop,
      title = self.breadcrumb and widgets.LastPathPart(self.breadcrumb) or self.title
    })

    local get_icon_func = function(item)
      if item:is_directory() then
        return img.file_directory
      elseif playlist_iterator:is_playlist(item) then
        return img.file_playlist
      elseif playback.is_playable(item:filepath()) then
        return img.file_music
      else
        return img.unknown
      end
    end

    widgets.InfiniteList(self.root, self.iterator, {
      focus_first_item = true,
      get_icon = get_icon_func,
      callback = function(item)
        return function()
          local is_dir = item:is_directory()
          if is_dir then
            backstack.push(require("file_browser"):new {
              title = self.title,
              iterator = filesystem.iterator(item:filepath()),
              breadcrumb = item:filepath()
            })
          elseif
              playlist_iterator:is_playlist(item) then
            queue.open_playlist(item:filepath())
            playback.playing:set(true)
            playing:push_if_not_shown()
          elseif playback.is_playable(item:filepath()) then
            queue.clear()
            queue.add(item:filepath())
            playback.playing:set(true)
            playing:push_if_not_shown()
          end
        end
      end
    })
  end
}
