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

  if not opts.transparent_bg then
    theme.set_subject(root, "header")
  end
  theme.set_subject(root, "status_bar")

  if opts.back_cb then
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

-- Actions for playing a list item immediately (e.g. by holding the bottom of
-- the click wheel), keyed by the item's button. Weak keys, so that deleted
-- buttons don't leak.
widgets.play_actions = setmetatable({}, { __mode = "k" })

-- Plays the currently focused list item, if it supports being played.
function widgets.play_focused()
  local group = lvgl.group.get_default()
  local focused = group and group:get_focused()
  local action = focused and widgets.play_actions[focused]
  if action then
    action()
  end
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

  -- Records are read from the database in batches and kept in a small window
  -- of batches around what's on screen, so that scrolling rarely touches the
  -- database. Iterators positioned just before nearby batches are kept too,
  -- so that batches which fell out of the window can be read again with one
  -- seek. Both are bounded, since each record and iterator uses scarce
  -- internal RAM; very long lists would otherwise run the device out of it.
  local kBatchSize = 20
  local kBatchesKept = 4
  local kAnchorsKept = 10
  local start_iterator = iterator:clone()
  local anchors = {}
  local batches = {}
  local last_batch = nil -- Index of the final batch, once we've seen it.

  -- Returns a fresh iterator positioned just before batch k.
  local function anchor_for(k)
    if k == 0 then return start_iterator:clone() end
    local anchor = anchors[k]
    if anchor then return anchor:clone() end
    -- Start from the nearest kept anchor below k (or the beginning), and skip
    -- the rest of the way.
    local base, base_k = start_iterator, 0
    for j, a in pairs(anchors) do
      if j < k and j > base_k then base, base_k = a, j end
    end
    local it = base:clone()
    it:skip((k - base_k) * kBatchSize)
    return it
  end

  local function load_batch(k)
    if batches[k] then return batches[k] end
    if k < 0 or (last_batch and k > last_batch) then return nil end
    local it = anchor_for(k)
    local records = it:next_batch(kBatchSize)
    batches[k] = records
    if #records == kBatchSize then
      anchors[k + 1] = anchors[k + 1] or it
    else
      last_batch = k
    end
    for other in pairs(batches) do
      if math.abs(other - k) > kBatchesKept then
        batches[other] = nil
      end
    end
    for other in pairs(anchors) do
      if math.abs(other - k) > kAnchorsKept then
        anchors[other] = nil
      end
    end
    return records
  end

  local function get_item(index)
    if index < 0 then return nil end
    local k = index // kBatchSize
    local records = load_batch(k)
    return records and records[index - k * kBatchSize + 1]
  end

  local last_selected = 0
  local last_index = 0
  local first_index = 0

  local function remove_top()
    local obj = infinite_list.root:get_child(0)
    obj:delete()
    first_index = first_index + 1
  end

  local function remove_last()
    local obj = infinite_list.root:get_child(-1)
    obj:delete()
    last_index = last_index - 1
  end

  local function add_item(item, index, item_opts)
    if not item then
      return
    end
    item_opts = item_opts or {}
    local this_item = index
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
    -- list) and return a function matching the arg of onClicked
    if opts.callback then
      btn:onClicked(opts.callback(item, this_item))
    end
    if opts.play_callback then
      widgets.play_actions[btn] = opts.play_callback(item, this_item)
    end
    -- Scroll instantly rather than animating on every step. Animating each
    -- item while the wheel is spinning makes the list feel chunky, as each
    -- animation is cut short by the next.
    btn:clear_flag(lvgl.FLAG.SCROLL_ON_FOCUS)
    btn:onevent(lvgl.EVENT.FOCUSED, function()
      btn:scroll_to_view(false)
      if refreshing then return end
      if this_item > last_selected and this_item - first_index > 3 then
        -- moving forward
        local to_add = get_item(last_index + 1)
        if to_add then
          remove_top()
          add_item(to_add, last_index + 1)
        end
      end
      if this_item < last_selected then
        -- moving backward
        if (first_index > 0 and last_index - this_item > 3) then
          local to_add = get_item(first_index - 1)
          if to_add then
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

  for idx = 0, 8 do
    local val = get_item(idx)
    if not val then
      break
    end
    add_item(val, idx, { focus = (opts.focus_first_item and idx == 0) })
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
