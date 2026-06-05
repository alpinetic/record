import AVFoundation

func listInputs() throws -> [Device] {
  var devices: [Device] = []

  try listInputDevices()?.forEach { input in
    devices.append(Device(id: input.uid, label: input.portName))
  }

  return devices
}

func listInputDevices() throws -> [AVAudioSessionPortDescription]? {
  let audioSession = AVAudioSession.sharedInstance()

  let inputCapableCategories: [AVAudioSession.Category] = [.record, .playAndRecord]
  if !inputCapableCategories.contains(audioSession.category) {
    do {
      try audioSession.setCategory(.playAndRecord, options: [.defaultToSpeaker, .allowBluetooth])
    } catch {
      throw RecorderError.error(message: "Failed to list inputs", details: "setCategory: \(error.localizedDescription)")
    }
  }

  return audioSession.availableInputs
}
