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

set_project("TrackQuestFromMap")
set_version("0.2.2")
set_license("GPL-3.0-or-later")
set_languages("c++23")
set_warnings("allextra")

add_rules("mode.debug", "mode.releasedbg", "mode.release")
add_rules("plugin.vsxmake.autoupdate")

target("TrackQuestSurfaceNativeOnly")
    set_kind("shared")
    set_arch("x64")
    set_pcxxheader("src/PCH.h")
    add_defines("_SILENCE_CXX23_ALIGNED_STORAGE_DEPRECATION_WARNING")
    add_deps("commonlibsf")
    add_files(
        "src/plugin.cpp",
        "src/Hooks.cpp",
        "src/StarMapInput.cpp",
        "src/SurfaceMap.cpp"
    )
    add_headerfiles(
        "src/PCH.h",
        "src/Hooks.h",
        "src/StarMapInput.h",
        "src/SurfaceMap.h"
    )
    add_includedirs("src")

    -- Deliberately do not apply CommonLibSF's plugin packaging rule. This
    -- target must never copy into Starfield or a mod manager as a build side
    -- effect.
