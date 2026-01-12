#include "lualib.h"
#include "lstate.h"

#include <yaml-cpp/yaml.h>
#include <unordered_set>
#include <string>
#include <sstream>
#include <cmath>


static bool is_array(lua_State* L, int index, int& len)
{
    index = lua_absindex(L, index);
    len = (int)lua_objlen(L, index);

    lua_pushnil(L);
    if (lua_next(L, index) == 0)
        return true; // empty map
    lua_pop(L, 2);

    // Verify no holes in 1..len
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

    // Verify all keys are integers in [1..len]
    lua_pushnil(L);
    while (lua_next(L, index) != 0)
    {
        if (lua_type(L, -2) != LUA_TNUMBER)
        {
            lua_pop(L, 2);
            return false;
        }
        lua_Number kn = lua_tonumber(L, -2);
        lua_Integer k = (lua_Integer)kn;
        if ((lua_Number)k != kn)
        {
            lua_pop(L, 2);
            return false;
        }

        if (k < 1 || k > len)
        {
            lua_pop(L, 2);
            return false;
        }

        lua_pop(L, 1); // pop value, keep key
    }

    return true;
}

static void push_yaml_node(lua_State* L, const YAML::Node& n);

static void push_yaml_seq(lua_State* L, const YAML::Node& n)
{
    const int count = (int)n.size();
    lua_createtable(L, count, 0);
    for (int i = 0; i < count; ++i)
    {
        luaL_checkstack(L, 6, "yaml seq decode");
        push_yaml_node(L, n[i]);
        lua_rawseti(L, -2, i + 1);
    }
}

static void push_yaml_map(lua_State* L, const YAML::Node& n)
{
    lua_createtable(L, 0, (int)n.size());
    for (auto it = n.begin(); it != n.end(); ++it)
    {
        luaL_checkstack(L, 8, "yaml map decode");

        const YAML::Node k = it->first;
        const YAML::Node v = it->second;

        // Default policy: string keys only
        if (!k.IsScalar())
            luaL_error(L, "YAML mapping keys must be scalars (string/number/bool); got non-scalar key");

        std::string key = k.Scalar();
        lua_pushlstring(L, key.data(), key.size());
        push_yaml_node(L, v);
        lua_settable(L, -3);
    }
}

static bool parse_number(const std::string& s, double& out)
{
    // very small “best effort” number parse; yaml-cpp does some tag typing but not always
    char* end = nullptr;
    out = std::strtod(s.c_str(), &end);
    return end && *end == '\0' && end != s.c_str();
}

static void push_yaml_node(lua_State* L, const YAML::Node& n)
{
    luaL_checkstack(L, 8, "yaml node decode");

    if (!n || n.IsNull())
    {
        lua_pushnil(L);
        return;
    }

    if (n.IsSequence())
    {
        push_yaml_seq(L, n);
        return;
    }

    if (n.IsMap())
    {
        push_yaml_map(L, n);
        return;
    }

    // Scalar: try bool, number, fallback string
    // YAML has many boolean spellings; handle common ones
    std::string s = n.Scalar();

    if (s == "true" || s == "True" || s == "TRUE")
    {
        lua_pushboolean(L, 1);
        return;
    }
    if (s == "false" || s == "False" || s == "FALSE")
    {
        lua_pushboolean(L, 0);
        return;
    }

    double num = 0.0;
    if (parse_number(s, num))
    {
        lua_pushnumber(L, (lua_Number)num);
        return;
    }

    lua_pushlstring(L, s.data(), s.size());
}


static YAML::Node encode_lua_value(lua_State* L, int index, std::unordered_set<const void*>& seen);

static YAML::Node encode_lua_table(lua_State* L, int index, std::unordered_set<const void*>& seen)
{
    index = lua_absindex(L, index);
    luaL_checkstack(L, 12, "yaml table encode");

    const void* ptr = lua_topointer(L, index);
    if (seen.count(ptr))
        luaL_error(L, "cannot serialize cyclic table to yaml");
    seen.insert(ptr);

    int len = 0;
    if (is_array(L, index, len))
    {
        YAML::Node seq(YAML::NodeType::Sequence);
        for (int i = 1; i <= len; ++i)
        {
            lua_rawgeti(L, index, i);
            seq.push_back(encode_lua_value(L, -1, seen));
            lua_pop(L, 1);
        }
        seen.erase(ptr);
        return seq;
    }

    YAML::Node map(YAML::NodeType::Map);
    lua_pushnil(L);
    while (lua_next(L, index) != 0)
    {
        luaL_checkstack(L, 12, "yaml map encode");

        // key at -2, value at -1
        if (!lua_isstring(L, -2))
            luaL_error(L, "yaml mapping keys must be strings");

        // YAML has no explicit nil value in mappings in a way that round-trips to Lua well.
        // Policy: omit nil values
        if (lua_type(L, -1) == LUA_TNIL)
        {
            lua_pop(L, 1);
            continue;
        }

        std::string key = lua_tostring(L, -2);
        map[key] = encode_lua_value(L, -1, seen);

        lua_pop(L, 1); // pop value
    }

    seen.erase(ptr);
    return map;
}

static YAML::Node encode_lua_value(lua_State* L, int index, std::unordered_set<const void*>& seen)
{
    index = lua_absindex(L, index);
    luaL_checkstack(L, 12, "yaml value encode");

    switch (lua_type(L, index))
    {
    case LUA_TNIL:
        // when nested in sequence, represent nil as YAML null
        return YAML::Node();

    case LUA_TBOOLEAN:
        return YAML::Node((bool)lua_toboolean(L, index));

    case LUA_TNUMBER:
        return YAML::Node((double)lua_tonumber(L, index));

    case LUA_TSTRING:
        return YAML::Node(std::string(lua_tostring(L, index)));

    case LUA_TTABLE:
        return encode_lua_table(L, index, seen);

    default:
        luaL_error(L, "unsupported Lua type for yaml serialization");
        return YAML::Node();
    }
}

static int yamldeserialize(lua_State* L)
{
    size_t len;
    const char* s = luaL_checklstring(L, 1, &len);
    if (len == 0)
    {
        luaL_error(L, "cannot deserialize empty string");
        return 0;
    }

    YAML::Node doc;
    try
    {
        doc = YAML::Load(std::string(s, len));
    }
    catch (const std::exception& e)
    {
        luaL_error(L, "%s", e.what());
        return 0;
    }

    push_yaml_node(L, doc);
    return 1;
}

static int yamlserialize(lua_State* L)
{
    luaL_checktype(L, 1, LUA_TTABLE);

    std::unordered_set<const void*> seen;
    YAML::Node node = encode_lua_value(L, 1, seen);

    YAML::Emitter out;
    out << node;

    if (!out.good())
    {
        luaL_error(L, "yaml emit failed");
        return 0;
    }

    const std::string str(out.c_str(), out.size());
    lua_pushlstring(L, str.data(), str.size());
    return 1;
}

static const luaL_Reg yamllib[] = {
    {"serialize", yamlserialize},
    {"deserialize", yamldeserialize},
    {NULL, NULL},
};

int luaopen_yaml(lua_State* L)
{
    luaL_register(L, LUA_YAMLLIBNAME, yamllib);
    return 1;
}
