//===== Copyright ?1996-2005, Valve Corporation, All rights reserved. ======//
//
// Purpose:
//
// $NoKeywords: $
//===========================================================================//

#include "BaseVSShader.h"

#include "screenspace_vs30.inc"
#include "postprocess_hdr_ps30.inc"

ConVar hdr_scene_brightness_scale("hdr_scene_brightness_scale", "8.0", FCVAR_ARCHIVE, "Scene Brightness scale");
ConVar hdr_screen_nits("hdr_screen_nits", "1400.0", FCVAR_ARCHIVE, "Screen Brightness in nits");
ConVar hdr_hud_nits("hdr_hud_nits", "400.0", FCVAR_ARCHIVE, "HDR Hud Brightness in nits");


BEGIN_VS_SHADER_FLAGS(hdr_postprocess, "HDR PostProcess", SHADER_NOT_EDITABLE)
BEGIN_SHADER_PARAMS
SHADER_PARAM(HDRFBTEXTURE, SHADER_PARAM_TYPE_TEXTURE, "_rt_HDRFB", "")
SHADER_PARAM(HUDTEXTURE, SHADER_PARAM_TYPE_TEXTURE, "_rt_HUD", "")
END_SHADER_PARAMS

SHADER_INIT
{
    if (params[HDRFBTEXTURE]->IsDefined())
    {
        LoadTexture(HDRFBTEXTURE);
    }

    if (params[HUDTEXTURE]->IsDefined())
    {
        LoadTexture(HUDTEXTURE);
    }
}

SHADER_FALLBACK
{
    // Requires DX9 + above
    if (g_pHardwareConfig->GetDXSupportLevel() < 90)
    {
        Assert(0);
        return "Wireframe";
    }
    return 0;
}

SHADER_DRAW
{
    SHADOW_STATE
    {
        pShaderShadow->EnableDepthWrites(false);
        pShaderShadow->EnableTexture(SHADER_SAMPLER0, true);
        pShaderShadow->EnableTexture(SHADER_SAMPLER2, true);

        pShaderShadow->VertexShaderVertexFormat(VERTEX_POSITION, 1, 0, 0);

        // Pre-cache shaders
        DECLARE_STATIC_VERTEX_SHADER(screenspace_vs30);
        SET_STATIC_VERTEX_SHADER(screenspace_vs30);

        DECLARE_STATIC_PIXEL_SHADER(postprocess_hdr_ps30);
        SET_STATIC_PIXEL_SHADER(postprocess_hdr_ps30);
    }

    DYNAMIC_STATE
    {
        BindTexture(SHADER_SAMPLER0, HDRFBTEXTURE, -1);
        BindTexture(SHADER_SAMPLER2, HUDTEXTURE, -1);

        float screen_nits = hdr_screen_nits.GetFloat();
        float hud_nits = hdr_hud_nits.GetFloat();
        float scale = hdr_scene_brightness_scale.GetFloat();

        pShaderAPI->SetPixelShaderConstant(0, &screen_nits);
        pShaderAPI->SetPixelShaderConstant(1, &hud_nits);
        pShaderAPI->SetPixelShaderConstant(2, &scale);

        DECLARE_DYNAMIC_VERTEX_SHADER(screenspace_vs30);
        SET_DYNAMIC_VERTEX_SHADER(screenspace_vs30);

        DECLARE_DYNAMIC_PIXEL_SHADER(postprocess_hdr_ps30);
        SET_DYNAMIC_PIXEL_SHADER(postprocess_hdr_ps30);
    }
    Draw();
}
END_SHADER
