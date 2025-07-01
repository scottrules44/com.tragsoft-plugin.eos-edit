// --------------------------------------------------------------------------------
// 
// DispatchEventTask.cpp
// Copyright (c) 2016 Corona Labs Inc. All rights reserved.
// This software may be modified and distributed under the terms
// of the MIT license.  See the LICENSE file for details.
//
// --------------------------------------------------------------------------------

#include "DispatchEventTask.h"
#include "CoronaLua.h"
#include "EosLuaInterface.h"
#include "RuntimeContext.h"
#include <sstream>
#include <string>
#include "eos_ecom.h"
#include <iomanip>

//---------------------------------------------------------------------------------
// BaseDispatchEventTask Class Members
//---------------------------------------------------------------------------------

BaseDispatchEventTask::BaseDispatchEventTask() {
}

BaseDispatchEventTask::~BaseDispatchEventTask() {
}

std::shared_ptr<LuaEventDispatcher> BaseDispatchEventTask::GetLuaEventDispatcher() const {
    return fLuaEventDispatcherPointer;
}

void BaseDispatchEventTask::SetLuaEventDispatcher(
        const std::shared_ptr<LuaEventDispatcher> &dispatcherPointer) {
    fLuaEventDispatcherPointer = dispatcherPointer;
}

bool BaseDispatchEventTask::Execute() {
    // Do not continue if not assigned a Lua event dispatcher.
    if (!fLuaEventDispatcherPointer) {
        return false;
    }

    // Fetch the Lua state the event dispatcher belongs to.
    auto luaStatePointer = fLuaEventDispatcherPointer->GetLuaState();
    if (!luaStatePointer) {
        return false;
    }

    // Push the derived class' event table to the top of the Lua stack.
    bool wasPushed = PushLuaEventTableTo(luaStatePointer);
    if (!wasPushed) {
        return false;
    }

    // Dispatch the event to all subscribed Lua listeners.
    bool wasDispatched = fLuaEventDispatcherPointer->DispatchEventWithoutResult(luaStatePointer,
                                                                                -1);

    // Pop the event table pushed above from the Lua stack.
    // Note: The DispatchEventWithoutResult() method above does not pop off this table.
    lua_pop(luaStatePointer, 1);

    // Return true if the event was successfully dispatched to Lua.
    return wasDispatched;
}


//---------------------------------------------------------------------------------
// BaseDispatchCallResultEventTask Class Members
//---------------------------------------------------------------------------------

BaseDispatchCallResultEventTask::BaseDispatchCallResultEventTask()
        : fHadIOFailure(false) {
}

BaseDispatchCallResultEventTask::~BaseDispatchCallResultEventTask() {
}

bool BaseDispatchCallResultEventTask::HadIOFailure() const {
    return fHadIOFailure;
}

void BaseDispatchCallResultEventTask::SetHadIOFailure(bool value) {
    fHadIOFailure = value;
}

//---------------------------------------------------------------------------------
// DispatchLoginResponseEventTask Class Members
//---------------------------------------------------------------------------------

const char DispatchLoginResponseEventTask::kLuaEventName[] = "loginResponse";

DispatchLoginResponseEventTask::DispatchLoginResponseEventTask()
        : fResult(EOS_EResult::EOS_UnexpectedError),
          fSelectedAccountID("") {
}

DispatchLoginResponseEventTask::~DispatchLoginResponseEventTask() {
}

void DispatchLoginResponseEventTask::AcquireEventDataFrom(
        const EOS_Auth_LoginCallbackInfo *eosEventData) {
    fResult = eosEventData->ResultCode;
    int sz = 0;
    if (fResult == EOS_EResult::EOS_Success && eosEventData->SelectedAccountId) {
        sz = EOS_EPICACCOUNTID_MAX_LENGTH + 1;
        EOS_EpicAccountId_ToString(eosEventData->SelectedAccountId, fSelectedAccountID, &sz);
    }
    fSelectedAccountID[sz] = 0;
}

