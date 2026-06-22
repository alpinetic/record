#include "record/record.h"
#include "record_windows_plugin.h"

namespace record_windows
{
	STDMETHODIMP Recorder::OnEvent(DWORD, IMFMediaEvent*) { return S_OK; }
	STDMETHODIMP Recorder::OnFlush(DWORD) { return S_OK; }

	HRESULT Recorder::OnReadSample(
		HRESULT hrStatus,
		DWORD dwStreamIndex,
		DWORD dwStreamFlags,
		LONGLONG llTimestamp,
		IMFSample* pSample
	)
	{
		AutoLock lock(m_critsec);

		if (FAILED(hrStatus))
		{
			auto errorText = std::system_category().message(hrStatus);
			printf("Record: Error when reading sample (0x%X)\n%s\n", hrStatus, errorText.c_str());
			RecordWindowsPlugin::RunOnMainThread([this]() -> void { Stop(); });
			return hrStatus;
		}

		HRESULT hr = S_OK;

		if (pSample)
		{
			hr = ProcessSample(dwStreamIndex, llTimestamp, pSample);
		}

		if (SUCCEEDED(hr) && m_pReader)
		{
			hr = m_pReader->ReadSample(
				(DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, 0, NULL, NULL, NULL, NULL);
		}

		return hr;
	}

	void Recorder::RebaseTimestamp(LONGLONG& llTimestamp)
	{
		if (m_bFirstSample)
		{
			m_llBaseTime = llTimestamp;
			m_bFirstSample = false;
			m_dataWritten = 0;
			if (m_bResuming)
			{
				// Paused before any sample arrived: treat as a fresh start.
				m_bResuming = false;
				UpdateState(RecordState::record);
			}
		}
		else if (m_bResuming)
		{
			m_bResuming = false;
			// Shift base so rebased timestamps continue from where we paused.
			// Without this, (llTimestamp - old_base) would jump backward.
			m_llBaseTime = llTimestamp - (m_llLastTime - m_llBaseTime);
			UpdateState(RecordState::record);
		}
		m_llLastTime = llTimestamp;
		llTimestamp -= m_llBaseTime;
	}

	HRESULT Recorder::ProcessSample(DWORD dwStreamIndex, LONGLONG llTimestamp, IMFSample* pSample)
	{
		RebaseTimestamp(llTimestamp);

		HRESULT hr = pSample->SetSampleTime(llTimestamp);
		if (FAILED(hr)) return hr;

		if (m_pWriter)
		{
			hr = m_pWriter->WriteSample(dwStreamIndex, pSample);
			if (FAILED(hr)) return hr;
		}

		return ProcessBuffer(pSample);
	}

	HRESULT Recorder::ProcessBuffer(IMFSample* pSample)
	{
		if (m_recordEventHandler && !m_pWriter && m_pStreamEncoder)
		{
			EventStreamHandler<>* h = m_recordEventHandler;
			for (auto& packet : m_pStreamEncoder->Feed(pSample))
			{
				RecordWindowsPlugin::RunOnMainThread([h, b = std::move(packet)]() mutable {
					h->Success(std::make_unique<flutter::EncodableValue>(std::move(b)));
				});
			}
		}

		IMFMediaBuffer* pBuffer = NULL;
		HRESULT hr = pSample->ConvertToContiguousBuffer(&pBuffer);
		if (FAILED(hr)) return hr;

		BYTE* pChunk = NULL;
		DWORD size   = 0;
		hr = pBuffer->Lock(&pChunk, NULL, &size);

		if (SUCCEEDED(hr))
		{
			m_dataWritten += size;
			m_amplitude.update(pChunk, size);
			pBuffer->Unlock();
		}

		SafeRelease(pBuffer);
		return hr;
	}
};
