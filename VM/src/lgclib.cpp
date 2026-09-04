// This file is part of the Luau programming language and is licensed under MIT License; see LICENSE.txt for details
// This code is based on Lua 5.x implementation licensed under MIT License; see lua_LICENSE.txt for details
#include "lualib.h"
#include "lgc.h"

#include "lvm.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

LUAU_FASTFLAGVARIABLE(LuauGcLibrary)

static int gc_stop(lua_State* L)
{
    lua_gc(L, LUA_GCSTOP, 0);
    return 0;
}

static int gc_restart(lua_State* L)
{
    lua_gc(L, LUA_GCRESTART, 0);
    return 0;
}

static int gc_collect(lua_State* L)
{
    lua_gc(L, LUA_GCCOLLECT, 0);
    return 0;
}

static int gc_count(lua_State* L)
{
    int kb = lua_gc(L, LUA_GCCOUNT, 0);
    int b = lua_gc(L, LUA_GCCOUNTB, 0);
    lua_pushinteger(L, kb * 1024 + b);
    return 1;
}

static int gc_isrunning(lua_State* L)
{
    int isrunning = lua_gc(L, LUA_GCISRUNNING, 0);
    lua_pushboolean(L, isrunning);
    return 1;
}

static int gc_step(lua_State* L)
{
    int step_size = luaL_checkinteger(L, 1);
    lua_gc(L, LUA_GCSTEP, step_size);
    return 0;
}

static int gc_setgoal(lua_State* L)
{
    int goal = luaL_checkinteger(L, 1);
    lua_gc(L, LUA_GCSETGOAL, goal);
    return 0;
}

static int gc_setstepmul(lua_State* L)
{
    int stepmul = luaL_checkinteger(L, 1);
    lua_gc(L, LUA_GCSETSTEPMUL, stepmul);
    return 0;
}

static int gc_setstepsize(lua_State* L)
{
    int stepsize = luaL_checkinteger(L, 1);
    lua_gc(L, LUA_GCSETSTEPSIZE, stepsize);
    return 0;
}

static int gc_ispaused(lua_State* L)
{
    int ispaused = lua_gc(L, LUA_GCISPAUSED, 0);
    lua_pushboolean(L, ispaused);
    return 1;
}

static int gc_stats(lua_State* L)
{
    global_State* g = L->global;
    GCStats* stats = &g->gcstats;

    lua_newtable(L);

    lua_pushinteger(L, stats->triggertermpos);
    lua_setfield(L, -2, "triggertermpos");

    lua_pushinteger(L, stats->triggerintegral);
    lua_setfield(L, -2, "triggerintegral");

    lua_pushinteger(L, stats->atomicstarttotalsizebytes);
    lua_setfield(L, -2, "atomicstarttotalsizebytes");

    lua_pushinteger(L, stats->endtotalsizebytes);
    lua_setfield(L, -2, "endtotalsizebytes");

    lua_pushinteger(L, stats->heapgoalsizebytes);
    lua_setfield(L, -2, "heapgoalsizebytes");

    lua_pushnumber(L, stats->starttimestamp);
    lua_setfield(L, -2, "starttimestamp");

    lua_pushnumber(L, stats->atomicstarttimestamp);
    lua_setfield(L, -2, "atomicstarttimestamp");

    lua_pushnumber(L, stats->endtimestamp);
    lua_setfield(L, -2, "endtimestamp");

    return 1;
}

