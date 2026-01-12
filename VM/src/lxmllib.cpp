#include "lualib.h"
#include "lstate.h"
#include "tinyxml2.h"

#include <string>
#include <sstream>
#include <unordered_set>

static void push_xml_element(lua_State* L, const tinyxml2::XMLElement* el);

static void push_attrs(lua_State* L, const tinyxml2::XMLElement* el)
{
    lua_newtable(L); // attr
    for (const tinyxml2::XMLAttribute* a = el->FirstAttribute(); a; a = a->Next())
    {
        lua_pushstring(L, a->Name());
        lua_pushstring(L, a->Value());
        lua_settable(L, -3);
    }
}

static void push_children(lua_State* L, const tinyxml2::XMLElement* el)
{
    lua_newtable(L); // children
    int i = 1;
    for (const tinyxml2::XMLElement* ch = el->FirstChildElement(); ch; ch = ch->NextSiblingElement())
    {
        luaL_checkstack(L, 12, "xml decode children");
        push_xml_element(L, ch);
        lua_rawseti(L, -2, i++);
    }
}

static void push_xml_element(lua_State* L, const tinyxml2::XMLElement* el)
{
    luaL_checkstack(L, 16, "xml decode element");

    lua_newtable(L);

    // tag
    lua_pushliteral(L, "tag");
    lua_pushstring(L, el->Name());
    lua_settable(L, -3);

    // attr
    lua_pushliteral(L, "attr");
    push_attrs(L, el);
    lua_settable(L, -3);

    // text (direct element text)
    const char* txt = el->GetText();
    if (txt && *txt)
    {
        lua_pushliteral(L, "text");
        lua_pushstring(L, txt);
        lua_settable(L, -3);
    }

    // children
    lua_pushliteral(L, "children");
    push_children(L, el);
    lua_settable(L, -3);
}

static int xmldeserialize(lua_State* L)
{
    size_t len;
    const char* s = luaL_checklstring(L, 1, &len);
    if (len == 0)
    {
        luaL_error(L, "cannot deserialize empty string");
        return 0;
    }

    tinyxml2::XMLDocument doc;
    tinyxml2::XMLError err = doc.Parse(s, len);
    if (err != tinyxml2::XML_SUCCESS)
    {
        luaL_error(L, "xml parse error: %s", doc.ErrorStr());
        return 0;
    }

    const tinyxml2::XMLElement* root = doc.RootElement();
    if (!root)
    {
        luaL_error(L, "xml has no root element");
        return 0;
    }

    push_xml_element(L, root);
    return 1;
}

static void xml_escape_append(std::string& out, const char* s)
{
    for (const unsigned char* p = (const unsigned char*)s; *p; ++p)
    {
        switch (*p)
        {
        case '&':
            out += "&amp;";
            break;
        case '<':
            out += "&lt;";
            break;
        case '>':
            out += "&gt;";
            break;
        case '"':
            out += "&quot;";
            break;
        case '\'':
            out += "&apos;";
            break;
        default:
            out += (char)*p;
            break;
        }
    }
}

static void emit_element(lua_State* L, int idx, std::unordered_set<const void*>& seen, std::string& out, int depth);

static void emit_indent(std::string& out, int depth)
{
    for (int i = 0; i < depth; ++i)
        out += '\t';
}

static const char* get_field_string(lua_State* L, int obj, const char* key, size_t* outLen)
{
    obj = lua_absindex(L, obj);
    lua_pushstring(L, key);
    lua_gettable(L, obj);
    if (lua_isnil(L, -1))
    {
        lua_pop(L, 1);
        return nullptr;
    }
    if (!lua_isstring(L, -1))
        luaL_error(L, "xml.%s must be a string", key);

    const char* s = lua_tolstring(L, -1, outLen);
    lua_pop(L, 1);
    return s;
}

