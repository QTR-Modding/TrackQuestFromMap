#include "Hooks.h"

SFSE_PLUGIN_LOAD(const SFSE::LoadInterface* a_sfse)
{
	if (!a_sfse)
	{
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
		"{} {} loaded; runtime={}, SFSE=0x{:08X}",
		SFSE::GetPluginName(),
		SFSE::GetPluginVersion(),
		runtime,
		a_sfse->SFSEVersion());

	if (runtime != SFSE::RUNTIME_SF_1_16_244)
	{
		logger::error(
			"Unsupported runtime {}; expected {}",
			runtime,
			SFSE::RUNTIME_SF_1_16_244);
		return false;
	}

	if (!SFSE::GetTaskInterface())
	{
		logger::error("Required SFSE TaskInterface is unavailable");
		return false;
	}

	return TrackQuestFromMap::Hooks::Install();
}