#ifdef LUAI_GCMETRICS
static int gc_metrics(lua_State* L)
{
    global_State* g = L->global;
    GCMetrics* metrics = &g->gcmetrics;

    lua_newtable(L);

    lua_pushnumber(L, metrics->stepexplicittimeacc);
    lua_setfield(L, -2, "stepexplicittimeacc");

    lua_pushnumber(L, metrics->stepassisttimeacc);
    lua_setfield(L, -2, "stepassisttimeacc");

    lua_pushinteger(L, metrics->completedcycles);
    lua_setfield(L, -2, "completedcycles");

    {
        lua_newtable(L);

        lua_pushinteger(L, metrics->lastcycle.starttotalsizebytes);
        lua_setfield(L, -2, "starttotalsizebytes");

        lua_pushinteger(L, metrics->lastcycle.heaptriggersizebytes);
        lua_setfield(L, -2, "heaptriggersizebytes");

        lua_pushnumber(L, metrics->lastcycle.pausetime);
        lua_setfield(L, -2, "pausetime");

        lua_pushnumber(L, metrics->lastcycle.starttimestamp);
        lua_setfield(L, -2, "starttimestamp");

        lua_pushnumber(L, metrics->lastcycle.endtimestamp);
        lua_setfield(L, -2, "endtimestamp");

        lua_pushnumber(L, metrics->lastcycle.marktime);
        lua_setfield(L, -2, "marktime");

        lua_pushnumber(L, metrics->lastcycle.markassisttime);
        lua_setfield(L, -2, "markassisttime");

        lua_pushnumber(L, metrics->lastcycle.markmaxexplicittime);
        lua_setfield(L, -2, "markmaxexplicittime");

        lua_pushinteger(L, metrics->lastcycle.markexplicitsteps);
        lua_setfield(L, -2, "markexplicitsteps");

        lua_pushinteger(L, metrics->lastcycle.markwork);
        lua_setfield(L, -2, "markwork");

        lua_pushnumber(L, metrics->lastcycle.atomictime);
        lua_setfield(L, -2, "atomictime");

        lua_pushnumber(L, metrics->lastcycle.sweeptime);
        lua_setfield(L, -2, "sweeptime");

        lua_pushnumber(L, metrics->lastcycle.sweepassisttime);
        lua_setfield(L, -2, "sweepassisttime");

        lua_pushnumber(L, metrics->lastcycle.sweepmaxexplicittime);
        lua_setfield(L, -2, "sweepmaxexplicittime");

        lua_pushinteger(L, metrics->lastcycle.sweepexplicitsteps);
        lua_setfield(L, -2, "sweepexplicitsteps");

        lua_pushinteger(L, metrics->lastcycle.sweepwork);
        lua_setfield(L, -2, "sweepwork");

        lua_pushinteger(L, metrics->lastcycle.assistwork);
        lua_setfield(L, -2, "assistwork");

        lua_pushinteger(L, metrics->lastcycle.explicitwork);
        lua_setfield(L, -2, "explicitwork");

        lua_pushinteger(L, metrics->lastcycle.propagatework);
        lua_setfield(L, -2, "propagatework");

        lua_pushinteger(L, metrics->lastcycle.propagateagainwork);
        lua_setfield(L, -2, "propagateagainwork");

        lua_pushinteger(L, metrics->lastcycle.endtotalsizebytes);
        lua_setfield(L, -2, "endtotalsizebytes");

        lua_setfield(L, -2, "lastcycle");
    }

    {
        lua_newtable(L);

        lua_pushinteger(L, metrics->currcycle.starttotalsizebytes);
        lua_setfield(L, -2, "starttotalsizebytes");

        lua_pushinteger(L, metrics->currcycle.heaptriggersizebytes);
        lua_setfield(L, -2, "heaptriggersizebytes");

        lua_pushnumber(L, metrics->currcycle.pausetime);
        lua_setfield(L, -2, "pausetime");

        lua_pushnumber(L, metrics->currcycle.starttimestamp);
        lua_setfield(L, -2, "starttimestamp");

        lua_pushnumber(L, metrics->currcycle.endtimestamp);
        lua_setfield(L, -2, "endtimestamp");

        lua_pushnumber(L, metrics->currcycle.marktime);
        lua_setfield(L, -2, "marktime");

        lua_pushnumber(L, metrics->currcycle.markassisttime);
        lua_setfield(L, -2, "markassisttime");

        lua_pushnumber(L, metrics->currcycle.markmaxexplicittime);
        lua_setfield(L, -2, "markmaxexplicittime");

        lua_pushinteger(L, metrics->currcycle.markexplicitsteps);
        lua_setfield(L, -2, "markexplicitsteps");

        lua_pushinteger(L, metrics->currcycle.markwork);
        lua_setfield(L, -2, "markwork");

        lua_pushnumber(L, metrics->currcycle.atomictime);
        lua_setfield(L, -2, "atomictime");

        lua_pushnumber(L, metrics->currcycle.sweeptime);
        lua_setfield(L, -2, "sweeptime");

        lua_pushnumber(L, metrics->currcycle.sweepassisttime);
        lua_setfield(L, -2, "sweepassisttime");

        lua_pushnumber(L, metrics->currcycle.sweepmaxexplicittime);
        lua_setfield(L, -2, "sweepmaxexplicittime");

        lua_pushinteger(L, metrics->currcycle.sweepexplicitsteps);
        lua_setfield(L, -2, "sweepexplicitsteps");

        lua_pushinteger(L, metrics->currcycle.sweepwork);
        lua_setfield(L, -2, "sweepwork");

        lua_pushinteger(L, metrics->currcycle.assistwork);
        lua_setfield(L, -2, "assistwork");

        lua_pushinteger(L, metrics->currcycle.explicitwork);
        lua_setfield(L, -2, "explicitwork");

        lua_pushinteger(L, metrics->currcycle.propagatework);
        lua_setfield(L, -2, "propagatework");

        lua_pushinteger(L, metrics->currcycle.propagateagainwork);
        lua_setfield(L, -2, "propagateagainwork");

        lua_pushinteger(L, metrics->currcycle.endtotalsizebytes);
        lua_setfield(L, -2, "endtotalsizebytes");

        lua_setfield(L, -2, "currcycle");
    }

    return 1;
};
#endif

static const luaL_Reg gclib[] = {
    {"stop", gc_stop},
    {"restart", gc_restart},
    {"collect", gc_collect},
    {"count", gc_count},
    {"isrunning", gc_isrunning},
    {"step", gc_step},
    {"setgoal", gc_setgoal},
    {"setstepmul", gc_setstepmul},
    {"setstepsize", gc_setstepsize},
    {"ispaused", gc_ispaused},

    {"stats", gc_stats},

#ifdef LUAI_GCMETRICS
    {"metrics", gc_metrics},
#endif

    {NULL, NULL},
};

int luaopen_gc(lua_State* L)
{
    luaL_register(L, LUA_GCLIBNAME, gclib);
    return 1;
}
