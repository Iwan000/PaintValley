import unreal
log = unreal.log
at = unreal.AssetToolsHelpers.get_asset_tools()
# 1) empty system through the stock factory (no C++ needed)
sysf = unreal.NiagaraSystemFactoryNew()
ns = at.create_asset("NS_PyEmpty", "/Game/ProbePy", unreal.NiagaraSystem, sysf)
log("PYPROBE factory system: %s" % ns)
# 2) module script factory
mf = unreal.NiagaraModuleScriptFactory()
ms = at.create_asset("NM_PyModule", "/Game/ProbePy", unreal.NiagaraScript, mf)
log("PYPROBE module script: %s" % ms)
# 3) emitter factory (empty emitter; ConfigureProperties is NOT called by create_asset)
ef = unreal.NiagaraEmitterFactoryNew()
em = at.create_asset("NE_PyEmitter", "/Game/ProbePy", unreal.NiagaraEmitter, ef)
log("PYPROBE emitter: %s" % em)
# 4) what does python see on NiagaraSystem?
props = [p for p in dir(ns) if not p.startswith('_')]
log("PYPROBE NiagaraSystem attrs: %s" % ", ".join(props))
for n in ["fixed_bounds", "b_fixed_bounds", "warmup_time", "effect_type", "emitter_handles", "exposed_parameters"]:
    try:
        log("PYPROBE get %s = %s" % (n, ns.get_editor_property(n)))
    except Exception as e:
        log("PYPROBE get %s FAILED: %s" % (n, e))
# 5) any Niagara editor libraries?
log("PYPROBE niagara classes: %s" % ", ".join(sorted([c for c in dir(unreal) if 'Niagara' in c and ('Library' in c or 'Editor' in c or 'Utilities' in c)])))
# 6) the C++ bridge from the project editor module
ok = unreal.NiagaraProbeLibrary.build_probe_system("/Game/ProbePy", "NS_FromPython")
log("PYPROBE C++ library build_probe_system -> %s" % ok)
unreal.EditorAssetLibrary.save_directory("/Game/ProbePy")
