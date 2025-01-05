#include <iostream>
#include <filesystem>
#include <functional>

#include "tier1/convar.h"
#include "materialsystem/imaterialsystem.h"
#include "materialsystem/MaterialSystemUtil.h"
#include "filesystem.h"
#include "icvar.h"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <MinHook.h>

typedef int (*LauncherMain_t)(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow);
typedef void* (*CreateInterfaceFn)(const char* pName, int* pReturnCode);
typedef decltype(LoadLibraryExW)* load_library_t;
typedef decltype(GetCommandLineA)* get_cmdline_t;

void init_material_system(HMODULE mod);
void init_file_system(HMODULE mod);
void init_client_hook(uint8_t* mod);
void init_engine_hook(uint8_t* mod);

ICvar* cvar = nullptr;
IMaterialSystem* materials = nullptr;
IFileSystem* fs = nullptr;
ITexture* hdr_rt = nullptr;
ITexture* hud_rt = nullptr;

enum PostProcessingCondition
{
    PPP_ALWAYS,
    PPP_IF_COND_VAR,
    PPP_IF_NOT_COND_VAR
};

struct PostProcessingPass
{
    PostProcessingCondition ppp_test;
    ConVar const* cvar_to_test;
    char const* material_name;  // terminate list with null
    char const* dest_rendering_target;
    char const* src_rendering_target;  // can be null. needed for source scaling
    int xdest_scale, ydest_scale;      // allows scaling down
    int xsrc_scale, ysrc_scale;        // allows scaling down
    CMaterialReference m_mat_ref;      // so we don't have to keep searching
};

#define PPP_PROCESS_PARTIAL_SRC(srcmatname, dest_rt_name, src_tname, scale) \
    {PPP_ALWAYS, 0, srcmatname, dest_rt_name, src_tname, 1, 1, scale, scale}
#define PPP_PROCESS_PARTIAL_DEST(srcmatname, dest_rt_name, src_tname, scale) \
    {PPP_ALWAYS, 0, srcmatname, dest_rt_name, src_tname, scale, scale, 1, 1}
#define PPP_PROCESS_PARTIAL_SRC_PARTIAL_DEST(srcmatname, dest_rt_name, src_tname, srcscale, destscale) \
    {PPP_ALWAYS, 0, srcmatname, dest_rt_name, src_tname, destscale, destscale, srcscale, srcscale}
#define PPP_END {PPP_ALWAYS, 0, NULL, NULL, 0, 0, 0, 0, 0}
#define PPP_PROCESS(srcmatname, dest_rt_name) {PPP_ALWAYS, 0, srcmatname, dest_rt_name, 0, 1, 1, 1, 1}
#define PPP_PROCESS_IF_CVAR(cvarptr, srcmatname, dest_rt_name) \
    {PPP_IF_COND_VAR, cvarptr, srcmatname, dest_rt_name, 0, 1, 1, 1, 1}
#define PPP_PROCESS_IF_NOT_CVAR(cvarptr, srcmatname, dest_rt_name) \
    {PPP_IF_NOT_COND_VAR, cvarptr, srcmatname, dest_rt_name, 0, 1, 1, 1, 1}
#define PPP_PROCESS_IF_NOT_CVAR_SRCTEXTURE(cvarptr, srcmatname, src_tname, dest_rt_name) \
    {PPP_IF_NOT_COND_VAR, cvarptr, srcmatname, dest_rt_name, src_tname, 1, 1, 1, 1}
#define PPP_PROCESS_IF_CVAR_SRCTEXTURE(cvarptr, srcmatname, src_txtrname, dest_rt_name) \
    {PPP_IF_COND_VAR, cvarptr, srcmatname, dest_rt_name, src_txtrname, 1, 1, 1, 1}
#define PPP_PROCESS_SRCTEXTURE(srcmatname, src_tname, dest_rt_name) \
    {PPP_ALWAYS, 0, srcmatname, dest_rt_name, src_tname, 1, 1, 1, 1}

struct ClipBox
{
    int m_minx, m_miny;
    int m_maxx, m_maxy;
};

