#pragma once
#include "media/playback/EqualizerDsp.h"
#include <memory>
#include <mftransform.h>
namespace yinamp::media::playback {
// One in-place sample outstanding; no independent clock, worker or audio queue.
HRESULT CreateEqualizerTransform(IMFMediaType* preferred, std::shared_ptr<EqualizerParameters> parameters,
                                 IMFTransform** transform) noexcept;
}
