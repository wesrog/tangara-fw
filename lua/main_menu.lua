-- SPDX-FileCopyrightText: 2023 jacqueline <me@jacqueline.id.au>
--
-- SPDX-License-Identifier: GPL-3.0-only

local lvgl = require("lvgl")
local widgets = require("widgets")
local database = require("database")
local sd_card = require("sd_card")
local backstack = require("backstack")
local browser = require("browser")
local playing = require("playing")
local styles = require("styles")
local filesystem = require("filesystem")
local theme = require("theme")
local img = require("images")
local playback = require("playback")
local playing_screen_settings = require("playing_screen_settings")

-- Line height of fusion_12 (ascent 14 + descent 3), used to keep the
-- now-playing title to a single line in "Ellipsize" mode.
local TITLE_LINE_HEIGHT = 17

return widgets.MenuScreen:new {
  create_ui = function(self)
    widgets.MenuScreen.create_ui(self)

    -- At the top, a card showing details about the current track. Hidden if
    -- there is no track currently playing.
    -- || Cool Song 0:01

    local now_playing = lvgl.Button(self.root, {
      flex = {
        flex_direction = "row",
        flex_wrap = "nowrap",
        justify_content = "flex-start",
        align_items = "center",
        align_content = "flex-start",
      },
      w = lvgl.PCT(100),
      h = lvgl.SIZE_CONTENT,
      margin_all = 2,
      pad_bottom = 2,
      pad_column = 4,
      border_width = 1,
    })
    theme.set_subject(now_playing, "now_playing");

    local play_pause = now_playing:Image { src = img.play_circ }
    -- The title is always a single line, so a long name can't push the list
    -- down. It follows Settings > Playing Screen > Long Text Mode, except that
    -- "Default" (wrap) becomes a circular marquee here.
    local title = now_playing:Label {
      flex_grow = 1,
      h = lvgl.SIZE_CONTENT,
      text = " ",
      long_mode = lvgl.LABEL.LONG_SCROLL_CIRCULAR,
    }
    local function set_title_mode(mode)
      if mode == lvgl.LABEL.LONG_DOT then
        title:set { long_mode = mode, h = TITLE_LINE_HEIGHT }
      elseif mode == lvgl.LABEL.LONG_SCROLL or mode == lvgl.LABEL.LONG_CLIP then
        title:set { long_mode = mode, h = lvgl.SIZE_CONTENT }
      else
        title:set {
          long_mode = lvgl.LABEL.LONG_SCROLL_CIRCULAR,
          h = lvgl.SIZE_CONTENT,
        }
      end
    end
    local time_remaining = now_playing:Label {
      text = " ",
      text_font = font.fusion_10,
    }

    now_playing:onClicked(function() backstack.push(playing:new()) end)

    local track_duration = nil

    self.bindings = self.bindings + {
      playing_screen_settings.long_text_scheme:bind(set_title_mode),
      playback.playing:bind(function(playing)
        if playing then
          play_pause:set_src(img.play_circ)
        else
          play_pause:set_src(img.pause_circ)
        end
      end),
      playback.track:bind(function(track)
        if not track then
          now_playing:add_flag(lvgl.FLAG.HIDDEN)
          return
        else
          now_playing:clear_flag(lvgl.FLAG.HIDDEN)
        end
        title:set { text = track.title }
        if track.duration then
          track_duration = track.duration
        end
      end),
      playback.position:bind(function(pos)
        if not pos then return end
        if not track_duration then return end
        local remaining = track_duration - pos
        if remaining < 0 then remaining = 0 end
        time_remaining:set {
          text = string.format("%d:%02d", remaining // 60, remaining % 60)
        }
      end),
    }

    -- Next, a list showing the user's prefer's music source. This defaults to
    -- a list of all available database indexes, but could also be the contents
    -- of the SD card root.
    local no_indexes_container = self.root:Object {
      w = lvgl.PCT(100),
      flex_grow = 1,
    }
    local no_indexes_label = no_indexes_container:Label {
      w = lvgl.PCT(100),
      h = lvgl.SIZE_CONTENT,
      text_align = 2,
      long_mode = 0,
      margin_all = 4,
      text = "",
    }
    no_indexes_label:center()

    local indexes_list = lvgl.List(self.root, {
      w = lvgl.PCT(100),
      h = lvgl.PCT(100),
      flex_grow = 1,
    })
    local indexes = {}

    for _, idx in ipairs(database.indexes()) do
      local btn = indexes_list:add_btn(nil, tostring(idx))
      btn:onClicked(function()
        backstack.push(browser:new {
          title = tostring(idx),
          iterator = idx:iter(),
          mediatype = idx:type(),
        })
      end)
      btn:add_style(styles.list_item)
      table.insert(indexes, {
        object = btn,
        index = idx,
      })
    end

    local playlist_btn = indexes_list:add_btn(nil, "Playlists")
    playlist_btn:onClicked(function()
      backstack.push(require("playlist_browser"):new {
        title = "Playlists",
        iterator = filesystem.iterator("/Playlists")
      })
    end)
    playlist_btn:add_style(styles.list_item)

    -- Files and Settings sit at the end of the menu, iPod style. They stay
    -- visible when there's no library, so Settings is always reachable.
    local files_btn = indexes_list:add_btn(nil, "Files")
    files_btn:onClicked(function()
      backstack.push(require("file_browser"):new {
        title = "Files",
        iterator = filesystem.iterator(""),
      })
    end)
    files_btn:add_style(styles.list_item)

    local settings_btn = indexes_list:add_btn(nil, "Settings")
    settings_btn:onClicked(function()
      backstack.push(require("settings").Root:new())
    end)
    settings_btn:add_style(styles.list_item)

    local function show_no_indexes(msg)
      for _, idx in ipairs(indexes) do
        idx.object:add_flag(lvgl.FLAG.HIDDEN)
      end
      playlist_btn:add_flag(lvgl.FLAG.HIDDEN)
      no_indexes_container:clear_flag(lvgl.FLAG.HIDDEN)
      no_indexes_label:set { text = msg }
      settings_btn:focus()
    end

    local function hide_no_indexes()
      no_indexes_container:add_flag(lvgl.FLAG.HIDDEN)

      if indexes[1] then
        indexes[1].object:focus()
      end
    end

    local function hide_playlist_listing()
      playlist_btn:add_flag(lvgl.FLAG.HIDDEN)
    end

    local function show_playlist_listing()
      playlist_btn:clear_flag(lvgl.FLAG.HIDDEN)
    end

    local function update_visible_indexes()
      local has_valid_index = false
      for _, idx in ipairs(indexes) do
        local it = idx.index:iter():next()
        if it then
          has_valid_index = true
          idx.object:clear_flag(lvgl.FLAG.HIDDEN)
        else
          idx.object:add_flag(lvgl.FLAG.HIDDEN)
        end
      end
      if has_valid_index then
        hide_no_indexes()

        -- If we have valid indexes, then also check for playlists
        if filesystem.iterator("/Playlists/"):next() == nil then
          hide_playlist_listing()
        else
          show_playlist_listing()
        end
      else
        if require("database").updating:get() then
          show_no_indexes("The database is updating for the first time. Please wait.")
        else
          show_no_indexes("No compatible media was found on your SD Card.")
        end
      end
    end

    self.bindings = self.bindings + {
      database.updating:bind(function()
        update_visible_indexes()
      end),
      sd_card.mounted:bind(function(mounted)
        if mounted then
          files_btn:clear_flag(lvgl.FLAG.HIDDEN)
        else
          files_btn:add_flag(lvgl.FLAG.HIDDEN)
        end
        if not mounted then
          show_no_indexes("SD Card is not inserted or could not be opened.")
        end
      end),
    }
  end,
}
