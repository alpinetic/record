import AVFoundation
import Foundation

class RecorderFileDelegate: NSObject, AudioRecordingFileDelegate, AVAudioRecorderDelegate {
  var config: RecordConfig?

  private var m_audioRecorder: AVAudioRecorder?
  private var m_path: String?
  private var m_stoppingIntentionally = false
  private var m_interruptionObserver: NSObjectProtocol?
  private var m_onRecord: () -> ()
  private var m_onPause: () -> ()
  private var m_onStop: () -> ()
  private let m_manageAudioSession: Bool

  init(manageAudioSession: Bool, onRecord: @escaping () -> (), onPause: @escaping () -> (), onStop: @escaping () -> ()) {
    m_manageAudioSession = manageAudioSession
    m_onRecord = onRecord
    m_onPause = onPause
    m_onStop = onStop
  }

  func start(config: RecordConfig, path: String) throws {
    try deleteFile(path: path)

    m_interruptionObserver = try initAVAudioSession(config: config, manageAudioSession: m_manageAudioSession)

    let url = URL(fileURLWithPath: path)

    let recorder = try AVAudioRecorder(url: url, settings: getOutputSettings(config: config))

    recorder.delegate = self
    recorder.isMeteringEnabled = true
    recorder.prepareToRecord()

    recorder.record()

    m_audioRecorder = recorder
    m_path = path
    self.config = config

    m_onRecord()
  }

  func stop(completionHandler: @escaping (String?) -> ()) {
    if let observer = m_interruptionObserver {
      NotificationCenter.default.removeObserver(observer)
      m_interruptionObserver = nil
    }

    m_stoppingIntentionally = true
    m_audioRecorder?.stop()
    m_audioRecorder = nil

    completionHandler(m_path)
    m_onStop()

    m_path = nil
    config = nil
  }

  func pause() {
    guard let recorder = m_audioRecorder, recorder.isRecording else {
      return
    }

    recorder.pause()
    m_onPause()
  }

  func resume() {
    m_audioRecorder?.record()
    m_onRecord()
  }

  func cancel() throws {
    guard let path = m_path else { return }

    stop { path in }

    try deleteFile(path: path)
  }

  func getAmplitude() -> Float {
    m_audioRecorder?.updateMeters()
    return m_audioRecorder?.averagePower(forChannel: 0) ?? -160
  }
  
  func dispose() {
    stop { path in }
  }

  func audioRecorderDidFinishRecording(_ recorder: AVAudioRecorder, successfully flag: Bool) {
    if m_stoppingIntentionally {
      m_stoppingIntentionally = false
      return
    }
    // System-terminated recording (disk full, audio route loss, etc.) — clean up state.
    stop { _ in }
  }
  
  private func deleteFile(path: String) throws {
    do {
      let fileManager = FileManager.default
      
      if fileManager.fileExists(atPath: path) {
        try fileManager.removeItem(atPath: path)
      }
    } catch {
      throw RecorderError.error(message: "Failed to delete previous recording", details: error.localizedDescription)
    }
  }
}
