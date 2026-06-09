#include "record_audio_device.h"
#include "record_config.h"
#include "utils.h"

#include <mfapi.h>
#include <mfidl.h>
#include <mftransform.h>
#include <vector>

namespace record_windows {
namespace AudioDevice {

HRESULT ListInputDevices(flutter::EncodableList& devices)
{
	HRESULT hr = MFStartup(MF_VERSION, MFSTARTUP_NOSOCKET);
	if (FAILED(hr)) return hr;

	IMFAttributes* pDeviceAttributes = NULL;
	IMFActivate**  ppDevices = NULL;
	UINT32         deviceCount = 0;

	hr = MFCreateAttributes(&pDeviceAttributes, 1);
	if (SUCCEEDED(hr))
	{
		hr = pDeviceAttributes->SetGUID(
			MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
			MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_AUDCAP_GUID);
	}
	if (SUCCEEDED(hr))
	{
		hr = MFEnumDeviceSources(pDeviceAttributes, &ppDevices, &deviceCount);
	}

	for (UINT32 i = 0; i < deviceCount; i++)
	{
		LPWSTR friendlyName = NULL;
		UINT32 friendlyNameLength = 0;
		LPWSTR id = NULL;
		UINT32 idLength = 0;

		HRESULT deviceHr = ppDevices[i]->GetAllocatedString(
			MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_AUDCAP_ENDPOINT_ID, &id, &idLength);
		if (SUCCEEDED(deviceHr))
		{
			deviceHr = ppDevices[i]->GetAllocatedString(
				MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, &friendlyName, &friendlyNameLength);
		}
		if (SUCCEEDED(deviceHr))
		{
			devices.push_back(flutter::EncodableMap({
				{flutter::EncodableValue("id"),    flutter::EncodableValue(Utf8FromUtf16(id))},
				{flutter::EncodableValue("label"), flutter::EncodableValue(Utf8FromUtf16(friendlyName))}
			}));
		}

		CoTaskMemFree(id);
		CoTaskMemFree(friendlyName);
	}

	for (UINT32 i = 0; i < deviceCount; i++)
	{
		SafeRelease(ppDevices[i]);
	}
	CoTaskMemFree(ppDevices);
	SafeRelease(&pDeviceAttributes);

	MFShutdown();
	return hr;
}

HRESULT IsEncoderSupported(const std::string& encoderName, bool* supported)
{
	MFT_REGISTER_TYPE_INFO typeLookup = {};
	typeLookup.guidMajorType = MFMediaType_Audio;

	if      (encoderName == AudioEncoder::aacLc)   typeLookup.guidSubtype = MFAudioFormat_AAC;
	else if (encoderName == AudioEncoder::amrNb)   typeLookup.guidSubtype = MFAudioFormat_AMR_NB;
	else if (encoderName == AudioEncoder::amrWb)   typeLookup.guidSubtype = MFAudioFormat_AMR_WB;
	else if (encoderName == AudioEncoder::opus)    typeLookup.guidSubtype = MFAudioFormat_Opus;
	else if (encoderName == AudioEncoder::flac)    typeLookup.guidSubtype = MFAudioFormat_FLAC;
	else if (encoderName == AudioEncoder::pcm16bits ||
	         encoderName == AudioEncoder::wav) {
		*supported = true;
		return S_OK;
	}
	else {
		*supported = false;
		return S_OK;
	}

	DWORD dwFlags =
		(MFT_ENUM_FLAG_ALL & (~MFT_ENUM_FLAG_FIELDOFUSE)) |
		MFT_ENUM_FLAG_SORTANDFILTER;

	IMFActivate** ppMFTActivate = NULL;
	UINT32        numMFTActivate = 0;

	HRESULT hr = MFStartup(MF_VERSION, MFSTARTUP_NOSOCKET);
	if (FAILED(hr)) return hr;

	hr = MFTEnumEx(
		MFT_CATEGORY_AUDIO_ENCODER,
		dwFlags,
		NULL,
		&typeLookup,
		&ppMFTActivate,
		&numMFTActivate
	);

	if (SUCCEEDED(hr))
	{
		*supported = numMFTActivate != 0;
	}

	for (UINT32 i = 0; i < numMFTActivate; i++)
	{
		SafeRelease(ppMFTActivate[i]);
	}
	CoTaskMemFree(ppMFTActivate);

	MFShutdown();
	return hr;
}

HRESULT AdjustConfigToCodecCaps(RecordConfig& config)
{
	// AMR has fixed mandatory parameters
	if (config.encoderName == AudioEncoder::amrNb) {
		config.sampleRate  = 8000;
		config.numChannels = 1;
		return S_OK;
	}
	if (config.encoderName == AudioEncoder::amrWb) {
		config.sampleRate  = 16000;
		config.numChannels = 1;
		return S_OK;
	}
	// PCM/WAV have no codec constraints
	if (config.encoderName == AudioEncoder::pcm16bits ||
	    config.encoderName == AudioEncoder::wav) {
		return S_OK;
	}

	GUID subtypeGuid = GUID_NULL;
	if (config.encoderName == AudioEncoder::aacLc ||
	    config.encoderName == AudioEncoder::aacEld ||
	    config.encoderName == AudioEncoder::aacHe) {
		subtypeGuid = MFAudioFormat_AAC;
	} else if (config.encoderName == AudioEncoder::flac) {
		subtypeGuid = MFAudioFormat_FLAC;
	} else {
		return S_OK;
	}

	IMFActivate**  ppMFTActivate  = NULL;
	UINT32         numMFTActivate = 0;

	MFT_REGISTER_TYPE_INFO typeInfo = { MFMediaType_Audio, subtypeGuid };

	DWORD dwFlags = (MFT_ENUM_FLAG_ALL & (~MFT_ENUM_FLAG_FIELDOFUSE)) | MFT_ENUM_FLAG_SORTANDFILTER;

	HRESULT hr = MFTEnumEx(MFT_CATEGORY_AUDIO_ENCODER, dwFlags, NULL, &typeInfo, &ppMFTActivate, &numMFTActivate);

	if (SUCCEEDED(hr) && numMFTActivate > 0)
	{
		IMFTransform* pMFT = NULL;
		if (SUCCEEDED(ppMFTActivate[0]->ActivateObject(IID_PPV_ARGS(&pMFT))))
		{
			struct CapsEntry { UINT32 sampleRate, channels, bitRate; };
			std::vector<CapsEntry> caps;

			for (DWORD i = 0; ; i++)
			{
				IMFMediaType* pType = NULL;
				if (FAILED(pMFT->GetOutputAvailableType(0, i, &pType))) break;

				CapsEntry e = {};
				pType->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &e.sampleRate);
				pType->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS,       &e.channels);
				pType->GetUINT32(MF_MT_AVG_BITRATE,              &e.bitRate);
				SafeRelease(&pType);

				if (e.sampleRate > 0 && e.channels > 0) caps.push_back(e);
			}

			if (!caps.empty())
			{
				const UINT32 reqSr = (UINT32)config.sampleRate;
				const UINT32 reqCh = (UINT32)config.numChannels;
				const UINT32 reqBr = (UINT32)config.bitRate;

				auto absDiff = [](UINT32 a, UINT32 b) -> UINT32 {
					return a >= b ? a - b : b - a;
				};

				// Cascaded selection: channels → sample rate → bitrate
				UINT32 bestCh = caps[0].channels;
				for (const auto& e : caps)
					if (absDiff(e.channels, reqCh) < absDiff(bestCh, reqCh))
						bestCh = e.channels;

				UINT32 bestSr = 0;
				for (const auto& e : caps)
					if (e.channels == bestCh)
						if (bestSr == 0 || absDiff(e.sampleRate, reqSr) < absDiff(bestSr, reqSr))
							bestSr = e.sampleRate;

				UINT32 bestBr = 0;
				for (const auto& e : caps)
					if (e.channels == bestCh && e.sampleRate == bestSr)
						if (bestBr == 0 || absDiff(e.bitRate, reqBr) < absDiff(bestBr, reqBr))
							bestBr = e.bitRate;

				config.numChannels = (int)bestCh;
				config.sampleRate  = (int)bestSr;
				if (bestBr > 0) config.bitRate = (int)bestBr;
			}

			ppMFTActivate[0]->ShutdownObject();
			SafeRelease(&pMFT);
		}
	}

	for (UINT32 i = 0; i < numMFTActivate; i++) SafeRelease(ppMFTActivate[i]);
	CoTaskMemFree(ppMFTActivate);

	return S_OK;
}

} // namespace AudioDevice
} // namespace record_windows
