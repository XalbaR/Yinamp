#pragma once
#include "media/playback/AudioSpectrum.h"
#include "media/playback/EqualizerDsp.h"
#include <memory>
#include <mfidl.h>
#include <mftransform.h>
#include <wrl/client.h>
namespace yinamp::media::playback {
// Resolve the optional branch before submission; any failure leaves the caller free to use its original topology.
HRESULT ResolveProcessingTopology(IMFTopology* original, IMFTopologyNode* input, IMFTopologyNode* output,
                                  IMFMediaType* sourceType, std::shared_ptr<AudioSpectrum> analyzer,
                                  std::shared_ptr<EqualizerParameters> equalizer, IMFTopology** resolved,
                                  IMFActivate** activation, IMFTransform** transform);
}
