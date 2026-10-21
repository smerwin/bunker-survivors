# Imports a single glTF to learn how Interchange names and structures the assets.
import unreal, os, json

SRC = r"C:\tmp\bunker-survivors\SC2Export\Models\zergling.glb"
DEST = "/Game/Probe/zergling"
log = []

task = unreal.AssetImportTask()
task.filename = SRC
task.destination_path = DEST
task.automated = True
task.replace_existing = True
task.save = True
unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
log.append("imported_object_paths: %s" % list(task.imported_object_paths))

reg = unreal.AssetRegistryHelpers.get_asset_registry()
for a in reg.get_assets_by_path(DEST, recursive=True):
    log.append("%s  %s" % (a.asset_class_path.asset_name, a.package_name))

for p in task.imported_object_paths:
    obj = unreal.load_asset(p)
    if isinstance(obj, unreal.SkeletalMesh):
        log.append("skeletal mesh %s materials=%s" % (p, [m.material_slot_name for m in obj.materials]))
        b = obj.get_bounds()
        log.append("bounds origin=%s extent=%s" % (b.origin, b.box_extent))
    if isinstance(obj, unreal.AnimSequence):
        log.append("anim %s len=%s" % (p, obj.get_editor_property("sequence_length") if hasattr(obj, "get_editor_property") else "?"))

with open(r"C:\tmp\bunker-survivors\Unreal\BunkerSurvivors\Saved\probe_import.txt", "w") as f:
    f.write("\n".join(log))
