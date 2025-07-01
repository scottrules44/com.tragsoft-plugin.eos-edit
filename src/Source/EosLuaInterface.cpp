// --------------------------------------------------------------------------------
// 
// EosLuaInterface.cpp
// Copyright (c) 2016 Corona Labs Inc. All rights reserved.
// This software may be modified and distributed under the terms
// of the MIT license.  See the LICENSE file for details.
//
// --------------------------------------------------------------------------------

#include "CoronaLua.h"
#include "CoronaMacros.h"
#include "DispatchEventTask.h"
#include "LuaEventDispatcher.h"
#include "PluginConfigLuaSettings.h"
#include "RuntimeContext.h"
#include <cmath>
#include <sstream>
#include <stdint.h>

#include <string>
#include <thread>

extern "C"
{
#	include "lua.h"
#	include "lauxlib.h"
}

#include "eos_sdk.h"
#include "eos_ui.h"
#include "eos_logging.h"
#include "eos_auth.h"
#include "eos_ecom.h"
#include "PlatformCommandLine.h"
#include "EosLuaInterface.h"

#if ALLOW_RESERVED_PLATFORM_OPTIONS
#include "ReservedPlatformOptions.h"
#endif

#ifdef _WIN32
#include <windows.h>
#include <windef.h>
#include <winbase.h>
#include "Windows/eos_Windows.h"
#endif

#if defined(__APPLE__)
  #include <TargetConditionals.h>
  #include "eos_ios.h"

  extern "C" void* CreateWebAuthContextProvider();
#endif

//---------------------------------------------------------------------------------
// Constants
//---------------------------------------------------------------------------------


//---------------------------------------------------------------------------------
// Private Static Variables
//---------------------------------------------------------------------------------

/**
  Gets the thread ID that all plugin instances are currently running in.
  This member is only applicable if at least 1 plugin instance exists.
  Intended to avoid multiple plugin instances from being loaded at the same time on different threads.
 */
static std::thread::id sMainThreadId;

//---------------------------------------------------------------------------------
// Private Static Functions
//---------------------------------------------------------------------------------
/**
  Determines if the given Lua state is running under the Corona Simulator.
  @param luaStatePointer Pointer to the Lua state to check.
  @return Returns true if the given Lua state is running under the Corona Simulator.

          Returns false if running under a real device/desktop application or if given a null pointer.
 */
bool IsRunningInCoronaSimulator(lua_State *luaStatePointer) {
    bool isSimulator = false;
    lua_getglobal(luaStatePointer, "system");
    if (lua_istable(luaStatePointer, -1)) {
        lua_getfield(luaStatePointer, -1, "getInfo");
        if (lua_isfunction(luaStatePointer, -1)) {
            lua_pushstring(luaStatePointer, "environment");
            int callResultCode = CoronaLuaDoCall(luaStatePointer, 1, 1);
            if (!callResultCode && (lua_type(luaStatePointer, -1) == LUA_TSTRING)) {
                isSimulator = (strcmp(lua_tostring(luaStatePointer, -1), "simulator") == 0);
            }
        }
        lua_pop(luaStatePointer, 1);
    }
    lua_pop(luaStatePointer, 1);
    return isSimulator;
}

RuntimeContext *GetRuntimeContextFromLuaState(lua_State *L) {
    RuntimeContext *context = nullptr;

    // Try to get from the Lua registry (safe, hidden)
    lua_getfield(L, LUA_REGISTRYINDEX, "__runtimeContext");
    if (lua_isuserdata(L, -1)) {
        context = static_cast<RuntimeContext *>(lua_touserdata(L, -1));
    }
    lua_pop(L, 1); // Always pop from stack

    // If not found in the registry, try to get from the global variable
    if (!context) {
        lua_getglobal(L, "__runtimeContext");
        if (lua_isuserdata(L, -1)) {
            context = static_cast<RuntimeContext *>(lua_touserdata(L, -1));
        }
        lua_pop(L, 1);
    }

    // Debug log (optional)
    if (!context) {
        printf("[Lua] Error: RuntimeContext not found!\n");
    }

    return context;
}

/**
* Callback function to use for EOS SDK log messages
*
* @param InMsg - A structure representing data for a log message
*/
void EOS_CALL onEOSLogMessageReceived(const EOS_LogMessage *InMsg) {
    if (InMsg->Level != EOS_ELogLevel::EOS_LOG_Off) {
        if (InMsg->Level == EOS_ELogLevel::EOS_LOG_Error ||
            InMsg->Level == EOS_ELogLevel::EOS_LOG_Fatal) {
            CoronaLog("ERROR: [EOS SDK] %s", InMsg->Message);
        } else if (InMsg->Level == EOS_ELogLevel::EOS_LOG_Warning) {
            CoronaLog("WARNING: [EOS SDK] %s", InMsg->Message);
        } else {
            CoronaLog("[EOS SDK] %s", InMsg->Message);
        }
    }
}

