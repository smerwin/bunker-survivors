# Imports SC2Export/ (made by scripts/export-assets.js) into /Game/SC2 and builds the game's materials
# and map. Run headless:
#   UnrealEditor-Cmd.exe BunkerSurvivors.uproject -run=pythonscript -script=Scripts/import_sc2.py
# Everything under /Game/SC2 is derived from the player's own StarCraft II install: never commit it.
import unreal, os, re, json, shutil, struct, traceback

PROJECT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EXPORT = os.path.normpath(os.path.join(PROJECT, "..", "..", "SC2Export"))
LOG_PATH = os.path.join(PROJECT, "Saved", "import_sc2.log")
ONLY = [s for s in os.environ.get("BS_ONLY", "").split(",") if s]

asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
mel = unreal.MaterialEditingLibrary
eal = unreal.EditorAssetLibrary
lines = []


def log(msg):
    lines.append(str(msg))
    unreal.log("[import_sc2] " + str(msg))


def save_log():
    os.makedirs(os.path.dirname(LOG_PATH), exist_ok=True)
    with open(LOG_PATH, "w", encoding="utf-8") as f:
        f.write("\n".join(lines))


def import_file(src, dest, name=None):
    task = unreal.AssetImportTask()
    task.filename = src
    task.destination_path = dest
    if name:
        task.destination_name = name
    task.automated = True
    task.replace_existing = True
    task.save = True
    asset_tools.import_asset_tasks([task])
    return list(task.imported_object_paths)


def glb_json(path):
    with open(path, "rb") as f:
        data = f.read()
    length = struct.unpack_from("<I", data, 12)[0]
    return json.loads(data[20:20 + length].decode("utf-8"))


def load(path):
    return unreal.load_asset(path)


# ---- Materials ------------------------------------------------------------------------------------
def new_material(path, name):
    # Rebuild in place: deleting a material that instances still reference leaves those instances parentless.
    full = path + "/" + name
    if eal.does_asset_exist(full):
        m = unreal.load_asset(full)
        mel.delete_all_material_expressions(m)
        return m
    return asset_tools.create_asset(name, path, unreal.Material, unreal.MaterialFactoryNew())


def expr(mat, cls, x, y, **props):
    e = mel.create_material_expression(mat, cls, x, y)
    for k, v in props.items():
        e.set_editor_property(k, v)
    return e


def tex_param(mat, name, x, y, default, sampler=None):
    e = expr(mat, unreal.MaterialExpressionTextureSampleParameter2D, x, y, parameter_name=name, texture=load(default))
    if sampler is not None:
        e.set_editor_property("sampler_type", sampler)
    return e


def scalar(mat, name, x, y, value):
    return expr(mat, unreal.MaterialExpressionScalarParameter, x, y, parameter_name=name, default_value=value)


def vector(mat, name, x, y, rgba):
    return expr(mat, unreal.MaterialExpressionVectorParameter, x, y, parameter_name=name, default_value=unreal.LinearColor(*rgba))


def binop(mat, cls, a, a_out, b, b_out, x, y):
    e = expr(mat, cls, x, y)
    mel.connect_material_expressions(a, a_out, e, "A")
    mel.connect_material_expressions(b, b_out, e, "B")
    return e


WHITE = "/Engine/EngineResources/WhiteSquareTexture"
BLACK = "/Engine/EngineResources/Black"
FLATN = "/Engine/EngineMaterials/DefaultNormal"


