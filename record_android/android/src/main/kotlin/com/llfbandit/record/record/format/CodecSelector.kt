package com.llfbandit.record.record.format

import android.media.MediaCodecList
import android.media.MediaFormat
import com.llfbandit.record.record.RecordConfig

internal object CodecSelector {
  fun findCodec(format: Format, config: RecordConfig, mediaFormat: MediaFormat): String? {
    val codecs = MediaCodecList(MediaCodecList.REGULAR_CODECS)

    for (info in codecs.codecInfos) {
      if (!info.isEncoder) continue

      try {
        val caps = info.getCapabilitiesForType(format.mimeTypeAudio)
        if (caps != null && format.adjustToCapabilities(caps, config, mediaFormat)) {
          return info.name
        }
      } catch (_: IllegalArgumentException) {
        // type not supported by this codec
      }
    }

    return null
  }
}