void EOS_CALL onLoginCallback(const EOS_Auth_LoginCallbackInfo *Data) {
    RuntimeContext *contextPointer = (RuntimeContext *) Data->ClientData;
    if (Data->ResultCode == EOS_EResult::EOS_Success) {
        contextPointer->fAccountId = Data->SelectedAccountId;
    }

    if (EOS_EResult_IsOperationComplete(Data->ResultCode)) {
        contextPointer->OnLoginResponse(Data);
    }
}

//---------------------------------------------------------------------------------
// Lua API Handlers
//---------------------------------------------------------------------------------
/** UserInfo eos.getAuthIdToken() */
extern "C" int InitializeSDK(lua_State *luaStatePointer, EOS_InitializeOptions SDKOptions) {
    // If this plugin instance is being loaded while another one already exists, then make sure that they're
    // both running on the same thread to avoid race conditions since EOS's event handlers are global.
    // Note: This can only happen if multiple Corona runtimes are running at the same time.
    if (RuntimeContext::GetInstanceCount() > 0) {
        if (std::this_thread::get_id() != sMainThreadId) {
            luaL_error(luaStatePointer,
                       "Cannot load another instance of 'plugin.eos' from another thread.");
            return 0;
        }
    } else {
        sMainThreadId = std::this_thread::get_id();
    }

    // Create a new runtime context used to receive EOS's event and dispatch them to Lua.
    // Also used to ensure that the EOS overlay is rendered when requested on Windows.
    auto contextPointer = new RuntimeContext(luaStatePointer);
    if (!contextPointer) {
        return 0;
    }
    lua_pushlightuserdata(luaStatePointer, contextPointer);
    lua_setfield(luaStatePointer, LUA_REGISTRYINDEX,
                 "__runtimeContext"); // Store in the registry under a unique key (cannot be modified by Lua scripts)

    // Fetch the EOS properties from the "config.lua" file.
    PluginConfigLuaSettings configLuaSettings;
    configLuaSettings.LoadFrom(luaStatePointer);

    // Initialize our connection with EOS if this is the first plugin instance.
    // Note: This avoid initializing twice in case multiple plugin instances exist at the same time.
    if (RuntimeContext::GetInstanceCount() == 1) {
        EOS_EResult InitResult = EOS_Initialize(&SDKOptions);
        if (InitResult == EOS_EResult::EOS_InvalidParameters) {
            CoronaLuaError(luaStatePointer, "[EOS SDK] Init Failed! Invalid Parameters");
            return 0;
        } else if (InitResult == EOS_EResult::EOS_Android_JavaVMNotStored) {
            CoronaLuaError(luaStatePointer, "[EOS SDK] Init Failed! Java VM not stored");
            return 0;
        } else if (InitResult ==
                   EOS_EResult::EOS_AlreadyConfigured) // TODO: Apparently this happens the first time the simulator reloads, should probably prevent reaching this state though
        {
            CoronaLog("WARNING: [EOS SDK] Init Failed! Already Configured");
            return 1;
        }

        CoronaLog("[EOS SDK] Initialized. Setting Logging Callback ...");
        EOS_EResult SetLogCallbackResult = EOS_Logging_SetCallback(&onEOSLogMessageReceived);
        if (SetLogCallbackResult != EOS_EResult::EOS_Success) {
            CoronaLog("WARNING: [EOS SDK] Set Logging Callback Failed!");
        } else {
            CoronaLog("[EOS SDK] Logging Callback Set");
        }

        // Create platform instance
        EOS_Platform_Options PlatformOptions = {};
        PlatformOptions.ApiVersion = EOS_PLATFORM_OPTIONS_API_LATEST;
        PlatformOptions.bIsServer = false;
        PlatformOptions.EncryptionKey = configLuaSettings.GetStringEncryptionKey();
        PlatformOptions.OverrideCountryCode = nullptr;
        PlatformOptions.OverrideLocaleCode = nullptr;
        PlatformOptions.Flags =
                EOS_PF_WINDOWS_ENABLE_OVERLAY_D3D9 | EOS_PF_WINDOWS_ENABLE_OVERLAY_D3D10 |
                EOS_PF_WINDOWS_ENABLE_OVERLAY_OPENGL; // Enable overlay support for D3D9/10 and OpenGL. This sample uses D3D11 or SDL.
        // PlatformOptions.CacheDirectory = FUtils::GetTempDirectory();

        PlatformOptions.ProductId = configLuaSettings.GetStringProductId();
        PlatformOptions.SandboxId = configLuaSettings.GetStringSandboxId();
        PlatformOptions.DeploymentId = configLuaSettings.GetStringDeploymentId();
        PlatformOptions.ClientCredentials.ClientId = configLuaSettings.GetStringClientId();
        PlatformOptions.ClientCredentials.ClientSecret = configLuaSettings.GetStringClientSecret();

#ifdef _WIN32
        EOS_Platform_RTCOptions RtcOptions = { 0 };
        RtcOptions.ApiVersion = EOS_PLATFORM_RTCOPTIONS_API_LATEST;

        wchar_t CurDir[MAX_PATH + 1] = {};
        ::GetCurrentDirectoryW(MAX_PATH + 1u, CurDir);
        std::wstring BasePath = std::wstring(CurDir);
        std::string XAudio29DllPath;
        XAudio29DllPath.append("/xaudio2_9redist.dll");

        EOS_Windows_RTCOptions WindowsRtcOptions = { 0 };
        WindowsRtcOptions.ApiVersion = EOS_WINDOWS_RTCOPTIONS_API_LATEST;
        WindowsRtcOptions.XAudio29DllPath = XAudio29DllPath.c_str();
        RtcOptions.PlatformSpecificOptions = &WindowsRtcOptions;

        PlatformOptions.RTCOptions = &RtcOptions;
#endif // _WIN32

#if ALLOW_RESERVED_PLATFORM_OPTIONS
        SetReservedPlatformOptions(PlatformOptions);
#else
        PlatformOptions.Reserved = NULL;
#endif // ALLOW_RESERVED_PLATFORM_OPTIONS

        EOS_HPlatform platformHandle = EOS_Platform_Create(&PlatformOptions);
        if (!platformHandle) {
            CoronaLuaError(luaStatePointer, "Failed to initialize connection with Epic client.");
        }
        contextPointer->fPlatformHandle = platformHandle;
    }

#ifndef EOS_STEAM_ENABLED
#if defined(__ANDROID__) || (defined(__APPLE__) && TARGET_OS_IPHONE)
    contextPointer->fAuthHandle = EOS_Platform_GetAuthInterface(contextPointer->fPlatformHandle);

    EOS_Auth_Credentials Credentials = {};
    Credentials.ApiVersion = EOS_AUTH_CREDENTIALS_API_LATEST;
    Credentials.Type = EOS_ELoginCredentialType::EOS_LCT_PersistentAuth;
    Credentials.Id = nullptr;
    Credentials.Token = nullptr;

    EOS_Auth_LoginOptions LoginOptions = {};
    LoginOptions.ApiVersion = EOS_AUTH_LOGIN_API_LATEST;
    LoginOptions.Credentials = &Credentials;

    EOS_Auth_Login(contextPointer->fAuthHandle, &LoginOptions, contextPointer, onLoginCallback);
#else
   auto launcherAuthTypeLaunchArg = CMDLine::Map().find("AUTH_TYPE");
       auto launcherAuthPasswordLaunchArg = CMDLine::Map().find("AUTH_PASSWORD");
       if (contextPointer->fPlatformHandle && launcherAuthTypeLaunchArg != CMDLine::End() && launcherAuthPasswordLaunchArg != CMDLine::End()) {
       	std::string launcherAuthType = launcherAuthTypeLaunchArg->second;
       	if (launcherAuthType == "exchangecode") {
       		std::string launcherAuthPassword = launcherAuthPasswordLaunchArg->second;
       		if (!launcherAuthPassword.empty())
       		{
       			contextPointer->fAuthHandle = EOS_Platform_GetAuthInterface(contextPointer->fPlatformHandle);
    
       			EOS_Auth_Credentials Credentials = {};
       			Credentials.ApiVersion = EOS_AUTH_CREDENTIALS_API_LATEST;
    
       			EOS_Auth_LoginOptions LoginOptions;
       			memset(&LoginOptions, 0, sizeof(LoginOptions));
       			LoginOptions.ApiVersion = EOS_AUTH_LOGIN_API_LATEST;
       			LoginOptions.ScopeFlags |= EOS_EAuthScopeFlags::EOS_AS_NoFlags;
    
       			Credentials.Type = EOS_ELoginCredentialType::EOS_LCT_ExchangeCode;
       			Credentials.Token = launcherAuthPassword.c_str();
       			LoginOptions.Credentials = &Credentials;
    
       			EOS_Auth_Login(contextPointer->fAuthHandle, &LoginOptions, contextPointer, onLoginCallback);
       		}
       	}
       }
#endif
#endif

    return 1;
}

