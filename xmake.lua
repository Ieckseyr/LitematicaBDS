add_rules("mode.debug", "mode.release")

set_project("LitematicaBDS")
set_version("0.1.0")
set_languages("c++20")

option("with_native")
    set_default(true)
    set_showmenu(true)
    set_description("构建 LeviLamina 原生插件（需要 levilamina 26.60 + HologramLib 导入库）")
option_end()

if is_plat("windows") then
    set_toolchains("clang-cl")
    if not has_config("vs_runtime") then
        set_runtimes("MD")
    end
end

if has_config("with_native") then
-- LeviLamina 26.60 离线仓库（服务器根目录 .llsdk/liteldev-repo）。
-- 换机器用 LITBDS_LEVILAMINA_REPO 指向同一份仓库。
local function find_levilamina_repo()
    local candidates = {}
    local env_repo = os.getenv("LITBDS_LEVILAMINA_REPO")
    if env_repo and env_repo ~= "" then
        table.insert(candidates, env_repo)
    end
    table.insert(candidates, path.join(os.projectdir(), "..", "xb26.60.x", ".llsdk", "liteldev-repo"))
    table.insert(candidates, path.join(os.projectdir(), "..", "..", "xb26.60.x", ".llsdk", "liteldev-repo"))
    for _, p in ipairs(candidates) do
        if os.exists(path.join(p, "packages", "l", "levilamina", "versions", "26_60_0.lua")) then
            return p
        end
    end
    return nil
end

local levilamina_repo = find_levilamina_repo()
if levilamina_repo then
    add_repositories("liteldev-repo " .. levilamina_repo)
else
    add_repositories("liteldev-repo https://github.com/LiteLDev/xmake-repo.git")
end

if is_plat("windows") then
    set_toolchains("clang-cl")
    if not has_config("vs_runtime") then
        set_runtimes("MD")
    end
end

add_requires("levilamina 26.60.0", {configs = {target_type = "server"}})
add_requires("levibuildscript")
end

option("lit_werror")
    set_default(false)
    set_showmenu(true)
    set_description("把警告当错误（CI 用）")
option_end()

-- ── 核心静态库：平台无关 ────────────────────────────────────────────────
target("litematicabds_core")
    set_kind("static")
    add_files("core/**.cpp")
    remove_files("core/c_api/*.cpp")
    add_files("third_party/miniz/miniz.c")
    add_includedirs("include", "core", "third_party/miniz", {public = true})
    add_cxflags("/utf-8", "/W4", {force = true})
    add_cflags("/utf-8", {force = true})
    add_defines("NOMINMAX", "UNICODE", "_CRT_SECURE_NO_WARNINGS", {public = true})
    if has_config("lit_werror") then
        add_cxflags("/WX", {force = true})
    end
    set_exceptions("cxx")
    set_symbols("debug")

-- ── 单元测试（宿主机直接跑，不加载 BDS）────────────────────────────────
target("litematicabds_tests")
    set_kind("binary")
    set_default(true)
    add_deps("litematicabds_core")
    add_files("tests/**.cpp")
    add_includedirs("include", "core", "third_party/miniz")
    add_cxflags("/utf-8", "/W4", "/EHsc", {force = true})
    add_defines("NOMINMAX", "UNICODE", "_CRT_SECURE_NO_WARNINGS")
    set_exceptions("cxx")
    set_symbols("debug")
    set_rundir("$(projectdir)")

-- ── C ABI DLL：给 endstone 套壳（ctypes）───────────────────────────────
target("litematica_core")
    set_kind("shared")
    set_languages("c++20")
    set_exceptions("cxx")
    add_deps("litematicabds_core")
    add_files("core/c_api/*.cpp")
    add_includedirs("include", "core")
    add_cxflags("/utf-8", "/W4", {force = true})
    add_defines("NOMINMAX", "UNICODE", "_CRT_SECURE_NO_WARNINGS", "LITBDS_CORE_EXPORTS")
    set_symbols("debug")
    after_build(function (target)
        local out = path.join(os.projectdir(), "platform", "endstone", "src", "litematicabds_endstone", "_native")
        os.mkdir(out)
        local dll = target:targetfile()
        local dest = path.join(out, "litematica_core.dll")
        try {
            function ()
                os.cp(dll, dest, {force = true})
            end,
            catch {
                function (e)
                    print("warn: 部署 litematica_core.dll 失败: " .. tostring(e))
                end
            }
        }
    end)

