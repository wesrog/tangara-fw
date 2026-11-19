-- SPDX-FileCopyrightText: 2023 jacqueline <me@jacqueline.id.au>
--
-- SPDX-License-Identifier: GPL-3.0-only

local lvgl = require("lvgl")
local widgets = require("widgets")
local backstack = require("backstack")
local queue = require("queue")
local playing = require("playing")
local styles = require("styles")
local playback = require("playback")
local theme = require("theme")
local screen = require("screen")
local database = require("database")
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
      title = self.breadcrumb and self.breadcrumb or self.title
    })

    -- Play, shuffle or queue the whole list from the hold-centre menu on any
    -- row, rather than from buttons that are only reachable at the very top.
    local whole_list = nil
    if (self.mediatype == database.MediaTypes.Music or self.mediatype == database.MediaTypes.Any) then
      whole_list = self.iterator:clone()
    end

    local get_icon_func = nil
    local longform_content = self.mediatype == database.MediaTypes.Audiobook or
    self.mediatype == database.MediaTypes.Podcast
    if longform_content then
      get_icon_func = function(item)
        local contents = item:contents()
        if type(contents) == "userdata" then
          return
        else
          local track = database.track_by_id(contents)
          if not track then return end
          if (track.play_count > 0) then
            return img.listened
        elseif track.saved_position > 0 then
            return img.partially_listened
        else
            return img.unlistened
          end
        end
      end
    end

    -- Plays a track from this list, then carries on through the rest of the
    -- list, like selecting a song in an album. index is relative to base
    -- (see widgets.InfiniteList), and may be negative.
    local play_track = function(contents, index, base)
      queue.clear()
      local track = database.track_by_id(contents)
      if (track and longform_content) then
        queue.play_from(track.filepath, track.saved_position)
      else
        -- Skip to the selected track in the background, rather than walking
        -- the list here one database lookup at a time.
        queue.add(base, index)
      end
      playback.playing:set(true)
      playing:push_if_not_shown()
    end

    widgets.InfiniteList(self.root, self.iterator, {
      focus_first_item = true,
      get_icon = get_icon_func,
      callback = function(item, index, base)
        return function()
          local contents = item:contents()
          if type(contents) == "userdata" then
            backstack.push(require("browser"):new {
              title = self.title,
              iterator = contents,
              mediatype = self.mediatype,
              breadcrumb = tostring(item)
            })
          else
            play_track(contents, index, base)
          end
        end
      end,
      -- Tapping the bottom of the click wheel plays the highlighted item
      -- straight away. Albums, artists, etc. play all of their contents.
      play_callback = function(item, index, base)
        return function()
          local contents = item:contents()
          print("qdbg play_callback " .. tostring(item) .. " " .. type(contents))
          if type(contents) == "userdata" then
            queue.clear()
            queue.add(contents)
            playback.playing:set(true)
            playing:push_if_not_shown()
          else
            play_track(contents, index, base)
          end
        end
      end,
      context_callback = function(item)
        return function()
          backstack.push(require("context_menu"):new {
            item = item,
            list = whole_list,
            list_title = self.breadcrumb or self.title,
          })
        end
      end
    })
  end
}
