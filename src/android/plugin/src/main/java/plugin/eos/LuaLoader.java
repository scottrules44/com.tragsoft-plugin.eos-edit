//
//  LuaLoader.java
//  TemplateApp
//
//  Copyright (c) 2012 __MyCompanyName__. All rights reserved.
//

// This corresponds to the name of the Lua library,
// e.g. [Lua] require "plugin.eos"
package plugin.eos;

import static com.ansca.corona.CoronaEnvironment.getApplicationContext;

import android.content.Context;
import android.net.ConnectivityManager;
import android.net.Network;
import android.net.NetworkRequest;
import android.net.wifi.SupplicantState;
import android.net.wifi.WifiInfo;
import android.net.wifi.WifiManager;
import android.os.Handler;
import android.provider.Settings;
import android.telephony.PhoneStateListener;
import android.telephony.TelephonyManager;
import android.util.Log;

import androidx.annotation.NonNull;

import com.ansca.corona.CoronaEnvironment;
import com.ansca.corona.CoronaLua;
import com.ansca.corona.CoronaRuntime;
import com.ansca.corona.CoronaRuntimeListener;
import com.ansca.corona.CoronaRuntimeTask;
import com.naef.jnlua.JavaFunction;
import com.naef.jnlua.LuaState;
import com.naef.jnlua.NamedJavaFunction;

import com.epicgames.mobile.eossdk.EOSSDK;

/**
 * Implements the Lua interface for a Corona plugin.
 * <p>
 * Only one instance of this class will be created by Corona for the lifetime of the application.
 * This instance will be re-used for every new Corona activity that gets created.
 */
@SuppressWarnings({"WeakerAccess", "unused"})
public class LuaLoader implements JavaFunction, CoronaRuntimeListener {
    /**
     * Lua registry ID to the Lua function to be called when the ad request finishes.
     */
    private int fListener;

    private static String Tag = "EOSLuaLoader";

    /**
     * This corresponds to the event name, e.g. [Lua] event.name
     */
    private static final String EVENT_NAME = "pluginLibraryEvent";

    private Handler handler = null;
    private Runnable r = null;

    private boolean loginInProgress = false;
    private Runnable loginRunnable = null;

    private boolean DataAvailable = false;
    private boolean WifiAvailable = false;

    ConnectivityManager connectivityManager = null;
    TelephonyManager telephonyManager = null;
    ConnectivityManager.NetworkCallback networkCallback = null;
    PhoneStateListener phoneStateListener = null;

    /**
     * This will maintain calling itself every 10th of a second after the initial trigger
     */

    private void checkConnectivity() {
        // Android allows Wifi connections while on Airplane mode, but no data
        // Check priority:
        // 		1 - Wifi
        // 		2 - Airplane mode
        // 		3 - Data
        if (WifiAvailable) {
            NetworkChanged(true);
        }
        // Android can be on Airplane mode AND connected to wifi
        else if (Settings.System.getInt(getApplicationContext().getContentResolver(), Settings.Global.AIRPLANE_MODE_ON, 0) != 0) {
            Log.d(Tag, "No active network, disabled");
            NetworkDisabled();
        } else {
            NetworkChanged(DataAvailable);
        }
    }

    /**
     * Register to network connectivity changes
     */
    void registerNetworkCallback() {
        NetworkRequest networkRequest = new NetworkRequest.Builder().build();
        networkCallback = new ConnectivityManager.NetworkCallback() {
            @Override
            public void onAvailable(@NonNull Network network) {
                super.onAvailable(network);
                updateNetworkConnection();
            }

            @Override
            public void onLost(@NonNull Network network) {
                super.onLost(network);
                updateNetworkConnection();
            }

        };
        connectivityManager.registerNetworkCallback(networkRequest, networkCallback);
    }

    void updateNetworkConnection() {
        WifiManager wifiManager = (WifiManager) getApplicationContext().getSystemService(Context.WIFI_SERVICE);
        if (wifiManager.isWifiEnabled()) {
            WifiInfo info = wifiManager.getConnectionInfo();
            WifiAvailable = info.getSupplicantState() == SupplicantState.COMPLETED;
        } else {
            WifiAvailable = false;
        }
        checkConnectivity();
    }

