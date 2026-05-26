import 'dart:async';

import 'package:flutter/foundation.dart';
import 'package:flutter/material.dart';
import 'package:record/record.dart';

import 'platform/audio_recorder_platform.dart';

class Recorder extends StatefulWidget {
  final void Function(String path) onStop;

  const Recorder({super.key, required this.onStop});

  @override
  State<Recorder> createState() => _RecorderState();
}

class _RecorderState extends State<Recorder> with AudioRecorderMixin {
  int _recordDuration = 0;
  Timer? _timer;
  late final AudioRecorder _audioRecorder;
  StreamSubscription<RecordState>? _recordSub;
  RecordState _recordState = RecordState.stop;
  StreamSubscription<Amplitude>? _amplitudeSub;
  Amplitude? _amplitude;

  @override
  void initState() {
    _audioRecorder = AudioRecorder();

    // Subscribe to state changes
    _recordSub = _audioRecorder.onStateChanged().listen(
      (recordState) => _updateRecordState(recordState),
    );

    // Subscribe to amplitude changes
    _amplitudeSub = _audioRecorder
        .onAmplitudeChanged(const Duration(milliseconds: 300))
        .listen((amp) => setState(() => _amplitude = amp));

    super.initState();
  }

  Future<void> _start() async {
    try {
      if (await _audioRecorder.hasPermission()) {
        final devs = await _audioRecorder.listInputDevices();
        debugPrint(devs.toString());

        const encoder = AudioEncoder.aacLc;

        if (!await _isEncoderSupported(encoder)) {
          return;
        }

        const config = RecordConfig(encoder: encoder, numChannels: 1);

        // Record to file
        await recordFile(_audioRecorder, config);

        // or record to stream
        // await recordStream(_audioRecorder, config);
      }
    } catch (e) {
      if (kDebugMode) {
        print(e);
      }
    }
  }

  Future<void> _stop() async {
    final path = await _audioRecorder.stop();

    if (path != null) {
      widget.onStop(path);

      downloadWebData(path);
    }
  }

  Future<void> _pause() => _audioRecorder.pause();

  Future<void> _resume() => _audioRecorder.resume();

  void _updateRecordState(RecordState recordState) {
    setState(() => _recordState = recordState);

    switch (recordState) {
      case RecordState.pause:
        _timer?.cancel();
      case RecordState.record:
        _startTimer();
      case RecordState.stop:
        _timer?.cancel();
        _recordDuration = 0;
    }
  }

  Future<bool> _isEncoderSupported(AudioEncoder encoder) async {
    final isSupported = await _audioRecorder.isEncoderSupported(encoder);

    if (!isSupported) {
      debugPrint('${encoder.name} is not supported on this platform.');
      debugPrint('Supported encoders are:');

      for (final e in AudioEncoder.values) {
        if (await _audioRecorder.isEncoderSupported(e)) {
          debugPrint('- ${e.name}');
        }
      }
    }

    return isSupported;
  }

  @override
  Widget build(BuildContext context) {
    return Column(
      mainAxisAlignment: MainAxisAlignment.center,
      spacing: 40,
      children: [
        Row(
          mainAxisAlignment: MainAxisAlignment.center,
          spacing: 20,
          children: <Widget>[
            _RecordStopControl(_recordState, onStart: _start, onStop: _stop),
            _PauseResumeControl(
              _recordState,
              onPause: _pause,
              onResume: _resume,
            ),
            _Timer(_recordState, _recordDuration),
          ],
        ),
        if (_amplitude != null) ...[
          Column(
            children: [
              Text('Current: ${_amplitude?.current ?? 0.0}'),
              Text('Max: ${_amplitude?.max ?? 0.0}'),
            ],
          ),
        ],
      ],
    );
  }

  @override
  void dispose() {
    _timer?.cancel();
    _recordSub?.cancel();
    _amplitudeSub?.cancel();
    _audioRecorder.dispose();
    super.dispose();
  }

  void _startTimer() {
    _timer?.cancel();

    _timer = Timer.periodic(const Duration(seconds: 1), (Timer t) {
      setState(() => _recordDuration++);
    });
  }
}

class _Timer extends StatelessWidget {
  final RecordState _recordState;
  final int _recordDuration;

  const _Timer(this._recordState, this._recordDuration);

  @override
  Widget build(BuildContext context) {
    if (_recordState != RecordState.stop) {
      return _buildTimer();
    }

    return const Text("Waiting for recording");
  }

  Widget _buildTimer() {
    String formatNumber(int number) {
      return '$number'.padLeft(2, '0');
    }

    final String minutes = formatNumber(_recordDuration ~/ 60);
    final String seconds = formatNumber(_recordDuration % 60);

    return Text(
      '$minutes : $seconds',
      style: const TextStyle(color: Colors.red),
    );
  }
}

class _RecordStopControl extends StatelessWidget {
  final RecordState _recordState;
  final VoidCallback onStop;
  final VoidCallback onStart;

  const _RecordStopControl(
    this._recordState, {
    required this.onStart,
    required this.onStop,
  });

  @override
  Widget build(BuildContext context) {
    late Icon icon;
    late Color color;

    if (_recordState != RecordState.stop) {
      icon = const Icon(Icons.stop, color: Colors.red, size: 30);
      color = Colors.red.withValues(alpha: 0.1);
    } else {
      final theme = Theme.of(context);
      icon = Icon(Icons.mic, color: theme.primaryColor, size: 30);
      color = theme.primaryColor.withValues(alpha: 0.1);
    }

    return ClipOval(
      child: Material(
        color: color,
        child: InkWell(
          child: SizedBox(width: 56, height: 56, child: icon),
          onTap: () {
            (_recordState != RecordState.stop) ? onStop() : onStart();
          },
        ),
      ),
    );
  }
}

class _PauseResumeControl extends StatelessWidget {
  final RecordState _recordState;
  final VoidCallback onResume;
  final VoidCallback onPause;

  const _PauseResumeControl(
    this._recordState, {
    required this.onPause,
    required this.onResume,
  });

  @override
  Widget build(BuildContext context) {
    if (_recordState == RecordState.stop) {
      return const SizedBox.shrink();
    }

    late Icon icon;
    late Color color;

    if (_recordState == RecordState.record) {
      icon = const Icon(Icons.pause, color: Colors.red, size: 30);
      color = Colors.red.withValues(alpha: 0.1);
    } else {
      final theme = Theme.of(context);
      icon = const Icon(Icons.play_arrow, color: Colors.red, size: 30);
      color = theme.primaryColor.withValues(alpha: 0.1);
    }

    return ClipOval(
      child: Material(
        color: color,
        child: InkWell(
          child: SizedBox(width: 56, height: 56, child: icon),
          onTap: () {
            (_recordState == RecordState.pause) ? onResume() : onPause();
          },
        ),
      ),
    );
  }
}
