import nuke

def cycleContactSheet():

    options = nuke.allNodes('LayerContactSheet')
    if not options: return
    
    viewer = nuke.activeViewer().node()
    idx = 0
    
    try:
        node = viewer.input(9)
    except ValueError:
        node = None
    
    if node:
        idx = options.index(node)
        idx = (idx + 1)%len(options)
    
    viewer.setInput(9, options[idx])
    nuke.activeViewer().activateInput(9)