def build_unit_material(path, name, two_sided):
    """Lit SC2 unit material: diffuse alpha is the team-colour mask; HitFlash brightens on damage."""
    m = new_material(path, name)
    m.set_editor_property("two_sided", two_sided)
    m.set_editor_property("used_with_skeletal_mesh", True)
    d = tex_param(m, "Diffuse", -900, -200, WHITE)
    team = vector(m, "TeamColor", -900, 100, (0.9, 0.12, 0.08, 1))
    tint = binop(m, unreal.MaterialExpressionMultiply, d, "RGB", team, "", -600, 0)
    boost = expr(m, unreal.MaterialExpressionMultiply, -450, 0, const_b=1.7)
    mel.connect_material_expressions(tint, "", boost, "A")
    lerp = expr(m, unreal.MaterialExpressionLinearInterpolate, -250, -100)
    mel.connect_material_expressions(d, "RGB", lerp, "A")
    mel.connect_material_expressions(boost, "", lerp, "B")
    mel.connect_material_expressions(d, "A", lerp, "Alpha")
    mel.connect_material_property(lerp, "", unreal.MaterialProperty.MP_BASE_COLOR)
    n = tex_param(m, "Normal", -900, 350, FLATN, unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL)
    mel.connect_material_property(n, "RGB", unreal.MaterialProperty.MP_NORMAL)
    e = tex_param(m, "Emissive", -900, 650, BLACK)
    es = scalar(m, "EmissiveStrength", -900, 900, 2.0)
    em = binop(m, unreal.MaterialExpressionMultiply, e, "RGB", es, "", -600, 700)
    flash = scalar(m, "HitFlash", -900, 1000, 0.0)
    flashcol = vector(m, "HitFlashColor", -900, 1100, (1.0, 0.85, 0.7, 1))
    fl = binop(m, unreal.MaterialExpressionMultiply, flash, "", flashcol, "", -600, 950)
    add = binop(m, unreal.MaterialExpressionAdd, em, "", fl, "", -350, 800)
    mel.connect_material_property(add, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    mel.connect_material_property(scalar(m, "Roughness", -350, 400, 0.55), "", unreal.MaterialProperty.MP_ROUGHNESS)
    mel.connect_material_property(scalar(m, "Specular", -350, 500, 0.45), "", unreal.MaterialProperty.MP_SPECULAR)
    # Corpses set DepthPush so living units always draw in front of them where they overlap.
    # (pixel depth offset is not exposed to Python, so slide the vertices back along the view ray instead).
    push = scalar(m, "DepthPush", -600, 1250, 0.0)
    campos = expr(m, unreal.MaterialExpressionCameraPositionWS, -900, 1350)
    wpos = expr(m, unreal.MaterialExpressionWorldPosition, -900, 1450)
    tocam = binop(m, unreal.MaterialExpressionSubtract, campos, "", wpos, "", -750, 1400)
    camv = expr(m, unreal.MaterialExpressionNormalize, -600, 1400)
    mel.connect_material_expressions(tocam, "", camv, "")
    away = binop(m, unreal.MaterialExpressionMultiply, camv, "", push, "", -400, 1300)
    neg = expr(m, unreal.MaterialExpressionMultiply, -250, 1300, const_b=-1.0)
    mel.connect_material_expressions(away, "", neg, "A")
    mel.connect_material_property(neg, "", unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)
    mel.recompile_material(m)
    return m


def build_glow_material(path, name, additive):
    """Unlit glow for SC2 engine flares / eyes (additive) or alpha-blended cards."""
    m = new_material(path, name)
    m.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    m.set_editor_property("blend_mode", unreal.BlendMode.BLEND_ADDITIVE if additive else unreal.BlendMode.BLEND_TRANSLUCENT)
    m.set_editor_property("two_sided", True)
    m.set_editor_property("used_with_skeletal_mesh", True)
    d = tex_param(m, "Diffuse", -700, 0, WHITE)
    k = scalar(m, "Intensity", -700, 300, 2.0 if additive else 1.0)
    e = binop(m, unreal.MaterialExpressionMultiply, d, "RGB", k, "", -400, 100)
    mel.connect_material_property(e, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    if not additive:
        mel.connect_material_property(d, "A", unreal.MaterialProperty.MP_OPACITY)
    mel.recompile_material(m)
    return m


def build_fx_material(path, name, ring):
    """Code-driven effects: Color * Intensity * Fade, additive. Ring variant draws a soft ring on a plane."""
    m = new_material(path, name)
    m.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    m.set_editor_property("blend_mode", unreal.BlendMode.BLEND_ADDITIVE)
    m.set_editor_property("two_sided", True)
    col = vector(m, "Color", -900, 0, (1, 0.6, 0.2, 1))
    inten = scalar(m, "Intensity", -900, 200, 4.0)
    fade = scalar(m, "Fade", -900, 300, 1.0)
    c1 = binop(m, unreal.MaterialExpressionMultiply, col, "", inten, "", -650, 50)
    c2 = binop(m, unreal.MaterialExpressionMultiply, c1, "", fade, "", -450, 100)
    if ring:
        uv = expr(m, unreal.MaterialExpressionTextureCoordinate, -1100, 500)
        center = expr(m, unreal.MaterialExpressionConstant2Vector, -1100, 650, r=0.5, g=0.5)
        dist = binop(m, unreal.MaterialExpressionDistance, uv, "", center, "", -900, 550)
        radius = scalar(m, "Radius", -1100, 800, 0.35)
        thick = scalar(m, "Thickness", -1100, 900, 0.08)
        diff = binop(m, unreal.MaterialExpressionSubtract, dist, "", radius, "", -700, 600)
        ab = expr(m, unreal.MaterialExpressionAbs, -550, 600)
        mel.connect_material_expressions(diff, "", ab, "")
        q = binop(m, unreal.MaterialExpressionDivide, ab, "", thick, "", -400, 650)
        inv = expr(m, unreal.MaterialExpressionOneMinus, -250, 650)
        mel.connect_material_expressions(q, "", inv, "")
        sat = expr(m, unreal.MaterialExpressionSaturate, -100, 650)
        mel.connect_material_expressions(inv, "", sat, "")
        # Inner fill so blasts read as a disc with a bright rim.
        fill = scalar(m, "Fill", -1100, 1000, 0.25)
        inside = expr(m, unreal.MaterialExpressionOneMinus, -700, 850)
        d2 = expr(m, unreal.MaterialExpressionMultiply, -850, 850, const_b=2.0)
        mel.connect_material_expressions(dist, "", d2, "A")
        mel.connect_material_expressions(d2, "", inside, "")
        insat = expr(m, unreal.MaterialExpressionSaturate, -550, 850)
        mel.connect_material_expressions(inside, "", insat, "")
        inf = binop(m, unreal.MaterialExpressionMultiply, insat, "", fill, "", -350, 850)
        mask = binop(m, unreal.MaterialExpressionAdd, sat, "", inf, "", 0, 700)
        c2 = binop(m, unreal.MaterialExpressionMultiply, c2, "", mask, "", -200, 300)
    else:
        # Soft edges on spheres/cylinders: fade where the surface turns away from the camera.
        fres = expr(m, unreal.MaterialExpressionFresnel, -700, 500)
        fres.set_editor_property("exponent", 1.5)
        inv = expr(m, unreal.MaterialExpressionOneMinus, -500, 500)
        mel.connect_material_expressions(fres, "", inv, "")
        c2 = binop(m, unreal.MaterialExpressionMultiply, c2, "", inv, "", -250, 300)
    mel.connect_material_property(c2, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    mel.recompile_material(m)
    return m


def build_particle_material(path, name):
    """Camera-facing sprite for sparks, muzzle flashes and fire. Driven per instance by custom data:
    0-2 colour, 3 intensity, 4 noise amount (0 = clean spark, 1 = ragged flame), 5 random seed."""
    m = new_material(path, name)
    m.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    m.set_editor_property("blend_mode", unreal.BlendMode.BLEND_ADDITIVE)
    m.set_editor_property("two_sided", True)
    m.set_editor_property("used_with_instanced_static_meshes", True)

    def cd(i, x, y):
        e = expr(m, unreal.MaterialExpressionPerInstanceCustomData, x, y)
        e.set_editor_property("data_index", i)
        return e

    r, g, b, inten, noise_amt, seed = cd(0, -1400, 0), cd(1, -1400, 100), cd(2, -1400, 200), cd(3, -1400, 300), cd(4, -1400, 400), cd(5, -1400, 500)
    rg = binop(m, unreal.MaterialExpressionAppendVector, r, "", g, "", -1200, 50)
    rgb = binop(m, unreal.MaterialExpressionAppendVector, rg, "", b, "", -1050, 100)
    # Soft round falloff: (1 - 2d)^2, hot core.
    uv = expr(m, unreal.MaterialExpressionTextureCoordinate, -1400, 650)
    centre = expr(m, unreal.MaterialExpressionConstant2Vector, -1400, 780, r=0.5, g=0.5)
    dist = binop(m, unreal.MaterialExpressionDistance, uv, "", centre, "", -1200, 700)
    d2 = expr(m, unreal.MaterialExpressionMultiply, -1050, 700, const_b=2.0)
    mel.connect_material_expressions(dist, "", d2, "A")
    inv = expr(m, unreal.MaterialExpressionOneMinus, -900, 700)
    mel.connect_material_expressions(d2, "", inv, "")
    sat = expr(m, unreal.MaterialExpressionSaturate, -780, 700)
    mel.connect_material_expressions(inv, "", sat, "")
    soft = binop(m, unreal.MaterialExpressionMultiply, sat, "", sat, "", -650, 700)
    # Noise breakup for flames: tiling noise offset by the per-particle seed.
    seed2 = binop(m, unreal.MaterialExpressionAppendVector, seed, "", seed, "", -1200, 900)
    uvs = expr(m, unreal.MaterialExpressionMultiply, -1200, 1000, const_b=0.7)
    mel.connect_material_expressions(uv, "", uvs, "A")
    uvn = binop(m, unreal.MaterialExpressionAdd, uvs, "", seed2, "", -1050, 950)
    noise = expr(m, unreal.MaterialExpressionTextureSample, -900, 950, texture=load("/Engine/EngineMaterials/Good64x64TilingNoiseHighFreq"))
    mel.connect_material_expressions(uvn, "", noise, "UVs")
    nboost = expr(m, unreal.MaterialExpressionMultiply, -700, 950, const_b=1.8)
    mel.connect_material_expressions(noise, "R", nboost, "A")
    one = expr(m, unreal.MaterialExpressionConstant, -700, 1080, r=1.0)
    nl = expr(m, unreal.MaterialExpressionLinearInterpolate, -550, 950)
    mel.connect_material_expressions(one, "", nl, "A")
    mel.connect_material_expressions(nboost, "", nl, "B")
    mel.connect_material_expressions(noise_amt, "", nl, "Alpha")
    mask = binop(m, unreal.MaterialExpressionMultiply, soft, "", nl, "", -400, 800)
    c1 = binop(m, unreal.MaterialExpressionMultiply, rgb, "", inten, "", -850, 150)
    out = binop(m, unreal.MaterialExpressionMultiply, c1, "", mask, "", -250, 400)
    mel.connect_material_property(out, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    mel.recompile_material(m)
    return m


def build_sprite_material(path, name, additive):
    """Textured particle sprite for StarCraft II effects (see BSFx.cpp). Per-instance custom data: 0-3 RGBA,
    4 flipbook frame, 5-6 flipbook columns and rows. Parameters: Tex, Boost (brightness)."""
    m = new_material(path, name)
    m.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    m.set_editor_property("blend_mode", unreal.BlendMode.BLEND_ADDITIVE if additive else unreal.BlendMode.BLEND_TRANSLUCENT)
    m.set_editor_property("two_sided", True)
    m.set_editor_property("used_with_instanced_static_meshes", True)

    def cd(i, x, y):
        e = expr(m, unreal.MaterialExpressionPerInstanceCustomData, x, y)
        e.set_editor_property("data_index", i)
        return e

    r, g, b, a, frame = cd(0, -1600, 0), cd(1, -1600, 100), cd(2, -1600, 200), cd(3, -1600, 300), cd(4, -1600, 500)
    cols, rows = cd(5, -1600, 650), cd(6, -1600, 750)
    # Flipbook: column = frame mod Cols, row = floor(frame / Cols); uv' = (uv + (col, row)) / (Cols, Rows).
    col = binop(m, unreal.MaterialExpressionFmod, frame, "", cols, "", -1400, 520)
    div = binop(m, unreal.MaterialExpressionDivide, frame, "", cols, "", -1400, 620)
    row = expr(m, unreal.MaterialExpressionFloor, -1250, 620)
    mel.connect_material_expressions(div, "", row, "")
    cr = binop(m, unreal.MaterialExpressionAppendVector, col, "", row, "", -1100, 560)
    grid = binop(m, unreal.MaterialExpressionAppendVector, cols, "", rows, "", -1100, 700)
    uv = expr(m, unreal.MaterialExpressionTextureCoordinate, -1100, 420)
    uvc = binop(m, unreal.MaterialExpressionAdd, uv, "", cr, "", -950, 480)
    uvf = binop(m, unreal.MaterialExpressionDivide, uvc, "", grid, "", -800, 520)
    tex = tex_param(m, "Tex", -650, 450, WHITE)
    mel.connect_material_expressions(uvf, "", tex, "UVs")
    rg = binop(m, unreal.MaterialExpressionAppendVector, r, "", g, "", -1400, 50)
    rgb = binop(m, unreal.MaterialExpressionAppendVector, rg, "", b, "", -1250, 100)
    boost = scalar(m, "Boost", -650, 250, 1.6 if additive else 1.0)
    col_rgb = binop(m, unreal.MaterialExpressionMultiply, tex, "RGB", rgb, "", -400, 200)
    lit = binop(m, unreal.MaterialExpressionMultiply, col_rgb, "", boost, "", -250, 200)
    alpha = binop(m, unreal.MaterialExpressionMultiply, tex, "A", a, "", -400, 500)
    # Soft intersection with the ground and units.
    fade = expr(m, unreal.MaterialExpressionDepthFade, -250, 500)
    fade.set_editor_property("fade_distance_default", 40.0)
    mel.connect_material_expressions(alpha, "", fade, "Opacity")
    if additive:
        out = binop(m, unreal.MaterialExpressionMultiply, lit, "", fade, "", -100, 300)
        mel.connect_material_property(out, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    else:
        mel.connect_material_property(lit, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
        mel.connect_material_property(fade, "", unreal.MaterialProperty.MP_OPACITY)
    mel.recompile_material(m)
    return m


def fx_texture_name(sc2_path):
    """Must match BSFx::TextureAssetName in BSFx.cpp."""
    base = os.path.splitext(os.path.basename(sc2_path))[0]
    return "FX_" + re.sub(r"[^A-Za-z0-9_]", "_", base)


def import_fx(manifest):
    """Particle textures into /Game/SC2/FX; effect descriptions copied as plain files into Content/SC2/FXData."""
    n = 0
    for sc2_path, exported in manifest.get("fxTextures", {}).items():
        src = os.path.join(EXPORT, "FX", "Textures", exported + ".png")
        if not os.path.exists(src):
            continue
        for p in import_file(src, "/Game/SC2/FX", fx_texture_name(sc2_path)):
            t = load(p)
            if isinstance(t, unreal.Texture2D):
                t.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_EFFECTS)
                t.set_editor_property("srgb", True)
                eal.save_loaded_asset(t)
                n += 1
    data_dir = os.path.join(PROJECT, "Content", "SC2", "FXData")
    os.makedirs(data_dir, exist_ok=True)
    for key, rel in manifest.get("fx", {}).items():
        shutil.copyfile(os.path.join(EXPORT, rel), os.path.join(data_dir, key + ".json"))
    log("fx: %d textures, %d effects" % (n, len(manifest.get("fx", {}))))
    # HUD fonts (SC2's Eurostile family) are loaded from plain files at runtime, like the effect data.
    font_dir = os.path.join(PROJECT, "Content", "SC2", "Fonts")
    os.makedirs(font_dir, exist_ok=True)
    for name, rel in manifest.get("fonts", {}).items():
        shutil.copyfile(os.path.join(EXPORT, rel), os.path.join(font_dir, name))
    log("fonts: %d" % len(manifest.get("fonts", {})))


def build_ground_material(path, name, ground_tex):
    m = new_material(path, name)
    uv = expr(m, unreal.MaterialExpressionTextureCoordinate, -1100, 0)
    tiling = scalar(m, "Tiling", -1100, 150, 1.0)
    uvs = binop(m, unreal.MaterialExpressionMultiply, uv, "", tiling, "", -900, 50)
    t = expr(m, unreal.MaterialExpressionTextureSampleParameter2D, -700, 0, parameter_name="Ground", texture=ground_tex or load(WHITE))
    mel.connect_material_expressions(uvs, "", t, "UVs")
    tint = vector(m, "Tint", -700, 300, (0.85, 0.8, 0.85, 1))
    bc = binop(m, unreal.MaterialExpressionMultiply, t, "RGB", tint, "", -400, 100)
    mel.connect_material_property(bc, "", unreal.MaterialProperty.MP_BASE_COLOR)
    mel.connect_material_property(scalar(m, "Roughness", -400, 300, 0.9), "", unreal.MaterialProperty.MP_ROUGHNESS)
    mel.recompile_material(m)
    return m


def build_terrain_material(path, name, ground_tex, cliff_tex):
    # Lane chokepoint ground (see BSGame::BuildTerrain): UV0 tiles the ground texture, UV1/UV2 project the cliff texture
    # along the two horizontal axes; vertex colour R = brightness (cliff-foot shadow, plateau lift), B = projection blend,
    # A = cliff weight.
    m = new_material(path, name)
    def coord(i, y):
        return expr(m, unreal.MaterialExpressionTextureCoordinate, -1300, y, coordinate_index=i)
    def sample(tex, uv, y):
        t = expr(m, unreal.MaterialExpressionTextureSample, -1050, y, texture=tex)
        mel.connect_material_expressions(uv, "", t, "UVs")
        return t
    g = sample(ground_tex or load(WHITE), coord(0, -300), -300)
    ca = sample(cliff_tex or load(WHITE), coord(1, 0), 0)
    cb = sample(cliff_tex or load(WHITE), coord(2, 300), 300)
    vc = expr(m, unreal.MaterialExpressionVertexColor, -1050, 600)
    tint = vector(m, "Tint", -1050, -500, (0.85, 0.8, 0.85, 1))
    gt = binop(m, unreal.MaterialExpressionMultiply, g, "RGB", tint, "", -800, -350)
    cl = expr(m, unreal.MaterialExpressionLinearInterpolate, -800, 100)
    mel.connect_material_expressions(cb, "RGB", cl, "A")
    mel.connect_material_expressions(ca, "RGB", cl, "B")
    mel.connect_material_expressions(vc, "B", cl, "Alpha")
    clb = expr(m, unreal.MaterialExpressionMultiply, -600, 100, const_b=1.25)
    mel.connect_material_expressions(cl, "", clb, "A")
    mix = expr(m, unreal.MaterialExpressionLinearInterpolate, -400, -100)
    mel.connect_material_expressions(gt, "", mix, "A")
    mel.connect_material_expressions(clb, "", mix, "B")
    mel.connect_material_expressions(vc, "A", mix, "Alpha")
    bright = expr(m, unreal.MaterialExpressionMultiply, -400, 300, const_b=1.25)
    mel.connect_material_expressions(vc, "R", bright, "A")
    bc = binop(m, unreal.MaterialExpressionMultiply, mix, "", bright, "", -200, 0)
    mel.connect_material_property(bc, "", unreal.MaterialProperty.MP_BASE_COLOR)
    mel.connect_material_property(scalar(m, "Roughness", -200, 300, 0.9), "", unreal.MaterialProperty.MP_ROUGHNESS)
    m.set_editor_property("two_sided", True)
    mel.recompile_material(m)
    return m


def make_mi(parent, path, name, textures, scalars=None):
    full = path + "/" + name
    if eal.does_asset_exist(full):
        eal.delete_asset(full)
    mi = asset_tools.create_asset(name, path, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    mi.set_editor_property("parent", parent)
    mel.set_material_instance_parent(mi, parent)
    if mi.get_editor_property("parent") is None:
        log("WARNING: %s/%s has no parent after assignment (parent=%s)" % (path, name, parent))
    for k, v in textures.items():
        if v:
            mel.set_material_instance_texture_parameter_value(mi, k, v)
    for k, v in (scalars or {}).items():
        mel.set_material_instance_scalar_parameter_value(mi, k, v)
    mel.update_material_instance(mi)
    eal.save_loaded_asset(mi)
    return mi


# ---- Main -------------------------------------------------------------------------------------------
def main():
    manifest = json.load(open(os.path.join(EXPORT, "manifest.json"), encoding="utf-8"))
    log("SC2 export from StarCraft II %s, %d models" % (manifest["sc2Version"], len(manifest["models"])))

    mats = "/Game/Materials"
    m_unit = build_unit_material(mats, "M_SC2Unit", False)
    m_unit2 = build_unit_material(mats, "M_SC2Unit_TwoSided", True)
    m_add = build_glow_material(mats, "M_SC2Additive", True)
    m_blend = build_glow_material(mats, "M_SC2Blend", False)
    build_fx_material(mats, "M_FX", False)
    build_fx_material(mats, "M_FXRing", True)
    build_particle_material(mats, "M_FXParticle")
    build_sprite_material(mats, "M_FXSpriteAdd", True)
    build_sprite_material(mats, "M_FXSpriteBlend", False)

    ground = None
    gpath = os.path.join(EXPORT, "Textures", "Ground_Diffuse.png")
    if os.path.exists(gpath):
        ground = load(import_file(gpath, "/Game/SC2/Textures", "T_Ground")[0])
    build_ground_material(mats, "M_Ground", ground)
    cliff = None
    cpath = os.path.join(EXPORT, "Textures", "Cliff_Diffuse.png")
    if os.path.exists(cpath):
        cliff = load(import_file(cpath, "/Game/SC2/Textures", "T_Cliff")[0])
        cliff.set_editor_property("address_y", unreal.TextureAddress.TA_CLAMP)
        eal.save_loaded_asset(cliff)
    build_terrain_material(mats, "M_Terrain", ground, cliff)
    for m in (m_unit, m_unit2, m_add, m_blend):
        eal.save_loaded_asset(m)
    for n in ("M_FX", "M_FXRing", "M_Ground", "M_Terrain", "M_FXParticle", "M_FXSpriteAdd", "M_FXSpriteBlend"):
        eal.save_asset(mats + "/" + n)
    import_fx(manifest)
    if os.environ.get("BS_FX_ONLY") == "1":
        return
    if os.environ.get("BS_MATERIALS_ONLY") == "1":
        log("materials rebuilt (materials-only run)")
        return

    # Icons for the UI: no mips, UI compression, never streamed.
    for name, rel in manifest["icons"].items():
        paths = import_file(os.path.join(EXPORT, rel), "/Game/SC2/Icons", "T_" + name.replace("-", "_"))
        for p in paths:
            t = load(p)
            if isinstance(t, unreal.Texture2D):
                t.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_EDITOR_ICON)
                t.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
                t.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_UI)
                t.set_editor_property("never_stream", True)
                eal.save_loaded_asset(t)
    log("icons imported: %d" % len(manifest["icons"]))
    if os.environ.get("BS_ICONS_ONLY") == "1":
        return

    count = 0
    for key, files in manifest["sounds"].items():
        for i, rel in enumerate(files):
            import_file(os.path.join(EXPORT, rel), "/Game/SC2/Sounds", "S_%s_%d" % (key, i))
            count += 1
    log("sounds imported: %d" % count)

    for key, info in manifest["models"].items():
        if ONLY and key not in ONLY:
            continue
        try:
            import_model(key, info, m_unit, m_unit2, m_add, m_blend)
        except Exception:
            log("FAILED %s\n%s" % (key, traceback.format_exc()))

    # Empty battlefield map: the game spawns its lights, ground and units at runtime.
    if not eal.does_asset_exist("/Game/Maps/Battlefield"):
        world = asset_tools.create_asset("Battlefield", "/Game/Maps", unreal.World, unreal.WorldFactory())
        eal.save_loaded_asset(world)
    log("map ready: /Game/Maps/Battlefield")


def import_model(key, info, m_unit, m_unit2, m_add, m_blend):
    src = os.path.join(EXPORT, info["file"])
    dest = "/Game/SC2/Models/" + key
    if eal.does_directory_exist(dest):
        eal.delete_directory(dest)
    paths = import_file(src, dest)
    gj = glb_json(src)
    gmats = {m["name"]: m for m in gj.get("materials", [])}
    mesh = None
    anims = []
    for p in paths:
        o = load(p)
        if isinstance(o, unreal.SkeletalMesh):
            mesh = o
        elif isinstance(o, unreal.AnimSequence):
            anims.append(o)
    if not mesh:
        log("%s: no skeletal mesh imported (%d assets)" % (key, len(paths)))
        return
    # Swap Interchange's generic glTF materials for ours, keeping the imported textures.
    new_slots = []
    for slot in mesh.get_editor_property("materials"):
        mi_old = slot.get_editor_property("material_interface")
        slot_name = str(slot.get_editor_property("material_slot_name"))
        texs = {}
        if isinstance(mi_old, unreal.MaterialInstanceConstant):
            for tp in mi_old.get_editor_property("texture_parameter_values"):
                pname = str(tp.get_editor_property("parameter_info").get_editor_property("name")).lower()
                val = tp.get_editor_property("parameter_value")
                if "basecolor" in pname or "diffuse" in pname:
                    texs["Diffuse"] = val
                elif "normal" in pname:
                    texs["Normal"] = val
                elif "emissive" in pname:
                    texs["Emissive"] = val
        g = gmats.get(slot_name, {})
        extras = g.get("extras", {})
        blend = extras.get("sc2BlendMode", "OPAQUE")
        if blend == "OPAQUE":
            parent = m_unit2 if g.get("doubleSided") else m_unit
            mi = make_mi(parent, dest + "/Materials", "MI_" + slot_name, texs)
        elif blend == "BLEND":
            mi = make_mi(m_blend, dest + "/Materials", "MI_" + slot_name, {"Diffuse": texs.get("Diffuse")})
        else:
            mi = make_mi(m_add, dest + "/Materials", "MI_" + slot_name, {"Diffuse": texs.get("Diffuse") or texs.get("Emissive")})
        slot.set_editor_property("material_interface", mi)
        new_slots.append(slot)
    mesh.set_editor_property("materials", new_slots)
    eal.save_loaded_asset(mesh)
    b = mesh.get_bounds()
    log("%s: mesh=%s anims=%d slots=%d bounds origin=(%.0f,%.0f,%.0f) extent=(%.0f,%.0f,%.0f)" % (
        key, mesh.get_path_name(), len(anims), len(new_slots), b.origin.x, b.origin.y, b.origin.z, b.box_extent.x, b.box_extent.y, b.box_extent.z))


try:
    main()
except Exception:
    log("FATAL\n" + traceback.format_exc())
finally:
    save_log()
