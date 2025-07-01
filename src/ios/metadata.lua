local metadata =
{
	plugin =
	{
		format = 'staticLibrary',
		staticLibs = { 'plugin_eos', },
		frameworks = {"EOSSDK"},
		frameworksOptional = {},
		frameworksSearchPaths = {
			"Frameworks",
		},
		-- usesSwift = true,
	},
}

return metadata
