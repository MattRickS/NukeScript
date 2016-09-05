import nuke

toolbar = nuke.toolbar("Nodes")
m = toolbar.addMenu("MatteHue", icon="MatteHue.png")
m.addCommand("Cell Noise", "nuke.createNode('Cell_Noise.gizmo')", icon="Cell_Noise.png")