/** UserInfo eos.isLoggedOn() */
extern "C" int OnIsLoggedOn(lua_State *luaStatePointer) {
    auto contextPointer = GetRuntimeContextFromLuaState(luaStatePointer);
    if (!contextPointer) {
        return 0;
    }

    if (!contextPointer->fAccountId) {
        lua_pushboolean(luaStatePointer, 0);
        return 1;
    }

    if (!contextPointer->fPlatformHandle) {
        lua_pushboolean(luaStatePointer, 0);
        return 1;
    }

    lua_pushboolean(luaStatePointer, 1);
    return 1;
}

/** UserInfo eos.loginWithAccountPortal() */
extern "C" int OnLoginWithAccountPortal(lua_State *luaStatePointer) {
    // Fetch this plugin's runtime context associated with the calling Lua state.
    auto contextPointer = GetRuntimeContextFromLuaState(luaStatePointer);
    if (!contextPointer) {
        return 0;
    }

    EOS_Auth_Credentials Credentials = {};
    Credentials.ApiVersion = EOS_AUTH_CREDENTIALS_API_LATEST;
    Credentials.Type = EOS_ELoginCredentialType::EOS_LCT_AccountPortal;
    Credentials.Id = nullptr;
    Credentials.Token = nullptr;

 #if (defined(__APPLE__) && TARGET_OS_IPHONE)
    // For iOS 13+ we need to pass the applications protocol implementation for ASWebAuthenticationPresentationContextProviding
    // We bridge this to the C++ API using CFBridgingRetain, the EOS SDK will always release the bridged value as part of the contract
    // NOTE: The SDK will consume this data before the scope is lost
    EOS_IOS_Auth_CredentialsOptions CredentialsOptions = {};
    CredentialsOptions.ApiVersion = EOS_IOS_AUTH_CREDENTIALSOPTIONS_API_LATEST;
    CredentialsOptions.PresentationContextProviding = (void*)CreateWebAuthContextProvider(); // SDK will release when consumed
    Credentials.SystemAuthCredentialsOptions = (void*)&CredentialsOptions;
 #endif

    EOS_Auth_LoginOptions LoginOptions = {};
    LoginOptions.ApiVersion = EOS_AUTH_LOGIN_API_LATEST;
    LoginOptions.ScopeFlags = EOS_EAuthScopeFlags::EOS_AS_BasicProfile;
    LoginOptions.Credentials = &Credentials;

    EOS_Auth_Login(contextPointer->fAuthHandle, &LoginOptions, contextPointer, onLoginCallback);

    return 1;
}

