import unreal
L = unreal.RainBuilderLibrary
W = "Particles.SpriteSize = float2(300.0, 300.0);\nParticles.Position = Particles.Position + float3(0.0, 0.0, 300.0) * Engine.DeltaTime;\n"
print(L.build_diag_system("WriteGPU", "", W, True, False, 10, True, False, False))
print(L.build_diag_system("WriteCPU", "", W, True, False, 10, False, False, False))
