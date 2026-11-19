-- SPDX-FileCopyrightText: 2023 jacqueline <me@jacqueline.id.au>
--
-- SPDX-License-Identifier: GPL-3.0-only

local lvgl = require("lvgl")

local power = require("power")
local bluetooth = require("bluetooth")
local backstack = require("backstack")
local styles = require("styles")
local database = require("database")
local theme = require("theme")
local screen = require("screen")
local images = require("images")

local spinner_frames = {}
for i = 0, 11 do
  spinner_frames[#spinner_frames + 1] =
      lvgl.ImgData("//lua/img/spinner/" .. i .. ".png")
end

local img = {
  chg = lvgl.ImgData("//lua/img/bat/chg.png"),
  chg_outline = lvgl.ImgData("//lua/img/bat/chg_outline.png"),
  bat_100 = lvgl.ImgData("//lua/img/bat/100.png"),
  bat_80 = lvgl.ImgData("//lua/img/bat/80.png"),
  bat_60 = lvgl.ImgData("//lua/img/bat/60.png"),
  bat_40 = lvgl.ImgData("//lua/img/bat/40.png"),
  bat_20 = lvgl.ImgData("//lua/img/bat/20.png"),
  bat_0 = lvgl.ImgData("//lua/img/bat/0.png"),
  bt_conn = lvgl.ImgData("//lua/img/bt_conn.png"),
  bt = lvgl.ImgData("//lua/img/bt.png")
}

local widgets = {}

function widgets.Description(obj, text)
  local label = obj:Label {}
  if text then
    label:set { text = text }
  end
  label:add_flag(lvgl.FLAG.HIDDEN)
  return label
end

widgets.MenuScreen = screen:new {
  show_back = false,
  title = "",
  create_ui = function(self)
    self.root = lvgl.Object(nil, {
      flex = {
        flex_direction = "column",
        flex_wrap = "nowrap",
        justify_content = "flex-start",
        align_items = "flex-start",
        align_content = "flex-start",
      },
      w = lvgl.PCT(100),
      h = lvgl.PCT(100),
    })
    self.root:center()
    self.status_bar = widgets.StatusBar(self, {
      back_cb = self.show_back and backstack.pop or nil,
      title = self.title,
      transparent_bg = true
    })
  end
}

function widgets.Row(parent, left_text, right_text)
  local container = parent:Object {
    flex = {
      flex_direction = "row",
      justify_content = "flex-start",
      align_items = "flex-start",
      align_content = "flex-start"
    },
    w = lvgl.PCT(100),
    h = lvgl.SIZE_CONTENT
  }
  container:add_style(styles.list_item)
  local left = container:Label {
    text = left_text,
    flex_grow = 1,
  }
  local right = container:Label {
    text = right_text or "",
  }
  return {
    left = left,
    right = right,
  }
end

local bindings_meta = {}
bindings_meta["__add"] = function(a, b)
  return setmetatable(table.move(a, 1, #a, #b + 1, b), bindings_meta)
end

-- Every status bar's database spinner, animated by one shared timer. Weak keys,
-- so a screen's spinner goes away with the screen.
local spinners = setmetatable({}, { __mode = "k" })
local spinner_frame = 1
local spinner_timer = nil

local function step_spinners()
  if not database.updating:get() then
    spinner_timer:pause()
    return
  end
  spinner_frame = spinner_frame % #spinner_frames + 1
  local src = spinner_frames[spinner_frame]
  for spinner in pairs(spinners) do
    -- The image may already be deleted along with its screen; forget it.
    if not pcall(spinner.set_src, spinner, src) then
      spinners[spinner] = nil
    end
  end
end

local function start_spinners()
  if not spinner_timer then
    spinner_timer = lvgl.Timer {
      period = 83,
      cb = step_spinners,
    }
  end
  spinner_timer:resume()
end

-- The last part of a file path, e.g. "Album" for "/Music/Artist/Album/".
function widgets.LastPathPart(path)
  return string.match(path, "([^/]+)/*$") or path
end

function widgets.StatusBar(parent, opts)
  local root = parent.root:Object {
    flex = {
      flex_direction = "row",
      justify_content = "flex-start",
      align_items = "center",
      align_content = "center",
    },
    w = lvgl.HOR_RES(),
    h = lvgl.SIZE_CONTENT,
    pad_top = 1,
    pad_bottom = 1,
    pad_left = 4,
    pad_right = 4,
    pad_column = 1,
    scrollbar_mode = lvgl.SCROLLBAR_MODE.OFF,
  }

  -- With the click wheel, tapping the top of the wheel always goes back (like
  -- an iPod's Menu button), so the on-screen back button is just one more
  -- thing to scroll past. Other control schemes still need it.
  local wheel_scheme = require("controls").wheel_scheme:get()
  local click_wheel = wheel_scheme == 2 or wheel_scheme == 3

  -- Without a back button, a transparent bar with a title looks bare, so give
  -- it the same header bar as lists.
  if not opts.transparent_bg or (click_wheel and opts.title) then
    theme.set_subject(root, "header")
  end
  theme.set_subject(root, "status_bar")
  if opts.back_cb and not click_wheel then
    local back = root:Button {
      w = lvgl.SIZE_CONTENT,
      h = lvgl.SIZE_CONTENT,
    }
    back:Image{src=images.back}
    theme.set_subject(back, "back_button")
    widgets.Description(back, "Back")
    back:onClicked(opts.back_cb)
    back:onevent(lvgl.EVENT.FOCUSED, function()
      local first_view = parent.content
      if not first_view then return end
      while first_view:get_child_cnt() > 0 do
        first_view = first_view:get_child(0)
      end
      if first_view then
        first_view:scroll_to_view_recursive(1)
      end
    end)
  end

  local title = root:Label {
    w = lvgl.PCT(100),
    h = lvgl.SIZE_CONTENT,
    text_font = font.fusion_10,
    text = "",
    align = lvgl.ALIGN.CENTER,
    flex_grow = 1,
    pad_left = 2,
    -- Keep long titles (e.g. a song's name) to one line.
    long_mode = lvgl.LABEL.LONG_DOT,
  }
  if opts.title then
    title:set { text = opts.title }
  end
  theme.set_subject(title, "status_bar_title")

  local db_updating = root:Image { src = spinner_frames[spinner_frame] }
  spinners[db_updating] = true
  theme.set_subject(db_updating, "database_indicator")
  local bt_icon = root:Image {}
  local battery_icon = root:Image {}
  local charge_icon = battery_icon:Image { src = img.chg }
  local charge_icon_outline = battery_icon:Image { src = img.chg_outline }
  charge_icon_outline:center();
  charge_icon:center()

  local is_charging = nil
  local percent = nil
  local charge_state = nil

  local function update_battery_icon()
    if is_charging == nil or percent == nil or charge_state == nil then return end
    local src
    theme.set_subject(battery_icon, "battery")
    if percent >= 95 then
      theme.set_subject(battery_icon, "battery_100")
      src = img.bat_100
    elseif percent >= 75 then
      theme.set_subject(battery_icon, "battery_80")
      src = img.bat_80
    elseif percent >= 55 then
      theme.set_subject(battery_icon, "battery_60")
      src = img.bat_60
    elseif percent >= 35 then
      theme.set_subject(battery_icon, "battery_40")
      src = img.bat_40
    elseif percent >= 15 then
      theme.set_subject(battery_icon, "battery_20")
      src = img.bat_20
    else
      theme.set_subject(battery_icon, "battery_0")
      src = img.bat_0
    end
    theme.set_subject(battery_icon, "battery_state_" .. charge_state)
    if is_charging then
      theme.set_subject(battery_icon, "battery_charging")
      theme.set_subject(charge_icon, "battery_charge_icon")
      theme.set_subject(charge_icon, "battery_charge_icon_state_" .. charge_state)
      theme.set_subject(charge_icon_outline, "battery_charge_outline")
      theme.set_subject(charge_icon_outline, "battery_charge_icon_outline_state_" .. charge_state)
      charge_icon:clear_flag(lvgl.FLAG.HIDDEN)
      charge_icon_outline:clear_flag(lvgl.FLAG.HIDDEN)
    else
      charge_icon:add_flag(lvgl.FLAG.HIDDEN)
      charge_icon_outline:add_flag(lvgl.FLAG.HIDDEN)
    end
    battery_icon:set_src(src)
  end

  parent.bindings = {
    database.updating:bind(function(yes)
      if yes then
        db_updating:set_src(spinner_frames[spinner_frame])
        db_updating:clear_flag(lvgl.FLAG.HIDDEN)
        start_spinners()
      else
        db_updating:add_flag(lvgl.FLAG.HIDDEN)
      end
    end),
    power.battery_pct:bind(function(pct)
      percent = pct
      update_battery_icon()
    end),
    power.charge_state:bind(function(state)
      charge_state = state
      update_battery_icon()
    end),
    power.plugged_in:bind(function(p)
      is_charging = p
      update_battery_icon()
    end),
    bluetooth.enabled:bind(function(en)
      if en then
        bt_icon:clear_flag(lvgl.FLAG.HIDDEN)
      else
        bt_icon:add_flag(lvgl.FLAG.HIDDEN)
      end
    end),
    bluetooth.connected:bind(function(connected)
      theme.set_subject(bt_icon, "bluetooth_icon")
      if connected then
        bt_icon:set_src(img.bt_conn)
      else
        bt_icon:set_src(img.bt)
      end
    end),
  }
  setmetatable(parent.bindings, bindings_meta)
end

function widgets.IconBtn(parent, icon, text)
  local btn = parent:Button {
    flex = {
      flex_direction = "row",
      justify_content = "flex-start",
      align_items = "center",
      align_content = "center"
    },
    w = lvgl.SIZE_CONTENT,
    h = lvgl.SIZE_CONTENT,
  }
  btn:Image {
    src = icon
  }
  btn:Label {
    text = text,
    text_font = font.fusion_10
  }
  return btn
end

-- Actions for playing a list item immediately (e.g. by tapping the bottom of
-- the click wheel), keyed by the item's button. Weak keys, so that deleted
-- buttons don't leak.
widgets.play_actions = setmetatable({}, { __mode = "k" })

-- Plays the currently focused list item, if it supports being played.
-- Returns true if it did.
function widgets.play_focused()
  local group = lvgl.group.get_default()
  local focused = group and group:get_focused()
  local action = focused and widgets.play_actions[focused]
  local n = 0
  for _ in pairs(widgets.play_actions) do n = n + 1 end
  print("qdbg play_focused group=" .. tostring(group) .. " focused=" ..
    tostring(focused) .. " action=" .. tostring(action) .. " n=" .. n)
  if action then
    action()
    return true
  end
  return false
end

-- List rows' jump-by-letter actions, keyed by row. Weak keys, so that rows can
-- be deleted along with their screen.
widgets.jump_actions = setmetatable({}, { __mode = "k" })

-- Passes a jump from the click wheel to the focused list, if it supports
-- jumping. direction is 1 (next letter), -1 (previous letter), or 0 (the
-- spin has slowed down, so land on the letter we jumped to).
function widgets.jump_focused(direction)
  local group = lvgl.group.get_default()
  local focused = group and group:get_focused()
  local action = focused and widgets.jump_actions[focused]
  if action then
    action(direction)
  end
end

-- Lists at least this long jump by letter when the click wheel is spun fast.
local kJumpMinItems = 50

-- The letter shown while jumping through a list: the upper case first letter,
-- or "#" for digits and symbols. Matches the grouping in the database.
local function letter_of(text)
  local first = string.sub(text, 1, 1)
  if first == "" then
    return "#"
  end
  if string.byte(first) >= 0x80 then
    local ok, cp = pcall(utf8.codepoint, text, 1)
    if ok then
      return utf8.char(cp)
    end
    return "#"
  end
  if string.match(first, "%a") then
    return string.upper(first)
  end
  return "#"
end

function widgets.InfiniteList(parent, iterator, opts)
  local infinite_list = {}

  infinite_list.root = lvgl.List(parent, {
    w = lvgl.PCT(100),
    h = lvgl.PCT(100),
    flex_grow = 1,
    scrollbar_mode = lvgl.SCROLLBAR_MODE.OFF
  })

  local refreshing = false -- Used so that we can ignore focus events during this phase
  local function refresh_group()
    refreshing = true
    local group = lvgl.group.get_default()
    local focused_obj = group:get_focused()
    local num_children = infinite_list.root:get_child_cnt()
    if (focused_obj) then
      group:clear_focus()
    end
    -- remove all children from the group and re-add them
    for i = 0, num_children - 1 do
      lvgl.group.remove_obj(infinite_list.root:get_child(i))
    end
    for i = 0, num_children - 1 do
      group:add_obj(infinite_list.root:get_child(i))
    end
    if (focused_obj) then
      lvgl.group.focus_obj(focused_obj)
    end
    refreshing = false
  end

  -- Item indices are relative to this iterator: index 0 is the record that
  -- base:next() returns. It changes when the list jumps to a letter, after
  -- which indices above the letter are negative.
  local base = iterator
  local fwd_iterator = iterator:clone()
  local bck_iterator = iterator:clone()

  local last_selected = 0
  local last_index = 0
  local first_index = 0

  -- Long lists sorted by name can be jumped through by letter, by spinning
  -- the click wheel fast. Rows of such lists are marked with USER_1, which
  -- tells the wheel to send jumps instead of scrolling row by row.
  local jumpable = false
  local ok, sorted_by_text = pcall(function() return iterator.sorted_by_text end)
  if ok and sorted_by_text and iterator:sorted_by_text() then
    local probe = iterator:clone()
    probe:skip(kJumpMinItems)
    jumpable = probe:value() ~= nil
  end

  local function remove_top()
    local obj = infinite_list.root:get_child(0)
    obj:delete()
    first_index = first_index + 1
    bck_iterator:next()
  end

  local function remove_last()
    local obj = infinite_list.root:get_child(-1)
    obj:delete()
    last_index = last_index - 1
    fwd_iterator:prev()
  end

  local jump
  -- Keeps the letter overlay (if it's showing) in step with the focused row.
  local follow_letter

  local function add_item(item, index, item_opts)
    if not item then
      return
    end
    item_opts = item_opts or {}
    local this_item = index
    local this_base = base
    local add_to_top = false
    if this_item < first_index then
      first_index = this_item
      add_to_top = true
    end
    if this_item > last_index then last_index = index end
    local icon = nil
    if opts.get_icon then
      icon = opts.get_icon(item)
    end
    local btn = infinite_list.root:add_btn(icon, tostring(item))
    if add_to_top then
      btn:move_to_index(0)
    end
    if item_opts.focus then
      btn:focus()
    end
    -- opts.callback should take an item (and optionally its index in the
    -- list, and the iterator that index is relative to) and return a function
    -- matching the arg of onClicked
    if opts.callback then
      btn:onClicked(opts.callback(item, this_item, this_base))
    end
    if opts.play_callback then
      widgets.play_actions[btn] = opts.play_callback(item, this_item, this_base)
    end
    if jumpable then
      btn:add_flag(lvgl.FLAG.USER_1)
      widgets.jump_actions[btn] = jump
    end
    -- Scroll instantly rather than animating on every step. Animating each
    -- item while the wheel is spinning makes the list feel chunky, as each
    -- animation is cut short by the next.
    btn:clear_flag(lvgl.FLAG.SCROLL_ON_FOCUS)
    btn:onevent(lvgl.EVENT.FOCUSED, function()
      btn:scroll_to_view(false)
      if follow_letter then
        follow_letter(item)
      end
      if refreshing then return end
      if this_item > last_selected and this_item - first_index > 3 then
        -- moving forward
        local to_add = fwd_iterator:next()
        if to_add then
          remove_top()
          add_item(to_add, last_index + 1)
        end
      end
      if this_item < last_selected then
        -- moving backward (this may go above index 0 after a jump)
        if last_index - this_item > 3 then
          local to_add = bck_iterator:value();
          if to_add then
            bck_iterator:prev()
            remove_last()
            add_item(to_add, first_index - 1)
            refresh_group()
          end
        end
      end
      last_selected = this_item
    end)
    if opts.context_callback then
      btn:onevent(lvgl.EVENT.LONG_PRESSED, opts.context_callback(item, this_item))
    end
    btn:add_style(styles.list_item)
    return btn
  end

  -- Shows the rows starting at index 0 of a new base, and focuses the first.
  local function fill(new_base)
    refreshing = true
    for i = infinite_list.root:get_child_cnt() - 1, 0, -1 do
      infinite_list.root:get_child(i):delete()
    end
    base = new_base
    fwd_iterator = new_base:clone()
    bck_iterator = new_base:clone()
    first_index = 0
    last_index = 0
    last_selected = 0
    for idx = 0, 8 do
      local val = fwd_iterator:next()
      if not val then
        break
      end
      add_item(val, idx, { focus = idx == 0 })
    end
    -- Also show a few rows above the one we landed on. Without these, the
    -- landed row is the first in the list, so scrolling back moves focus out
    -- of the list and into the buttons above it.
    for _ = 1, 4 do
      local to_add = bck_iterator:value()
      if not to_add then
        break
      end
      bck_iterator:prev()
      add_item(to_add, first_index - 1)
    end
    refreshing = false
    -- Rows added above must come first in the focus order too.
    refresh_group()
  end

  -- Jumping by letter. While the wheel is spinning fast, only the letter
  -- overlay changes; the rows are rebuilt once, when the spin slows down.
  local pending = nil -- Iterator on the record we've jumped to.
  local overlay = nil
  local overlay_label = nil
  local overlay_timer = nil
  local overlay_shown = false

  local function hide_overlay_after(ms)
    if overlay_timer then
      overlay_timer:delete()
    end
    overlay_timer = lvgl.Timer {
      period = ms,
      cb = function(t)
        t:pause()
        overlay_shown = false
        if overlay then
          pcall(overlay.add_flag, overlay, lvgl.FLAG.HIDDEN)
        end
      end,
    }
  end

  local function show_letter(text)
    if not overlay then
      overlay = parent:Object {
        w = 36,
        h = 36,
        radius = 6,
        pad_all = 0,
      }
      -- Not part of the screen's layout, and drawn over the list.
      overlay:add_flag(lvgl.FLAG.FLOATING)
      theme.set_subject(overlay, "pop_up")
      overlay_label = overlay:Label {
        text = "",
        text_font = font.fusion_12,
      }
      overlay_label:center()
      overlay:center()
    end
    overlay_label:set { text = letter_of(text) }
    overlay_label:center()
    overlay:clear_flag(lvgl.FLAG.HIDDEN)
    overlay_shown = true
    -- The overlay stays up until the finger lifts. This is only a fallback,
    -- in case the release never reaches this list (e.g. the screen changed).
    hide_overlay_after(10000)
  end

  follow_letter = function(item)
    if overlay_shown then
      overlay_label:set { text = letter_of(tostring(item)) }
      overlay_label:center()
    end
  end

  -- While jumping, the rows follow the letter too, once the wheel pauses on it
  -- for a moment. Rebuilding the rows on every jump would make fast spins
  -- stutter, so they're only rebuilt after kPreviewDelayMs without a jump.
  local kPreviewDelayMs = 200
  local preview_timer = nil
  local previewed = false -- Whether the rows already show `pending`.

  -- Shows the rows starting at the letter we've jumped to, without ending
  -- the jump.
  local function show_pending()
    if not pending then
      return
    end
    local target = pending:clone()
    -- See the comment in jump() about why the base sits before the target.
    target:prev()
    fill(target)
    previewed = true
  end

  local function preview_after_pause()
    previewed = false
    if preview_timer then
      preview_timer:delete()
    end
    preview_timer = lvgl.Timer {
      period = kPreviewDelayMs,
      cb = function(t)
        t:pause()
        -- The screen may have gone since; if so, do nothing.
        pcall(show_pending)
      end,
    }
  end

  jump = function(direction)
    if direction == nil then
      -- The finger has lifted. Stop showing the letter.
      hide_overlay_after(300)
      return
    end
    if direction == 0 then
      -- The spin has slowed down. Land on the letter we jumped to.
      if preview_timer then
        preview_timer:pause()
      end
      if pending and previewed then
        -- The rows already show this letter.
        pending = nil
      elseif pending then
        local target = pending
        pending = nil
        -- The new base sits just before the target, so that the target is
        -- index 0. (If the target is the very first record, prev() fails and
        -- leaves it just before the target anyway.)
        target:prev()
        fill(target)
      end
      -- Keep showing the letter until the finger lifts.
      return
    end
    if not pending then
      -- Start from the focused row.
      pending = base:clone()
      pending:skip(last_selected + 1)
      if not pending:value() then
        pending = nil
        return
      end
    end
    local rec = pending:jump(direction)
    show_letter(tostring(rec or pending:value()))
    preview_after_pause()
  end

  for idx = 0, 8 do
    local val = fwd_iterator:next()
    if not val then
      break
    end
    add_item(val, idx, { focus = (opts.focus_first_item and idx == 0) })
  end
  -- Lists that start part way through (e.g. the queue, at the current track)
  -- can show a few rows above the first one, so that it's possible to scroll
  -- back from it.
  if opts.rows_above then
    refreshing = true
    for _ = 1, opts.rows_above do
      local to_add = bck_iterator:value()
      if not to_add then
        break
      end
      bck_iterator:prev()
      add_item(to_add, first_index - 1)
    end
    refreshing = false
    refresh_group()
  end

  return infinite_list
end

function widgets.PopUp(text) 
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
          pad_all = 5,
        })
        theme.set_subject(container, "pop_up")
        container:Label {
          text = text,
          text_font = font.fusion_10,
          w = lvgl.PCT(100),
          h = lvgl.SIZE_CONTENT, 
          long_mode = lvgl.LABEL.LONG_WRAP,
        }
        container:center()
      end)
end

return widgets