static void emit_attributes(lua_State* L, int attrIdx, std::string& out)
{
    attrIdx = lua_absindex(L, attrIdx);

    lua_pushnil(L);
    while (lua_next(L, attrIdx) != 0)
    {
        if (!lua_isstring(L, -2))
            luaL_error(L, "xml.attr keys must be strings");
        if (!lua_isstring(L, -1))
            luaL_error(L, "xml.attr values must be strings");

        const char* k = lua_tostring(L, -2);
        const char* v = lua_tostring(L, -1);

        out += ' ';
        out += k;
        out += "=\"";
        xml_escape_append(out, v);
        out += '"';

        lua_pop(L, 1);
    }
}

static void emit_children(lua_State* L, int childrenIdx, std::unordered_set<const void*>& seen, std::string& out, int depth)
{
    childrenIdx = lua_absindex(L, childrenIdx);
    int n = (int)lua_objlen(L, childrenIdx);

    for (int i = 1; i <= n; ++i)
    {
        luaL_checkstack(L, 16, "xml encode children");
        lua_rawgeti(L, childrenIdx, i);
        if (!lua_istable(L, -1))
            luaL_error(L, "xml.children entries must be tables");
        emit_element(L, -1, seen, out, depth);
        lua_pop(L, 1);
    }
}

static void emit_element(lua_State* L, int idx, std::unordered_set<const void*>& seen, std::string& out, int depth)
{
    luaL_checkstack(L, 24, "xml encode element");
    idx = lua_absindex(L, idx);

    const void* ptr = lua_topointer(L, idx);
    if (seen.count(ptr))
        luaL_error(L, "cannot serialize cyclic table to xml");
    seen.insert(ptr);

    size_t tagLen = 0;
    const char* tag = get_field_string(L, idx, "tag", &tagLen);
    if (!tag || tagLen == 0)
        luaL_error(L, "xml element missing non-empty .tag");

    // attr table
    lua_pushliteral(L, "attr");
    lua_gettable(L, idx);
    if (!lua_isnil(L, -1) && !lua_istable(L, -1))
        luaL_error(L, "xml.attr must be a table or nil");
    int attrIdx = lua_absindex(L, -1);

    // text string
    size_t textLen = 0;
    const char* text = get_field_string(L, idx, "text", &textLen);

    // children table
    lua_pushliteral(L, "children");
    lua_gettable(L, idx);
    if (!lua_isnil(L, -1) && !lua_istable(L, -1))
        luaL_error(L, "xml.children must be a table or nil");
    int childrenIdx = lua_absindex(L, -1);

    // start tag
    emit_indent(out, depth);
    out += '<';
    out += std::string(tag, tagLen);
    if (!lua_isnil(L, attrIdx))
        emit_attributes(L, attrIdx, out);

    bool hasChildren = !lua_isnil(L, childrenIdx) && lua_objlen(L, childrenIdx) > 0;
    bool hasText = text && textLen > 0;

    if (!hasChildren && !hasText)
    {
        out += "/>\n";
    }
    else
    {
        out += '>';

        if (hasText)
        {
            xml_escape_append(out, text);
        }

        if (hasChildren)
        {
            out += '\n';
            emit_children(L, childrenIdx, seen, out, depth + 1);
            emit_indent(out, depth);
        }

        out += "</";
        out += std::string(tag, tagLen);
        out += ">\n";
    }

    // pop children + attr from stack
    lua_pop(L, 2);

    seen.erase(ptr);
}

static int xmlserialize(lua_State* L)
{
    luaL_checktype(L, 1, LUA_TTABLE);

    std::unordered_set<const void*> seen;
    std::string out;
    out.reserve(1024);

    emit_element(L, 1, seen, out, 0);

    lua_pushlstring(L, out.data(), out.size());
    return 1;
}

static const luaL_Reg xmllib[] = {
    {"serialize", xmlserialize},
    {"deserialize", xmldeserialize},
    {NULL, NULL},
};

int luaopen_xml(lua_State* L)
{
    luaL_register(L, LUA_XMLLIBNAME, xmllib);
    return 1;
}