/** UserInfo eos.getAuthIdToken() */
extern "C" int OnGetAuthIdToken(lua_State *luaStatePointer) {
    // Validate.
    if (!luaStatePointer) {
        return 0;
    }

    // Fetch this plugin's runtime context associated with the calling Lua state.
    auto contextPointer = GetRuntimeContextFromLuaState(luaStatePointer);
    if (!contextPointer) {
        return 0;
    }

    auto eosAccountId = contextPointer->fAccountId;
    if (!eosAccountId) {
        return 0;
    }

    EOS_Auth_CopyIdTokenOptions CopyTokenOptions = {0};
    CopyTokenOptions.ApiVersion = EOS_AUTH_COPYUSERAUTHTOKEN_API_LATEST;
    CopyTokenOptions.AccountId = eosAccountId;

    EOS_Auth_IdToken *outIdToken;
    if (EOS_Auth_CopyIdToken(contextPointer->fAuthHandle, &CopyTokenOptions, &outIdToken) ==
        EOS_EResult::EOS_Success) {
        lua_pushstring(luaStatePointer, outIdToken->JsonWebToken);
        EOS_Auth_IdToken_Release(outIdToken);
        return 1;
    } else {
        CoronaLog("WARNING: [EOS SDK] User Auth Token is invalid");
        return 0;
    }
}

