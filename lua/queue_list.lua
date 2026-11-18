-- SPDX-FileCopyrightText: 2026 wes
--
-- SPDX-License-Identifier: GPL-3.0-only

-- Shows the play queue, starting at the current track. Selecting a track
-- plays it.

local lvgl = require("lvgl")
local backstack = require("backstack")
local queue = require("queue")
local screen = require("screen")
local widgets = require("widgets")
local img = require("images")

-- How many tracks to read from the queue at a time.
local kBatch = 10

local entry_meta = {
  __tostring = function(e) return e.title end,
}

-- An iterator over the queue in the same shape as database iterators, so that
-- it works with widgets.InfiniteList. `pos` is the position whose entry
-- value() returns; next() moves to pos + 1 and returns it.
local function QueueIterator(cache, pos)
  local it = {}

  local function get(p)
    if p < 1 or p > queue.size:get() then
      return nil
    end
    if cache[p] == nil then
      -- Read a batch around p, so that scrolling either way rarely waits.
      local first = math.max(1, p - kBatch // 2)
      for _, e in ipairs(queue.entries(first, kBatch)) do
        cache[e.position] = setmetatable(e, entry_meta)
      end
      if cache[p] == nil then
        cache[p] = false
      end
    end
    return cache[p] or nil
  end

  function it:next()
    local e = get(pos + 1)
    if e then pos = pos + 1 end
    return e
  end

  function it:prev()
    if pos >= 1 then pos = pos - 1 end
  end

  function it:value()
    return get(pos)
  end

  function it:clone()
    return QueueIterator(cache, pos)
  end

  return it
end

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
      title = "Queue",
      transparent_bg = true,
    })

    local current = queue.position:get() or 1
    local cache = {}
    -- Start just before the current track, so that it's the first row.
    local iterator = QueueIterator(cache, current - 1)

    self.list = widgets.InfiniteList(self.root, iterator, {
      focus_first_item = true,
      rows_above = 4,
      get_icon = function(item)
        if item.position == current then
          return img.play_small
        end
        return nil
      end,
      callback = function(item)
        return function()
          queue.position:set(item.position)
          backstack.pop()
        end
      end,
    })
  end,
}
