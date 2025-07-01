--
-- For more information on config.lua see the Project Configuration Guide at:
-- https://docs.coronalabs.com/guide/basics/configSettings
--

application =
{
	content =
	{
		width = 320,
		height = 480,
		scale = "zoomEven",
		-- scale = "letterbox",
		-- scale = "none",
	},
    eos = {
        encryptionKey = "REPLACE_THIS",
        clientId = "REPLACE_THIS",
        clientSecret = "REPLACE_THIS",
        productId = "REPLACE_THIS",
        sandboxId = "REPLACE_THIS",
        deploymentId = "REPLACE_THIS"
    },
}