/** bool eos.setNotificationPosition(positionName) */
int OnSetNotificationPosition(lua_State *luaStatePointer) {
    // Validate.
    if (!luaStatePointer) {
        return 0;
    }

    // Fetch the required position name argument.
    if (lua_type(luaStatePointer, 1) != LUA_TSTRING) {
        CoronaLuaError(luaStatePointer, "Given argument is not of type string.");
        lua_pushboolean(luaStatePointer, 0);
        return 1;
    }
    const char *positionName = lua_tostring(luaStatePointer, 1);
    if (!positionName) {
        positionName = "";
    }

    // Convert the position name to its equivalent Epic enum constant.
    EOS_UI_ENotificationLocation positionId;
    if (!strcmp(positionName, "topLeft")) {
        positionId = EOS_UI_ENotificationLocation::EOS_UNL_TopLeft;
    } else if (!strcmp(positionName, "topRight")) {
        positionId = EOS_UI_ENotificationLocation::EOS_UNL_TopRight;
    } else if (!strcmp(positionName, "bottomLeft")) {
        positionId = EOS_UI_ENotificationLocation::EOS_UNL_BottomLeft;
    } else if (!strcmp(positionName, "bottomRight")) {
        positionId = EOS_UI_ENotificationLocation::EOS_UNL_BottomRight;
    } else {
        CoronaLuaError(luaStatePointer, "Given unknown position name '%s'", positionName);
        lua_pushboolean(luaStatePointer, 0);
        return 1;
    }

    // Fetch the runtime context associated with the calling Lua state.
    auto contextPointer = GetRuntimeContextFromLuaState(luaStatePointer);
    if (!contextPointer) {
        return 0;
    }

    auto eosPlatformHandle = contextPointer->fPlatformHandle;
    if (eosPlatformHandle) {
        // Change EOS's notification position with given setting.
        EOS_HUI ExternalUIHandle = EOS_Platform_GetUIInterface(eosPlatformHandle);

        EOS_UI_SetDisplayPreferenceOptions Options = {};
        Options.ApiVersion = EOS_UI_SETDISPLAYPREFERENCE_API_LATEST;
        Options.NotificationLocation = positionId;

        const EOS_EResult Result = EOS_UI_SetDisplayPreference(ExternalUIHandle, &Options);
        if (Result == EOS_EResult::EOS_Success) {
            lua_pushboolean(luaStatePointer, 1);
            return 1;
        } else {
            lua_pushboolean(luaStatePointer, 0);
            return 1;
        }
    } else {
        lua_pushboolean(luaStatePointer, 0);
        return 1;
    }
}

// /** bool eos.setAchievementUnlocked(achievementName) */
// int OnSetAchievementUnlocked(lua_State* luaStatePointer)
// {
// 	// Validate.
// 	if (!luaStatePointer)
// 	{
// 		lua_pushboolean(luaStatePointer, 0);
// 		return 1;
// 	}

// 	// Fetch the achievement name.
// 	const char* achievementName = nullptr;
// 	if (lua_type(luaStatePointer, 1) == LUA_TSTRING)
// 	{
// 		achievementName = lua_tostring(luaStatePointer, 1);
// 	}
// 	if (!achievementName)
// 	{
// 		CoronaLuaError(luaStatePointer, "1st argument must be set to the achievement's unique name.");
// 		lua_pushboolean(luaStatePointer, 0);
// 		return 1;
// 	}

// 	// Fetch the runtime context associated with the calling Lua state.
// 	auto contextPointer = GetRuntimeContextFromLuaState(luaStatePointer);
// 	if (!contextPointer)
// 	{
// 		lua_pushboolean(luaStatePointer, 0);
// 		return 1;
// 	}

// 	if (!contextPointer->fAccountId)
// 	{
// 		lua_pushboolean(luaStatePointer, 0);
// 		return 1;
// 	}

// 	// Attempt to unlock the given achievement.
// 	EOS_Achievements_UnlockAchievementsOptions UnlockAchievementsOptions = {};
// 	UnlockAchievementsOptions.ApiVersion = EOS_ACHIEVEMENTS_UNLOCKACHIEVEMENTS_API_LATEST;
// 	UnlockAchievementsOptions.UserId = nullptr; // TODO: Acquire this ProductUserId from EOS_Connect_Login call
// 	UnlockAchievementsOptions.AchievementsCount = 1;
// 	UnlockAchievementsOptions.AchievementIds = new const char* {achievementName};
// 	EOS_Achievements_UnlockAchievements(contextPointer->fAchievementsHandle, &UnlockAchievementsOptions, nullptr, nullptr); // TODO: Acquire fAchievementsHandle from EOS_Connect_Login call

// 	lua_pushboolean(luaStatePointer, 1);
// 	return 1;
// }

/** bool eos.init() */
int OnFakeIAPInit(lua_State *luaStatePointer) {
    // Is required for being an IAP plugin
    return 0;
}

void EOS_CALL QueryStoreCompleteCallbackFn(const EOS_Ecom_QueryOffersCallbackInfo *OfferData) {
    if (!EOS_EResult_IsOperationComplete(OfferData->ResultCode)) {
        return;
    }

    auto contextPointer = (RuntimeContext *) OfferData->ClientData;
    if (contextPointer) {
        contextPointer->OnLoadProductsResponse(OfferData);
    }
}

