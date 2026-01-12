// This file is part of the Luau programming language and is licensed under MIT License; see LICENSE.txt for details
// This code is based on Lua 5.x implementation licensed under MIT License; see lua_LICENSE.txt for details
#include "lualib.h"
#include "lstate.h"

#include "json.hpp"
#include <unordered_set>

using json = nlohmann::json;

class lua_sax : public nlohmann::json_sax<json>
{
public:
    struct Frame
    {
        bool isArray;
        int index; // only for arrays
    };


    lua_State* L = nullptr;
    std::vector<Frame> stack;

    // Insert value into current container
    void insert_value()
    {
        if (stack.empty())
            return; // root value stays on stack

        Frame& f = stack.back();

        if (f.isArray)
        {
            lua_rawseti(L, -2, ++f.index);
        }
        else
        {
            lua_settable(L, -3);
        }
    }

    bool start_object(std::size_t) override
    {
        lua_createtable(L, 0, 0);
        stack.push_back({false, 0});
        return true;
    }

    bool end_object() override
    {
        stack.pop_back();
        insert_value();
        return true;
    }

    bool start_array(std::size_t) override
    {
        lua_createtable(L, 0, 0);
        stack.push_back({true, 0});
        return true;
    }

    bool end_array() override
    {
        stack.pop_back();
        insert_value();
        return true;
    }

    bool key(string_t& key) override
    {
        lua_pushlstring(L, key.data(), key.size());
        return true;
    }

    bool null() override
    {
        lua_pushnil(L);
        insert_value();
        return true;
    }

    bool string(string_t& val) override
    {
        lua_pushlstring(L, val.data(), val.size());
        insert_value();
        return true;
    }

    bool boolean(bool val) override
    {
        lua_pushboolean(L, val);
        insert_value();
        return true;
    }

    bool number_integer(number_integer_t val) override
    {
        lua_pushinteger(L, val);
        insert_value();
        return true;
    }

    bool number_unsigned(number_unsigned_t val) override
    {
        lua_pushinteger(L, val);
        insert_value();
        return true;
    }

    bool number_float(number_float_t val, const string_t&) override
    {
        lua_pushnumber(L, val);
        insert_value();
        return true;
    }

    bool binary(binary_t&) override
    {
        luaL_error(L, "binary json values are not supported");
        return false;
    }

    bool parse_error(std::size_t, const std::string&, const nlohmann::detail::exception& ex) override
    {
        luaL_error(L, "%s", ex.what());
        return false;
    }
};

static bool is_array(lua_State* L, int index, int& len)
{
    index = lua_absindex(L, index);
    len = lua_objlen(L, index);

    if (len == 0)
    {
        // check if table has *any* keys
        lua_pushnil(L);
        if (!lua_next(L, index))
            return false; // empty object
        lua_pop(L, 2);
    }


    // Check array part 1..len
    for (int i = 1; i <= len; ++i)
    {
        lua_rawgeti(L, index, i);
        if (lua_isnil(L, -1))
        {
            lua_pop(L, 1);
            return false;
        }
        lua_pop(L, 1);
    }

    // Check for non-array keys
    lua_pushnil(L);
    while (lua_next(L, index))
    {
        // key at -2, value at -1
        if (lua_type(L, -2) != LUA_TNUMBER)
        {
            lua_pop(L, 2);
            return false;
        }

        lua_Integer k = lua_tointeger(L, -2);
        if (k < 1 || k > len)
        {
            lua_pop(L, 2);
            return false;
        }

        lua_pop(L, 1);
    }

    return true;
}



static json encode_value(lua_State* L, int index, std::unordered_set<const void*>& seen);

static json encode_table(lua_State* L, int index, std::unordered_set<const void*>& seen)
{
    index = lua_absindex(L, index);

    const void* ptr = lua_topointer(L, index);
    if (seen.count(ptr))
        luaL_error(L, "cannot serialize cyclic table");

    seen.insert(ptr);

    int len = 0;
    if (is_array(L, index, len))
    {
        json arr = json::array();
        for (int i = 1; i <= len; ++i)
        {
            lua_rawgeti(L, index, i);
            arr.push_back(encode_value(L, -1, seen));
            lua_pop(L, 1);
        }
        seen.erase(ptr);
        return arr;
    }

    json obj = json::object();
    lua_pushnil(L);
    while (lua_next(L, index))
    {
        if (!lua_isstring(L, -2))
            luaL_error(L, "json object keys must be strings");

        obj[lua_tostring(L, -2)] = encode_value(L, -1, seen);
        lua_pop(L, 1);
    }

    seen.erase(ptr);
    return obj;
}

static json encode_value(lua_State* L, int index, std::unordered_set<const void*>& seen)
{
    switch (lua_type(L, index))
    {
    case LUA_TNIL:
        return nullptr;

    case LUA_TBOOLEAN:
        return (bool)lua_toboolean(L, index);

    case LUA_TNUMBER:
        return lua_tonumber(L, index);

    case LUA_TSTRING:
        return std::string(lua_tostring(L, index));

    case LUA_TTABLE:
        return encode_table(L, index, seen);

    default:
        luaL_error(L, "unsupported lua type for json serialization");
        return nullptr;
    }
}

static int jsonserialize(lua_State* L)
{
    luaL_checktype(L, 1, LUA_TTABLE);

    std::unordered_set<const void*> seen;
    json j = encode_value(L, 1, seen);

    std::string out = j.dump();
    lua_pushlstring(L, out.c_str(), out.size());
    return 1;
}

static int jsondeserialize(lua_State* L)
{
    size_t len;
    const char* s = luaL_checklstring(L, 1, &len);

    if (len == 0)
    {
        luaL_error(L, "cannot deserialize empty string");
        return 0;
    }

    lua_sax sax;
    sax.L = L;

    bool ok = json::sax_parse(s, s + len, &sax);
    if (!ok)
    {
        luaL_error(L, "json parse failed");
        return 0;
    }

    return 1;
}

static const luaL_Reg jsonlib[] = {
    {"serialize", jsonserialize},
    {"deserialize", jsondeserialize},
    {NULL, NULL},
};

int luaopen_json(lua_State* L)
{
    luaL_register(L, LUA_JSONLIBNAME, jsonlib);
    return 1;
}
