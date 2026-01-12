// This file is part of the Luau programming language and is licensed under MIT License; see LICENSE.txt for details
// This code is based on Lua 5.x implementation licensed under MIT License; see lua_LICENSE.txt for details
#include "lualib.h"
#include "lstate.h"

#include "toml.hpp"
#include <unordered_set>

static void push_toml_value(lua_State* L, const toml::node& node);

static void push_toml_table(lua_State* L, const toml::table& tbl)
{
    lua_createtable(L, 0, (int)tbl.size());

    for (const auto& [key, val] : tbl)
    {
        lua_pushlstring(L, key.data(), key.length());
        push_toml_value(L, val);
        lua_settable(L, -3);
    }
}

static void push_toml_array(lua_State* L, const toml::array& arr)
{
    lua_createtable(L, (int)arr.size(), 0);

    int i = 1;
    for (const auto& val : arr)
    {
        push_toml_value(L, val);
        lua_rawseti(L, -2, i++);
    }
}

static void push_toml_value(lua_State* L, const toml::node& node)
{
    if (node.is_string())
        lua_pushstring(L, node.value<std::string>().value().c_str());
    else if (node.is_integer())
        lua_pushnumber(L, (lua_Number)*node.value<int64_t>());
    else if (node.is_floating_point())
        lua_pushnumber(L, (lua_Number)*node.value<double>());
    else if (node.is_boolean())
        lua_pushboolean(L, *node.value<bool>());
    else if (node.is_date_time())
        lua_pushstring(L, node.value<std::string>().value().c_str());
    else if (node.is_array())
        push_toml_array(L, *node.as_array());
    else if (node.is_table())
        push_toml_table(L, *node.as_table());
    else
        lua_pushnil(L);
}


static int tomldeserialize(lua_State* L)
{
    size_t len;
    const char* src = luaL_checklstring(L, 1, &len);

    try
    {
        toml::table tbl = toml::parse(std::string_view(src, len));

        push_toml_table(L, tbl);
    }
    catch (const toml::parse_error& err)
    {
        luaL_error(L, "%s", err.description().data());
        return 0;
    }

    return 1;
}

static toml::table encode_lua_table(lua_State* L, int idx, std::unordered_set<const void*>& seen);

static toml::array encode_lua_array(lua_State* L, int idx, std::unordered_set<const void*>& seen);

static bool is_array(lua_State* L, int idx)
{
    idx = lua_absindex(L, idx);
    int len = (int)lua_objlen(L, idx);

    lua_pushnil(L);
    while (lua_next(L, idx))
    {
        if (lua_type(L, -2) == LUA_TNUMBER || lua_tointeger(L, -2) < 1 || lua_tointeger(L, -2) > len)
        {
            lua_pop(L, 2);
            return false;
        }
        lua_pop(L, 1);
    }
    return true;
}

static toml::value<std::string> encode_string(lua_State* L, int idx)
{
    return toml::value<std::string>(lua_tostring(L, idx));
}

static toml::value<double> encode_number(lua_State* L, int idx)
{
    return toml::value<double>(lua_tonumber(L, idx));
}

static toml::value<bool> encode_boolean(lua_State* L, int idx)
{
    return toml::value<bool>(lua_toboolean(L, idx));
}

static toml::array encode_lua_array(lua_State* L, int idx, std::unordered_set<const void*>& seen)
{
    toml::array arr;
    int len = (int)lua_objlen(L, idx);
    for (int i = 1; i <= len; ++i)
    {
        lua_rawgeti(L, idx, i);
        switch (lua_type(L, -1))
        {
        case LUA_TSTRING:
            arr.push_back(encode_string(L, -1));
            break;
        case LUA_TNUMBER:
            arr.push_back(encode_number(L, -1));
            break;
        case LUA_TBOOLEAN:
            arr.push_back(encode_boolean(L, -1));
            break;
        case LUA_TTABLE:
            if (is_array(L, -1))
                arr.push_back(encode_lua_array(L, -1, seen));
            else
                arr.push_back(encode_lua_table(L, -1, seen));
            break;
        default:
            luaL_error(L, "unsupported lua type for toml array");
        }
        lua_pop(L, 1);
    }
    return arr;
}

static toml::table encode_lua_table(lua_State* L, int idx, std::unordered_set<const void*>& seen)
{
    toml::table tbl;
    idx = lua_absindex(L, idx);
    const void* ptr = lua_topointer(L, idx);
    if (seen.count(ptr))
        luaL_error(L, "cyclic tables not allowed in toml");
    seen.insert(ptr);

    lua_pushnil(L);
    while (lua_next(L, idx))
    {
        if (!lua_isstring(L, -2))
            luaL_error(L, "toml keys must be strings");
        std::string key = lua_tostring(L, -2);

        switch (lua_type(L, -1))
        {
        case LUA_TSTRING:
            tbl.insert(key, encode_string(L, -1));
            break;
        case LUA_TNUMBER:
            tbl.insert(key, encode_number(L, -1));
            break;
        case LUA_TBOOLEAN:
            tbl.insert(key, encode_boolean(L, -1));
            break;
        case LUA_TTABLE:
            if (is_array(L, -1))
                tbl.insert(key, encode_lua_array(L, -1, seen));
            else
                tbl.insert(key, encode_lua_table(L, -1, seen));
            break;
        default:
            luaL_error(L, "unsupported lua type for toml table");
        }

        lua_pop(L, 1);
    }

    seen.erase(ptr);
    return tbl;
}

static int tomlserialize(lua_State* L)
{
    luaL_checktype(L, 1, LUA_TTABLE);

    std::unordered_set<const void*> seen;
    toml::table tbl = encode_lua_table(L, 1, seen);

    std::ostringstream oss;
    oss << tbl; // streams TOML text

    std::string out = oss.str();
    lua_pushlstring(L, out.c_str(), out.size());
    return 1;
}

static const luaL_Reg tomllib[] = {
    {"serialize", tomlserialize},
    {"deserialize", tomldeserialize},
    {NULL, NULL},
};

int luaopen_toml(lua_State* L)
{
    luaL_register(L, LUA_TOMLLIBNAME, tomllib);
    return 1;
}
