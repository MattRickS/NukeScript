import nuke

toolbar = nuke.toolbar("Nodes")
m = toolbar.addMenu("MatteHue", icon="MatteHue.png")
m.addCommand("ParticleRenderer", "nuke.createNode('ParticleRenderer_V01_01.gizmo')", icon="ParticleRenderer.png")
