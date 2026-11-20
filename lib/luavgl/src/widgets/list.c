#include <string.h>

#include "luavgl.h"
#include "private.h"

static int luavgl_list_create(lua_State *L)
{
  return luavgl_obj_create_helper(L, lv_list_create);
}

/* clang-format off */
static const luavgl_value_setter_t list_property_table[] = {
    {"dummy", SETTER_TYPE_STACK, {.setter_stack = _lv_dummy_set}},
};
/* clang-format on */

LUALIB_API int luavgl_list_set_property_kv(lua_State *L, void *data)
{
  lv_obj_t *obj = data;
  int ret = luavgl_set_property(L, obj, list_property_table);

  if (ret == 0) {
    return 0;
  }

  /* a base obj property? */
  ret = luavgl_obj_set_property_kv(L, obj);
  if (ret != 0) {
    LV_LOG_ERROR("unkown property for list");
  }

  return ret;
}

static int luavgl_list_set(lua_State *L)
{
  lv_obj_t *obj = luavgl_to_obj(L, 1);

  if (!lua_istable(L, -1)) {
    luaL_error(L, "expect a table on 2nd para.");
    return 0;
  }

  luavgl_iterate(L, -1, luavgl_list_set_property_kv, obj);

  return 0;
}

static int luavgl_list_add_text(lua_State *L)
{
  lv_obj_t *list = luavgl_to_obj(L, 1);
  const char *str = lua_tostring(L, 2);
  lv_obj_t *obj = lv_list_add_text(list, str);
  luavgl_add_lobj(L, obj)->lua_created = true;
  return 1;
}

static int luavgl_list_add_btn(lua_State *L)
{
  lv_obj_t *list = luavgl_to_obj(L, 1);
  const void *icon = luavgl_toimgsrc(L, 2);
  const char *str = lua_tostring(L, 3);
  lv_obj_t *obj = lv_list_add_btn(list, icon, str);
  luavgl_add_lobj(L, obj)->lua_created = true;
  return 1;
}

static int luavgl_get_btn_text(lua_State *L)
{
  lv_obj_t *list = luavgl_to_obj(L, 1);
  lv_obj_t *btn = luavgl_to_obj(L, 2);

  lua_pushstring(L, lv_list_get_btn_text(list, btn));
  return 1;
}

/*
 * list:set_btn(btn, icon, text): reuses a button made by add_btn for a new
 * row, by changing its icon and text in place. This is much cheaper than
 * deleting the button and making a new one. Returns false (and changes
 * nothing) if the button's shape doesn't fit, i.e. it has an icon and the new
 * row doesn't, or the other way around.
 */
static int luavgl_list_set_btn(lua_State *L)
{
  lv_obj_t *btn = luavgl_to_obj(L, 2);
  const void *icon = lua_isnoneornil(L, 3) ? NULL : luavgl_toimgsrc(L, 3);
  const char *str = lua_tostring(L, 4);

  lv_obj_t *img = NULL;
  lv_obj_t *label = NULL;
  uint32_t cnt = lv_obj_get_child_count(btn);
  for (uint32_t i = 0; i < cnt; i++) {
    lv_obj_t *child = lv_obj_get_child(btn, i);
    if (lv_obj_check_type(child, &lv_image_class)) {
      img = child;
    } else if (lv_obj_check_type(child, &lv_label_class)) {
      label = child;
    }
  }

  if (label == NULL || (icon == NULL) != (img == NULL)) {
    lua_pushboolean(L, false);
    return 1;
  }

  if (img != NULL) {
    /* Setting an image's source re-reads its header, so skip it when the
     * icon is the same. File sources are copied by LVGL, so compare those by
     * value; others by pointer. */
    const void *old = lv_image_get_src(img);
    bool same;
    if (old == NULL) {
      same = false;
    } else if (lv_image_src_get_type(icon) == LV_IMAGE_SRC_FILE &&
               lv_image_src_get_type(old) == LV_IMAGE_SRC_FILE) {
      same = strcmp((const char *)old, (const char *)icon) == 0;
    } else {
      same = old == icon;
    }
    if (!same) {
      lv_image_set_src(img, icon);
    }
  }

  lv_label_set_text(label, str ? str : "");

  lua_pushboolean(L, true);
  return 1;
}

static const luaL_Reg luavgl_list_methods[] = {
    {"set",          luavgl_list_set     },

    {"add_text",     luavgl_list_add_text},
    {"add_btn",      luavgl_list_add_btn },
    {"get_btn_text", luavgl_get_btn_text },
    {"set_btn",      luavgl_list_set_btn },

    {NULL,           NULL              },
};

static void luavgl_list_init(lua_State *L)
{
  luavgl_obj_newmetatable(L, &lv_list_class, "lv_list", luavgl_list_methods);
  lua_pop(L, 1);
}