if has_config("with_native") then
-- ── LeviLamina 原生插件（HologramLib 前置）────────────────────────────────
local hololib_dir = os.getenv("LITBDS_HOLOLIB_DIR")
if not hololib_dir or hololib_dir == "" then
    hololib_dir = path.join(os.projectdir(), "..", "xb26.60.x", "mods", "HologramLib")
end
local hololib_include = path.join(hololib_dir, "include")
local hololib_libdir  = hololib_dir

local deploy_dir = os.getenv("LITBDS_DEPLOY_DIR")
if not deploy_dir or deploy_dir == "" then
    deploy_dir = path.join(os.projectdir(), "..", "xb26.60.x", "mods", "LitematicaBDS")
end

rule("litematicabds_deploy")
    after_build(function (target)
        local projectdir = os.projectdir()
        local targetfile = target:targetfile()
        if not os.isfile(targetfile) then
            return
        end
        os.mkdir(deploy_dir)

        local function deploy(src, dst)
            if not os.isfile(src) then
                return false
            end
            local ok = false
            try {
                function ()
                    os.cp(src, dst, {force = true})
                    ok = true
                end,
                catch {
                    function (e)
                    end
                }
            }
            if ok then
                return true
            end
            local backup = dst .. ".old"
            try {
                function ()
                    os.rm(backup)
                end,
                catch {
                    function (e)
                    end
                }
            }
            try {
                function ()
                    os.mv(dst, backup)
                end,
                catch {
                    function (e)
                    end
                }
            }
            try {
                function ()
                    os.cp(src, dst, {force = true})
                    ok = true
                end,
                catch {
                    function (e)
                    end
                }
            }
            if ok then
                cprint("${yellow}[LitematicaBDS]: ${reset}目标被占用，旧文件改名为 " .. backup)
            else
                print("warn: 部署失败: " .. dst)
            end
            return ok
        end

        deploy(targetfile, path.join(deploy_dir, "LitematicaBDS.dll"))
        local pdbfile = targetfile:gsub("%.dll$", ".pdb")
        deploy(pdbfile, path.join(deploy_dir, "LitematicaBDS.pdb"))
        deploy(path.join(projectdir, "platform", "levilamina", "manifest.json"), path.join(deploy_dir, "manifest.json"))

        os.mkdir(path.join(deploy_dir, "schematics"))
        os.mkdir(path.join(deploy_dir, "exports"))
        local config = path.join(deploy_dir, "config.json")
        if not os.isfile(config) then
            io.writefile(config, [[{
  // 渲染
  "render": {
    "blocksPerTick": 256,
    "clearPerTick": 512,
    "viewDistance": 64,
    "autoLayerRadius": 2,
    "surfaceOnly": true,
    "maxBlocksPerViewer": 30000,
    "planMoveThreshold": 4,
    "showStatusHud": false
  },
  // 投影默认姿态
  "projection": {
    "showBounds": true,
    "boundsColor": "#00FF00",
    "defaultRotation": 0,
    "defaultMirrorX": false,
    "defaultMirrorZ": false
  },
  "sulfur": {
    "enabled": true,
    "radius": 8,
    "maxDisplays": 64,
    "visible": false,
    "faceBlock": true
  },
  "fill": {
    "enabled": true,
    "requireOp": false,
    "consumeItem": true,
    "quickFill": false
  },
  "save": {
    "maxVolume": 262144
  },
  "storage": {
    "schematicsDir": "schematics",
    "exportsDir": "exports"
  },
  "debug": false
}
]])
        end
        cprint("${bright green}[LitematicaBDS]: ${reset}DLL / manifest / 配置已部署到 " .. deploy_dir)
    end)
rule_end()

target("LitematicaBDS")
    add_rules("@levibuildscript/linkrule")
    add_rules("litematicabds_deploy")
    set_kind("shared")
    set_languages("c++20")
    add_deps("litematicabds_core")
    add_packages("levilamina")
    add_files("platform/levilamina/src/*.cpp")
    add_includedirs("include", "platform/levilamina/src")
    add_includedirs(hololib_include)
    add_linkdirs(hololib_libdir)
    add_links("HologramLib")
    add_cxflags("/EHa", "/utf-8", "/W4", "/Zm2000", "/wd4100", "/wd4244", "/wd4267", {force = true})
    add_defines("NOMINMAX", "UNICODE", "_AMD64_", "_CRT_SECURE_NO_WARNINGS")
    set_symbols("debug")
    add_ldflags("/OPT:REF", "/OPT:ICF")
end
