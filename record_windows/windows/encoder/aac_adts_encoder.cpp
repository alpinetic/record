#include "encoder/aac_adts_encoder.h"
#include "record_mediatype.h"

#include <cstring>

namespace record_windows {

// static
HRESULT AacAdtsEncoder::Create(const RecordConfig& config, PacketCallback onPacket, AacAdtsEncoder** ppOut)
{
	auto* p = new (std::nothrow) AacAdtsEncoder(std::move(onPacket));
	if (!p) return E_OUTOFMEMORY;

	HRESULT hr = p->Init(config);
	if (SUCCEEDED(hr))
		*ppOut = p;
	else
		delete p;

	return hr;
}

AacAdtsEncoder::AacAdtsEncoder(PacketCallback onPacket)
	: m_onPacket(std::move(onPacket))
{
}

AacAdtsEncoder::~AacAdtsEncoder()
{
	if (m_pTransform)
	{
		m_pTransform->ProcessMessage(MFT_MESSAGE_NOTIFY_END_OF_STREAM, 0);
		SafeRelease(m_pTransform);
	}
}

HRESULT AacAdtsEncoder::Init(const RecordConfig& config)
{
	IMFActivate** ppActivate = NULL;
	UINT32        count      = 0;
	IMFTransform* pTransform = NULL;
	IMFMediaType* pTypeIn    = NULL;

	MFT_REGISTER_TYPE_INFO outputInfo = { MFMediaType_Audio, MFAudioFormat_AAC };
	DWORD dwFlags = (MFT_ENUM_FLAG_ALL & (~MFT_ENUM_FLAG_FIELDOFUSE)) | MFT_ENUM_FLAG_SORTANDFILTER;

	HRESULT hr = MFTEnumEx(MFT_CATEGORY_AUDIO_ENCODER, dwFlags, NULL, &outputInfo, &ppActivate, &count);
	if (SUCCEEDED(hr) && count == 0) hr = MF_E_NOT_FOUND;
	if (SUCCEEDED(hr)) hr = ppActivate[0]->ActivateObject(IID_PPV_ARGS(&pTransform));

	// The encoder only accepts output types it advertises; enumerate and pick best match.
	if (SUCCEEDED(hr))
	{
		const UINT32 reqSr = (UINT32)config.sampleRate;
		const UINT32 reqCh = (UINT32)config.numChannels;
		const UINT32 reqBr = (UINT32)config.bitRate;
		auto absDiff = [](UINT32 a, UINT32 b) -> UINT32 { return a >= b ? a - b : b - a; };

		IMFMediaType* pBest  = NULL;
		UINT32 bestSr = 0, bestCh = 0, bestBr = 0;

		for (DWORD i = 0; ; i++)
		{
			IMFMediaType* pType = NULL;
			if (FAILED(pTransform->GetOutputAvailableType(0, i, &pType))) break;

			UINT32 sr = 0, ch = 0, br = 0;
			pType->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &sr);
			pType->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS,       &ch);
			pType->GetUINT32(MF_MT_AVG_BITRATE,              &br);

			bool better = (pBest == NULL)
				|| absDiff(ch, reqCh) < absDiff(bestCh, reqCh)
				|| (absDiff(ch, reqCh) == absDiff(bestCh, reqCh) && absDiff(sr, reqSr) < absDiff(bestSr, reqSr))
				|| (absDiff(ch, reqCh) == absDiff(bestCh, reqCh) && absDiff(sr, reqSr) == absDiff(bestSr, reqSr) && absDiff(br, reqBr) < absDiff(bestBr, reqBr));

			if (better) { SafeRelease(pBest); pBest = pType; pBest->AddRef(); bestSr = sr; bestCh = ch; bestBr = br; }
			SafeRelease(&pType);
		}

		if (pBest) { hr = pTransform->SetOutputType(0, pBest, 0); SafeRelease(pBest); }
		else        { hr = MF_E_NOT_FOUND; }
	}

	if (SUCCEEDED(hr)) hr = MediaType::CreateInputProfile(config, &pTypeIn);
	if (SUCCEEDED(hr)) hr = pTransform->SetInputType(0, pTypeIn, 0);
	if (SUCCEEDED(hr)) hr = pTransform->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0);

	if (SUCCEEDED(hr))
	{
		m_pTransform  = pTransform;
		m_pTransform->AddRef();
		m_sampleRate  = config.sampleRate;
		m_numChannels = config.numChannels;
	}

	for (UINT32 i = 0; i < count; i++) SafeRelease(ppActivate[i]);
	CoTaskMemFree(ppActivate);
	SafeRelease(&pTypeIn);
	SafeRelease(&pTransform);

	return hr;
}

