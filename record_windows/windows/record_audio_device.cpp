#include "record_audio_device.h"
#include "record_config.h"
#include "utils.h"

#include <mfapi.h>
#include <mfidl.h>

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

} // namespace AudioDevice
} // namespace record_windows
