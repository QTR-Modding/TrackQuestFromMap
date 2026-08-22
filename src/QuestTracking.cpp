#include "QuestTracking.h"

namespace TrackQuestFromMap::QuestTracking
{
	namespace
	{
		[[nodiscard]] std::string_view SourceName(const Source a_source) noexcept
		{
			switch (a_source)
			{
			case Source::kSurface:
				return "Surface Map";
			case Source::kGalaxy:
				return "Galaxy Map";
			case Source::kSystem:
				return "System Map";
			}
			return "Star Map";
		}

		void ActivateOnMainThreadImpl(
			const RE::QuestInstanceKey a_key,
			const Source a_source,
			const PostTrack a_postTrack)
		{
			auto* quest = ResolveQuest(a_key);
			if (!quest || !quest->IsRunning() || quest->IsStopped() || quest->IsTracked())
			{
				logger::warn(
					"Skipped stale/already-active {} quest 0x{:08X}, instance={}",
					SourceName(a_source),
					a_key.formID,
					a_key.instanceID);
				return;
			}

			quest->ToggleTracking();
			quest = ResolveQuest(a_key);
			if (!quest || !quest->IsTracked())
			{
				logger::warn(
					"Vanilla helper rejected {} quest 0x{:08X}, instance={}",
					SourceName(a_source),
					a_key.formID,
					a_key.instanceID);
				return;
			}

			logger::info(
				"Tracked {} quest 0x{:08X}, instance={}",
				SourceName(a_source),
				a_key.formID,
				a_key.instanceID);
			if (a_postTrack)
			{
				a_postTrack();
			}
		}

		void ActivateOnMainThread(
			const RE::QuestInstanceKey a_key,
			const Source a_source,
			const PostTrack a_postTrack) noexcept
		{
			try
			{
				ActivateOnMainThreadImpl(a_key, a_source, a_postTrack);
			}
			catch (const std::exception& error)
			{
				logger::error("Queued Track Quest task failed: {}", error.what());
			}
		}

		bool QueueTrackImpl(
			const RE::QuestInstanceKey& a_key,
			const Source a_source,
			const PostTrack a_postTrack)
		{
			if (!IsInactiveTrackable(a_key))
			{
				logger::warn(
					"Rejected stale/already-active {} quest 0x{:08X}, instance={} before queue",
					SourceName(a_source),
					a_key.formID,
					a_key.instanceID);
				return false;
			}

			const auto* tasks = SFSE::GetTaskInterface();
			if (!tasks)
			{
				logger::error("SFSE TaskInterface is unavailable");
				return false;
			}

			tasks->AddTask([key = a_key, source = a_source, postTrack = a_postTrack]
			{
				ActivateOnMainThread(key, source, postTrack);
			});
			return true;
		}
	}

	RE::TESQuest* ResolveQuest(const RE::QuestInstanceKey& a_key) noexcept
	{
		auto* quest = RE::TESForm::LookupByID<RE::TESQuest>(a_key.formID);
		return quest && quest->GetInstanceKey() == a_key ? quest : nullptr;
	}

	bool IsInactiveTrackable(const RE::QuestInstanceKey& a_key) noexcept
	{
		const auto* quest = ResolveQuest(a_key);
		return quest && quest->IsRunning() && !quest->IsStopped() && !quest->IsTracked();
	}

	bool QueueTrack(
		const RE::QuestInstanceKey& a_key,
		const Source a_source,
		const PostTrack a_postTrack) noexcept
	{
		try
		{
			return QueueTrackImpl(a_key, a_source, a_postTrack);
		}
		catch (const std::exception& error)
		{
			logger::error("Track Quest queue request failed: {}", error.what());
			return false;
		}
	}
}
