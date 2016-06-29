// Max number of points hard coded. Must be this number of points declared in param, and added to points[] in init()
# define upper_limit 16

kernel Lines : ImageComputationKernel<ePixelWise>
{
  
  Image<eWrite> dst;


  param:
    int max_pts;
    int max_limit;
    bool close;
    bool round_ends;
    float4 colour;
    float width;
    float softness;
    bool dashed;
    float spacing;
    float offset;
    float anim_time;
    float2 start;

    // # of points = upper_limit
    float2 pt1;
    float2 pt2;
    float2 pt3;
    float2 pt4;
    float2 pt5;
    float2 pt6;
    float2 pt7;
    float2 pt8;
    float2 pt9;
    float2 pt10;
    float2 pt11;
    float2 pt12;
    float2 pt13;
    float2 pt14;
    float2 pt15;


  local:
    float2 points[upper_limit];
    float lines[upper_limit][5];
    int pt_limit;
    int line_limit;
    float max_length;
    float animated_end;


  void define() {
    defineParam( max_pts,    "Max PTS",     2 );
    defineParam( max_limit,  "Max Limit",   upper_limit );
    defineParam( close,      "Close end",   false );
    defineParam( round_ends, "Round ends",  false );
    defineParam( colour,     "Colour",      float4( 1.0f, 1.0f, 1.0f, 1.0f ) );
    defineParam( width,      "Width",       5.0f );
    defineParam( softness,   "Softness",    1.0f );
    defineParam( dashed,     "Dashed",      false );
    defineParam( spacing,    "Spacing",     30.0f );
    defineParam( offset,     "Dash Offset", 0.0f );
    defineParam( anim_time,  "Time",        1.0f );
    defineParam( start,      "Start",       float2( 100.0f, 100.0f ) );
    defineParam( pt1,        "pt1",         float2( 540.0f, 100.0f ) );
    defineParam( pt2,        "pt2",         float2( 540.0f, 380.0f ) );
    defineParam( pt3,        "pt3",         float2( 100.0f, 380.0f ) );
  }


  void init() {

    // # of points = upper_limit
    points[0] = start;
    points[1] = pt1;
    points[2] = pt2;
    points[3] = pt3;
    points[4] = pt4;
    points[5] = pt5;
    points[6] = pt6;
    points[7] = pt7;
    points[8] = pt8;
    points[9] = pt9;
    points[10] = pt10;
    points[11] = pt11;
    points[12] = pt12;
    points[13] = pt13;
    points[14] = pt14;
    points[15] = pt15;

    // Keep used points within upper limit
    pt_limit = min( max_pts, upper_limit );
    line_limit = close ? pt_limit : pt_limit - 1;

    max_length = 0.0f;

    // Calculate line equation components for each line
    // [ slope, y_intercept, slope_perp, horizontal/vertical, cumulative distance ]
    for ( int i = 0; i < line_limit; i++ ) {

      int next = ( i + 1 ) % pt_limit;
      float2 direction = points[next] - points[i];

      if ( direction.y == 0 ) {
        // Horizontal
        lines[i][3] = 1.0f;
      } else if ( direction.x == 0 ) {
        // Vertical
        lines[i][3] = 2.0f;
      } else {
        // Slope
        lines[i][0] = direction.y / direction.x;
        // Y Intercept
        lines[i][1] = points[next].y - lines[i][0] * points[next].x;
        // Slope Perp
        lines[i][2] = -( 1 / lines[i][0] );
        // Not Horizontal or Vertical
        lines[i][3] = 0.0f;
      }

      float line_length = length( points[next] - points[i] );
      max_length += line_length;
      lines[i][4] = max_length;
    }

    
    // --- Animation ---

    animated_end = clamp( anim_time, 0.0f, 1.0f ) * max_length;
  }



  // --- Width and Softness ---
  float distanceToValue( float distance ) {
  	
    float result = 0.0f;

    if ( distance <= width )
      result = 1.0f;
    else if ( distance <= width + softness )
      result = 1 - ( distance - width ) / softness;

    return result;
  }



  void process( int2 pos ) {

    float result = 0.0f;
    float2 intersection;

    // Find the current pixel value from each line
    for ( int i = 0; i < line_limit; i++) {

      // --- Intersection from current point ---

      if ( lines[i][3] == 2.0f ) {
        // Vertical
        intersection = float2( points[i].x, pos.y );
      } else if ( lines[i][3] == 1.0f ) {
        // Horizontal
        intersection = float2( pos.x, points[i].y );
      } else {
        // Intersection @ x using line equations
        intersection[0] = ( pos.y - lines[i][1] - lines[i][2] * pos.x ) / ( lines[i][0] - lines[i][2] );
        // Intersection @ y using Line 1 equation
        intersection[1] = intersection[0] * lines[i][0] + lines[i][1];
      }


      // --- Value for current line ---

      int next = ( i + 1 ) % pt_limit;
      float distance = length( float2( pos.x - intersection.x, pos.y - intersection.y ) );
      float line_result = 0.0f;
      float distance_along_line = i > 0 ? lines[i-1][4] : 0.0f;
      bool edge = false;

      // If in bounds
      if ( intersection.x >= min( points[i].x, points[next].x ) && max( points[i].x, points[next].x ) >= intersection.x &&
           intersection.y >= min( points[i].y, points[next].y ) && max( points[i].y, points[next].y ) >= intersection.y ) {

        line_result = distanceToValue( distance );
        distance_along_line += length( intersection - points[i] );

      } else {

        edge = true;

        // Calculate closest end point
        float dist_to_start = length( float2( points[i].x - pos.x, points[i].y - pos.y ) );
        float dist_to_end = length( float2( points[next].x - pos.x, points[next].y - pos.y ) );

        float closest = min( dist_to_start, dist_to_end );
        distance_along_line = ( closest == dist_to_end ) ? lines[i][4] : distance_along_line;

        // Straight edges
        if ( !close && !round_ends && ( i == 0 || i == pt_limit - 2 ) ) {

          float closest_end;
          if ( dist_to_start < dist_to_end )
            closest_end = length( intersection - points[i] );
          else
            closest_end = length( intersection - points[next] );

          if ( closest_end <= softness )
            line_result = distanceToValue( closest_end + width ) * distanceToValue( distance );
        }
        // Round ends
        else {
          line_result = distanceToValue( closest );
        }
      }

      // Cutoff Animated Length
      if ( distance_along_line > animated_end )
      	line_result = 0.0f;


      // --- Dashed Line ---

      // If dashed
      if ( dashed && line_result > 0.0f ) {

        // Dash animation
        float dash_offset = distance_along_line + spacing * offset;
        // Prevent double line at start if reversed
        float new_distance = dash_offset < 0 ? fabs( dash_offset - spacing ) : dash_offset;

        // On / Off segments
        float segment = new_distance / spacing;
        int gap = ( int )segment % 2;

        // If an odd numbered segment, fade edges into nothing
        if ( gap == 1) {
          float segment_distance = ( segment - floor( segment ) ) * spacing;
          if ( segment_distance < softness && !edge )
            line_result = line_result * ( 1 - segment_distance / softness );
          else if ( spacing - segment_distance < softness && !edge )
            line_result = line_result * ( 1 - ( spacing - segment_distance ) / softness );
          else
            line_result = 0.0f;
        }
      }


      // --- Use max line value ---

      result = max( line_result, result );
    }

    dst() = colour * result;
  }

};