const char *DispatchLoginResponseEventTask::GetLuaEventName() const {
    return kLuaEventName;
}

bool DispatchLoginResponseEventTask::PushLuaEventTableTo(lua_State *luaStatePointer) const {
    // Validate.
    if (!luaStatePointer) {
        return false;
    }

    // Push the event data to Lua.
    CoronaLuaNewEvent(luaStatePointer, kLuaEventName);

    if (fResult == EOS_EResult::EOS_Success) {
        lua_pushstring(luaStatePointer, fSelectedAccountID);
        lua_setfield(luaStatePointer, -2, "selectedAccountId");
    }

    lua_pushboolean(luaStatePointer, fResult != EOS_EResult::EOS_Success ? 1 : 0);
    lua_setfield(luaStatePointer, -2, "isError");
    lua_pushinteger(luaStatePointer, (int) fResult);
    lua_setfield(luaStatePointer, -2, "resultCode");
    return true;
}

//---------------------------------------------------------------------------------
// DispatchLoadProductsEventTask Class Members
//---------------------------------------------------------------------------------

const char DispatchLoadProductsEventTask::kLuaEventName[] = "loadProducts";

DispatchLoadProductsEventTask::DispatchLoadProductsEventTask()
        : fResult(EOS_EResult::EOS_UnexpectedError) {
}

DispatchLoadProductsEventTask::~DispatchLoadProductsEventTask() {
}

void DispatchLoadProductsEventTask::AcquireEventDataFrom(
        const EOS_Ecom_QueryOffersCallbackInfo *eosEventData) {
    fResult = eosEventData->ResultCode;

    int selectedAccountIDLength = 0;
    if (fResult == EOS_EResult::EOS_Success && eosEventData->LocalUserId) {
        selectedAccountIDLength = EOS_EPICACCOUNTID_MAX_LENGTH + 1;
        EOS_EpicAccountId_ToString(eosEventData->LocalUserId, fSelectedAccountID,
                                   &selectedAccountIDLength);
    }
    fSelectedAccountID[selectedAccountIDLength] = 0;

    if (fResult != EOS_EResult::EOS_Success) {
        return;
    }

    auto contextPointer = (RuntimeContext *) eosEventData->ClientData;
    if (!contextPointer) {
        return;
    }

    auto eosPlatformHandle = contextPointer->fPlatformHandle;
    if (!eosPlatformHandle) {
        return;
    }

    EOS_HEcom EcomHandle = EOS_Platform_GetEcomInterface(eosPlatformHandle);

    EOS_Ecom_GetOfferCountOptions CountOptions{0};
    CountOptions.ApiVersion = EOS_ECOM_GETOFFERCOUNT_API_LATEST;
    CountOptions.LocalUserId = eosEventData->LocalUserId;
    uint32_t OfferCount = EOS_Ecom_GetOfferCount(EcomHandle, &CountOptions);

    CoronaLog("[EOS SDK] NumOffers: %d", OfferCount);

    fOffers.reserve(OfferCount);

    EOS_Ecom_CopyOfferByIndexOptions IndexOptions{0};
    IndexOptions.ApiVersion = EOS_ECOM_COPYOFFERBYINDEX_API_LATEST;
    IndexOptions.LocalUserId = eosEventData->LocalUserId;
    for (IndexOptions.OfferIndex = 0;
         IndexOptions.OfferIndex < OfferCount; ++IndexOptions.OfferIndex) {
        EOS_Ecom_CatalogOffer *Offer;
        EOS_EResult CopyResult = EOS_Ecom_CopyOfferByIndex(EcomHandle, &IndexOptions, &Offer);
        switch (CopyResult) {
            case EOS_EResult::EOS_Success:
            case EOS_EResult::EOS_Ecom_CatalogOfferPriceInvalid:
            case EOS_EResult::EOS_Ecom_CatalogOfferStale: {
                CoronaLog(
                        "[EOS SDK] Offer[%d] id(%ls) title(%ls) Price[Result(%d) Curr(%ull) Original(%ull) DecimalPoint(%ud)] Available?(%ls) Limit[%d]",
                        IndexOptions.OfferIndex,
                        Offer->Id,
                        Offer->TitleText,
                        Offer->PriceResult, Offer->CurrentPrice64, Offer->OriginalPrice64,
                        Offer->DecimalPoint,
                        Offer->bAvailableForPurchase ? L"true" : L"false",
                        Offer->PurchaseLimit);

                std::ostringstream localizedPriceStream;
                localizedPriceStream << Offer->CurrencyCode << std::fixed
                                     << std::setprecision(static_cast<int>(Offer->DecimalPoint))
                                     << static_cast<double>(Offer->CurrentPrice64) /
                                        std::pow(10.0, Offer->DecimalPoint);
                fOffers.push_back(EOSOfferData{
                        std::string(Offer->Id),
                        std::string(Offer->TitleText),
                        std::string(Offer->DescriptionText),
                        localizedPriceStream.str(),
                        Offer->PriceResult == EOS_EResult::EOS_Success,
                });

                EOS_Ecom_CatalogOffer_Release(Offer);
                break;
            }
            default:
                CoronaLog("[EOS SDK] Offer[%d] invalid : %d", IndexOptions.OfferIndex, CopyResult);
                break;
        }
    }
}

