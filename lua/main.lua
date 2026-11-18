-- SPDX-FileCopyrightText: 2023 jacqueline <me@jacqueline.id.au>
--
-- SPDX-License-Identifier: GPL-3.0-only

-- require() everything else needed for the main menu + global bindings. Do
-- this now instead of in init_ui because loading and parsing the scripts can
-- take a while.
local vol = require("volume")
local theme = require("theme")
local controls = require("controls")
local time = require("time")
local sd_card = require("sd_card")
local backstack = require("backstack")
local main_menu = require("main_menu")

local saved_theme = theme.theme_filename()
local res = theme.load_theme(saved_theme)
if not res then
  -- Set a default theme (in case the saved theme does not load)
  local default_theme = require("theme_light")
  theme.set(default_theme)
end

local lock_time = time.ticks()

-- Set up property bindings that are used across every screen.
GLOBAL_BINDINGS = {
  -- Show an alert with the current volume whenever the volume changes
  vol.current_pct:bind(function(pct)
    require("alerts").show(function()
      local container = lvgl.Object(nil, {
        w = lvgl.PCT(80),
        h = lvgl.SIZE_CONTENT,
        flex = {
          flex_direction = "column",
          justify_content = "center",
          align_items = "center",
          align_content = "center",
        },
        radius = 8,
        pad_all = 2,
      })
      theme.set_subject(container, "pop_up")
      container:Label {
        text = string.format("Volume %i%%", pct),
        text_font = font.fusion_10
      }
      container:Bar {
        w = lvgl.PCT(100),
        h = 8,
        range = { min = 0, max = 100 },
        value = pct,
      }
      container:center()
    end)
  end),
  -- When the device has been locked for a while, default to showing the now
  -- playing screen after unlocking.
  controls.lock_switch:bind(function(locked)
    if locked then
      lock_time = time.ticks()
      collectgarbage()
    elseif time.ticks() - lock_time > 8000 then
      local queue = require("queue")
      if queue.size:get() > 0 then
        require("playing"):push_if_not_shown()
      end
    end
  end),
  controls.wheel_scheme:bind(function()
    -- Set up a shortcut for jumping straight to the 'now playing' screen.
    -- Implemented as a binding so that the shortcut is still applied even if
    -- the control scheme is changed at runtime.
    local hooks = controls.hooks()
    local input_method = hooks.wheel or hooks.dpad
    if input_method and input_method.right then
      input_method.right.long_press = function()
        require("playing"):push_if_not_shown()
      end
    end
    -- Holding the top of the click wheel (its 'menu' button) goes all the way
    -- back to the main menu.
    if hooks.wheel and hooks.wheel.up then
      hooks.wheel.up.long_press = function()
        backstack.reset(main_menu:new())
      end
    end
    -- The bottom of the click wheel is its Play button, as on an iPod. In a
    -- music list, a tap plays the highlighted item straight away and a hold
    -- pauses or resumes. Everywhere else, a tap pauses or resumes. Starting
    -- playback always shows Now Playing.
    if hooks.wheel and hooks.wheel.down then
      local toggle = function()
        local playback = require("playback")
        local play = not playback.playing:get()
        playback.playing:set(play)
        if play and require("queue").size:get() > 0 then
          require("playing"):push_if_not_shown()
        end
      end
      hooks.wheel.down.click = function()
        if not require("widgets").play_focused() then
          toggle()
        end
      end
      hooks.wheel.down.long_press = toggle
    end
    -- Spinning the click wheel fast through a long list jumps by letter.
    if hooks.wheel and hooks.wheel.jump_next then
      hooks.wheel.jump_next.click = function()
        require("widgets").jump_focused(1)
      end
      hooks.wheel.jump_prev.click = function()
        require("widgets").jump_focused(-1)
      end
      hooks.wheel.jump_end.click = function()
        require("widgets").jump_focused(0)
      end
      if hooks.wheel.jump_release then
        hooks.wheel.jump_release.click = function()
          require("widgets").jump_focused(nil)
        end
      end
    end
  end),
  sd_card.mounted:bind(function(mounted)
    backstack.reset(main_menu:new())
  end),
}
