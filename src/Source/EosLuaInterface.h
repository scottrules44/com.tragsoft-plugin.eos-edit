#ifndef EOS_LUA_INTERFACE_H
#define EOS_LUA_INTERFACE_H

extern "C" {
#   include "lua.h"
#   include "lauxlib.h"
}

struct EOSEntitlementData
{
    /** User associated with this entitlement */
    EOS_EpicAccountId UserId;
    /** The EOS_Ecom_EntitlementName */
    std::string Name;
    /** The EOS_Ecom_EntitlementInstanceId */
    std::string InstanceId;
    /** The EOS_Ecom_CatalogItemId */
    std::string CatalogItemId;
    /** If true then this entitlement has been retrieved */
    bool bRedeemed;
};

struct EOSOfferData
{
    /** The EOS_Ecom_CatalogOfferId */
    std::string Id;
    /** The title */
    std::string Title;
    /** The description */
    std::string Description;
    /** The localizedPrice */
    std::string localizedPrice;
    /** True if the price was properly retrieved */
    bool bPriceValid;
};

struct EOSTransactionData
{
    /** The EOS_Ecom_EntitlementId */
    std::string EntitlementId;
    /** The EOS_Ecom_CatalogOfferId */
    std::string OfferId;
    /** The title */
    std::string Receipt;
};

// Declare the function so other .cpp files can call it
extern "C" int InitializeSDK(lua_State* luaStatePointer, EOS_InitializeOptions SDKOptions);
extern "C" int OnIsLoggedOn(lua_State* luaStatePointer);
extern "C" int OnAddEventListener(lua_State* luaStatePointer);
extern "C" int OnRemoveEventListener(lua_State* luaStatePointer);
extern "C" int OnLoginWithAccountPortal(lua_State* luaStatePointer);
extern "C" int OnGetAuthIdToken(lua_State* luaStatePointer);

extern "C" int OnLoadProducts(lua_State* luaStatePointer);
extern "C" int OnPurchaseProduct(lua_State* luaStatePointer);
extern "C" int OnRestorePurchases(lua_State* luaStatePointer);
extern "C" int OnFinishTransaction(lua_State* luaStatePointer);

#endif // EOS_LUA_INTERFACE_H