/** bool eos.loadProducts() */
extern "C" int OnLoadProducts(lua_State *luaStatePointer) {
    // Do not continue if the 1st argument is not a Lua function.
//    if (!lua_isfunction(luaStatePointer, 1)) {
//        CoronaLuaError(luaStatePointer, "1st argument must be a Lua function.");
//        lua_pushboolean(luaStatePointer, 0);
//        return 1;
//    }

    // Fetch the runtime context associated with the calling Lua state.
    auto contextPointer = GetRuntimeContextFromLuaState(luaStatePointer);
    if (!contextPointer) {
        return 0;
    }

    auto eosPlatformHandle = contextPointer->fPlatformHandle;
    if (!eosPlatformHandle) {
        return 0;
    }

    auto eosAccountId = contextPointer->fAccountId;
    if (!eosAccountId) {
        return 0;
    }

    EOS_HEcom EcomHandle = EOS_Platform_GetEcomInterface(eosPlatformHandle);

    EOS_Ecom_QueryOffersOptions QueryOptions{0};
    QueryOptions.ApiVersion = EOS_ECOM_QUERYOFFERS_API_LATEST;
    QueryOptions.LocalUserId = eosAccountId;
    QueryOptions.OverrideCatalogNamespace = nullptr;

    EOS_Ecom_QueryOffers(EcomHandle, &QueryOptions, contextPointer, QueryStoreCompleteCallbackFn);

    return 1;
}

void EOS_CALL CheckoutCompleteCallbackFn(const EOS_Ecom_CheckoutCallbackInfo *CheckoutData) {
    if (!EOS_EResult_IsOperationComplete(CheckoutData->ResultCode)) {
        return;
    }

    if (CheckoutData->ResultCode != EOS_EResult::EOS_Success) {
        return;
    }

    auto contextPointer = (RuntimeContext *) CheckoutData->ClientData;
    if (contextPointer) {
        contextPointer->OnCheckoutProductResponse(CheckoutData);
    }
}

/** bool eos.purchase() */
extern "C" int OnPurchaseProduct(lua_State *luaStatePointer) {
    // Do not continue if the 1st argument is not a Lua function.
    if (!lua_isstring(luaStatePointer, 1)) {
        CoronaLuaError(luaStatePointer, "1st argument must be a Lua String.");
        return 0;
    }
    const char* offerId = lua_tostring(luaStatePointer, 1);

    // Fetch the runtime context associated with the calling Lua state.
    auto contextPointer = GetRuntimeContextFromLuaState(luaStatePointer);
    if (!contextPointer) {
        return 0;
    }

    auto eosPlatformHandle = contextPointer->fPlatformHandle;
    if (!eosPlatformHandle) {
        return 0;
    }

    auto eosAccountId = contextPointer->fAccountId;
    if (!eosAccountId) {
        return 0;
    }

    EOS_HEcom EcomHandle = EOS_Platform_GetEcomInterface(eosPlatformHandle);

    std::vector<EOS_Ecom_CheckoutEntry> CheckoutEntries;
    EOS_Ecom_CheckoutEntry Entry;
    Entry.ApiVersion = EOS_ECOM_CHECKOUTENTRY_API_LATEST;
    Entry.OfferId = offerId;
    CheckoutEntries.push_back(Entry);

    EOS_Ecom_CheckoutOptions CheckoutOptions{0};
    CheckoutOptions.ApiVersion = EOS_ECOM_CHECKOUT_API_LATEST;
    CheckoutOptions.LocalUserId = eosAccountId;
    CheckoutOptions.OverrideCatalogNamespace = nullptr;
    // CheckoutOptions.PreferredOrientation = nullptr;
    CheckoutOptions.EntryCount = static_cast<uint32_t>(CheckoutEntries.size());
    CheckoutOptions.Entries = &CheckoutEntries[0];

    EOS_Ecom_Checkout(EcomHandle, &CheckoutOptions, contextPointer, CheckoutCompleteCallbackFn);

    return 1;
}

void EOS_CALL QueryEntitlementsCompleteCallbackFn(const EOS_Ecom_QueryEntitlementsCallbackInfo *QueryEntitlementsData) {
    if (!EOS_EResult_IsOperationComplete(QueryEntitlementsData->ResultCode)) {
        return;
    }

    if (QueryEntitlementsData->ResultCode != EOS_EResult::EOS_Success) {
        return;
    }

    auto contextPointer = (RuntimeContext *) QueryEntitlementsData->ClientData;
    if (contextPointer) {
        contextPointer->OnQueryEntitlementsResponse(QueryEntitlementsData);
    }
}

/** bool eos.restore() */
extern "C" int OnRestorePurchases(lua_State *luaStatePointer) {
    // Fetch the runtime context associated with the calling Lua state.
    auto contextPointer = GetRuntimeContextFromLuaState(luaStatePointer);
    if (!contextPointer) {
        return 0;
    }

    auto eosPlatformHandle = contextPointer->fPlatformHandle;
    if (!eosPlatformHandle) {
        return 0;
    }

    auto eosAccountId = contextPointer->fAccountId;
    if (!eosAccountId) {
        return 0;
    }

    EOS_HEcom EcomHandle = EOS_Platform_GetEcomInterface(eosPlatformHandle);

    EOS_Ecom_QueryEntitlementsOptions QueryOptions{ 0 };
    QueryOptions.ApiVersion = EOS_ECOM_QUERYENTITLEMENTS_API_LATEST;
    QueryOptions.LocalUserId = eosAccountId;
    QueryOptions.bIncludeRedeemed = true;

    EOS_Ecom_QueryEntitlements(EcomHandle, &QueryOptions, contextPointer, QueryEntitlementsCompleteCallbackFn);

    return 1;
}

