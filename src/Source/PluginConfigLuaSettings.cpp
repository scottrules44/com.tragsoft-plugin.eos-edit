// ----------------------------------------------------------------------------
// 
// PluginConfigLuaSettings.cpp
// Copyright (c) 2016 Corona Labs Inc. All rights reserved.
// This software may be modified and distributed under the terms
// of the MIT license.  See the LICENSE file for details.
//
// ----------------------------------------------------------------------------

#include "PluginConfigLuaSettings.h"
#include "CoronaLua.h"
#include <sstream>
#include <string>


PluginConfigLuaSettings::PluginConfigLuaSettings()
{
}

PluginConfigLuaSettings::~PluginConfigLuaSettings()
{
}

const char* PluginConfigLuaSettings::GetStringEncryptionKey() const
{
	return fStringEncryptionKey.c_str();
}

void PluginConfigLuaSettings::SetStringEncryptionKey(const char* stringId)
{
	if (stringId)
	{
		fStringEncryptionKey = stringId;
	}
	else
	{
		fStringEncryptionKey.clear();
	}
}

const char* PluginConfigLuaSettings::GetStringAppId() const
{
	return fStringAppId.c_str();
}

void PluginConfigLuaSettings::SetStringAppId(const char* stringId)
{
	if (stringId)
	{
		fStringAppId = stringId;
	}
	else
	{
		fStringAppId.clear();
	}
}


const char* PluginConfigLuaSettings::GetStringProductId() const
{
	return fStringProductId.c_str();
}

void PluginConfigLuaSettings::SetStringProductId(const char* stringId)
{
	if (stringId)
	{
		fStringProductId = stringId;
	}
	else
	{
		fStringProductId.clear();
	}
}

const char* PluginConfigLuaSettings::GetStringSandboxId() const
{
	return fStringSandboxId.c_str();
}

void PluginConfigLuaSettings::SetStringSandboxId(const char* stringId)
{
	if (stringId)
	{
		fStringSandboxId = stringId;
	}
	else
	{
		fStringSandboxId.clear();
	}
}

const char* PluginConfigLuaSettings::GetStringDeploymentId() const
{
	return fStringDeploymentId.c_str();
}

void PluginConfigLuaSettings::SetStringDeploymentId(const char* stringId)
{
	if (stringId)
	{
		fStringDeploymentId = stringId;
	}
	else
	{
		fStringAppId.clear();
	}
}

const char* PluginConfigLuaSettings::GetStringClientId() const
{
	return fStringClientId.c_str();
}

void PluginConfigLuaSettings::SetStringClientId(const char* stringId)
{
	if (stringId)
	{
		fStringClientId = stringId;
	}
	else
	{
		fStringClientId.clear();
	}
}

const char* PluginConfigLuaSettings::GetStringClientSecret() const
{
	return fStringClientSecret.c_str();
}

void PluginConfigLuaSettings::SetStringClientSecret(const char* stringId)
{
	if (stringId)
	{
		fStringClientSecret = stringId;
	}
	else
	{
		fStringClientSecret.clear();
	}
}


const char* PluginConfigLuaSettings::GetStringProductName() const
{
    return fStringClientSecret.c_str();
}

void PluginConfigLuaSettings::SetStringProductName(const char* stringId)
{
    if (stringId)
    {
        fStringProductName = stringId;
    }
    else
    {
        fStringProductName.clear();
    }
}

const char* PluginConfigLuaSettings::GetStringProductVersion() const
{
    return fStringProductVersion.c_str();
}

void PluginConfigLuaSettings::SetStringProductVersion(const char* stringId)
{
    if (stringId)
    {
        fStringProductVersion = stringId;
    }
    else
    {
        fStringProductVersion.clear();
    }
}

void PluginConfigLuaSettings::Reset()
{
	fStringAppId.clear();
	fStringClientId.clear();
	fStringClientSecret.clear();
    fStringProductName.clear();
    fStringProductVersion.clear();
}

