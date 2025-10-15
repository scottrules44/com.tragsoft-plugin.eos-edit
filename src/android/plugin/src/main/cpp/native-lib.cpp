#include <jni.h>
#include <string>
#include <set>
#include <eos_init.h>
#include <eos_sdk.h>
#include <eos_auth.h>
#include <eos_connect.h>
#include <eos_logging.h>
#include <eos_auth_types.h>
#include <eos_userinfo.h>
#include "Android/eos_android.h"
#include "EosLuaInterface.h"

bool IsSDKInitialized = false;
EOS_HPlatform PlatformHandle = nullptr;
JNIEnv *LocalENV = nullptr;
jclass GlobalRefLuaLoaderClass = nullptr;
jobject GlobalRefLuaLoaderInstance = nullptr;

static EOS_EpicAccountId LocalUserId = nullptr;
static EOS_UserInfo *LocalUserInfo = nullptr;
static EOS_NotificationId NotifyLoginStatusChangedId = EOS_INVALID_NOTIFICATIONID;
static jobject GlobalRefActivity = nullptr;

void DeletePersistentAuth();

/** Call Java showtext method to display log in Android view */
void OS_LOG(const char *Text) {
    if (Text) {
        jmethodID MethodID = LocalENV->GetMethodID(GlobalRefLuaLoaderClass, "ShowText", "(Ljava/lang/String;)V");
        LocalENV->CallVoidMethod(GlobalRefLuaLoaderInstance, MethodID, LocalENV->NewStringUTF(Text));
    }
}

// Get native Lua state pointer from Java LuaState object
lua_State* GetLuaStatePointer(JNIEnv *env, jobject luaStateObj) {
    jclass luaStateClass = env->GetObjectClass(luaStateObj);
    jfieldID luaStatePointerField = env->GetFieldID(luaStateClass, "luaState", "J"); // Assuming LuaState stores pointer in a long field
    return (lua_State*) env->GetLongField(luaStateObj, luaStatePointerField);
}

/** Call Java UIButtonHandler method to hide/show correct button */
void LoginStateChanged(bool loggedIn) {
    jmethodID MethodID = LocalENV->GetMethodID(GlobalRefLuaLoaderClass, "LoginStateChanged", "(Z)V");
    LocalENV->CallVoidMethod(GlobalRefLuaLoaderInstance, MethodID, loggedIn);
}

void LoginInProgress() {
    jmethodID MethodID = LocalENV->GetMethodID(GlobalRefLuaLoaderClass, "LoginInProgress", "()V");
    LocalENV->CallVoidMethod(GlobalRefLuaLoaderInstance, MethodID);
}

/** An example of obtaining the display name for the user currently logged into the EOS Auth Interface */
std::string GetLoggedInDisplayName() {
    if (PlatformHandle == nullptr) {
        return "";
    }

    EOS_HUserInfo UserInfoHandle = EOS_Platform_GetUserInfoInterface(PlatformHandle);

    /** Release any data returned to us from a previous call to GetLoggedInDisplayName */
    if (LocalUserInfo != nullptr) {
        EOS_UserInfo_Release(LocalUserInfo);
        LocalUserInfo = nullptr;
    }

    EOS_UserInfo_CopyUserInfoOptions CopyUserInfoOptions = {};
    CopyUserInfoOptions.ApiVersion = EOS_USERINFO_COPYUSERINFO_API_LATEST;
    CopyUserInfoOptions.LocalUserId = LocalUserId;
    CopyUserInfoOptions.TargetUserId = LocalUserId;

    EOS_EResult ResultCode = EOS_UserInfo_CopyUserInfo(UserInfoHandle, &CopyUserInfoOptions, &LocalUserInfo);
    bool bSuccessful = ResultCode == EOS_EResult::EOS_Success;
    return std::string(bSuccessful ? LocalUserInfo->DisplayName : "");
}

