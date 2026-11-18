-- SPDX-FileCopyrightText: 2023 jacqueline <me@jacqueline.id.au>
--
-- SPDX-License-Identifier: GPL-3.0-only

local lvgl = require("lvgl")
local widgets = require("widgets")
local backstack = require("backstack")
local playback = require("playback")
local queue = require("queue")
local screen = require("screen")
local theme = require("theme")
local playing_menu = require("playing_menu")
local playing_screen_settings = require("playing_screen_settings")

local img = require("images")

local format_time = function(time)
  time = math.floor(time)
  local seconds = time % 60
  local minutes = (time // 60) % 60
  local hours = time // (60 * 60)
  if (hours > 0) then
    return string.format("%d:%02d:%02d", hours, minutes, seconds)
  else
    return string.format("%d:%02d", minutes, seconds)
  end
end

local is_now_playing_shown = false

local icon_enabled_class = "icon_enabled"
local icon_disabled_class = "icon_disabled"

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

    local info = self.root:Object {
      flex = {
        flex_direction = "column",
        flex_wrap = "wrap",
        justify_content = "center",
        align_items = "center",
        align_content = "center",
      },
      w = lvgl.PCT(100),
      h = lvgl.SIZE_CONTENT,
      flex_grow = 1,
    }

    local artist = info:Label {
      w = lvgl.PCT(100),
      h = lvgl.SIZE_CONTENT,
      text = "",
      text_font = font.fusion_10,
      text_align = 2,
    }

    local album = info:Label {
      w = lvgl.PCT(100),
      h = lvgl.SIZE_CONTENT,
      text = "",
      text_font = font.fusion_10,
      text_align = 2,
    }

    local title = info:Label {
      w = lvgl.PCT(100),
      h = lvgl.SIZE_CONTENT,
      text = "",
      text_align = 2,
    }

    if playing_screen_settings.long_text_scheme:get() then
      local long_text_mode = playing_screen_settings.long_text_scheme:get()
      artist:set{long_mode = long_text_mode}
      album:set{long_mode = long_text_mode}
      title:set{long_mode=long_text_mode}
    end

    local playlist = self.root:Object {
      flex = {
        flex_direction = "row",
        justify_content = "space_between",
        align_items = "center",
        align_content = "center",
      },
      w = lvgl.PCT(95),
      h = lvgl.SIZE_CONTENT,
    }

    playlist:Object({ w = 3, h = 1 }) -- spacer

    local cur_time = playlist:Label {
      w = lvgl.PCT(30),
      h = lvgl.SIZE_CONTENT,
      align = lvgl.ALIGN.LEFT_MID,
      text = "",
      text_font = font.fusion_10,
    }

    playlist:Object({ flex_grow = 1, h = 1 }) -- spacer

    local playlist_pos_container = playlist:Object{
      flex = {
        flex_direction = "row",
        justify_content = "center",
        align_items = "center",
        align_content = "center",
      },
      w = lvgl.SIZE_CONTENT,
      h = lvgl.SIZE_CONTENT,
    }
    local playlist_pos = playlist_pos_container:Label {
      text = "",
      text_font = font.fusion_10,
    }
    playlist_pos_container:Label {
      text = "/",
      text_font = font.fusion_10,
    }
    local playlist_total = playlist_pos_container:Label {
      text = "",
      text_font = font.fusion_10,
    }

    playlist:Object({ flex_grow = 1, h = 1 }) -- spacer

    local end_time = playlist:Label {
      w = lvgl.PCT(30),
      h = lvgl.SIZE_CONTENT,
      text_align = 3, -- LV_TEXT_ALIGN_RIGHT
      text = format_time(0),
      text_font = font.fusion_10,
    }
    playlist:Object({ w = 3, h = 1 }) -- spacer

    local scrubber = self.root:Slider {
      w = lvgl.PCT(90),
      h = 5,
      range = { min = 0, max = 100 },
      value = 0,
    }
    theme.set_subject(scrubber, "scrubber");
    local scrubber_desc = widgets.Description(scrubber, "Scrubber")

    scrubber:onevent(lvgl.EVENT.RELEASED, function()
      local track = playback.track:get()
      if not track then return end
      if not track.duration then return end
      playback.position:set(scrubber:value() / 100 * track.duration)
    end)
    scrubber:onevent(lvgl.EVENT.VALUE_CHANGED, function()
      if scrubber:is_dragged() then
        local track = playback.track:get()
        if not track then return end
        if not track.duration then return end
        cur_time:set {
          text = format_time(scrubber:value() / 100 * track.duration)
        }
      end
    end)

    self.root:Object({ w = 1, h = 1 }) -- spacer

    local controls = self.root:Object {
      flex = {
        flex_direction = "row",
        justify_content = "center",
        align_items = "center",
        align_content = "center",
      },
      w = lvgl.PCT(100),
      h = lvgl.SIZE_CONTENT,
      pad_column = 5,
      pad_all = 2,
    }

    controls:Object({ flex_grow = 1, h = 1 }) -- spacer

    -- One button for the play mode, cycling through In order, Repeat all,
    -- Repeat one and Shuffle. Shuffle carries on forever.
    local current_mode = function()
      if queue.random:get() then return "shuffle" end
      local r = queue.repeat_mode:get()
      if r == queue.RepeatMode.REPEAT_QUEUE then return "repeat_all" end
      if r == queue.RepeatMode.REPEAT_TRACK then return "repeat_one" end
      return "in_order"
    end
    local next_mode = {
      in_order = "repeat_all",
      repeat_all = "repeat_one",
      repeat_one = "shuffle",
      shuffle = "in_order",
    }
    local set_mode = function(mode)
      if mode == "shuffle" then
        queue.repeat_mode:set(queue.RepeatMode.REPEAT_QUEUE)
        queue.random:set(true)
        return
      end
      queue.random:set(false)
      if mode == "repeat_all" then
        queue.repeat_mode:set(queue.RepeatMode.REPEAT_QUEUE)
      elseif mode == "repeat_one" then
        queue.repeat_mode:set(queue.RepeatMode.REPEAT_TRACK)
      else
        queue.repeat_mode:set(queue.RepeatMode.OFF)
      end
    end
    local mode_icons = {
      in_order = { img.repeat_off, "Play in order" },
      repeat_all = { img.repeat_queue, "Repeat all" },
      repeat_one = { img.repeat_track, "Repeat one" },
      shuffle = { img.shuffle, "Shuffle" },
    }

    local mode_btn = controls:Button {}
    mode_btn:onClicked(function()
      set_mode(next_mode[current_mode()])
    end)
    local mode_img = mode_btn:Image { src = img.repeat_off }
    theme.set_subject(mode_btn, icon_enabled_class)
    local mode_desc = widgets.Description(mode_btn)
    local update_mode = function()
      local icon = mode_icons[current_mode()]
      mode_img:set_src(icon[1])
      mode_desc:set { text = icon[2] }
    end


    local prev_btn = controls:Button {}
    prev_btn:onClicked(function()
      if playback.position:get() > 3 then
        playback.position:set(0)
      else
        queue.previous()
      end
    end)
    local prev_img = prev_btn:Image { src = img.prev }
    theme.set_subject(prev_btn, icon_enabled_class)
    local prev_desc = widgets.Description(prev_btn, "Previous track")

    local play_pause_btn = controls:Button {}
    play_pause_btn:onClicked(function()
      if (not playback.track:get()) then
        -- Restart the last played track
        queue.position:set(queue.position:get())
      end
      playback.playing:set(not playback.playing:get())
    end)
    play_pause_btn:focus()
    local play_pause_img = play_pause_btn:Image { src = img.pause }
    theme.set_subject(play_pause_btn, icon_enabled_class)
    local play_pause_desc = widgets.Description(play_pause_btn, "Play")

    local next_btn = controls:Button {}
    next_btn:onClicked(queue.next)
    local next_img = next_btn:Image { src = img.next }
    theme.set_subject(next_btn, icon_disabled_class)
    local next_desc = widgets.Description(next_btn, "Next track")

    local menu_btn = controls:Button {}
    menu_btn:onClicked(function()
      backstack.push(playing_menu:new())
    end)
    local menu_img = menu_btn:Image { src = img.menu }
    theme.set_subject(menu_btn, icon_enabled_class)
    local menu_desc = widgets.Description(menu_btn, "Track info")

    controls:Object({ flex_grow = 1, h = 1 }) -- spacer


    self.bindings = self.bindings + {
      playback.playing:bind(function(playing)
        if playing then
          play_pause_img:set_src(img.pause)
          play_pause_desc:set { text = "Pause" }
        else
          play_pause_img:set_src(img.play)
          play_pause_desc:set { text = "Play" }
        end
      end),
      playback.position:bind(function(pos)
        if not pos then return end
        if not scrubber:is_dragged() then
          cur_time:set {
            text = format_time(pos)
          }
          local track = playback.track:get()
          if not track then
            scrubber:set{value = 0}
            return
          end
          if not track.duration then return end
          scrubber:set { value = pos / track.duration * 100 }
        end
      end),
      queue.ready:bind(function(ready)
        if not ready then
          if queue.loading:get() then
            title:set { text = "Loading..." }
          else
            title:set{ text = "Not Playing" }
          end
          album:set{text=""}
          artist:set{text=""}
          cur_time:set { text = format_time(0) }
          end_time:set { text = format_time(0) }
        end
      end),
      playback.track:bind(function(track)
        if not track then
          return
        end
        if track.duration then
          end_time:set { text = format_time(track.duration) }
        else
          end_time:set { text = format_time(playback.position:get()) }
        end

        if not (track.title == title:get_text()) then
          title:set { text = track.title }
          if track.album then
            album:set { text = string.sub(track.album, 1, 58) }
            album:clear_flag(lvgl.FLAG.HIDDEN)
          else
            album:add_flag(lvgl.FLAG.HIDDEN)
          end
          artist:set { text = track.artist or "Unknown Artist" }
        end
      end),
      queue.position:bind(function(pos)
        if not pos then return end
        playlist_pos:set { text = tostring(pos) }

        local can_next = pos < queue.size:get() or queue.random:get() or queue.repeat_mode:get() == queue.RepeatMode.REPEAT_QUEUE
        theme.set_subject(
          next_btn, can_next and icon_enabled_class or icon_disabled_class
        )
      end),
      queue.random:bind(function() update_mode() end),
      queue.repeat_mode:bind(function() update_mode() end),
      queue.size:bind(function(num)
        if not num then return end
        playlist_total:set { text = tostring(num) }
      end),
    }
  end,
  on_show = function() is_now_playing_shown = true end,
  on_hide = function() is_now_playing_shown = false end,
  push_if_not_shown = function(self)
    if not is_now_playing_shown then
      backstack.push(self:new())
    end
  end
}