bool PluginConfigLuaSettings::LoadFrom(lua_State* luaStatePointer)
{
	bool wasLoaded = false;

	// Validate.
	if (!luaStatePointer)
	{
		return false;
	}

	// Determine if the "config.lua" file was already loaded.
	bool wasConfigLuaAlreadyLoaded = false;
	lua_getglobal(luaStatePointer, "package");
	if (lua_istable(luaStatePointer, -1))
	{
		lua_getfield(luaStatePointer, -1, "loaded");
		if (lua_istable(luaStatePointer, -1))
		{
			lua_getfield(luaStatePointer, -1, "config");
			if (!lua_isnil(luaStatePointer, -1))
			{
				wasConfigLuaAlreadyLoaded = true;
			}
			lua_pop(luaStatePointer, 1);
		}
		lua_pop(luaStatePointer, 1);
	}
	lua_pop(luaStatePointer, 1);

	// Check if an "application" global already exists.
	// If so, then push it to the top of the stack. Otherwise, we push nil to the stack.
	// Note: We need to do this since loading the "config.lua" down below will replace the "application" global.
	//       This allows us to restore this global to its previous state when we're done loading everything.
	lua_getglobal(luaStatePointer, "application");

	// These curly braces indicate the stack count is up by 1 due to the above.
	{
		// Load the "config.lua" file via the Lua require() function.
		lua_getglobal(luaStatePointer, "require");
		if (lua_isfunction(luaStatePointer, -1))
		{
			lua_pushstring(luaStatePointer, "config");
			CoronaLuaDoCall(luaStatePointer, 1, 1);
			lua_pop(luaStatePointer, 1);
		}
		else
		{
			lua_pop(luaStatePointer, 1);
		}

		// Fetch this plugin's settings from the "config.lua" file's application settings.
		lua_getglobal(luaStatePointer, "application");
		if (lua_istable(luaStatePointer, -1))
		{
			lua_getfield(luaStatePointer, -1, "eos");
			if (lua_istable(luaStatePointer, -1))
			{
				// Flag that this plugin's table was found in the "config.lua" file.
				wasLoaded = true;

				// Fetch the EOS app ID in string form.
				lua_getfield(luaStatePointer, -1, "encryptionKey");
				const auto encryptionKeyLuaValueType = lua_type(luaStatePointer, -1);
				if (encryptionKeyLuaValueType == LUA_TSTRING)
				{
					auto stringValue = lua_tostring(luaStatePointer, -1);
					if (stringValue)
					{
						fStringEncryptionKey = stringValue;
					}
				}
				lua_pop(luaStatePointer, 1);

				// Fetch the EOS app ID in string form.
				lua_getfield(luaStatePointer, -1, "appId");
				const auto appIdLuaValueType = lua_type(luaStatePointer, -1);
				if (appIdLuaValueType == LUA_TSTRING)
				{
					auto stringValue = lua_tostring(luaStatePointer, -1);
					if (stringValue)
					{
						fStringAppId = stringValue;
					}
				}
				lua_pop(luaStatePointer, 1);

				// Fetch the EOS app ID in string form.
				lua_getfield(luaStatePointer, -1, "productId");
				const auto productIdLuaValueType = lua_type(luaStatePointer, -1);
				if (productIdLuaValueType == LUA_TSTRING)
				{
					auto stringValue = lua_tostring(luaStatePointer, -1);
					if (stringValue)
					{
						fStringProductId = stringValue;
					}
				}
				lua_pop(luaStatePointer, 1);

				// Fetch the EOS app ID in string form.
				lua_getfield(luaStatePointer, -1, "sandboxId");
				const auto sandboxIdLuaValueType = lua_type(luaStatePointer, -1);
				if (sandboxIdLuaValueType == LUA_TSTRING)
				{
					auto stringValue = lua_tostring(luaStatePointer, -1);
					if (stringValue)
					{
						fStringSandboxId = stringValue;
					}
				}
				lua_pop(luaStatePointer, 1);

				// Fetch the EOS app ID in string form.
				lua_getfield(luaStatePointer, -1, "deploymentId");
				const auto deploymentIdLuaValueType = lua_type(luaStatePointer, -1);
				if (deploymentIdLuaValueType == LUA_TSTRING)
				{
					auto stringValue = lua_tostring(luaStatePointer, -1);
					if (stringValue)
					{
						fStringDeploymentId = stringValue;
					}
				}
				lua_pop(luaStatePointer, 1);

				// Fetch the EOS client ID in string form.
				lua_getfield(luaStatePointer, -1, "clientId");
				const auto clientIdLuaValueType = lua_type(luaStatePointer, -1);
				if (clientIdLuaValueType == LUA_TSTRING)
				{
					auto stringValue = lua_tostring(luaStatePointer, -1);
					if (stringValue)
					{
						fStringClientId = stringValue;
					}
				}
				lua_pop(luaStatePointer, 1);

				// Fetch the EOS client secret in string form.
				lua_getfield(luaStatePointer, -1, "clientSecret");
				const auto clientSecretLuaValueType = lua_type(luaStatePointer, -1);
				if (clientSecretLuaValueType == LUA_TSTRING)
				{
					auto stringValue = lua_tostring(luaStatePointer, -1);
					if (stringValue)
					{
						fStringClientSecret = stringValue;
					}
				}
				lua_pop(luaStatePointer, 1);

                
                // Fetch the Product Name in string form.
                lua_getfield(luaStatePointer, -1, "productName");
                const auto productNameLuaValueType = lua_type(luaStatePointer, -1);
                if (productNameLuaValueType == LUA_TSTRING)
                {
                    auto stringValue = lua_tostring(luaStatePointer, -1);
                    if (stringValue)
                    {
                        fStringProductName = stringValue;
                    }
                }
                lua_pop(luaStatePointer, 1);
                
                // Fetch the Product Version in string form.
                lua_getfield(luaStatePointer, -1, "productVersion");
                const auto productVersionLuaValueType = lua_type(luaStatePointer, -1);
                if (productVersionLuaValueType == LUA_TSTRING)
                {
                    auto stringValue = lua_tostring(luaStatePointer, -1);
                    if (stringValue)
                    {
                        fStringProductVersion = stringValue;
                    }
                }
                lua_pop(luaStatePointer, 1);
                
				// *** In the future, other "config.lua" plugin settings can be loaded here. ***
			}
			lua_pop(luaStatePointer, 1);
		}
		lua_pop(luaStatePointer, 1);

		// Unload the "config.lua", but only if it wasn't loaded before.
		if (!wasConfigLuaAlreadyLoaded)
		{
			lua_getglobal(luaStatePointer, "package");
			if (lua_istable(luaStatePointer, -1))
			{
				lua_getfield(luaStatePointer, -1, "loaded");
				if (lua_istable(luaStatePointer, -1))
				{
					lua_pushnil(luaStatePointer);
					lua_setfield(luaStatePointer, -2, "config");
				}
				lua_pop(luaStatePointer, 1);
			}
			lua_pop(luaStatePointer, 1);
		}
	}

	// Restore the "application" global to its previous value which is at the top of the stack.
	// If it wasn't set before then nil we be at the top of stack, which will remove this global.
	lua_setglobal(luaStatePointer, "application");

	// Returns true if this plugin's settings table was found in the "config.lua" file.
	// Note that this does not necessarily mean that any fields were found under this table.
	return wasLoaded;
}