const char *DispatchLoadProductsEventTask::GetLuaEventName() const {
    return kLuaEventName;
}

bool DispatchLoadProductsEventTask::PushLuaEventTableTo(lua_State *luaStatePointer) const {
    // Validate.
    if (!luaStatePointer) {
        return false;
    }

    // Push the event data to Lua.
    CoronaLuaNewEvent(luaStatePointer, kLuaEventName);

    lua_pushboolean(luaStatePointer, fResult != EOS_EResult::EOS_Success ? 1 : 0);
    lua_setfield(luaStatePointer, -2, "isError");
    if (fResult == EOS_EResult::EOS_Success) {
        lua_pushstring(luaStatePointer, fSelectedAccountID);
        lua_setfield(luaStatePointer, -2, "selectedAccountId");

        lua_pushinteger(luaStatePointer, (int) fResult);
        lua_setfield(luaStatePointer, -2, "resultCode");

        lua_createtable(luaStatePointer, (int) fOffers.size(), 0);
        for (int index = 0; index < (int) fOffers.size(); index++) {
            const EOSOfferData &offer = fOffers.at(index);
            lua_newtable(luaStatePointer);

            lua_pushstring(luaStatePointer, (const char *) offer.Title.c_str());
            lua_setfield(luaStatePointer, -2, "title");

            lua_pushstring(luaStatePointer, "");
            lua_setfield(luaStatePointer, -2, "description");

            lua_pushstring(luaStatePointer, (const char *) offer.Id.c_str());
            lua_setfield(luaStatePointer, -2, "productIdentifier");

            lua_pushstring(luaStatePointer, (const char *) offer.localizedPrice.c_str());
            lua_setfield(luaStatePointer, -2, "localizedPrice");

            lua_pushboolean(luaStatePointer, offer.bPriceValid);
            lua_setfield(luaStatePointer, -2, "priceIsValid");

            lua_rawseti(luaStatePointer, -2, index + 1);
        }
        lua_setfield(luaStatePointer, -2, "products");
    }

    return true;
}

//---------------------------------------------------------------------------------
// DispatchStoreTransactionCheckoutEventTask Class Members
//---------------------------------------------------------------------------------

const char DispatchStoreTransactionCheckoutEventTask::kLuaEventName[] = "storeTransaction";

DispatchStoreTransactionCheckoutEventTask::DispatchStoreTransactionCheckoutEventTask()
        : fResult(EOS_EResult::EOS_UnexpectedError),
          fSelectedAccountID("") {
}

DispatchStoreTransactionCheckoutEventTask::~DispatchStoreTransactionCheckoutEventTask() {
}

