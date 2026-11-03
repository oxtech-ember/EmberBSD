# ESP-Mosaico audio smoke test

`mosaico-smoke.sh` uses the standard `audioctl`, `mixerctl`, `audiorecord`,
`audioplay`, `timeout`, and `wc` programs against the default audio devices.
The requested capture is one to ten seconds; recording and playback each have
a wall-clock timeout two seconds longer than the requested duration. Pass an
optional duration and output path, for example:

```sh
./mosaico-smoke.sh 3 /tmp/audio-check.wav
```

The script prints the current audio and mixer controls, records mono
48 kHz signed 16-bit PCM as WAV, reports requested PCM bytes and actual WAV
size, and rejects files shorter than the requested PCM payload. It
then plays that WAV back. `AUDIODEVICE`, `AUDIOCTLDEVICE`, and
`MIXERDEVICE` can select different device paths.

A zero exit status only means the control queries and bounded I/O operations
returned successfully and the recording exceeded the size floor. It does not
establish that sound was audible, that the recording contains a useful signal,
or that a particular board connector is wired as expected. Confirm playback
and microphone signal separately, then record those observations with the
board and kernel revision.
The ESP-Mosaico codec/pin path remains subject to physical verification.