/** bool eos.finishTransaction() */
extern "C" int OnFinishTransaction(lua_State *luaStatePointer) {
    return 0;
}

/** eos.addEventListener(eventName, listener) */
extern "C" int OnAddEventListener(lua_State *luaStatePointer) {
    // Validate.
    if (!luaStatePointer) {
        return 0;
    }

    // Fetch the global Epic event name to listen to.
    const char *eventName = nullptr;
    if (lua_type(luaStatePointer, 1) == LUA_TSTRING) {
        eventName = lua_tostring(luaStatePointer, 1);
    }
    if (!eventName || ('\0' == eventName[0])) {
        CoronaLuaError(luaStatePointer, "1st argument must be set to an event name.");
        return 0;
    }

    // Determine if the 2nd argument references a Lua listener function/table.
    if (!CoronaLuaIsListener(luaStatePointer, 2, eventName)) {
        CoronaLuaError(luaStatePointer, "2nd argument must be set to a listener.");
        return 0;
    }

    // Fetch the runtime context associated with the calling Lua state.
    auto *contextPointer = GetRuntimeContextFromLuaState(luaStatePointer);
    if (!contextPointer) {
        return 0;
    }

    // Add the given listener for the global Epic event.
    auto luaEventDispatcherPointer = contextPointer->GetLuaEventDispatcher();
    if (luaEventDispatcherPointer) {
        luaEventDispatcherPointer->AddEventListener(luaStatePointer, eventName, 2);
    }

    return 0;
}

/** eos.removeEventListener(eventName, listener) */
extern "C" int OnRemoveEventListener(lua_State *luaStatePointer) {
    // Validate.
    if (!luaStatePointer) {
        return 0;
    }

    // Fetch the global EOS event name to stop listening to.
    const char *eventName = nullptr;
    if (lua_type(luaStatePointer, 1) == LUA_TSTRING) {
        eventName = lua_tostring(luaStatePointer, 1);
    }
    if (!eventName || ('\0' == eventName[0])) {
        CoronaLuaError(luaStatePointer, "1st argument must be set to an event name.");
        return 0;
    }

    // Determine if the 2nd argument references a Lua listener function/table.
    if (!CoronaLuaIsListener(luaStatePointer, 2, eventName)) {
        CoronaLuaError(luaStatePointer, "2nd argument must be set to a listener.");
        return 0;
    }

    // Fetch the runtime context associated with the calling Lua state.
    auto contextPointer = GetRuntimeContextFromLuaState(luaStatePointer);
    if (!contextPointer) {
        return 0;
    }

    // Remove the given listener from the global EOS event.
    auto luaEventDispatcherPointer = contextPointer->GetLuaEventDispatcher();
    if (luaEventDispatcherPointer) {
        luaEventDispatcherPointer->RemoveEventListener(luaStatePointer, eventName, 2);
    }
    return 0;
}

/** Called when a property field is being read from the plugin's Lua table. */
int OnAccessingField(lua_State *luaStatePointer) {
    // Validate.
    if (!luaStatePointer) {
        return 0;
    }

    // Fetch the field name being accessed.
    if (lua_type(luaStatePointer, 2) != LUA_TSTRING) {
        return 0;
    }
    auto fieldName = lua_tostring(luaStatePointer, 2);
    if (!fieldName) {
        return 0;
    }

    if (!strcmp(fieldName, "isLoggedOn") || !strcmp(fieldName, "canLoadProducts")) {
        // Fetch the runtime context associated with the calling Lua state.
        auto contextPointer = (RuntimeContext *) lua_touserdata(luaStatePointer,
                                                                lua_upvalueindex(1));
        if (!contextPointer) {
            return 0;
        }

        if (!contextPointer->fAccountId) {
            lua_pushboolean(luaStatePointer, 0);
            return 1;
        }

        if (!contextPointer->fPlatformHandle) {
            lua_pushboolean(luaStatePointer, 0);
            return 1;
        }

        lua_pushboolean(luaStatePointer, 1);
        return 1;
    }

    // Attempt to fetch the requested field value.
    CoronaLuaError(luaStatePointer, "Accessing unknown field: '%s'", fieldName);

    // Return the number of value pushed to Lua as return values.
    return 0;
}

/** Called when a property field is being written to in the plugin's Lua table. */
int OnAssigningField(lua_State *luaStatePointer) {
    // Writing to fields is not currently supported.
    return 0;
}

/**
  Called when the Lua plugin table is being destroyed.
  Expected to happen when the Lua runtime is being terminated.

  Performs finaly cleanup and terminates connection with the EOS client.
 */
