local eos = require "plugin.eos"

eos.init()

local function onLoginError()
    print("onLoginError")
end
local function onLoginSuccess(_authIdToken)
    print("onLoginSuccess", _authIdToken)

    eos.addEventListener("loadProducts", function(event)
        print("loadProducts event")
        for k,v in pairs(event) do
            if k == "products" then
                for i = 1, #v, 1 do
                    for productK,productV in pairs(v[i]) do
                        print("product", i, productK,productV)
                    end
                end
            else
                print(k,v)
            end
        end

        eos.addEventListener("storeTransaction", function(event)
            print("storeTransaction event")
            for k,v in pairs(event) do
                if k == "transactions" then
                    for i = 1, #v, 1 do
                        for transactionK,transactionV in pairs(v[i]) do
                            print("transaction", i, transactionK,transactionV)
                        end
                    end
                else
                    print(k,v)
                end
            end
        end)

--         print("purchasing 8ae78367bc4b450ca2995e81303abc78")
--         eos.purchase("8ae78367bc4b450ca2995e81303abc78")
        eos.restore()
    end)
    eos.loadProducts()
end

Runtime:addEventListener("enterFrame", function(event)
    if Runtime.getFrameID() == 3 * 60 then

        if eos.isLoggedOn() then -- Already authenticated, return authIdToken immediately
            local authIdToken = eos.getAuthIdToken()
            if not authIdToken then
                onLoginError()
            else
                onLoginSuccess(--[[---@not nil]] authIdToken)
            end
        elseif system.getInfo("platform") == "win32" or system.getInfo("platform") == "macos" then
            onLoginError()
        else
            eos.addEventListener("loginResponse", function()
                if event.isError then
                    onLoginError()
                else
                    local authIdToken = eos.getAuthIdToken()
                    if not authIdToken then
                        onLoginError()
                    else
                        onLoginSuccess(--[[---@not nil]] authIdToken)
                    end
                end
            end)
            eos.loginWithAccountPortal()
        end
    end
end)

-- timer.performWithDelay( 7000, function()
-- 	eos.getAuthIdToken()
-- 	print(eos.getAuthIdToken())
-- end )

