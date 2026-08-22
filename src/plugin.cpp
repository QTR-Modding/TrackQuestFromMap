#include "Hooks.h"

SFSE_PLUGIN_VERSION = []() noexcept {
	SFSE::PluginVersionData version{};
	version.PluginVersion({ 0, 3, 0, 0 });
	version.PluginName("TrackQuestFromMap");
	version.AuthorName("Quantumyilmaz");
	version.UsesSigScanning(false);
	version.UsesAddressLibrary(true);
	version.HasNoStructUse(false);
	version.IsLayoutDependent(true);
	version.CompatibleVersions({ SFSE::RUNTIME_SF_1_16_244 });
	version.MinimumRequiredXSEVersion(SFSE::SFSE_PACK_LATEST);
	return version;
}();

SFSE_PLUGIN_LOAD(const SFSE::LoadInterface* a_sfse)
{
	try {
		if (!a_sfse) {
			return false;
		}

		constexpr SFSE::InitInfo initInfo{
			.logPattern = "%Y-%m-%d %H:%M:%S.%e [%l] %v",
			.trampoline = true,
			.trampolineSize = 128
		};
		SFSE::Init(a_sfse, initInfo);

		const auto runtime = a_sfse->RuntimeVersion();
		logger::info(
			"TrackQuestFromMap 0.3.0 loaded; runtime={}, SFSE=0x{:08X}",
			runtime,
			a_sfse->SFSEVersion());

		if (runtime != SFSE::RUNTIME_SF_1_16_244) {
			logger::error(
				"Unsupported runtime {}; this prototype is gated to {}",
				runtime,
				SFSE::RUNTIME_SF_1_16_244);
			return false;
		}

		if (!SFSE::GetTaskInterface()) {
			logger::error("Required SFSE TaskInterface is unavailable");
			return false;
		}

		if (!TrackQuestSurface::Hooks::Install()) {
			logger::error("Transactional map/input hooks could not be installed");
			return false;
		}
		return true;
	} catch (const std::exception& error) {
		try {
			logger::error("Plugin load failed: {}", error.what());
		} catch (...) {
		}
	} catch (...) {
		try {
			logger::error("Plugin load failed unexpectedly");
		} catch (...) {
		}
	}
	return false;
}
