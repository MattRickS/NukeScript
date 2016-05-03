kernel BlinkBeam : ImageComputationKernel<ePixelWise>
{
  Image<eRead, eAccessPoint, eEdgeNone> src;  //the input image
  Image<eWrite> dst;  //the output image


  param:
    float2 start;
    float2 end;
    float start_width;
    float end_width;
    float start_time;
    float len;
    float softness;
    bool persp;


  local:
    float2 direction;
    float2 anim_end;
    float2 anim_start;
    float full_length;
    float slope;
    float y_intercept;
    float slope_perp;
    float min_x;
    float max_x;
    float min_width;
    float max_width;
    float _time;
    bool horizontal;
    bool vertical;


  void define() {
    defineParam( start, "Start", float2( 0.0f, 0.0f ) );
    defineParam( end, "End", float2( 100.0f, 100.0f ) );
    defineParam( start_width, "Start Width", 5.0f );
    defineParam( end_width, "End Width", 40.0f );
    defineParam( start_time, "Time", 0.5f );
    defineParam( len, "Length", 0.25f );
    defineParam( softness, "Softness", 0.5f );
    defineParam( persp, "3D Perspective", true );
  }


  void init() {

    // Basic initialisation
    direction = end - start;
    full_length = length( start - end );
    min_width = min( start_width, end_width );
    max_width = max( start_width, end_width );

    if ( direction.y == 0 )
      horizontal = true;
    else if ( direction.x == 0 )
      vertical = true;
    else {
      vertical = false;
      horizontal = false;
      slope = direction.y / direction.x;
      y_intercept = end.y - slope * end.x;
      slope_perp = -( 1 / slope );
    }


    // ---------- Beam location ----------

    // Fit time to allow for length to disappear from both ends
    float current_length = ( full_length * len ) / full_length;
    float range = clamp( start_time, 0.0f, 1.0f) * ( 1 + current_length );
    float start_dist = clamp( range - current_length, 0.0f, 1.0f );
    float end_dist = clamp( range, 0.0f, 1.0f );

    if ( persp ) {
      start_dist = linear_to_perspective( start_dist );
      end_dist = linear_to_perspective( end_dist );
    }

    anim_start = start + direction * start_dist;
    anim_end = start + direction * end_dist;

  }


  float linear_to_perspective( float linear ) {
    // Perspective Deformation
    int depth = -1;
    float last_low = 0.0f;
    float last_high = 1.0f;
    float tolerance = 0.00001f;
    float distance_to_high = fabs( last_high - linear );
    float distance_to_low = fabs( last_low - linear );

    // Find the amount of times to halve 1 to get to time
    while ( distance_to_high > tolerance && distance_to_low > tolerance) {
      depth++;
      float difference = ( last_high - last_low ) / 2.0f;
      if ( distance_to_high < distance_to_low )
        last_low += difference;
      else
        last_high -= difference;
      distance_to_high = fabs( last_high - linear );
      distance_to_low = fabs( last_low - linear );
    }

    // Calculate the multiplier values for the trapezoid midpoint equation
    float options = pow( 2, depth );
    float factor = options * linear;

    float min_mult = factor * 2;
    float max_mult = ( options - factor ) * 2;

    // Calculate perspective using midpoint equation with multipliers
    float perspective;
    if ( start_width < end_width )
      perspective = ( ( min_mult * min_width * full_length ) / ( min_mult * min_width + max_mult * max_width ) / full_length );
    else
      perspective = 1 - ( ( max_mult * min_width * full_length ) / ( max_mult * min_width + min_mult * max_width ) / full_length );

    return perspective;
  }


  void process( int2 pos ) {
    
    // -------- Beam appearance ---------

    float intersect_x;
    float intersect_y;

    if ( vertical ) {
      intersect_x = start.x;
      intersect_y = pos.y;
    }
    else if ( horizontal ) {
      intersect_x = pos.x;
      intersect_y = start.y;
    }
    else {
      // Line 1 Equation : y = slope * x + y_intercept
      // Line 2 Equation : y = slope_perp * (x - pos.x) + pos.y
      // Intersection @ x where y = y :
      //    slope * x + y_intercept = slope_perp * (x - pos.x) + pos.y
      //    slope * x = slope_perp * x - (slope_perp * pos.x) + pos.y - y_intercept
      //    slope * x - slope_perp * x = pos.y - y_intercept - (slope_perp * pos.x)
      //    x * (slope - slope_perp) = pos.y - y_intercept - (slope_perp * pos.x)
      //    x = (pos.y - y_intercept - (slope_perp * pos.x)) / (slope - slope_perp)
      intersect_x = ( pos.y - y_intercept - ( slope_perp * pos.x ) ) / ( slope - slope_perp );

      // Intersection @ y using Line 1 equation
      intersect_y = intersect_x * slope + y_intercept;
    }

    // If intersection is out of bounds, use the closest end point
    float dist = 0.0f;
    if ( intersect_x < min( anim_start.x, anim_end.x ) || max( anim_start.x, anim_end.x ) < intersect_x ||
          intersect_y < min( anim_start.y, anim_end.y ) || max( anim_start.y, anim_end.y ) < intersect_y ) {
      float dist_to_start = length( float2( anim_start.x - pos.x, anim_start.y - pos.y ) );
      float dist_to_end = length( float2( anim_end.x - pos.x, anim_end.y - pos.y ) );
      dist = min( dist_to_start, dist_to_end );
    }
    else {
      float2 perp_vec = float2( pos.x - intersect_x, pos.y - intersect_y );
      dist = length( perp_vec );
    }

    // Percentage along path fit to the width range
    float width_extra;
    if ( vertical )
      width_extra = ( ( intersect_y - start.y ) / direction.y ) * fabs( start_width - end_width );
    else
      width_extra = ( ( intersect_x - start.x ) / direction.x ) * fabs( start_width - end_width );

    float width;
    if ( start_width < end_width)
      width = start_width + width_extra;
    else
      width = start_width - width_extra;


    float result = 0.0f;
    if ( dist <= width ) {
      result = min( ( width - dist ) / ( width * softness ), 1.0f );
    }


    // ----------- Set result -----------

    for ( int component = 0; component <= 3; component++ ) {
      dst( component ) = result;
    }
  }
};