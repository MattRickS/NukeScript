kernel IDToColour : ImageComputationKernel<ePixelWise>
{
  Image<eRead> src;
  Image<eRead, eAccessRandom> col;
  Image<eWrite> dst;

  param:
    bool use_pcol;

  void process() {
    int id = int( src(0) ) - 1;
    if ( id < 0 )
      return;
    if ( !use_pcol ) {
      dst() = 1.0f;
      return;
    }
    int x = id % col.bounds.width();
    int y = id / col.bounds.width();
    dst() = col( x, y );
  }
};