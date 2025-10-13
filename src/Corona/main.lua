local eos = require("plugin.eos")

eos.init()

local function onLoginError()
    print("onLoginError")
end

local function onLoginSuccess(authIdToken)
    print("onLoginSuccess", authIdToken)

    eos.addEventListener("loadProducts", function(event)
        print("loadProducts event")

        for k, v in pairs(event) do
            if k == "products" then
                for i = 1, #v do
                    for productK, productV in pairs(v[i]) do
                        print("product", i, productK, productV)
                    end
                end
            else
                print(k, v)
            end
        end

        eos.addEventListener("storeTransaction", function(event)
            print("storeTransaction event")

            for k, v in pairs(event) do
                if k == "transactions" then
                    for i = 1, #v do
                        for transactionK, transactionV in pairs(v[i]) do
                            print("transaction", i, transactionK, transactionV)
                        end
                    end
                else
                    print(k, v)
                end
            end
            timer.performWithDelay(1000, function()
                eos.purchase("08606ab019b8411487a44da041b6115b")
            end)
        end)

        eos.restore()
    end)

    eos.loadProducts()
end

local function logout()
    if eos.isLoggedOn() then
        eos.addEventListener("logoutResponse", function(event)
            local json = require("json")
            print("logoutResponse/", json.prettify(event))
        end)
        eos.logout()
    end
end

Runtime:addEventListener("enterFrame", function(event)
    if Runtime.getFrameID() == 3 * 60 then
        if eos.isLoggedOn() then
            -- Already authenticated, return authIdToken immediately
            local authIdToken = eos.getAuthIdToken()
            if not authIdToken then
                onLoginError()
            else
                onLoginSuccess(authIdToken)
            end
        elseif system.getInfo("platform") == "win32" or system.getInfo("platform") == "macos" then
            onLoginError()
        else
            eos.addEventListener("loginResponse", function(event)
                if event.isError then
                    onLoginError()
                else
                    local authIdToken = eos.getAuthIdToken()
                    if not authIdToken then
                        onLoginError()
                    else
                        onLoginSuccess(authIdToken)
                    end
                end
            end)

            eos.loginWithAccountPortal()
        end
    end
end)