extern "C"
JNIEXPORT jstring JNICALL
Java_plugin_eos_LuaLoader_GetUsername(JNIEnv *env, jobject thiz) {
    return LocalENV->NewStringUTF(GetLoggedInDisplayName().c_str());
}

/** Callback to handle login status changes */
void EOS_CALL AuthNotifyLoginStatusChangedCb(const EOS_Auth_LoginStatusChangedCallbackInfo *Data) {
    if (Data->CurrentStatus == EOS_ELoginStatus::EOS_LS_LoggedIn) {
        LoginStateChanged(true);
    } else if (Data->CurrentStatus == EOS_ELoginStatus::EOS_LS_NotLoggedIn) {
        DeletePersistentAuth();
        LoginStateChanged(false);
    }
}

/** Callback to handle result of attempting a login using the web account portal */
void EOS_CALL AuthLoginCb(const EOS_Auth_LoginCallbackInfo *Data) {
    if (!EOS_EResult_IsOperationComplete(Data->ResultCode)) {
        return;
    }

    std::string result = std::string("Login Result: ") + EOS_EResult_ToString(Data->ResultCode);
    OS_LOG(result.c_str());
    bool bSuccessful = Data->ResultCode == EOS_EResult::EOS_Success;
    if (bSuccessful) {
        LocalUserId = Data->LocalUserId;
        std::string DisplayName = std::string("DisplayName= ") + GetLoggedInDisplayName();
        OS_LOG(DisplayName.c_str());
    }
    LoginStateChanged(bSuccessful);
}

/** Callback to handle result of attempting a login with stored secure credentials */
void EOS_CALL PersistentAuthLoginCb(const EOS_Auth_LoginCallbackInfo *Data) {

    if (!EOS_EResult_IsOperationComplete(Data->ResultCode)) {
        return;
    }

    std::string result = std::string(
            "LoginPersistentAuth: Result=") + EOS_EResult_ToString(Data->ResultCode);
    OS_LOG(result.c_str());
    bool bSuccessful = Data->ResultCode == EOS_EResult::EOS_Success;
    if (bSuccessful) {
        LocalUserId = Data->LocalUserId;
        std::string DisplayName = std::string("DisplayName= ") + GetLoggedInDisplayName();
        OS_LOG(DisplayName.c_str());
    } else {
        // Check the specific error if we fail to complete a persistent login attempt, as we may need to flush any stored secure credentials
        switch (Data->ResultCode) {
            case EOS_EResult::EOS_Canceled:
            case EOS_EResult::EOS_AlreadyPending:
            case EOS_EResult::EOS_TooManyRequests:
            case EOS_EResult::EOS_TimedOut:
            case EOS_EResult::EOS_ServiceFailure:
            case EOS_EResult::EOS_NotFound:
                OS_LOG("LoginPersistentAuth: Login Failed");
                break;
            default:
                OS_LOG("LoginPersistentAuth: Delete persistent auth");
                DeletePersistentAuth();
                break;
        }
    }

    /** Update native UI */
    LoginStateChanged(bSuccessful);
}

/** Callback to handle result of attempting to delete any secure credentials on the device */
void EOS_CALL AuthDeletePersistentAuthCb(const EOS_Auth_DeletePersistentAuthCallbackInfo *Data) {
    std::string result = std::string("Delete PersistentAuth: Result=") + EOS_EResult_ToString(Data->ResultCode);
    OS_LOG(result.c_str());

    bool bSuccessful = Data->ResultCode == EOS_EResult::EOS_Success;
    if (bSuccessful) {
        LocalUserId = nullptr;
        OS_LOG("Delete successful");
    }
}

/** Callback to handle result of attempting a logout */
void EOS_CALL AuthLogoutCb(const EOS_Auth_LogoutCallbackInfo *Data) {
    bool bSuccessful = Data->ResultCode == EOS_EResult::EOS_Success;
    if (bSuccessful) {
        LocalUserId = nullptr;
        // Release any data returned to us from GetLoggedInDisplayName
        if (LocalUserInfo != nullptr) {
            EOS_UserInfo_Release(LocalUserInfo);
            LocalUserInfo = nullptr;
        }
        // Delete any stored secure credentials, now that we have logged out
        DeletePersistentAuth();
    }
}