void DispatchStoreTransactionCheckoutEventTask::AcquireEventDataFrom(
        const EOS_Ecom_CheckoutCallbackInfo *eosEventData) {
    fResult = eosEventData->ResultCode;

    int sz = 0;
    if (fResult == EOS_EResult::EOS_Success && eosEventData->LocalUserId) {
        sz = EOS_EPICACCOUNTID_MAX_LENGTH + 1;
        EOS_EpicAccountId_ToString(eosEventData->LocalUserId, fSelectedAccountID, &sz);
    }
    fSelectedAccountID[sz] = 0;

    if (fResult != EOS_EResult::EOS_Success) {
        return;
    }

    if (eosEventData->TransactionId)
    {
        auto contextPointer = (RuntimeContext *) eosEventData->ClientData;
        if (!contextPointer) {
            return;
        }

        auto eosPlatformHandle = contextPointer->fPlatformHandle;
        if (!eosPlatformHandle) {
            return;
        }

        EOS_Ecom_HTransaction TransactionHandle;

        EOS_Ecom_CopyTransactionByIdOptions CopyTransactionOptions{ 0 };
        CopyTransactionOptions.ApiVersion = EOS_ECOM_COPYTRANSACTIONBYID_API_LATEST;
        CopyTransactionOptions.LocalUserId = eosEventData->LocalUserId;
        CopyTransactionOptions.TransactionId = eosEventData->TransactionId;

        EOS_HEcom EcomHandle = EOS_Platform_GetEcomInterface(eosPlatformHandle);
        if (EOS_Ecom_CopyTransactionById(EcomHandle, &CopyTransactionOptions, &TransactionHandle) == EOS_EResult::EOS_Success)
        {
            EOS_Ecom_Transaction_GetEntitlementsCountOptions CountOptions{ 0 };
            CountOptions.ApiVersion = EOS_ECOM_TRANSACTION_GETENTITLEMENTSCOUNT_API_LATEST;
            uint32_t EntitlementCount = EOS_Ecom_Transaction_GetEntitlementsCount(TransactionHandle, &CountOptions);

            CoronaLog("[EOS SDK] New Entitlements: %d", EntitlementCount);

            std::vector<EOSEntitlementData> NewEntitlements;
            NewEntitlements.reserve(EntitlementCount);

            EOS_Ecom_Transaction_CopyEntitlementByIndexOptions IndexOptions{ 0 };
            IndexOptions.ApiVersion = EOS_ECOM_TRANSACTION_COPYENTITLEMENTBYINDEX_API_LATEST;
            for (IndexOptions.EntitlementIndex = 0; IndexOptions.EntitlementIndex < EntitlementCount; ++IndexOptions.EntitlementIndex)
            {
                EOS_Ecom_Entitlement* Entitlement;
                EOS_EResult CopyResult = EOS_Ecom_Transaction_CopyEntitlementByIndex(TransactionHandle, &IndexOptions, &Entitlement);
                switch (CopyResult)
                {
                    case EOS_EResult::EOS_Success:
                    case EOS_EResult::EOS_Ecom_EntitlementStale:
                        CoronaLog("[EOS SDK] New Entitlement[%d] : %ls : %ls : %ls",
                                  IndexOptions.EntitlementIndex,
                                  Entitlement->EntitlementId,
                                  Entitlement->EntitlementName,
                                  Entitlement->bRedeemed ? L"TRUE" : L"FALSE");

                        NewEntitlements.push_back(EOSEntitlementData{
                                eosEventData->LocalUserId,
                                std::string(Entitlement->EntitlementName),
                                std::string(Entitlement->EntitlementId),
                                std::string(Entitlement->CatalogItemId),
                                Entitlement->bRedeemed == EOS_TRUE
                        });

                        EOS_Ecom_Entitlement_Release(Entitlement);
                        break;
                    default:
                        CoronaLog("[EOS SDK] New Entitlement[%d] invalid : %d", IndexOptions.EntitlementIndex, CopyResult);
                        break;
                }
            }

            EOS_Ecom_Transaction_Release(TransactionHandle);
        }
    }
}

