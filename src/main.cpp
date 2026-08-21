#include "SurfaceActivation.h"

#include "RE/Starfield.h"
#include "SFSE/SFSE.h"

SFSE_PLUGIN_VERSION = []() noexcept {
  SFSE::PluginVersionData version{};
  version.PluginVersion({0, 2, 2, 0});
  version.PluginName("TrackQuestSurfaceNativeOnly");
  version.AuthorName("Quantumyilmaz");
  version.UsesSigScanning(false);
  version.UsesAddressLibrary(true);
  version.HasNoStructUse(false);
  version.IsLayoutDependent(true);
  version.CompatibleVersions({SFSE::RUNTIME_SF_1_16_244});
  version.MinimumRequiredXSEVersion(SFSE::SFSE_PACK_LATEST);
  return version;
}();

SFSE_PLUGIN_LOAD(const SFSE::LoadInterface *a_sfse) {
  if (!a_sfse) {
    return false;
  }

  SFSE::InitInfo initInfo{.logPattern = "%Y-%m-%d %H:%M:%S.%e [%l] %v",
                          .trampoline = true,
                          .trampolineSize = 64};
  SFSE::Init(a_sfse, initInfo);

  const auto runtime = a_sfse->RuntimeVersion();
  REX::INFO(
      "TrackQuestSurfaceNativeOnly 0.2.2 loaded; runtime={}, SFSE=0x{:08X}",
      runtime, a_sfse->SFSEVersion());

  if (runtime != SFSE::RUNTIME_SF_1_16_244) {
    REX::ERROR("Unsupported runtime {}; this prototype is gated to {}", runtime,
               SFSE::RUNTIME_SF_1_16_244);
    return false;
  }

  const auto *taskInterface = SFSE::GetTaskInterface();
  if (!taskInterface) {
    REX::ERROR("Required SFSE TaskInterface is unavailable");
    return false;
  }

  if (!TrackQuestSurface::SurfaceActivation::Install()) {
    REX::ERROR("Transactional SurfaceMap/input hooks could not be installed");
    return false;
  }
  return true;
}
