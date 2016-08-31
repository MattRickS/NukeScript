kernel MAIN_V01_01 : ImageComputationKernel<ePixelWise>
{
  Image<eRead, eAccessRandom> prebuffer;
  Image<eRead, eAccessPoint> particles;
  Image<eRead, eAccessPoint> particle_colour;
  Image<eRead, eAccessRandom, eEdgeClamped> filterImage;
  Image<eRead, eAccessRandom> depth;
  Image<eWrite, eAccessRandom> dst;


  param:
    bool use_filter;
    bool use_pcolour;
    bool use_zclip;
    bool use_depth;
    bool use_psize;
    bool safety;
    bool edge_disable;
    int reduce;
    int safety_limit;
    int width;
    int height;
    float overscan;
    float depth_max;
    float size;
    float haperture;
    float focal;
    float znear;
    float zfar;
    float4x4 camToWorldM;
    float4x4 particleTransform;


  local:
    float4x4 worldToCamM;
    float4x4 perspM;
    int filterWidth;
    int filterHeight;
    float filterAspectWidth;
    float filterAspectHeight;


  // Multiplies a vector 4 by a 4x4 matrix (COLUMN ORDER) (Affine and homogenous)
  float4 multVectMatrix( float4 vec, float4x4 M ) {
    float4 out;
    out[0]  = vec.x * M[0][0] + vec.y * M[0][1] + vec.z * M[0][2] + M[0][3];
    out[1]  = vec.x * M[1][0] + vec.y * M[1][1] + vec.z * M[1][2] + M[1][3];
    out[2]  = vec.x * M[2][0] + vec.y * M[2][1] + vec.z * M[2][2] + M[2][3];
    float w = vec.x * M[3][0] + vec.y * M[3][1] + vec.z * M[3][2] + M[3][3];
 
    if (w != 1.0f) { 
        out.x /= w; 
        out.y /= w; 
        out.z /= w; 
    } 

    return out;
  }


  void define() {
    defineParam( use_filter,        "Use Filter Image",       false );
    defineParam( use_pcolour,       "Use Particle Colour",    false );
    defineParam( use_zclip,         "Use Depth Clipping",     true );
    defineParam( use_depth,         "Use Depth Mask",         false );
    defineParam( use_psize,         "Use Particle Size",      false );
    defineParam( safety,            "Safety",                 true );
    defineParam( edge_disable,      "Edge Disable",           false );
    defineParam( reduce,            "Reduction",              1 );
    defineParam( safety_limit,      "Safety Limit",           150 );
    defineParam( width,             "Width",                  1440 );
    defineParam( height,            "Height",                 810 );
    defineParam( overscan,          "Overscan",               0.0f );
    defineParam( depth_max,         "Depth Range",            1000.0f );
    defineParam( size,              "Particle Size",          5.0f );
    defineParam( haperture,         "Horizontal Aperture",    24.576f );
    defineParam( focal,             "Focal Length",           50.0f );
    defineParam( znear,             "Near Clipping",          0.1f );
    defineParam( zfar,              "Far Clipping",           10000.0f );
    defineParam( camToWorldM,       "Camera Matrix",          float4x4(
             1.0f,0.0f,0.0f,0.0f,
             0.0f,1.0f,0.0f,0.0f,
             0.0f,0.0f,1.0f,0.0f,
             0.0f,0.0f,0.0f,1.0f
             ));
    defineParam( particleTransform, "Particle Matrix",        float4x4(
             1.0f,0.0f,0.0f,0.0f,
             0.0f,1.0f,0.0f,0.0f,
             0.0f,0.0f,1.0f,0.0f,
             0.0f,0.0f,0.0f,1.0f
             ));
  }


  void init() {

    // Matrix from world space to camera local space
    worldToCamM = camToWorldM.invert();

    // Filter size
    filterWidth  = filterImage.bounds.width();
    filterHeight = filterImage.bounds.height();
    filterAspectWidth  = use_filter ? min( filterWidth / float( filterHeight ), 1.0f ) : 1.0f;
    filterAspectHeight = use_filter ? min( filterHeight / float( filterWidth ), 1.0f ) : 1.0f;

    // Output image aspect
    float aspect = width / float( height );

    // Corner co-ordinates of the viewing frustrum
    float right = ( 0.5f * haperture / focal) * znear;
    float left = -right;
    float top = right / aspect;
    float bottom = -top;

    // Set the Perspective Matrix ( Fits camera space to screen space)
    perspM[0][0] = ( 2 * znear ) / ( right - left );
    perspM[0][2] = ( right + left ) / ( right - left );
    perspM[1][1] = ( 2 * znear ) / ( top - bottom );
    perspM[1][2] = ( top + bottom ) / ( top - bottom );
    perspM[2][2] = - ( ( zfar + znear ) / ( zfar - znear ) );
    perspM[2][3] = - ( ( 2 * zfar * znear ) / ( zfar - znear ) );
    perspM[3][2] = -1;

  }


  void process( int2 pos ) {

    // --- Convert to screen space, eliminating out of range points ---

    // Ignore pixels that are not active ( As precalculated by ZBuffer )
    float4 pre_buffer = prebuffer( pos.x, pos.y );
    if ( pre_buffer.x != 1.0f )
      return;

    float4 particle = particles();

    // Transform the particle to desired location, then to camera local space
    float4 particleSpace = multVectMatrix( particle, particleTransform );
    float4 point_local = multVectMatrix( particleSpace, worldToCamM );

    // Transform position to screen space
    float4 screen_center = multVectMatrix( point_local, perspM );

    // --- Target Position and Depth ---

    // Fit screen space to NDC space ( 0 to 1 range ), multiply to get centerpoint pixel
    float ct_x = ( screen_center.x + 1 ) * 0.5f * width + overscan;
    float ct_y = ( screen_center.y + 1 ) * 0.5f * height + overscan;
    // Normalise desired depth range ( 1 @ cam, 0 @ depth_max )
    float zdepth = 1.0f + point_local.z / depth_max;

    // --- Default colour ---

    // Set default output colour
    float4 out_colour = zdepth;
    out_colour[3] = 1.0f;
    if ( use_pcolour ) {
      float4 pcol = particle_colour();
      out_colour *= pcol;
      out_colour[3] = pcol.w;
    }

    // --- Pixel bounds on screen ---

    // Add size to create a quad centered on the particle
    // Can use just bottom left and top right for a non distorted plane
    float psize = use_psize ? size * particle.w : size;
    float4 botleft  = point_local - float4( psize * filterAspectWidth, psize * filterAspectHeight, 0.0f, 0.0f );
    float4 topright = point_local + float4( psize * filterAspectWidth, psize * filterAspectHeight, 0.0f, 0.0f );

    // Transform position to screen space
    float4 screen_bl = multVectMatrix( botleft, perspM );
    float4 screen_tr = multVectMatrix( topright, perspM );

    // Fit screen space to NDC space ( 0 to 1 range ), multiply to get cornerpoints of particle
    float bl_x = ( screen_bl.x + 1 ) * 0.5f * width + overscan;
    float bl_y = ( screen_bl.y + 1 ) * 0.5f * height + overscan;
    float tr_x = ( screen_tr.x + 1 ) * 0.5f * width + overscan;
    float tr_y = ( screen_tr.y + 1 ) * 0.5f * height + overscan;


    // --- Iteration over affected pixels, set output ---

    // Range of pixels to be set, starting from bottom left
    int2 start = int2( floor( bl_x ), floor( bl_y ) );
    int2 range = int2( floor( tr_x ), floor( tr_y ) ) - start;

    // Limit maximum size to safety limit : prevents timeout crashes
    bool edging = false;
    if ( safety ) {
      if ( range.x > safety_limit || range.y > safety_limit ) {
        start += int2( max( 0, ( range.x - safety_limit ) / 2 ), max( 0, ( range.y - safety_limit ) / 2 ) );
        range = int2( safety_limit, safety_limit );
        edging = !edge_disable;
      }
    }


    for ( int x = 0; x <= range.x; x++ ) {
      for ( int y = 0; y <= range.y; y++ ) {

        // Current output pixel
        int2 out = int2( start.x + x, start.y + y );

        if ( dst.bounds.inside( out) ) {

          // Sets a red border for any particle above the size limit
          if ( edging && ( x == 0 || y == 0 || x == range.x || y == range.y ) ) {
            dst( out.x, out.y ) = float4( 1.0f, 0.0f, 0.0f, 0.0f );
            continue;
          }

          // Percentage area covered
          float distanceFromLeft  = min( out.x + 1 - bl_x, 1.0f );
          float distanceFromBot   = min( out.y + 1 - bl_y, 1.0f );
          float distanceFromRight = min( tr_x - out.x, 1.0f );
          float distanceFromTop   = min( tr_y - out.y, 1.0f );

          float4 result = out_colour * ( distanceFromBot * distanceFromLeft * distanceFromRight * distanceFromTop );
          

          // --- Filter Image Values ---

          if ( use_filter ) {
            // Fit the new size to the filter image, exit if 0 alpha
            float filterX = ( x / float( range.x ) ) * filterWidth;
            float filterY = ( y / float( range.y ) ) * filterHeight;
            float4 filter_value = bilinear( filterImage, filterX, filterY );
            if ( filter_value.w <= 0.0f )
              continue;
            for ( int component = 0; component < 4; component++ )
              result[ component ] *= filter_value[ component ];
          }


          // Prevents NaN pixels
          if ( result.w != result.w )
            continue;
          for ( int component = 0; component < 3; component++ ) {
            if ( result[ component ] != result[ component ] )
              result[ component ] == 0.0f;
          }
          result[3] = min( result.w, 1.0f );

          // --- Ensure foremost pixel gets full colour ---
          
          // Fit top value over pixel
          float front_depth = prebuffer( out.x, out.y, 3 ); // Particle closest to cam's depth
          float4 existing = dst( out.x, out.y ); // Already written rgba values
          float remaining_alpha = 1.0f - existing.w;
          if ( zdepth == front_depth ) {

            // If there's enough space for the current value, add it in
            if ( remaining_alpha >= result.w ) {
                dst( out.x, out.y ) += result;
            }
            // Else squash the existing values and add the current value
            else {
              existing *= ( 1.0f - result.w ) / existing.w;
              dst( out.x, out.y ) = result + existing;
            }
            continue;
          }


          // --- Combine alphas into single pixel --- 

          // Exit if target alpha is full
          if ( remaining_alpha <= 0.0f )
            continue;

          // Cap alpha per pixel at 1
          if ( result.w > remaining_alpha ) {
            float partial = remaining_alpha / result.w;
            result *= partial;
            result[3] = remaining_alpha;
          }

          // Add result
          dst( out.x, out.y ) += result;
        }
      }
    }
  
  }

};