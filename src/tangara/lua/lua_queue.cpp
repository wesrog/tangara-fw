/*
 * Copyright 2023 jacqueline <me@jacqueline.id.au>
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "audio/audio_events.hpp"
#include "lua/lua_database.hpp"

#include <algorithm>
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

namespace lua {

[[maybe_unused]] static constexpr char kTag[] = "lua_queue";

static auto queue_add(lua_State* state) -> int {
  // Read the optional skip count before Bridge::Get, which pushes onto the
  // stack and would otherwise show up as argument 2.
  bool has_skip = lua_gettop(state) >= 2 && lua_isinteger(state, 2);
  lua_Integer skip = has_skip ? lua_tointeger(state, 2) : 0;
  Bridge* instance = Bridge::Get(state);

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
    // Optionally skip the first n records (or go back n records). This is
    // done off the UI task, and with a single seek rather than one per record.
    instance->services().bg_worker().Dispatch<void>([=]() mutable {
      audio::TrackQueue& queue = instance->services().track_queue();
      if (skip != 0) {
        // May be negative, for lists that started part way through.
        it.skip(static_cast<int>(skip));
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

// queue.entries(first, count): up to `count` tracks from the queue, starting
// at position `first` (1-based, like queue.position). Each is a table with the
// track's `position`, `path`, and `title` (and `artist`, if known).
static auto queue_entries(lua_State* state) -> int {
  lua_Integer first = luaL_checkinteger(state, 1);
  lua_Integer count = luaL_checkinteger(state, 2);
  // Bridge::Get pushes a value onto the stack, so get it before creating the
  // result table; otherwise the entries below would be stored into that value
  // instead of the table (corrupting the Lua heap), and it would be returned.
  Bridge* instance = Bridge::Get(state);
  lua_newtable(state);
  const int result = lua_gettop(state);
  if (first < 1 || count < 1) {
    return 1;
  }
  // Keep each call small; the list asks for a screenful at a time.
  count = std::min<lua_Integer>(count, 32);

  audio::TrackQueue& queue = instance->services().track_queue();
  auto paths = queue.entries(first - 1, count);
  auto db = instance->services().database().lock();

  lua_Integer i = 0;
  for (const auto& path : paths) {
    lua_createtable(state, 0, 4);

    lua_pushinteger(state, first + i);
    lua_setfield(state, -2, "position");
    lua_pushlstring(state, path.data(), path.size());
    lua_setfield(state, -2, "path");

    std::shared_ptr<database::Track> track;
    if (db) {
      auto id = db->getTrackID(path);
      if (id) {
        track = db->getTrack(*id);
      }
    }
    const auto* title = track ? &track->tags().title() : nullptr;
    if (title && *title) {
      lua_pushlstring(state, (*title)->data(), (*title)->size());
    } else {
      // Not in the database (e.g. from a playlist file); use the file name.
      auto slash = path.find_last_of('/');
      std::string name =
          slash == std::string::npos ? path : path.substr(slash + 1);
      lua_pushlstring(state, name.data(), name.size());
    }
    lua_setfield(state, -2, "title");
    if (track && track->tags().artist()) {
      const auto& artist = *track->tags().artist();
      lua_pushlstring(state, artist.data(), artist.size());
      lua_setfield(state, -2, "artist");
    }

    lua_rawseti(state, result, ++i);
  }
  lua_settop(state, result);
  return 1;
}

static const struct luaL_Reg kQueueFuncs[] = {
    {"add", queue_add},
    {"clear", queue_clear},
    {"open_playlist", queue_open_playlist},
    {"save_to_playlist", queue_save_to_playlist},
    {"play_from", queue_play_from},
    {"entries", queue_entries},
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