/** Delete secure stored credentials on this device */
void DeletePersistentAuth() {
    EOS_HAuth AuthHandle = EOS_Platform_GetAuthInterface(PlatformHandle);
    EOS_Auth_DeletePersistentAuthOptions DeletePersistentAuthOptions = {};
    DeletePersistentAuthOptions.ApiVersion = EOS_AUTH_DELETEPERSISTENTAUTH_API_LATEST;
    EOS_Auth_DeletePersistentAuth(AuthHandle, &DeletePersistentAuthOptions, nullptr, AuthDeletePersistentAuthCb);
}

/** Initialize the EOS SDK for use before we call any other functions, normally during application launching
 *  We supply optional internal/external directory */
extern "C" JNIEXPORT jboolean JNICALL
Java_plugin_eos_LuaLoader_nativeInitializeSDK(
        JNIEnv *env,
        jobject /* this */,
        jobject luaStateObj,
        jstring Path) {
    if (IsSDKInitialized) {
        // SDK previously initialized. Skip.
        OS_LOG("EOS_Initialize already initialized");
        return true;
    }

    EOS_InitializeOptions SDKOptions = {0};
    SDKOptions.ApiVersion = EOS_INITIALIZE_API_LATEST;
    SDKOptions.ProductName = "Coromon";
    SDKOptions.ProductVersion = "1.3.6";

    const char *androidPath = env->GetStringUTFChars(Path, nullptr);
    static EOS_Android_InitializeOptions JNIOptions = {0};
    JNIOptions.ApiVersion = EOS_ANDROID_INITIALIZEOPTIONS_API_LATEST;
    JNIOptions.Reserved = nullptr;
    JNIOptions.OptionalInternalDirectory = androidPath;
    JNIOptions.OptionalExternalDirectory = androidPath;
    SDKOptions.SystemInitializeOptions = &JNIOptions;

    lua_State* L = GetLuaStatePointer(env, luaStateObj);
    IsSDKInitialized = InitializeSDK(L, SDKOptions);
    return IsSDKInitialized;
}

extern "C" JNIEXPORT jint JNICALL
Java_plugin_eos_LuaLoader_nativeLoadProducts(
        JNIEnv *env,
        jobject /* this */,
        jobject luaStateObj) {
    lua_State* L = GetLuaStatePointer(env, luaStateObj);
    return OnLoadProducts(L);
}
extern "C" JNIEXPORT jint JNICALL
Java_plugin_eos_LuaLoader_nativePurchase(
        JNIEnv *env,
        jobject /* this */,
        jobject luaStateObj) {
    lua_State* L = GetLuaStatePointer(env, luaStateObj);
    return OnPurchaseProduct(L);
}

extern "C" JNIEXPORT jint JNICALL
Java_plugin_eos_LuaLoader_nativeRestorePurchases(
        JNIEnv *env,
        jobject /* this */,
        jobject luaStateObj) {
    lua_State* L = GetLuaStatePointer(env, luaStateObj);
    return OnRestorePurchases(L);
}


extern "C" JNIEXPORT jint JNICALL
Java_plugin_eos_LuaLoader_nativeFinishTransaction(
        JNIEnv *env,
        jobject /* this */,
        jobject luaStateObj) {
    lua_State* L = GetLuaStatePointer(env, luaStateObj);
    return OnFinishTransaction(L);
}

extern "C" JNIEXPORT jboolean JNICALL
Java_plugin_eos_LuaLoader_nativeIsLoggedOn(
        JNIEnv *env,
        jobject /* this */,
        jobject luaStateObj) {
    lua_State* L = GetLuaStatePointer(env, luaStateObj);
    return OnIsLoggedOn(L);
}