int OnFinalizing(lua_State *luaStatePointer) {
    // Delete this plugin's runtime context from memory.
    auto contextPointer = GetRuntimeContextFromLuaState(luaStatePointer);
    if (contextPointer) {
        delete contextPointer;
    }

    return 0;
}


//---------------------------------------------------------------------------------
// Public Exports
//---------------------------------------------------------------------------------

/**
  Called when this plugin is being loaded from Lua via a require() function.
  Initializes itself with EOS and returns the plugin's Lua table.
 */
CORONA_EXPORT int luaopen_plugin_eos(lua_State *luaStatePointer) {
    // Validate.
    if (!luaStatePointer) {
        return 0;
    }

    EOS_InitializeOptions SDKOptions = {};
    SDKOptions.ApiVersion = EOS_INITIALIZE_API_LATEST;
    SDKOptions.AllocateMemoryFunction = nullptr;
    SDKOptions.ReallocateMemoryFunction = nullptr;
    SDKOptions.ReleaseMemoryFunction = nullptr;
    SDKOptions.ProductName = "Coromon"; // JOCHEM - TODO
    SDKOptions.ProductVersion = "1.3.6"; // JOCHEM - TODO
    SDKOptions.Reserved = nullptr;
    SDKOptions.SystemInitializeOptions = nullptr;
    SDKOptions.OverrideThreadAffinity = nullptr;
    InitializeSDK(luaStatePointer, SDKOptions);

    // Push this plugin's Lua table and all of its functions to the top of the Lua stack.
    // Note: The RuntimeContext pointer is pushed as an upvalue to all of these functions via luaL_openlib().
    auto contextPointer = GetRuntimeContextFromLuaState(luaStatePointer);
    {
        const struct luaL_Reg luaFunctions[] =
                {
                        {"addEventListener",        OnAddEventListener},
                        {"removeEventListener",     OnRemoveEventListener},
                        {"isLoggedOn",              OnIsLoggedOn},
                        {"getAuthIdToken",          OnGetAuthIdToken},
                        {"loginWithAccountPortal",  OnLoginWithAccountPortal},
                        {"setNotificationPosition", OnSetNotificationPosition},

                        {"init",                    OnFakeIAPInit},
                        {"loadProducts",            OnLoadProducts},
                        {"purchase",                OnPurchaseProduct},
                        {"restore",                 OnRestorePurchases},
                        {"finishTransaction",       OnFinishTransaction},

                        // { "setAchievementUnlocked", OnSetAchievementUnlocked },
                        {nullptr,                   nullptr}
                };
        lua_createtable(luaStatePointer, 0, 0);
        lua_pushlightuserdata(luaStatePointer, contextPointer);
        luaL_openlib(luaStatePointer, nullptr, luaFunctions, 1);
    }

    // Add a Lua finalizer to the plugin's Lua table and to the Lua registry.
    // Note: Lua 5.1 tables do not support the "__gc" metatable field, but Lua light-userdata types do.
    {
        // Create a Lua metatable used to receive the finalize event.
        const struct luaL_Reg luaFunctions[] =
                {
                        {"__gc",  OnFinalizing},
                        {nullptr, nullptr}
                };
        luaL_newmetatable(luaStatePointer, "plugin.eos.__gc");
        lua_pushlightuserdata(luaStatePointer, contextPointer);
        luaL_openlib(luaStatePointer, nullptr, luaFunctions, 1);
        lua_pop(luaStatePointer, 1);

        // Add the finalizer metable to the Lua registry.
        CoronaLuaPushUserdata(luaStatePointer, nullptr, "plugin.eos.__gc");
        int luaReferenceKey = luaL_ref(luaStatePointer, LUA_REGISTRYINDEX);

        // Add the finalizer metatable to the plugin's Lua table as an undocumented "__gc" field.
        // Note that a developer can overwrite this field, which is why we add it to the registry above too.
        lua_rawgeti(luaStatePointer, LUA_REGISTRYINDEX, luaReferenceKey);
        lua_setfield(luaStatePointer, -2, "__gc");
    }

    // Wrap the plugin's Lua table in a metatable used to provide readable/writable property fields.
    {
        const struct luaL_Reg luaFunctions[] =
                {
                        {"__index",    OnAccessingField},
                        {"__newindex", OnAssigningField},
                        {nullptr,      nullptr}
                };
        luaL_newmetatable(luaStatePointer, "plugin.eos");
        lua_pushlightuserdata(luaStatePointer, contextPointer);
        luaL_openlib(luaStatePointer, nullptr, luaFunctions, 1);
        lua_setmetatable(luaStatePointer, -2);
    }

    // We're returning 1 Lua plugin table.
    return 1;
}
