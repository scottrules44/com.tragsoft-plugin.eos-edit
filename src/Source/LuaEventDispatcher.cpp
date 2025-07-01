// ----------------------------------------------------------------------------
// 
// LuaEventDispatcher.cpp
// Copyright (c) 2016 Corona Labs Inc. All rights reserved.
// This software may be modified and distributed under the terms
// of the MIT license.  See the LICENSE file for details.
//
// ----------------------------------------------------------------------------

#include "LuaEventDispatcher.h"
#include "CoronaLua.h"
#include <exception>
extern "C"
{
#	include "lua.h"
#	include "lauxlib.h"
}


LuaEventDispatcher::LuaEventDispatcher(const LuaEventDispatcher&)
{
	// Copy constructor not supported.
	throw std::exception();
}

LuaEventDispatcher::LuaEventDispatcher(lua_State* luaStatePointer)
:	fLuaStatePointer(luaStatePointer),
	fLuaRegistryReferenceId(LUA_NOREF)
{
	// Validate.
	if (!luaStatePointer)
	{
		return;
	}

	// If the given Lua state belongs to a coroutine, then use the main Lua state instead.
	// Note: Not sure if this is the right move, but it does match Corona's core Runtime event behavior.
	lua_State* mainLuaStatePointer = CoronaLuaGetCoronaThread(luaStatePointer);
	if (mainLuaStatePointer && (mainLuaStatePointer != luaStatePointer))
	{
		luaStatePointer = mainLuaStatePointer;
		fLuaStatePointer = mainLuaStatePointer;
	}

	// Create a new Corona "EventDispatcher" by calling the Lua system.newEventDispatcher() function.
	// Store it under the Lua registry to prevent the object from being garbage collected.
	int luaStackCount = lua_gettop(luaStatePointer);
	lua_getglobal(luaStatePointer, "system");
	if (lua_istable(luaStatePointer, -1))
	{
		lua_getfield(luaStatePointer, -1, "newEventDispatcher");
		if (lua_isfunction(luaStatePointer, -1))
		{
			CoronaLuaDoCall(luaStatePointer, 0, 1);
			if (lua_istable(luaStatePointer, -1))
			{
				fLuaRegistryReferenceId = luaL_ref(luaStatePointer, LUA_REGISTRYINDEX);
			}
		}
	}
	lua_settop(luaStatePointer, luaStackCount);
}

LuaEventDispatcher::~LuaEventDispatcher()
{
	// Release our reference to the Lua EventDispatcher created by this class' constructor.
	if (fLuaStatePointer && (fLuaRegistryReferenceId != LUA_NOREF))
	{
		luaL_unref(fLuaStatePointer, LUA_REGISTRYINDEX, fLuaRegistryReferenceId);
	}
}

lua_State* LuaEventDispatcher::GetLuaState() const
{
	return fLuaStatePointer;
}

bool LuaEventDispatcher::AddEventListener(
	lua_State* luaStatePointer, const char* eventName, int luaListenerStackIndex)
{
	// Validate arguments.
	if (!luaStatePointer || !eventName || !luaListenerStackIndex)
	{
		return false;
	}

	// Do not continue if we've failed to create an EventDispatcher object in Lua.
	if (LUA_NOREF == fLuaRegistryReferenceId)
	{
		return false;
	}

	// Do not continue if the indexed Lua object on the stack is not a Lua listener.
	if (!CoronaLuaIsListener(luaStatePointer, luaListenerStackIndex, eventName))
	{
		return false;
	}

	// Convert the given Lua stack index from a relative index to an absolute index.
	if ((luaListenerStackIndex < 0) && (luaListenerStackIndex > LUA_REGISTRYINDEX))
	{
		luaListenerStackIndex += lua_gettop(luaStatePointer) + 1;
	}

	// Add the given Lua listener to this object's EventDispatcher.
	lua_rawgeti(luaStatePointer, LUA_REGISTRYINDEX, fLuaRegistryReferenceId);
	if (!lua_istable(luaStatePointer, -1))
	{
		lua_pop(luaStatePointer, 1);
		return false;
	}
	lua_getfield(luaStatePointer, -1, "addEventListener");
	if (!lua_isfunction(luaStatePointer, -1))
	{
		lua_pop(luaStatePointer, 2);
		return false;
	}
	lua_insert(luaStatePointer, -2);
	lua_pushstring(luaStatePointer, eventName);
	lua_pushvalue(luaStatePointer, luaListenerStackIndex);
	CoronaLuaDoCall(luaStatePointer, 3, 0);
	return true;
}