extern "C" JNIEXPORT jboolean JNICALL
Java_plugin_eos_LuaLoader_nativeLoginWithAccountPortal(
        JNIEnv *env,
        jobject /* this */,
        jobject luaStateObj) {
    lua_State* L = GetLuaStatePointer(env, luaStateObj);
    return OnLoginWithAccountPortal(L);
}

extern "C" JNIEXPORT jboolean JNICALL
Java_plugin_eos_LuaLoader_nativeAddEventListener(
        JNIEnv *env,
        jobject /* this */,
        jobject luaStateObj) {
    lua_State* L = GetLuaStatePointer(env, luaStateObj);
    return OnAddEventListener(L);
}

extern "C" JNIEXPORT jboolean JNICALL
Java_plugin_eos_LuaLoader_nativeRemoveEventListener(
        JNIEnv *env,
        jobject /* this */,
        jobject luaStateObj) {
    lua_State* L = GetLuaStatePointer(env, luaStateObj);
    return OnRemoveEventListener(L);
}

extern "C" JNIEXPORT jboolean JNICALL
Java_plugin_eos_LuaLoader_nativeLogout(
        JNIEnv *env,
        jobject /* this */,
        jobject luaStateObj) {
    lua_State* L = GetLuaStatePointer(env, luaStateObj);
    return OnLogout(L);
}

void AuthLogin(const EOS_Auth_LoginOptions &options, const EOS_Auth_OnLoginCallback delegate) {
    EOS_HAuth handle = EOS_Platform_GetAuthInterface(PlatformHandle);
    LoginInProgress();
    EOS_Auth_Login(handle, &options, nullptr, delegate);
}

/** Attempt a login to the EOS Auth Interface with any previously stored secure credentials (as a result of a previous session calling LoginWithAccountPortal successfully)
 *  If no credential exist then the result EOS_NotFound will be returned to indicate the we still need to login for the first time
 *  If credentials do exist they will be maintained across sessions until we call logout
 *  This should be called after createPlatform and before allowing the user any manual login options */
void LoginPersistentAuth() {
    OS_LOG("Performing Persistent login");

    EOS_HAuth AuthHandle = EOS_Platform_GetAuthInterface(PlatformHandle);

    EOS_Auth_Credentials Credentials = {};
    Credentials.ApiVersion = EOS_AUTH_CREDENTIALS_API_LATEST;
    Credentials.Type = EOS_ELoginCredentialType::EOS_LCT_PersistentAuth;
    Credentials.Id = nullptr;
    Credentials.Token = nullptr;

    EOS_Auth_LoginOptions LoginOptions = {};
    LoginOptions.ApiVersion = EOS_AUTH_LOGIN_API_LATEST;
    LoginOptions.Credentials = &Credentials;
    AuthLogin(LoginOptions, PersistentAuthLoginCb);
}

/** Register for updates that reflect changes in the users login status for the EOS Auth Interface */
void AddNotifyLoginStatusChanged() {
    if (NotifyLoginStatusChangedId != EOS_INVALID_NOTIFICATIONID) {
        return;
    }

    EOS_HAuth AuthHandle = EOS_Platform_GetAuthInterface(PlatformHandle);
    EOS_Auth_AddNotifyLoginStatusChangedOptions LoginStatusChangedOptions = {0};
    LoginStatusChangedOptions.ApiVersion = EOS_AUTH_ADDNOTIFYLOGINSTATUSCHANGED_API_LATEST;
    NotifyLoginStatusChangedId = EOS_Auth_AddNotifyLoginStatusChanged(AuthHandle, &LoginStatusChangedOptions, nullptr,
                                                                      AuthNotifyLoginStatusChangedCb);
}

/** Shutdown the EOS SDK, normally during application termination
 *  This is also the safest way to release any created platforms we are tracking
 *  NOTE: initializeSDK and shutdownSDK must be called on the main thread */