    /**
     * Register to data connectivity changes
     */
    void registerDataCallback() {
        phoneStateListener = new PhoneStateListener() {
            @Override
            public void onDataConnectionStateChanged(int state, int networkType) {
                super.onDataConnectionStateChanged(state, networkType);
                String message = "Current data network: ";
                switch (networkType) {
                    case TelephonyManager.NETWORK_TYPE_EDGE:
                    case TelephonyManager.NETWORK_TYPE_GPRS:
                    case TelephonyManager.NETWORK_TYPE_CDMA:
                        Log.d(Tag, message + " 2G");
                        DataAvailable = false;
                        break;

                    case TelephonyManager.NETWORK_TYPE_UMTS:
                    case TelephonyManager.NETWORK_TYPE_HSDPA:
                    case TelephonyManager.NETWORK_TYPE_HSPA:
                    case TelephonyManager.NETWORK_TYPE_HSPAP:
                        Log.d(Tag, message + " 3G");
                        DataAvailable = true;
                        break;

                    case TelephonyManager.NETWORK_TYPE_LTE:
                        Log.d(Tag, message + " 4G");
                        DataAvailable = true;
                        break;

                    case TelephonyManager.NETWORK_TYPE_NR:
                        Log.d(Tag, message + " 5G");
                        DataAvailable = true;
                        break;

                    default:
                        Log.d(Tag, message + " unknown");
                        DataAvailable = false;
                        break;
                }

                checkConnectivity();
            }
        };
        telephonyManager.listen(phoneStateListener, PhoneStateListener.LISTEN_DATA_CONNECTION_STATE);
    }

    /**
     * Unregister to network connectivity changes
     */
    void unregisterNetworkCallback() {
        if (networkCallback != null) {
            connectivityManager.unregisterNetworkCallback(networkCallback);
            networkCallback = null;
        }
    }

    /**
     * Unregister to data connectivity changes
     */
    void unregisterDataCallback() {
        if (phoneStateListener != null) {
            telephonyManager.listen(phoneStateListener, PhoneStateListener.LISTEN_NONE);
            phoneStateListener = null;
        }
    }

    /**
     * Creates a new Lua interface to this plugin.
     * <p>
     * Note that a new LuaLoader instance will not be created for every CoronaActivity instance.
     * That is, only one instance of this class will be created for the lifetime of the application process.
     * This gives a plugin the option to do operations in the background while the CoronaActivity is destroyed.
     */
    @SuppressWarnings("unused")
    public LuaLoader() {
        // Initialize member variables.
        fListener = CoronaLua.REFNIL;

        // Set up this plugin to listen for Corona runtime events to be received by methods
        // onLoaded(), onStarted(), onSuspended(), onResumed(), and onExiting().
        CoronaEnvironment.addRuntimeListener(this);
    }

    /**
     * Called when this plugin is being loaded via the Lua require() function.
     * <p>
     * Note that this method will be called every time a new CoronaActivity has been launched.
     * This means that you'll need to re-initialize this plugin here.
     * <p>
     * Warning! This method is not called on the main UI thread.
     *
     * @param L Reference to the Lua state that the require() function was called from.
     * @return Returns the number of values that the require() function will return.
     * <p>
     * Expected to return 1, the library that the require() function is loading.
     */
    @Override
    public int invoke(LuaState L) {
        // Register this plugin into Lua with the following functions.
        NamedJavaFunction[] luaFunctions = new NamedJavaFunction[]{
                new InitWrapper(),
                new IsLoggedOnWrapper(),
                new LoginWithAccountPortalWrapper(),
                new AddEventListenerWrapper(),
                new RemoveEventListenerWrapper(),
                new GetAuthIdTokenWrapper(),
                new LogoutWrapper(),
                new RestoreWrapper(),
                new PurchaseWrapper(),
                new LoadProductsWrapper(),
                new FinishTransactionWrapper(),
        };

        String libName = L.toString(1);
        L.register(libName, luaFunctions);

        // Returning 1 indicates that the Lua require() function will return the above Lua library.
        return 1;
    }

    /**
     * Called after the Corona runtime has been created and just before executing the "main.lua" file.
     * <p>
     * Warning! This method is not called on the main thread.
     *
     * @param runtime Reference to the CoronaRuntime object that has just been loaded/initialized.
     *                Provides a LuaState object that allows the application to extend the Lua API.
     */
    @Override
    public void onLoaded(CoronaRuntime runtime) {
        // Note that this method will not be called the first time a Corona activity has been launched.
        // This is because this listener cannot be added to the CoronaEnvironment until after
        // this plugin has been required-in by Lua, which occurs after the onLoaded() event.
        // However, this method will be called when a 2nd Corona activity has been created.
        System.loadLibrary("EOSSDK");
        System.loadLibrary("native-lib");
    }

    /**
     * Called just after the Corona runtime has executed the "main.lua" file.
     * <p>
     * Warning! This method is not called on the main thread.
     *
     * @param runtime Reference to the CoronaRuntime object that has just been started.
     */
    @Override
    public void onStarted(CoronaRuntime runtime) {
    }

