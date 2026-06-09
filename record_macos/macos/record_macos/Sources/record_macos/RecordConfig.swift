import AVFoundation

public enum AudioEncoder: String {
  case aacLc = "aacLc"
  case aacEld = "aacEld"
  case aacHe = "aacHe"
  case amrNb = "amrNb"
  case amrWb = "amrWb"
  case opus = "opus"
  case flac = "flac"
  case pcm16bits = "pcm16bits"
  case wav = "wav"
}

public class RecordConfig {
  let encoder: String
  var bitRate: Int
  var sampleRate: Int
  var numChannels: Int
  let device: Device?
  let autoGain: Bool
  let echoCancel: Bool
  let noiseSuppress: Bool
  let streamBufferSize: Int?

  private var m_args: [String: Any] = [:]

  var isModified: Bool {
    bitRate    != (m_args["bitRate"]      as? Int ?? 128000) ||
    sampleRate != (m_args["sampleRate"]   as? Int ?? 44100)  ||
    numChannels != (m_args["numChannels"] as? Int ?? 2)
  }

  init(encoder: String,
       bitRate: Int,
       sampleRate: Int,
       numChannels: Int,
       device: Device? = nil,
       autoGain: Bool = false,
       echoCancel: Bool = false,
       noiseSuppress: Bool = false,
       streamBufferSize: Int?
  ) {
    self.encoder = encoder
    self.bitRate = bitRate
    self.sampleRate = sampleRate
    self.numChannels = numChannels
    self.device = device
    self.autoGain = autoGain
    self.echoCancel = echoCancel
    self.noiseSuppress = noiseSuppress
    self.streamBufferSize = streamBufferSize
  }
}

public class Device {
  let id: String
  let label: String

  init(id: String, label: String) {
    self.id = id
    self.label = label
  }

  init(map: [String: Any]) {
    self.id = map["id"] as! String
    self.label = map["label"] as! String
  }

  func toMap() -> [String: Any] {
    return [
      "id": id,
      "label": label
    ]
  }
}

extension RecordConfig {
  func toMap() -> [String: Any] {
    var map = m_args
    map["bitRate"] = bitRate
    map["sampleRate"] = sampleRate
    map["numChannels"] = numChannels
    return map
  }

  static func fromMap(_ args: [String: Any]) throws -> RecordConfig {
    guard let encoder = args["encoder"] as? String else {
      throw RecorderError.error(message: "Call missing mandatory parameter encoder.", details: nil)
    }
    let device = (args["device"] as? [String: Any]).map(Device.init(map:))
    let config = RecordConfig(
      encoder: encoder,
      bitRate: args["bitRate"] as? Int ?? 128000,
      sampleRate: args["sampleRate"] as? Int ?? 44100,
      numChannels: args["numChannels"] as? Int ?? 2,
      device: device,
      autoGain: args["autoGain"] as? Bool ?? false,
      echoCancel: args["echoCancel"] as? Bool ?? false,
      noiseSuppress: args["noiseSuppress"] as? Bool ?? false,
      streamBufferSize: args["streamBufferSize"] as? Int
    )
    config.m_args = args
    return config
  }
}
