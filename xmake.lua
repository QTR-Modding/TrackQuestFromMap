set_xmakever("3.0.9")
set_policy("package.requires_lock", true)

if is_plat("windows") then
    local project_root = os.projectdir()
    add_cxflags(
        "/Brepro",
        "/experimental:deterministic",
        '/d1trimfile:"' .. project_root .. '"',
        '/pathmap:"' .. project_root .. '"=.',
        {
            tools = "cl",
            force = true
        })
    add_shflags("/Brepro", "/PDBALTPATH:%_PDB%", {
        force = true
    })
end

option("sf1sdk_dir")
    set_default("../SF1SDK")
    set_showmenu(true)
    set_description("Directory containing the SF1SDK checkout")
option_end()

includes(get_config("sf1sdk_dir") or "../SF1SDK")
add_requires("spdlog v1.16.0", { configs = { header_only = false, wchar = true, std_format = true } })

local plugin_name = "TrackQuestFromMap"
local dll_name = "TrackQuestFromMarker"
local plugin_version = "0.3.2"
local plugin_author = "Quantumyilmaz"
local plugin_license = "GPL-3.0-or-later"

set_project(plugin_name)
set_version(plugin_version)
set_license(plugin_license)
set_arch("x64")
set_languages("c++23")
set_warnings("allextra")

add_rules("mode.debug", "mode.releasedbg", "mode.release")
add_rules("plugin.vsxmake.autoupdate")

target(dll_name)
    set_kind("shared")
    add_rules("sf1sdk.plugin")
    add_deps("SF1SDK")
    add_packages("spdlog")
    local major, minor, patch = plugin_version:match("^(%d+)%.(%d+)%.(%d+)$")
    local version = tonumber(major) * 0x1000000 + tonumber(minor) * 0x10000 + tonumber(patch) * 0x10
    add_defines(
        'TRACK_QUEST_NAME="' .. plugin_name .. '"',
        'TRACK_QUEST_AUTHOR="' .. plugin_author .. '"',
        'TRACK_QUEST_VERSION="' .. plugin_version .. '"',
        'TRACK_QUEST_FILE_VERSION="' .. plugin_version .. '.0"',
        'TRACK_QUEST_LICENSE="' .. plugin_license .. ' License"',
        "TRACK_QUEST_VERSION_MAJOR=" .. major,
        "TRACK_QUEST_VERSION_MINOR=" .. minor,
        "TRACK_QUEST_VERSION_PATCH=" .. patch,
        "TRACK_QUEST_PACKED_VERSION=" .. version)
    set_pcxxheader("src/PCH.h")
    add_defines("_SILENCE_CXX23_ALIGNED_STORAGE_DEPRECATION_WARNING")
    add_files(
        "src/plugin.cpp",
        "src/Version.rc",
        "src/GalaxyMap.cpp",
        "src/GalaxyMapSelection.cpp",
        "src/Hooks.cpp",
        "src/QuestTracking.cpp",
        "src/StarMapInput.cpp",
        "src/StarMapSelection.cpp",
        "src/SurfaceMap.cpp",
        "src/SurfaceMapSelection.cpp"
    )
    add_headerfiles(
        "src/PCH.h",
        "src/GalaxyMap.h",
        "src/GalaxyMapSelection.h",
        "src/Hooks.h",
        "src/QuestTracking.h",
        "src/StarMapInput.h",
        "src/StarMapSelection.h",
        "src/StarMapSelectionUtil.h",
        "src/SurfaceMap.h",
        "src/SurfaceMapSelection.h"
    )
    add_includedirs("src")
