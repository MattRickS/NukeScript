import nuke
import os


def particleWrite():
    path, ext = os.path.splitext(nuke.thisNode()['write'].value())
    path += '.exr'
    write = nuke.toNode('Write1')
    write['file'].setValue(path)
    write['Render'].execute()


def getInput( node, input, ignoreMe='Dot' ):
    """return node's input but ignore the given node class"""
    found = False
    while not found:
        curr_input = node.input( input )
        if not curr_input:
            return None
        if curr_input.Class() == ignoreMe:
            return getInput( curr_input, 0, ignoreMe )
        else:
            found = True
    return curr_input


def getMinMax( srcNode, channel='depth.Z' ):
    '''
    Return the min and max values of a given node's image as a tuple
    args:
       srcNode  - node to analyse
       channels  - channels to analyse. This can either be a channel or layer name
    '''
    MinColor = nuke.nodes.MinColor( channels=channel, target=0, inputs=[srcNode] )
    Inv = nuke.nodes.Invert( channels=channel, inputs=[srcNode])
    MaxColor = nuke.nodes.MinColor( channels=channel, target=0, inputs=[Inv] )
    
    curFrame = nuke.frame()
    nuke.execute( MinColor, curFrame, curFrame )
    minV = -MinColor['pixeldelta'].value()
    
    nuke.execute( MaxColor, curFrame, curFrame )
    maxV = MaxColor['pixeldelta'].value() + 1
    
    for n in ( MinColor, MaxColor, Inv ):
        nuke.delete( n )
    return minV, maxV


def setBoundingBox():
    '''
    Sets the bounding box dimensions for the particles at the current frame
    Applies an offset to the particles to keep them everything centered on origin
    '''
    # Calculate the particle bounds
    pos_node = nuke.toNode('Position_Checker')
    x = getMinMax( pos_node, 'rgba.red' )
    y = getMinMax( pos_node, 'rgba.green' )
    z = getMinMax( pos_node, 'rgba.blue' )

    cube = nuke.toNode('BoundingBox')

    # Centerpoint of Bounds
    mid_x = ( x[1] - x[0] ) / 2.0 + x[0]
    mid_y = ( y[1] - y[0] ) / 2.0 + y[0]
    mid_z = ( z[1] - z[0] ) / 2.0 + z[0]

    # Set cube size centered on origin
    cube['cube'].setValue( [ x[0] - mid_x, y[0] - mid_y, z[0] - mid_z, x[1] - mid_x, y[1] - mid_y, z[1] - mid_z ] )

    if nuke.thisNode()['center_pivot'].value():
        # Grade offset to particle positions to center on origin
        offset = nuke.toNode('Offset')
        offset['value'].setValue( -mid_x, 0 )
        offset['value'].setValue( -mid_y, 1 )
        offset['value'].setValue( -mid_z, 2 )
