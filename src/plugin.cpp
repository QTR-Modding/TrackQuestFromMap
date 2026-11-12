#include "Hooks.h"

#include <spdlog/sinks/basic_file_sink.h>

extern "C" __declspec(dllexport) constinit SFSE::PluginVersionData SFSEPlugin_Version{
	1,
	TRACK_QUEST_PACKED_VERSION,
	TRACK_QUEST_NAME,
	TRACK_QUEST_AUTHOR,
	1 << 2,
	1 << 3,
	{SFSE::RUNTIME_SF_1_16_244}
};

static void ConfigureLogging()
{
	try
	{
		if (const auto directory = SFSE::GetLogDirectory())
		{
			const auto file = *directory / (TRACK_QUEST_NAME ".log");
			logger::set_default_logger(spdlog::basic_logger_mt(TRACK_QUEST_NAME, file.string(), true));
		}
	}
	catch (const spdlog::spdlog_ex& error)
	{
		logger::warn("Could not open the Track Quest log: {}", error.what());
	}
	logger::set_pattern("%Y-%m-%d %H:%M:%S.%e [%l] %v");
	logger::flush_on(spdlog::level::info);
}

extern "C" __declspec(dllexport) bool SFSEPlugin_Load(const SFSE::LoadInterface* sfse)
{
	if (!sfse)
	{
		return false;
	}

	ConfigureLogging();
	const auto runtime = sfse->RuntimeVersion();
	logger::info("{} {} loaded; runtime=0x{:08X}, SFSE=0x{:08X}",
		TRACK_QUEST_NAME, TRACK_QUEST_VERSION, runtime, sfse->sfseVersion);
	if (runtime != SFSE::RUNTIME_SF_1_16_244)
	{
		logger::error("Unsupported runtime 0x{:08X}; expected 0x{:08X}", runtime, SFSE::RUNTIME_SF_1_16_244);
		return false;
	}
	if (!SFSE::Init(*sfse, 128))
	{
		logger::error("Could not initialize SF1SDK");
		return false;
	}
	if (!SFSE::GetTaskInterface())
	{
		logger::error("Required SFSE TaskInterface is unavailable");
		return false;
	}
	return TrackQuestFromMap::Hooks::Install();
}