    /**
     * Called just after the Corona runtime has been suspended which pauses all rendering, audio, timers,
     * and other Corona related operations. This can happen when another Android activity (ie: window) has
     * been displayed, when the screen has been powered off, or when the screen lock is shown.
     * <p>
     * Warning! This method is not called on the main thread.
     *
     * @param runtime Reference to the CoronaRuntime object that has just been suspended.
     */
    @Override
    public void onSuspended(CoronaRuntime runtime) {
        Suspend();
    }

    /**
     * Called just after the Corona runtime has been resumed after a suspend.
     * <p>
     * Warning! This method is not called on the main thread.
     *
     * @param runtime Reference to the CoronaRuntime object that has just been resumed.
     */
    @Override
    public void onResumed(CoronaRuntime runtime) {
        Resume();

        if (loginInProgress) {
            loginRunnable = new Runnable() {
                @Override
                public void run() {
                    if (loginInProgress) {
                        LoginStateChanged(false);
                    }
                }
            };

            Handler handler = new Handler();
            handler.postDelayed(
                    loginRunnable, // Runnable
                    5000 // Delay in milliseconds
            );
        }
    }

    /**
     * Called just before the Corona runtime terminates.
     * <p>
     * This happens when the Corona activity is being destroyed which happens when the user presses the Back button
     * on the activity, when the native.requestExit() method is called in Lua, or when the activity's finish()
     * method is called. This does not mean that the application is exiting.
     * <p>
     * Warning! This method is not called on the main thread.
     *
     * @param runtime Reference to the CoronaRuntime object that is being terminated.
     */
    @Override
    public void onExiting(CoronaRuntime runtime) {
        // Remove the Lua listener reference.
        CoronaLua.deleteRef(runtime.getLuaState(), fListener);
        fListener = CoronaLua.REFNIL;

        if (handler != null) {
            handler.removeCallbacks(r);
        }
        unregisterNetworkCallback();
        unregisterDataCallback();
    }

    /**
     * Simple example on how to dispatch events to Lua. Note that events are dispatched with
     * Runtime dispatcher. It ensures that Lua is accessed on it's thread to avoid race conditions
     *
     * @param message simple string to sent to Lua in 'message' field.
     */
    @SuppressWarnings("unused")
    public void dispatchEvent(final String message) {
        CoronaEnvironment.getCoronaActivity().getRuntimeTaskDispatcher().send(new CoronaRuntimeTask() {
            @Override
            public void executeUsing(CoronaRuntime runtime) {
                LuaState L = runtime.getLuaState();

                CoronaLua.newEvent(L, EVENT_NAME);

                L.pushString(message);
                L.setField(-2, "message");

                try {
                    CoronaLua.dispatchEvent(L, fListener, 0);
                } catch (Exception ignored) {
                }
            }
        });
    }

    /**
     * The following Lua function has been called:  eos.init( listener )
     * <p>
     * Warning! This method is not called on the main thread.
     *
     * @param L Reference to the Lua state that the Lua function was called from.
     * @return Returns the number of values to be returned by the eos.init() function.
     */
    @SuppressWarnings({"WeakerAccess", "SameReturnValue"})
    public int init(LuaState L) {
        int listenerIndex = 1;

        if (CoronaLua.isListener(L, listenerIndex, EVENT_NAME)) {
            fListener = CoronaLua.newRef(L, listenerIndex);
        }

        /** Pass instance of main activity class to JNI for calling UI functions */
        PassLuaLoaderInstance();

        /** Pass Application context to EOS SDK Java side */
        EOSSDK.init(CoronaEnvironment.getCoronaActivity());

        /** Initialize SDK and start tick */
        boolean result = nativeInitializeSDK(L, getApplicationContext().getFilesDir().getAbsolutePath() + "/");
        if (!result) {
            Log.e(Tag, "Failed to initialize SDK");
            return 0;
        }

        return 0;
    }

    /**
     * Implements the eos.init() Lua function.
     */
    @SuppressWarnings("unused")
    private class InitWrapper implements NamedJavaFunction {
        @Override
        public String getName() {
            return "init";
        }

        @Override
        public int invoke(LuaState L) {
            return init(L);
        }
    }

    /**
     * Implements the eos.addEventListener() Lua function.
     */
    @SuppressWarnings("unused")
    private class AddEventListenerWrapper implements NamedJavaFunction {
        @Override
        public String getName() {
            return "addEventListener";
        }

        @Override
        public int invoke(LuaState L) {
            nativeAddEventListener(L);
            return 0;
        }
    }

    /**
     * Implements the eos.removeEventListener() Lua function.
     */
    @SuppressWarnings("unused")
    private class RemoveEventListenerWrapper implements NamedJavaFunction {
        @Override
        public String getName() {
            return "removeEventListener";
        }