const char *DispatchStoreTransactionCheckoutEventTask::GetLuaEventName() const {
    return kLuaEventName;
}

bool DispatchStoreTransactionCheckoutEventTask::PushLuaEventTableTo(lua_State *luaStatePointer) const {
    // Validate.
    if (!luaStatePointer) {
        return false;
    }

    // Push the event data to Lua.
    CoronaLuaNewEvent(luaStatePointer, kLuaEventName);

    if (fResult == EOS_EResult::EOS_Success) {
        lua_pushstring(luaStatePointer, fSelectedAccountID);
        lua_setfield(luaStatePointer, -2, "selectedAccountId");
    }

    lua_pushboolean(luaStatePointer, fResult != EOS_EResult::EOS_Success ? 1 : 0);
    lua_setfield(luaStatePointer, -2, "isError");
    lua_pushinteger(luaStatePointer, (int) fResult);
    lua_setfield(luaStatePointer, -2, "resultCode");

    lua_createtable(luaStatePointer, (int) fEntitlements.size(), 0);
    for (int index = 0; index < (int) fEntitlements.size(); index++) {
        const EOSEntitlementData &entitlement = fEntitlements.at(index);
        lua_newtable(luaStatePointer);

        // We use CatalogItemId as receipt, since that is what is used in the Web API
        lua_pushstring(luaStatePointer, (const char *) entitlement.CatalogItemId.c_str());
        lua_setfield(luaStatePointer, -2, "receipt");

        lua_pushstring(luaStatePointer, (const char *) entitlement.InstanceId.c_str());
        lua_setfield(luaStatePointer, -2, "identifier");

        lua_pushstring(luaStatePointer, (const char *) entitlement.CatalogItemId.c_str());
        lua_setfield(luaStatePointer, -2, "productIdentifier");

        lua_rawseti(luaStatePointer, -2, index + 1);
    }
    lua_setfield(luaStatePointer, -2, "transactions");

    return true;
}

//---------------------------------------------------------------------------------
// DispatchStoreTransactionQueryEntitlementsEventTask Class Members
//---------------------------------------------------------------------------------

const char DispatchStoreTransactionQueryEntitlementsEventTask::kLuaEventName[] = "storeTransaction";

DispatchStoreTransactionQueryEntitlementsEventTask::DispatchStoreTransactionQueryEntitlementsEventTask()
        : fResult(EOS_EResult::EOS_UnexpectedError),
          fSelectedAccountID("") {
}

DispatchStoreTransactionQueryEntitlementsEventTask::~DispatchStoreTransactionQueryEntitlementsEventTask() {
}

