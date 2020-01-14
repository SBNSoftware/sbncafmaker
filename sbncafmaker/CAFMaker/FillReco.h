
#ifndef CAF_FILLRECO_H
#define CAF_FILLRECO_H

#include "art/Framework/Services/Registry/ServiceHandle.h"

#include "lardataobj/RecoBase/Slice.h"

#include "sbncafmaker/StandardRecord/SRSlice.h"
#include "sbncafmaker/StandardRecord/StandardRecord.h"

namespace caf
{
  void FillSliceVars(const recob::Slice& slice,
                     caf::SRSlice& srslice,
                     bool allowEmpty = false);

}

#endif
