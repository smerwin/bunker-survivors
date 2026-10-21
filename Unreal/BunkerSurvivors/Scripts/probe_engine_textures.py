import unreal
cands = ["/Engine/EngineResources/WhiteSquareTexture", "/Engine/EngineResources/Black", "/Engine/EngineMaterials/DefaultNormal",
         "/Engine/EngineMaterials/DefaultDiffuse", "/Engine/EngineMaterials/BlackSquareTexture", "/Engine/EngineMaterials/FlatNormal",
         "/Engine/EngineResources/DefaultTexture", "/Engine/EngineMaterials/DefaultWhiteGrid", "/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture",
         "/Engine/EngineMaterials/T_Default_Material_Grid_M", "/Engine/ArtTools/RenderToTexture/Textures/127grey", "/Engine/EngineMaterials/Good64x64TilingNoiseHighFreq",
         "/Engine/Functions/Engine_MaterialFunctions02/PivotPainter2/Black_1x1_EXR_Texture", "/Engine/EngineSky/T_Sky_Blue"]
out = []
for c in cands:
    try:
        o = unreal.load_asset(c)
        out.append("%s -> %s" % (c, (o.get_class().get_name() if o else None)))
    except Exception as e:
        out.append("%s -> EXC %s" % (c, e))
open(r"C:\tmp\bunker-survivors\Unreal\BunkerSurvivors\Saved\probe_engine_textures.txt", "w").write("\n".join(out))
