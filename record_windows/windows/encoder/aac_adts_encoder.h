#pragma once

#include <windows.h>
#include <mfidl.h>
#include <mfapi.h>
#include <mferror.h>
#include <mftransform.h>
#include <functional>
#include <vector>
#include <cstdint>

#include "record_config.h"
#include "utils.h"

namespace record_windows {

class AacAdtsEncoder
{
public:
	using PacketCallback = std::function<void(std::vector<uint8_t>)>;

	static HRESULT Create(const RecordConfig& config, PacketCallback onPacket, AacAdtsEncoder** ppOut);
	~AacAdtsEncoder();

	AacAdtsEncoder(const AacAdtsEncoder&) = delete;
	AacAdtsEncoder& operator=(const AacAdtsEncoder&) = delete;

	HRESULT Feed(IMFSample* pSample);

private:
	explicit AacAdtsEncoder(PacketCallback onPacket);
	HRESULT Init(const RecordConfig& config);
	void    Drain();

	static void BuildAdtsHeader(uint8_t* hdr, int sampleRate, int channels, DWORD frameLen);

	IMFTransform*  m_pTransform  = NULL;
	int            m_sampleRate  = 0;
	int            m_numChannels = 0;
	PacketCallback m_onPacket;
};

} // namespace record_windows
