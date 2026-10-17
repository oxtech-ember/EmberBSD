# EAP audio device label

The `AUDIO_GETDEV` name, version and configuration fields each have 16 bytes.
The audio property and OSS compatibility consumers use them as C strings.
`EnsoniqAudioPCI` preserves the vendor and model tokens and fits with its NUL
terminator. The ioctl layout does not change.

Run the actual initializer and `eap_getdev` callback fixture on a 64-bit host:

```sh
HOST_GCC16=/absolute/native/gcc-16 HOST_CC=cc \
  ruby ember/tests/eap-device-label.rb /absolute/source-root
```

On macOS, set `HOST_GCC16_FLAGS="-isysroot $(xcrun --sdk macosx --show-sdk-path)"`
when the selected GCC needs the SDK explicitly. An optional second argument
saves generated C, compiler commands and logs in a new output directory.

The fixture checks all three bounded terminators, the exact label and the
48-byte ioctl layout. It compiles with host GCC16, warnings as errors and
`-fanalyzer`. A separate native Clang-compatible build runs under ASan/UBSan.
The default temporary directory is removed. This is a host callback contract;
it does not establish physical audio or kernel boot acceptance.
