kernel SinglePixel_V01_01 : ImageComputationKernel<ePixelWise>
{
  Image<eRead> format;
  Image<eRead, eAccessPoint> particles;
  Image<eRead, eAccessPoint> particle_colour;
  Image<eRead, eAccessPoint> velocity;
  Image<eRead, eAccessPoint> velocityNext;
  Image<eRead, eAccessRandom> depth;
  Image<eWrite, eAccessRandom> dst;


  param:
    bool use_zclip;
    bool use_depth;
    bool add_velocity;
    int reduce;
    int width;
    int height;
    float overscan;
    float depth_max;
    float haperture;
    float focal;
    float znear;
    float zfar;
    float4x4 camToWorldM;
    float4x4 particleTransform;


  local:
    float4x4 worldToCamM;
    float4x4 perspM;


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
    defineParam( use_zclip,         "Use Depth Clipping",     true );
    defineParam( use_depth,         "Use Depth Mask",         false );
    defineParam( add_velocity,      "Add Velocity",           true );
    defineParam( reduce,            "Reduction",              1 );
    defineParam( width,             "Width",                  1440 );
    defineParam( height,            "Height",                 810 );
    defineParam( overscan,          "Overscan",               0.0f );
    defineParam( depth_max,         "Depth Range",            1000.0f );
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

    // Reduction and out of bounds checks
    int id = ( pos.y * particles.bounds.width() + pos.x );
    if ( !particles.bounds.inside( pos ) || id % reduce != 0.0f )
      return;

    float4 particle = particles();

    // If particle has size 0 / doesn't exist
    if ( particle.w == 0.0f )
      return;

    // Transform the particle to desired location, then to camera local space
    float4 particleSpace = multVectMatrix( particle, particleTransform );
    float4 point_local = multVectMatrix( particleSpace, worldToCamM );

    // Check if position is in front of camera
    if ( point_local.z > 0.0f )
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

    // --- Velocity ---

    float2 out_vel;
    if ( add_velocity ) {
      // Calculate position from previous frame, project, and trace screen space vector motion
      // Particles previous and next position
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


    // Only set foremost pixel
    if ( dst( ct_x, ct_y, 3 ) > zdepth )
      return;

    dst( ct_x, ct_y, 0 ) = float( id + 1 );
    dst( ct_x, ct_y, 1 ) = out_vel.x;
    dst( ct_x, ct_y, 2 ) = out_vel.y;
    dst( ct_x, ct_y, 3 ) = zdepth;
  
  }

};