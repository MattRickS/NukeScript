kernel BlinkMove_V01_01 : ImageComputationKernel<ePixelWise>
{
  Image<eRead> position;
  Image<eWrite> dst;


  param:
    float4 movement;
    bool worldSpaceMove;
    bool loopBbox;
    int frame;
    float bbox[6];
    float4x4 bboxT;


  local:
    float4 dir_movement;
    float3 bboxSize;
    float4x4 bboxT_Inv;


  float4 multVectMatrix( float4 vec, float4x4 M )
  {
    // Rotation only
    float4 out = float4(
      vec.x * M[0][0] + vec.y * M[0][1] + vec.z * M[0][2],// + M[0][3],
      vec.x * M[1][0] + vec.y * M[1][1] + vec.z * M[1][2],// + M[1][3],
      vec.x * M[2][0] + vec.y * M[2][1] + vec.z * M[2][2],// + M[2][3],
      vec.w
    );
    return out;
  }


  void define()
  {
    defineParam( movement, "Movement", float4( 0.0f, 0.0f, 0.0f, 0.0f ) );
    defineParam( loopBbox, "Loop", true );
    defineParam( frame, "Frame", 0 );
    defineParam( size, "ShrinkExtend", float3( 0.0f, 0.0f, 0.0f ) );
  }


  void init()
  {

    // Max Size for each dimension
    for ( int component = 0; component < 3; component++ )
      bboxSize[ component ] = bbox[ component + 3 ] - bbox[ component ];

    // Transform movement direction to world space, multiply by frame
    bboxT_Inv = bboxT.invert();
    if ( worldSpaceMove )
      dir_movement = multVectMatrix( movement, bboxT_Inv ) * frame;
    else
      dir_movement = movement * frame;
  }


  void process()
  {
    // Add movement to current position
    float4 start = position();
    if ( start.w <= 0.0f )
      return;

    float4 current = start;

    float4 new_pos = current + dir_movement;

    if ( loopBbox ) {
      for ( int component = 0; component < 3; component++ ) {
        if ( new_pos[ component ] < bbox[ component ] )
          new_pos[ component ] = fmod( new_pos[ component ] + bbox[ component ], bboxSize[ component ] ) - bbox[ component ];
        if ( bbox[ component + 3 ] < new_pos[ component ] )
          new_pos[ component ] = fmod( new_pos[ component ] - bbox[ component + 3 ], bboxSize[ component ] ) + bbox[ component + 3 ];
      }
    }

    dst() = new_pos;
    return;
  }

};