void ShutdownSDK() {
    // Release any data returned to us from GetLoggedInDisplayName
    if (LocalUserInfo != nullptr) {
        EOS_UserInfo_Release(LocalUserInfo);
        LocalUserInfo = nullptr;
    }

    EOS_Platform_Release(PlatformHandle);
    PlatformHandle = nullptr;

    EOS_Shutdown();
}

/** Unregister for login status updates for the EOS Auth Interface */
void RemoveNotifyLoginStatusChanged() {
    OS_LOG("RemoveNotifyLoginStatusChanged: Unregister");

    if (NotifyLoginStatusChangedId == EOS_INVALID_NOTIFICATIONID) {
        return;
    }

    EOS_HAuth AuthHandle = EOS_Platform_GetAuthInterface(PlatformHandle);
    EOS_Auth_RemoveNotifyLoginStatusChanged(AuthHandle, NotifyLoginStatusChangedId);
    NotifyLoginStatusChangedId = EOS_INVALID_NOTIFICATIONID;
}

/** Initialize the platform interface using the settings we have obtained from the Developer Portal
 *  This is our hub interface for gaining access to other systems */
extern "C" JNIEXPORT jboolean JNICALL
Java_plugin_eos_LuaLoader_CreatePlatform(
        JNIEnv *env,
        jobject /* this */, jstring ProductID, jstring SandboxID, jstring DeploymentID, jstring ClientID,
        jstring ClientSecret,
        jboolean IsServer, jint Flags) {
    if (PlatformHandle != nullptr) {
        // Platform previously created. Skip.
        OS_LOG("EOS Platform already created");
    } else {
        EOS_Platform_Options PlatformOptions{0};

        PlatformOptions.ApiVersion = EOS_PLATFORM_OPTIONS_API_LATEST;
        PlatformOptions.ProductId = env->GetStringUTFChars(ProductID, nullptr);
        PlatformOptions.SandboxId = env->GetStringUTFChars(SandboxID, nullptr);
        PlatformOptions.DeploymentId = env->GetStringUTFChars(DeploymentID, nullptr);
        PlatformOptions.ClientCredentials.ClientId = env->GetStringUTFChars(ClientID, nullptr);
        PlatformOptions.ClientCredentials.ClientSecret = env->GetStringUTFChars(ClientSecret, nullptr);
        PlatformOptions.bIsServer = IsServer ? EOS_TRUE : EOS_FALSE;
        PlatformOptions.Flags = Flags;

        PlatformHandle = EOS_Platform_Create(&PlatformOptions);
        if (PlatformHandle == nullptr) {
            OS_LOG("EOS Platform creation failed");
            return false;
        }

        OS_LOG("EOS Platform creation successful");
    }

    AddNotifyLoginStatusChanged();
    LoginPersistentAuth();
    return true;
}

/** Attempt to logout of the EOS Auth Interface
 *  If any stored secure credentials exist on the device, they will also be removed */
extern "C" JNIEXPORT void JNICALL
Java_plugin_eos_LuaLoader_Logout(
        JNIEnv *env,
        jobject /* this */) {
    EOS_HAuth AuthHandle = EOS_Platform_GetAuthInterface(PlatformHandle);
    EOS_Auth_LogoutOptions LogoutOptions = {};
    LogoutOptions.ApiVersion = EOS_AUTH_LOGOUT_API_LATEST;
    LogoutOptions.LocalUserId = LocalUserId;
    EOS_Auth_Logout(AuthHandle, &LogoutOptions, nullptr, AuthLogoutCb);
}

extern "C"
JNIEXPORT jint JNICALL
Java_plugin_eos_LuaLoader_nativeGetAuthIdToken(JNIEnv *env, jobject thiz, jobject luaStateObj) {
    lua_State* L = GetLuaStatePointer(env, luaStateObj);
    return OnGetAuthIdToken(L);
}


