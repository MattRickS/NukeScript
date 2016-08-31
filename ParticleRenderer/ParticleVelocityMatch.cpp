kernel ParticleVelocityMatch : ImageComputationKernel<ePixelWise>
{
  Image<eRead> current;
  Image<eRead, eAccessRandom> next;
  Image<eWrite> dst;

  void process( int2 pos )
  {
    float4 cur = current();
    // Only match pixels within current frame bounds, and with existing ids
    if ( current.bounds.inside( pos ) && cur.w != 0.0f )
    {
      // Buffer pixel value iteration
      int max_x = pos.y * current.bounds.width() + pos.x;
      for ( int x = max_x; x >= 0; x-- )
      {
        float4 nxt = next( x, 0 );
        if ( nxt.w == cur.w )
        {
          dst() = nxt;
          return;
        }
      }
    }

    dst() = 0.0f;
  }
};