void (*ApplyPostProcessingPasses)(PostProcessingPass* pass_list, ClipBox const* clipbox, ClipBox* dest_coords_out) =
    nullptr;

load_library_t load_library_orig;
HMODULE WINAPI load_library_hook(LPCWSTR lpLibFileName, HANDLE hFile, DWORD dwFlags)
{
    std::filesystem::path file{lpLibFileName};
    auto filename = file.filename().string();

    if (filename == "dxvk_d3d9.dll")
    {
        return GetModuleHandleA("dxvk_d3d9.dll");
    }

    if (filename == "engine.dll")
    {
        auto lib = load_library_orig(lpLibFileName, hFile, dwFlags);
        init_engine_hook(reinterpret_cast<uint8_t*>(lib));

        return lib;
    }

    if (filename == "materialsystem.dll")
    {
        auto lib = load_library_orig(lpLibFileName, hFile, dwFlags);
        init_material_system(lib);

        return lib;
    }

    if (filename == "game_shader_dx9.dll")
    {
        auto lib = load_library_orig(L"game_shader_dx9.dll", nullptr, 0);
        return lib;
    }

    if (filename == "client.dll")
    {
        auto lib = load_library_orig(lpLibFileName, hFile, dwFlags);
        init_client_hook(reinterpret_cast<uint8_t*>(lib));

        return lib;
    }

    return load_library_orig(lpLibFileName, hFile, dwFlags);
}

template <typename T>
void patch_mem(void* loc, T value)
{
    DWORD old_protect;
    VirtualProtect(loc, sizeof(T), PAGE_EXECUTE_READWRITE, &old_protect);
    memcpy(loc, &value, sizeof(T));
    VirtualProtect(loc, sizeof(T), old_protect, &old_protect);
    FlushInstructionCache(GetCurrentProcess(), loc, sizeof(T));
}

// don't try it
void do_engine_post_process(int x, int y, int w, int h, bool bFlashlightIsOn, bool bPostVGui) {}

template <class T>
T* get_interface(HMODULE mod, const char* interface_version)
{
    int rc_int = 0;
    auto create_interface =
        reinterpret_cast<CreateInterfaceFn>(GetProcAddress(mod, MAKEINTRESOURCEA(IMAGE_ORDINAL(1))));
    return reinterpret_cast<T*>(create_interface(interface_version, &rc_int));
}

void init_material_system(HMODULE mod)
{
    materials = get_interface<IMaterialSystem>(mod, MATERIAL_SYSTEM_INTERFACE_VERSION);
}

void (*init_well_known_rts_orig)();
void init_well_known_rts_hook()
{
    init_well_known_rts_orig();

    int w = 0, h = 0;
    materials->GetBackBufferDimensions(w, h);

    hdr_rt = materials->CreateNamedRenderTargetTexture(
        "_rt_HDRFB", w, h, RT_SIZE_FULL_FRAME_BUFFER, IMAGE_FORMAT_RGBA16161616F
    );

    hud_rt =
        materials->CreateNamedRenderTargetTexture("_rt_HUD", w, h, RT_SIZE_FULL_FRAME_BUFFER, IMAGE_FORMAT_RGBA8888);

    auto fs_mod = GetModuleHandleA("filesystem_stdio.dll");
    fs = get_interface<IFileSystem>(fs_mod, FILESYSTEM_INTERFACE_VERSION);
}

void (*on_connected_orig)();
void on_connected()
{
    on_connected_orig();

    // add our own search path after connected
    // added search path is purged somehow after change expansion
    auto mod = (std::filesystem::current_path() / "hl2_hdr").string();
    fs->AddSearchPath(mod.data(), "GAME");
}

void init_engine_hook(uint8_t* mod)
{
    MH_CreateHook(mod + 0xDED20, on_connected, reinterpret_cast<void**>(&on_connected_orig));
    MH_CreateHook(mod + 0x135880, init_well_known_rts_hook, reinterpret_cast<void**>(&init_well_known_rts_orig));
    MH_EnableHook(MH_ALL_HOOKS);
}