        @Override
        public int invoke(LuaState L) {
            nativeRemoveEventListener(L);
            return 0;
        }
    }

    /**
     * Implements the eos.loginWithAccountPortal() Lua function.
     */
    @SuppressWarnings("unused")
    private class LoginWithAccountPortalWrapper implements NamedJavaFunction {
        @Override
        public String getName() {
            return "loginWithAccountPortal";
        }

        @Override
        public int invoke(LuaState L) {
            nativeLoginWithAccountPortal(L);
            return 0;
        }
    }

    /**
     * Implements the eos.isLoggedOn() Lua function.
     */
    @SuppressWarnings("unused")
    private class IsLoggedOnWrapper implements NamedJavaFunction {
        @Override
        public String getName() {
            return "isLoggedOn";
        }

        @Override
        public int invoke(LuaState L) {
            nativeIsLoggedOn(L);
            return 1;
        }
    }

    /**
     * Implements the eos.getAuthIdToken() Lua function.
     */
    @SuppressWarnings("unused")
    private class GetAuthIdTokenWrapper implements NamedJavaFunction {
        @Override
        public String getName() {
            return "getAuthIdToken";
        }

        @Override
        public int invoke(LuaState L) {
            return nativeGetAuthIdToken(L);
        }
    }
    
    /**
     * Implements the eos.purchase() Lua function.
     */
    @SuppressWarnings("unused")
    private class FinishTransactionWrapper implements NamedJavaFunction {
        @Override
        public String getName() {
            return "finishTransaction";
        }

        @Override
        public int invoke(LuaState L) {
            return nativeFinishTransaction(L);
        }
    }

    /**
     * Implements the eos.logout() Lua function.
     */
    @SuppressWarnings("unused")
    private class LogoutWrapper implements NamedJavaFunction {
        @Override
        public String getName() {
            return "logout";
        }

        @Override
        public int invoke(LuaState L) {
            nativeLogout(L);
            return 0;
        }
    }
    /**
     * Implements the eos.purchase() Lua function.
     */
    @SuppressWarnings("unused")
    private class PurchaseWrapper implements NamedJavaFunction {
        @Override
        public String getName() {
            return "purchase";
        }

        @Override
        public int invoke(LuaState L) {
            return nativePurchase(L);
        }
    }
    
    /**
     * Implements the eos.restore() Lua function.
     */
    @SuppressWarnings("unused")
    private class LoadProductsWrapper implements NamedJavaFunction {
        @Override
        public String getName() {
            return "loadProducts";
        }

        @Override
        public int invoke(LuaState L) {
            return nativeLoadProducts(L);
        }
    }

    /**
     * Implements the eos.restore() Lua function.
     */
    @SuppressWarnings("unused")
    private class RestoreWrapper implements NamedJavaFunction {
        @Override
        public String getName() {
            return "restore";
        }

        @Override
        public int invoke(LuaState L) {
            return nativeRestorePurchases(L);
        }
    }

    /**
     * Helper function to hide/show Login/Logout button
     */
    public void LoginStateChanged(boolean loggedIn) {
        loginInProgress = false;
    }

    public void LoginInProgress() {
        loginInProgress = true;
    }

    /**
     * Helper function to view text on screen
     */
    public void ShowText(String text) {
        Log.d("EOSSDK LuaLoader", text);
    }

    /**
     * Native functions callable from java
     */

    public native boolean CreatePlatform(String ProductID, String SandboxID, String DeploymentId, String ClientId, String ClientSecret, boolean IsServer, int Flags);

    public native void Logout();

    public native void Suspend();

    public native void Resume();

    public native void NetworkChanged(boolean connected);

    public native void NetworkDisabled();

    public native void LoginWithAccountPortal();

    public native int nativeLoadProducts(LuaState luaStatePointer);

    public native int nativePurchase(LuaState luaStatePointer);

    public native int nativeRestorePurchases(LuaState luaStatePointer);

    public native int nativeFinishTransaction(LuaState luaStatePointer);

    public native boolean nativeIsLoggedOn(LuaState luaStatePointer);

    public native boolean nativeAddEventListener(LuaState luaStatePointer);

    public native boolean nativeRemoveEventListener(LuaState luaStatePointer);

    public native boolean nativeInitializeSDK(LuaState luaStatePointer, String Path);

    public native boolean nativeLoginWithAccountPortal(LuaState luaStatePointer);

    public native int nativeGetAuthIdToken(LuaState luaStatePointer);

    public native boolean nativeLogout(LuaState luaStatePointer);

    public native String GetUsername();

    public native void PassLuaLoaderInstance();

}