void DispatchStoreTransactionQueryEntitlementsEventTask::AcquireEventDataFrom(
        const EOS_Ecom_QueryEntitlementsCallbackInfo *eosEventData) {
    fResult = eosEventData->ResultCode;

    int sz = 0;
    if (fResult == EOS_EResult::EOS_Success && eosEventData->LocalUserId) {
        sz = EOS_EPICACCOUNTID_MAX_LENGTH + 1;
        EOS_EpicAccountId_ToString(eosEventData->LocalUserId, fSelectedAccountID, &sz);
    }
    fSelectedAccountID[sz] = 0;

    if (fResult != EOS_EResult::EOS_Success) {
        return;
    }

    auto contextPointer = (RuntimeContext *) eosEventData->ClientData;
    if (!contextPointer) {
        return;
    }

    auto eosPlatformHandle = contextPointer->fPlatformHandle;
    if (!eosPlatformHandle) {
        return;
    }

    EOS_HEcom EcomHandle = EOS_Platform_GetEcomInterface(eosPlatformHandle);

    EOS_Ecom_GetEntitlementsCountOptions CountOptions{ 0 };
    CountOptions.ApiVersion = EOS_ECOM_GETENTITLEMENTSCOUNT_API_LATEST;
    CountOptions.LocalUserId = eosEventData->LocalUserId;
    uint32_t EntitlementCount = EOS_Ecom_GetEntitlementsCount(EcomHandle, &CountOptions);

    CoronaLog("[EOS SDK] NumEntitlements: %d", EntitlementCount);

    std::vector<EOSEntitlementData> NewEntitlements;
    NewEntitlements.reserve(EntitlementCount);

    EOS_Ecom_CopyEntitlementByIndexOptions IndexOptions{ 0 };
    IndexOptions.ApiVersion = EOS_ECOM_COPYENTITLEMENTBYINDEX_API_LATEST;
    IndexOptions.LocalUserId = eosEventData->LocalUserId;
    for (IndexOptions.EntitlementIndex = 0; IndexOptions.EntitlementIndex < EntitlementCount; ++IndexOptions.EntitlementIndex)
    {
        EOS_Ecom_Entitlement* Entitlement;
        EOS_EResult CopyResult = EOS_Ecom_CopyEntitlementByIndex(EcomHandle, &IndexOptions, &Entitlement);
        switch (CopyResult)
        {
            case EOS_EResult::EOS_Success:
            case EOS_EResult::EOS_Ecom_EntitlementStale:
                CoronaLog("[EOS SDK] Entitlement[%d] : %ls : %ls : %ls",
                               IndexOptions.EntitlementIndex,
                               Entitlement->EntitlementName,
                               Entitlement->EntitlementId,
                               Entitlement->bRedeemed ? L"TRUE" : L"FALSE");

                NewEntitlements.push_back(EOSEntitlementData{
                        eosEventData->LocalUserId,
                        std::string(Entitlement->EntitlementName),
                        std::string(Entitlement->EntitlementId),
                        std::string(Entitlement->CatalogItemId),
                        Entitlement->bRedeemed == EOS_TRUE
                });

                EOS_Ecom_Entitlement_Release(Entitlement);
                break;
            default:
                CoronaLog("[EOS SDK] Entitlement[%d] invalid : %d", IndexOptions.EntitlementIndex, CopyResult);
                break;
        }
    }
}

const char *DispatchStoreTransactionQueryEntitlementsEventTask::GetLuaEventName() const {
    return kLuaEventName;
}

bool DispatchStoreTransactionQueryEntitlementsEventTask::PushLuaEventTableTo(lua_State *luaStatePointer) const {
    // Validate.
    if (!luaStatePointer) {
        return false;
    }

    // Push the event data to Lua.
    CoronaLuaNewEvent(luaStatePointer, kLuaEventName);

    if (fResult == EOS_EResult::EOS_Success) {
        lua_pushstring(luaStatePointer, fSelectedAccountID);
        lua_setfield(luaStatePointer, -2, "selectedAccountId");
    }

    lua_pushboolean(luaStatePointer, fResult != EOS_EResult::EOS_Success ? 1 : 0);
    lua_setfield(luaStatePointer, -2, "isError");
    lua_pushinteger(luaStatePointer, (int) fResult);
    lua_setfield(luaStatePointer, -2, "resultCode");

    lua_createtable(luaStatePointer, (int) fEntitlements.size(), 0);
    for (int index = 0; index < (int) fEntitlements.size(); index++) {
        const EOSEntitlementData &entitlement = fEntitlements.at(index);
        lua_newtable(luaStatePointer);

        // We use CatalogItemId as receipt, since that is what is used in the Web API
        lua_pushstring(luaStatePointer, (const char *) entitlement.CatalogItemId.c_str());
        lua_setfield(luaStatePointer, -2, "receipt");

        lua_pushstring(luaStatePointer, (const char *) entitlement.InstanceId.c_str());
        lua_setfield(luaStatePointer, -2, "identifier");

        lua_pushstring(luaStatePointer, (const char *) entitlement.CatalogItemId.c_str());
        lua_setfield(luaStatePointer, -2, "productIdentifier");

        lua_rawseti(luaStatePointer, -2, index + 1);
    }
    lua_setfield(luaStatePointer, -2, "transactions");

    return true;
}