bool LuaEventDispatcher::RemoveEventListener(
	lua_State* luaStatePointer, const char* eventName, int luaListenerStackIndex)
{
	// Validate arguments.
	if (!luaStatePointer || !eventName || !luaListenerStackIndex)
	{
		return false;
	}

	// Do not continue if we've failed to create an EventDispatcher object in Lua.
	if (LUA_NOREF == fLuaRegistryReferenceId)
	{
		return false;
	}

	// Do not continue if the indexed Lua object on the stack is not a Lua listener.
	if (!CoronaLuaIsListener(luaStatePointer, luaListenerStackIndex, eventName))
	{
		return false;
	}

	// Convert the given Lua stack index from a relative index to an absolute index.
	if ((luaListenerStackIndex < 0) && (luaListenerStackIndex > LUA_REGISTRYINDEX))
	{
		luaListenerStackIndex += lua_gettop(luaStatePointer) + 1;
	}

	// Remove the given Lua listener from this object's EventDispatcher.
	lua_rawgeti(luaStatePointer, LUA_REGISTRYINDEX, fLuaRegistryReferenceId);
	if (!lua_istable(luaStatePointer, -1))
	{
		lua_pop(luaStatePointer, 1);
		return false;
	}
	lua_getfield(luaStatePointer, -1, "removeEventListener");
	if (!lua_isfunction(luaStatePointer, -1))
	{
		lua_pop(luaStatePointer, 2);
		return false;
	}
	lua_insert(luaStatePointer, -2);
	lua_pushstring(luaStatePointer, eventName);
	lua_pushvalue(luaStatePointer, luaListenerStackIndex);
	CoronaLuaDoCall(luaStatePointer, 3, 0);
	return true;
}

bool LuaEventDispatcher::DispatchEventWithResult(lua_State* luaStatePointer, int luaEventTableStackIndex)
{
	// Validate arguments.
	if (!fLuaStatePointer || !luaEventTableStackIndex)
	{
		return false;
	}

	// Do not continue if we've failed to create an EventDispatcher object in Lua.
	if (LUA_NOREF == fLuaRegistryReferenceId)
	{
		return false;
	}

	// Convert the given Lua stack index from a relative index to an absolute index.
	if ((luaEventTableStackIndex < 0) && (luaEventTableStackIndex > LUA_REGISTRYINDEX))
	{
		luaEventTableStackIndex += lua_gettop(luaStatePointer) + 1;
	}

	// Dispatch the given Lua event table.
	// Note: This does not pop the given event table off of the stack.
	lua_rawgeti(fLuaStatePointer, LUA_REGISTRYINDEX, fLuaRegistryReferenceId);
	if (!lua_istable(fLuaStatePointer, -1))
	{
		lua_pop(fLuaStatePointer, 1);
		return false;
	}
	lua_getfield(fLuaStatePointer, -1, "dispatchEvent");
	if (!lua_isfunction(fLuaStatePointer, -1))
	{
		lua_pop(fLuaStatePointer, 2);
		return false;
	}
	lua_insert(fLuaStatePointer, -2);
	lua_pushvalue(luaStatePointer, luaEventTableStackIndex);
	CoronaLuaDoCall(fLuaStatePointer, 2, 1);
	return true;
}

bool LuaEventDispatcher::DispatchEventWithoutResult(lua_State* luaStatePointer, int luaEventTableStackIndex)
{
	// Dispatch the given event table to Lua with a returned result pushed to the top of the stack.
	bool wasDispatched = DispatchEventWithResult(luaStatePointer, luaEventTableStackIndex);

	// Pop off the result from the Lua stack.
	if (wasDispatched)
	{
		lua_pop(luaStatePointer, 1);
	}

	// Returns true if given event was successfully dispatched to Lua.
	return wasDispatched;
}