HRESULT AacAdtsEncoder::Feed(IMFSample* pSample)
{
	HRESULT hr = m_pTransform->ProcessInput(0, pSample, 0);
	if (SUCCEEDED(hr))
		Drain();
	return hr;
}

void AacAdtsEncoder::Drain()
{
	MFT_OUTPUT_STREAM_INFO si = {};
	m_pTransform->GetOutputStreamInfo(0, &si);
	const bool  mftProvidesBuffer = (si.dwFlags & MFT_OUTPUT_STREAM_PROVIDES_SAMPLES) != 0;
	const DWORD bufSize = si.cbSize > 0 ? si.cbSize : 8192;

	while (true)
	{
		IMFSample*      pOut = NULL;
		IMFMediaBuffer* pBuf = NULL;

		if (!mftProvidesBuffer)
		{
			if (FAILED(MFCreateSample(&pOut))) break;
			if (FAILED(MFCreateMemoryBuffer(bufSize, &pBuf)))
			{
				SafeRelease(pOut);
				break;
			}
			pOut->AddBuffer(pBuf);
			SafeRelease(pBuf);
		}

		MFT_OUTPUT_DATA_BUFFER outputBuffer = {};
		outputBuffer.pSample = pOut;
		DWORD status = 0;

		HRESULT hr = m_pTransform->ProcessOutput(0, 1, &outputBuffer, &status);

		if (outputBuffer.pEvents) outputBuffer.pEvents->Release();

		if (hr == MF_E_TRANSFORM_NEED_MORE_INPUT)
		{
			SafeRelease(pOut);
			break;
		}

		if (SUCCEEDED(hr))
		{
			IMFSample* pResult = mftProvidesBuffer ? outputBuffer.pSample : pOut;
			if (pResult)
			{
				IMFMediaBuffer* pContiguous = NULL;
				if (SUCCEEDED(pResult->ConvertToContiguousBuffer(&pContiguous)))
				{
					BYTE* pData  = NULL;
					DWORD cbData = 0;
					if (SUCCEEDED(pContiguous->Lock(&pData, NULL, &cbData)) && cbData > 0)
					{
						std::vector<uint8_t> packet(7 + cbData);
						BuildAdtsHeader(packet.data(), m_sampleRate, m_numChannels, cbData);
						std::memcpy(packet.data() + 7, pData, cbData);
						m_onPacket(std::move(packet));
						pContiguous->Unlock();
					}
					SafeRelease(pContiguous);
				}
			}
		}

		if (mftProvidesBuffer)
			SafeRelease(outputBuffer.pSample);
		else
			SafeRelease(pOut);

		if (FAILED(hr)) break;
	}
}

// static
void AacAdtsEncoder::BuildAdtsHeader(uint8_t* hdr, int sampleRate, int channels, DWORD frameLen)
{
	static const int kFreqTable[] = {
		96000, 88200, 64000, 48000, 44100, 32000,
		24000, 22050, 16000, 12000, 11025, 8000, 7350
	};
	int freqIdx = 4; // 44100 fallback
	for (int i = 0; i < 13; i++)
		if (kFreqTable[i] == sampleRate) { freqIdx = i; break; }

	DWORD totalLen = frameLen + 7;
	hdr[0] = 0xFF;
	hdr[1] = 0xF1; // MPEG-4, layer=0, no CRC
	hdr[2] = (uint8_t)((freqIdx << 2) | (channels >> 2));
	hdr[3] = (uint8_t)(((channels & 3) << 6) | ((totalLen >> 11) & 0x3));
	hdr[4] = (uint8_t)((totalLen >> 3) & 0xFF);
	hdr[5] = (uint8_t)(((totalLen & 7) << 5) | 0x1F);
	hdr[6] = 0xFC;
}

} // namespace record_windows
