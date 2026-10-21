# Checks that unit material instances have a parent and that the master material compiles and samples textures.
import unreal
out = []
mel = unreal.MaterialEditingLibrary
for path in ["/Game/SC2/Models/zergling/Materials/MI_Standard_4", "/Game/SC2/Models/bunker/Materials"]:
    pass
reg = unreal.AssetRegistryHelpers.get_asset_registry()
for a in reg.get_assets_by_path("/Game/SC2/Models/zergling", recursive=True) + reg.get_assets_by_path("/Game/SC2/Models/bunker", recursive=True):
    if str(a.asset_class_path.asset_name) == "MaterialInstanceConstant" and "/Materials/MI_" in str(a.package_name):
        mi = unreal.load_asset(str(a.package_name))
        p = mi.get_editor_property("parent")
        out.append("%s parent=%s" % (a.package_name, p.get_path_name() if p else None))
        for n in ("Diffuse", "Normal", "Emissive"):
            out.append("   %s = %s" % (n, mel.get_material_instance_texture_parameter_value(mi, n)))
m = unreal.load_asset("/Game/Materials/M_SC2Unit")
out.append("M_SC2Unit expressions=%d" % mel.get_num_material_expressions(m))
out.append("M_SC2Unit used textures=%s" % [t.get_name() for t in mel.get_used_textures(m)])
out.append("M_SC2Unit texture params=%s" % mel.get_texture_parameter_names(m))
out.append("M_SC2Unit vector params=%s" % mel.get_vector_parameter_names(m))
out.append("base color input=%s" % mel.get_material_property_input_node(m, unreal.MaterialProperty.MP_BASE_COLOR))
mesh = unreal.load_asset("/Game/SC2/Models/zergling/zergling/SkeletalMeshes/zergling")
for s in mesh.get_editor_property("materials"):
    mi = s.get_editor_property("material_interface")
    out.append("zergling slot %s -> %s" % (s.get_editor_property("material_slot_name"), mi.get_path_name() if mi else None))
open(r"C:\tmp\bunker-survivors\Unreal\BunkerSurvivors\Saved\probe_materials.txt", "w").write("\n".join(out))