void __fastcall push_rt_and_vp_hook(
    IMatRenderContext* ctx, void* /*edx*/, ITexture* /*pTexture*/, ITexture* /*pDepthTexture*/, int nViewX, int nViewY,
    int nViewW, int nViewH
)
{
    ctx->PushRenderTargetAndViewport(hud_rt, nullptr, nViewX, nViewY, nViewW, nViewH);

    ctx->OverrideAlphaWriteEnable(true, true);
    ctx->ClearColor4ub(0, 0, 0, 0);
    ctx->ClearBuffers(true, false);
}

PostProcessingPass* passes;

void __fastcall pop_view_hook(IMatRenderContext* ctx)
{
    ctx->PopRenderTargetAndViewport();
    ctx->CopyRenderTargetToTexture(hdr_rt);

    ApplyPostProcessingPasses(passes, nullptr, nullptr);
}

void init_client_hook(uint8_t* mod)
{
    ApplyPostProcessingPasses = reinterpret_cast<decltype(ApplyPostProcessingPasses)>(mod + 0x1D4190);
    passes = new PostProcessingPass[]{PPP_PROCESS("hdr_pp/final", nullptr), PPP_END};

    // render hud to our rt texture
    patch_mem<uint8_t>(mod + 0x1E318B, 0xE8);
    patch_mem<uint32_t>(mod + 0x1E318B + 1, int32_t(size_t(push_rt_and_vp_hook) - (size_t(mod + 0x1E318B) + 5)));
    patch_mem<uint8_t>(mod + 0x1E318B + 5, 0x90);

    // pop view do our postprocess
    patch_mem<uint8_t>(mod + 0x1E3329, 0xE8);
    patch_mem<uint32_t>(mod + 0x1E3329 + 1, int32_t(size_t(pop_view_hook) - (size_t(mod + 0x1E3329) + 5)));
    patch_mem<uint8_t>(mod + 0x1E3329 + 5, 0x90);

    MH_CreateHook(mod + 0x1D5010, do_engine_post_process, nullptr);
    MH_EnableHook(MH_ALL_HOOKS);
}

get_cmdline_t get_cmdline_orig;
LPSTR __stdcall get_cmdline_hook()
{
    static auto cmd = get_cmdline_orig() + std::string{" -steam -vulkan -game hl2_complete"};
    return cmd.data();
}

void init()
{
    LoadLibraryA("dxvk_d3d9.dll");

    MH_Initialize();
    MH_CreateHookApi(
        L"kernelbase.dll", "LoadLibraryExW", load_library_hook, reinterpret_cast<void**>(&load_library_orig)
    );
    MH_CreateHookApi(
        L"kernelbase.dll", "GetCommandLineA", get_cmdline_hook, reinterpret_cast<void**>(&get_cmdline_orig)
    );

    MH_EnableHook(MH_ALL_HOOKS);
}

void enable_dpi_awareness()
{
    auto user32 = LoadLibraryA("user32.dll");
    if (!user32) return;

    auto set_dpi =
        reinterpret_cast<BOOL(WINAPI*)(DPI_AWARENESS_CONTEXT)>(GetProcAddress(user32, "SetProcessDpiAwarenessContext"));

    if (set_dpi) set_dpi(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
}

int main(int argc, char** argv)
{
    enable_dpi_awareness();

    char self_path[MAX_PATH];
    GetModuleFileNameA(GetModuleHandleA(nullptr), self_path, MAX_PATH);

    auto path = std::filesystem::path{self_path}.parent_path();
    auto bin_dir = path / "bin";
    auto hdr_bin_dir = path / "hl2_hdr" / "bin";

    AddDllDirectory(bin_dir.wstring().data());
    AddDllDirectory(hdr_bin_dir.wstring().data());
    SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);

    auto launcher = LoadLibraryA("launcher.dll");

    if (!launcher)
    {
        MessageBoxA(nullptr, "Failed to load launcher.dll", "Error", MB_ICONERROR);
        return 1;
    }

    init();

    return reinterpret_cast<LauncherMain_t>(GetProcAddress(launcher, "LauncherMain"))(
        GetModuleHandleA(nullptr), nullptr, nullptr, SW_NORMAL
    );
}
