/*
 * Copyright 2023 jacqueline <me@jacqueline.id.au>
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "audio/audio_events.hpp"
#include "lua/lua_database.hpp"

#include <memory>
#include <string>

#include "lua.hpp"

#include "esp_log.h"
#include "lauxlib.h"
#include "lua.h"
#include "lvgl.h"

#include "audio/track_queue.hpp"
#include "database/database.hpp"
#include "database/index.hpp"
#include "database/track.hpp"
#include "events/event_queue.hpp"
#include "lua/bridge.hpp"
#include "lua/property.hpp"
#include "system_fsm/service_locator.hpp"
#include "ui/ui_events.hpp"
#include "perf_trace.hpp"

namespace lua {

[[maybe_unused]] static constexpr char kTag[] = "lua_queue";

static auto queue_add(lua_State* state) -> int {
  Bridge* instance = Bridge::Get(state);
  perf::milestone("lua queue.add");

  if (lua_isinteger(state, 1)) {
    database::TrackId id = luaL_checkinteger(state, 1);
    instance->services().bg_worker().Dispatch<void>([=]() {
      audio::TrackQueue& queue = instance->services().track_queue();
      queue.append(id);
    });
  } else if (lua_isstring(state, 1)) {
    size_t len;
    const char* str = luaL_checklstring(state, 1, &len);
    std::string path{str, len};
    instance->services().bg_worker().Dispatch<void>([=]() {
      audio::TrackQueue& queue = instance->services().track_queue();
      queue.append(path);
    });
  } else {
    // Copy the iterator now; the Lua object may be collected before the
    // background task runs.
    database::Iterator it = *db_check_iterator(state, 1);
    // Optionally skip the first n records. This is done off the UI task, and
    // with a single seek rather than one per record.
    bool has_skip = lua_isinteger(state, 2);
    lua_Integer skip = luaL_optinteger(state, 2, 0);
    instance->services().bg_worker().Dispatch<void>([=]() mutable {
      audio::TrackQueue& queue = instance->services().track_queue();
      if (skip > 0) {
        it.skip(skip);
      }
      if (has_skip) {
        // The user picked this track, so play it first even when shuffling.
        queue.appendStartingWithFirst(database::TrackIterator{it});
      } else {
        queue.append(database::TrackIterator{it});
      }
    });
  }

  return 0;
}

static auto queue_clear(lua_State* state) -> int {
  perf::markRequest("play request (lua queue.clear)");
  perf::Scope scope{"queue.clear"};
  Bridge* instance = Bridge::Get(state);
  audio::TrackQueue& queue = instance->services().track_queue();
  queue.clear();
  return 0;
}

static auto queue_open_playlist(lua_State* state) -> int {
  Bridge* instance = Bridge::Get(state);
  audio::TrackQueue& queue = instance->services().track_queue();
  size_t len = 0;
  const char* str = luaL_checklstring(state, 1, &len);
  if (!str) {
    return 0;
  }
  queue.clear();
  queue.openPlaylist(str);
  return 0;
}

static auto queue_save_to_playlist(lua_State* state) -> int {
  Bridge* instance = Bridge::Get(state);
  audio::TrackQueue& queue = instance->services().track_queue();
  size_t len = 0;
  const char* str = luaL_checklstring(state, 1, &len);
  if (!str) {
    return 0;
  }
  bool res = queue.saveToNewPlaylist(str);
  lua_pushboolean(state, res);
  return 1;
}

static auto queue_play_from(lua_State* state) -> int {
  perf::milestone("lua queue.play_from");
  Bridge* instance = Bridge::Get(state);
  audio::TrackQueue& queue = instance->services().track_queue();
  size_t len = 0;
  const char* str = luaL_checklstring(state, 1, &len);
  if (!str) {
    return 0;
  }
  auto pos = luaL_checkinteger(state, 2);
  queue.playFromPosition(str, pos);
  return 0;
}

static const struct luaL_Reg kQueueFuncs[] = {
    {"add", queue_add},
    {"clear", queue_clear},
    {"open_playlist", queue_open_playlist},
    {"save_to_playlist", queue_save_to_playlist},
    {"play_from", queue_play_from},
    {NULL, NULL}};

static auto lua_queue(lua_State* state) -> int {
  luaL_newlib(state, kQueueFuncs);

  // Repeat Mode Enum
  lua_pushliteral(state, "RepeatMode");
  lua_newtable(state);

  lua_pushliteral(state, "OFF");
  lua_pushinteger(state, (int)audio::TrackQueue::RepeatMode::OFF);
  lua_rawset(state, -3);

  lua_pushliteral(state, "REPEAT_TRACK");
  lua_pushinteger(state, (int)audio::TrackQueue::RepeatMode::REPEAT_TRACK);
  lua_rawset(state, -3);

  lua_pushliteral(state, "REPEAT_QUEUE");
  lua_pushinteger(state, (int)audio::TrackQueue::RepeatMode::REPEAT_QUEUE);
  lua_rawset(state, -3);

  lua_rawset(state, -3);
  return 1;
}

auto RegisterQueueModule(lua_State* s) -> void {
  luaL_requiref(s, "queue", lua_queue, true);
  lua_pop(s, 1);
}

}  // namespace lua
