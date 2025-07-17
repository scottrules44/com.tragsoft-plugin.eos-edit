// ----------------------------------------------------------------------------
// 
// PluginConfigLuaSettings.h
// Copyright (c) 2016 Corona Labs Inc. All rights reserved.
// This software may be modified and distributed under the terms
// of the MIT license.  See the LICENSE file for details.
//
// ----------------------------------------------------------------------------

#pragma once

#include <string>
extern "C"
{
#	include "lua.h"
}


/**
  Provides an easy means of reading this plugin's settings from a "config.lua" file.

  Will ensure that the "config.lua" file is removed from the Lua package manager
  and the file's "application" Lua global is nil'ed out if not loaded before.
 */
class PluginConfigLuaSettings
{
	public:
		PluginConfigLuaSettings();
		virtual ~PluginConfigLuaSettings();

		const char* GetStringEncryptionKey() const;
		void SetStringEncryptionKey(const char* stringId);
		const char* GetStringAppId() const;
		void SetStringAppId(const char* stringId);
		const char* GetStringProductId() const;
		void SetStringProductId(const char* stringId);
		const char* GetStringSandboxId() const;
		void SetStringSandboxId(const char* stringId);
		const char* GetStringDeploymentId() const;
		void SetStringDeploymentId(const char* stringId);
		const char* GetStringClientId() const;
		void SetStringClientId(const char* stringId);
		const char* GetStringClientSecret() const;
		void SetStringClientSecret(const char* stringId);
        const char* GetStringProductName() const;
        void SetStringProductName(const char* stringId);
        const char* GetStringProductVersion() const;
        void SetStringProductVersion(const char* stringId);
        
		void Reset();
		bool LoadFrom(lua_State* luaStatePointer);

	private:
		std::string fStringEncryptionKey;
		std::string fStringAppId;
		std::string fStringProductId;
		std::string fStringSandboxId;
		std::string fStringDeploymentId;
		std::string fStringClientId;
		std::string fStringClientSecret;
        std::string fStringProductName;
        std::string fStringProductVersion;
};
