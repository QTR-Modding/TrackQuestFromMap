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

local commonlibsf = path.join(os.projectdir(), "lib", "commonlibsf")
includes(commonlibsf)

local plugin_name = "TrackQuestFromMap"
local dll_name = "TrackQuestFromMarker"
local plugin_version = "0.3.4"
local plugin_author = "Quantumyilmaz"

set_project(plugin_name)
set_version(plugin_version)
set_license("GPL-3.0-or-later")
set_languages("c++23")
set_warnings("allextra")

add_rules("mode.debug", "mode.releasedbg", "mode.release")
add_rules("plugin.vsxmake.autoupdate")

target(dll_name)
    add_rules("commonlibsf.plugin", {
        author = plugin_author,
        name = plugin_name,
        options = {
            sig_scanning = false,
            address_library = true,
            no_struct_use = false,
            layout_dependent = true
        }
    })
    set_pcxxheader("src/PCH.h")
    add_defines("_SILENCE_CXX23_ALIGNED_STORAGE_DEPRECATION_WARNING")
    add_files(
        "src/plugin.cpp",
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