/** Attempt a login to the EOS Auth Interface using the web account portal */
extern "C" JNIEXPORT void JNICALL
Java_plugin_eos_LuaLoader_LoginWithAccountPortal(
        JNIEnv *env,
        jobject /* this */) {
    EOS_HAuth AuthHandle = EOS_Platform_GetAuthInterface(PlatformHandle);

    EOS_Auth_Credentials Credentials = {};
    Credentials.ApiVersion = EOS_AUTH_CREDENTIALS_API_LATEST;
    Credentials.Type = EOS_ELoginCredentialType::EOS_LCT_AccountPortal;
    Credentials.Id = nullptr;
    Credentials.Token = nullptr;

    EOS_Auth_LoginOptions LoginOptions = {};
    LoginOptions.ApiVersion = EOS_AUTH_LOGIN_API_LATEST;
    LoginOptions.Credentials = &Credentials;
    LoginOptions.ScopeFlags = EOS_EAuthScopeFlags::EOS_AS_BasicProfile | EOS_EAuthScopeFlags::EOS_AS_Presence |
                              EOS_EAuthScopeFlags::EOS_AS_FriendsList;
    AuthLogin(LoginOptions, AuthLoginCb);
}

/** Tick all active platforms so that they can update and processes any in-flight/incoming HTTP requests or services */
extern "C" JNIEXPORT void JNICALL
Java_plugin_eos_LuaLoader_Tick(
        JNIEnv *env,
        jobject
        /* this */) {
    EOS_Platform_Tick(PlatformHandle);
}

/** Suspend signals to the SDK that the application status will change to background */
extern "C"
JNIEXPORT void JNICALL
Java_plugin_eos_LuaLoader_Suspend(JNIEnv *env, jobject thiz) {
    EOS_Platform_SetApplicationStatus(PlatformHandle, EOS_EApplicationStatus::EOS_AS_BackgroundSuspended);
}

/** Resume signals to the SDK that the application status will change to foreground */
extern "C"
JNIEXPORT void JNICALL
Java_plugin_eos_LuaLoader_Resume(JNIEnv *env, jobject thiz) {
    EOS_Platform_SetApplicationStatus(PlatformHandle, EOS_EApplicationStatus::EOS_AS_Foreground);
}

void UpdateNetwork(EOS_ENetworkStatus status) {
    EOS_Platform_SetNetworkStatus(PlatformHandle, status);
}

extern "C"
JNIEXPORT void JNICALL
Java_plugin_eos_LuaLoader_NetworkChanged(JNIEnv *env, jobject thiz, jboolean connected) {
    UpdateNetwork(connected ? EOS_ENetworkStatus::EOS_NS_Online : EOS_ENetworkStatus::EOS_NS_Disabled);
}

extern "C"
JNIEXPORT void JNICALL
Java_plugin_eos_LuaLoader_NetworkDisabled(JNIEnv *env, jobject thiz) {
    UpdateNetwork(EOS_ENetworkStatus::EOS_NS_Disabled);
}

/** Store reference to LuaLoader instance */
extern "C"
JNIEXPORT void JNICALL
Java_plugin_eos_LuaLoader_PassLuaLoaderInstance(JNIEnv *env, jobject thiz) {
    GlobalRefLuaLoaderInstance = LocalENV->NewGlobalRef(thiz);
}

/** Called by load.library on Java side
    Stores LuaLoader class for accessing Java methods from JNI */
jint JNI_OnLoad(JavaVM *vm, void *Reserved) {
    if (vm->GetEnv(reinterpret_cast<void **>(&LocalENV), JNI_VERSION_1_6) != JNI_OK) {
        return -1;
    }
    jclass LuaLoader = LocalENV->FindClass("plugin/eos/LuaLoader");
    GlobalRefLuaLoaderClass = reinterpret_cast<jclass>(LocalENV->NewGlobalRef(LuaLoader));
    return JNI_VERSION_1_6;
}

void JNI_OnUnload(JavaVM *vm, void *Reserved) {
    RemoveNotifyLoginStatusChanged();
    ShutdownSDK();
}
