kernel ZBuffer_V01_01 : ImageComputationKernel<ePixelWise>
{
  Image<eRead> format;
  Image<eRead, eAccessPoint> particles;
  Image<eRead, eAccessPoint> active;
  Image<eRead, eAccessPoint> particle_colour;
  Image<eRead, eAccessPoint> velocity;
  Image<eRead, eAccessPoint> velocityNext;
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
    bool add_velocity;
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
    defineParam( add_velocity,      "Add Velocity",           true );
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

    // OUTPUT WILL BE :
    // RED (0)           = ACTIVE   : Only particles that are on screen / valid
    // GREEN, BLUE (1,2) = VELOCITY : Motion Vector of the topmost particle
    // ALPHA (3)         = DEPTH    : Depth of the topmost particle

    // --- Convert to screen space, eliminating out of range points ---

    // Ignore pixels that are not active or have 0 alpha
    float4 exists = active();
    if ( exists.x != 1.0f || ( use_pcolour && particle_colour(3) == 0.0f ) )
      return;

    float4 particle = particles();

    // Ignore pixels outside of the input image or limited to the nth position
    float id = ( pos.y * particles.bounds.width() + pos.x );
    if ( !particles.bounds.inside( pos ) || fmod( id, float( reduce ) ) != 0.0f )
      return;

    // Transform the particle to desired location, then to camera local space
    float4 particleSpace = multVectMatrix( particle, particleTransform );
    float4 point_local = multVectMatrix( particleSpace, worldToCamM );

    // Check if position is in front of camera
    if ( point_local.z > 0 )
      return;

    // Transform position to screen space
    float4 screen_center = multVectMatrix( point_local, perspM );

    // Trim points outside of clipping planes
    if ( use_zclip && ( screen_center.z < -1.0f || 1.0f < screen_center.z ) )
      return;


    // --- Target Position and Depth ---

    // Fit screen space to NDC space ( 0 to 1 range ), multiply to get centerpoint pixel
    float ct_x = ( screen_center.x + 1 ) * 0.5f * width + overscan;
    float ct_y = ( screen_center.y + 1 ) * 0.5f * height + overscan;
    if ( !dst.bounds.inside( ct_x, ct_y ) )
      return;
    
    // Normalise desired depth range ( 1 @ cam, 0 @ depth_max )
    float zdepth = 1.0f + point_local.z / depth_max;


    // --- Optional depth masking ---

    // Clip points beyond the depth mask
    if ( use_depth && depth_max != 0.0f ) {
      // Move this to filter size settings? More accurate, slower
      int depth_x = floor( ( screen_center.x + 1 ) * 0.5f * depth.bounds.width() );
      int depth_y = floor( ( screen_center.y + 1 ) * 0.5f * depth.bounds.height() );
      float depth_mask = depth( depth_x, depth_y, 0 ); // Use channel_id as picked by user from a channel dropdown (r=0, g=1 etc...)
      if ( zdepth < depth_mask )
        return;
    }


    // --- This particle may affect the image, and should be re-evaluated when calculating colour ---

    dst( pos.x, pos.y, 0 ) = 1.0f;


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


    // --- Velocity ---

    float2 out_vel = 0.0f;
    if ( add_velocity ) {
      // Calculate position from previous frame, project, and trace screen space vector motion
      float4 vel = velocity();
      float4 prev = particle - vel;
      float4 next = particle + velocityNext();

      // Smooth derivative of the particle at current point
      float4 dir = prev - next;
      // Apply velocity length to smoothed direction
      dir[3] = 0.0f;
      vel[3] = 0.0f;
      dir = normalize(dir) * length(vel);

      // Move new end position to screen space
      particleSpace = multVectMatrix( particle + dir, particleTransform );
      point_local = multVectMatrix( particleSpace, worldToCamM );
      screen_center = multVectMatrix( point_local, perspM );

      // Calculate screen velocity
      float last_x = ( screen_center.x + 1 ) * 0.5f * width + overscan;
      float last_y = ( screen_center.y + 1 ) * 0.5f * height + overscan;
      out_vel = float2( ct_x - last_x, ct_y - last_y );
    }


    // --- Iteration over affected pixels, set output ---

    // Range of pixels to be set, starting from bottom left
    int2 start = int2( floor( bl_x ), floor( bl_y ) );
    int2 range = int2( floor( tr_x ), floor( tr_y ) ) - start;

    // Limit maximum size to safety limit : prevents timeout crashes
    if ( safety && ( range.x > safety_limit || range.y > safety_limit ) ) {
      start += int2( max( 0, ( range.x - safety_limit ) / 2 ), max( 0, ( range.y - safety_limit ) / 2 ) );
      range = int2( min( safety_limit, range.x ), min( safety_limit, range.y ) );
    }

    for ( int x = 0; x <= range.x; x++ ) {
      for ( int y = 0; y <= range.y; y++ ) {

        // Current output pixel
        int2 out = int2( start.x + x, start.y + y );

        if ( dst.bounds.inside( out) ) {

          // Exit if existing pixel is closer than current pixel
          float existing_depth = dst( out.x, out.y, 3 );
          if ( existing_depth > zdepth )
            continue;

          // --- Filter Image Values ---

          if ( use_filter ) {
            // Fit the new size to the filter image, exit if 0 alpha
            float filterX = ( x / float( range.x ) ) * filterWidth;
            float filterY = ( y / float( range.y ) ) * filterHeight;
            float4 filter_value = bilinear( filterImage, filterX, filterY );
            if ( filter_value.w <= 0.0f )
              continue;
          }

          // Multiple passes
          dst( out.x, out.y, 1 ) = out_vel.x;
          dst( out.x, out.y, 2 ) = out_vel.y;
          dst( out.x, out.y, 3 ) = zdepth;
        }
      }
    }
  
  }

};