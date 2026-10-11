# EmberBSD

**Unix for intelligent devices.**

EmberBSD is an independent fork of [NetBSD](https://github.com/NetBSD/src),
based on version 11, for single-board computers and embedded systems.
It adds hardware support for Raspberry Pi 5 and related boards, for
the Allwinner A733 and for the ESP32-S31, a 32-bit RISC-V chip with
16 MB of memory. The OS and its companion projects bring together board
support, local AI, robotics, graphical interfaces, and the system work
needed for GPU acceleration. Capabilities and validation limits are
summarized below; application stacks are installed separately.

## Purpose and getting started

This is the central EmberBSD project. It owns the operating system, device
drivers, board adaptations, boot and firmware integration, system builds
and OS validation. Application dependencies, examples and developer tools
are maintained in the related repositories below.

- For a board or OS build, start with
  [hardware support and build instructions](README.ember.md).
- For an application, start with
  [EmberBSD-Examples](https://github.com/oxtech-ember/EmberBSD-Examples) and its
  documented dependencies in [EmberBSD-Ports](https://github.com/oxtech-ember/EmberBSD-Ports).
- For an AI assistant, choose [user skills](#connect-user-skills) for device use
  or [developer skills](#connect-developer-skills) for development.

## EmberBSD ecosystem

| Repository | Purpose | Current scope |
| --- | --- | --- |
| [EmberBSD](https://github.com/oxtech-ember/EmberBSD) | Central OS project: kernel, drivers, boards, boot, firmware and system validation | Source tree and board-specific evidence; see the support tables |
| [EmberBSD-Ports](https://github.com/oxtech-ember/EmberBSD-Ports) | Third-party build recipes, portability patches and native package profiles | pkgsrc recipes and experimental probes, each with its own validation limits |
| [EmberBSD-Examples](https://github.com/oxtech-ember/EmberBSD-Examples) | Standalone applications and reproducible demonstrations | Local AI, robotics and desktop scenarios with requirements and checks |
| [EmberBSD-Runtime](https://github.com/oxtech-ember/EmberBSD-Runtime) | Application execution, lifecycle, permissions and shared device operations | Design stage; no released runtime implementation |
| [EmberBSD-SDK](https://github.com/oxtech-ember/EmberBSD-SDK) | Application interfaces, package contracts, developer tools and compatibility checks | Design stage; no stable application API or released SDK tools |
| [Ember-Agent-Skills](https://github.com/oxtech-ember/Ember-Agent-Skills) | Instructions for developers and their AI assistants | Application development, board bring-up, shared ports and tested contributions; source analysis and memory checks for own and ported code |
| [EmberBSD-User-Skills](https://github.com/oxtech-ember/EmberBSD-User-Skills) | Instructions for users and their local or remote AI assistants | OS installation onto boards, desktop VMs and USB-cabled phones, applications, device diagnostics, personal Ports and GitHub workflows; package checks and client discovery verified |

Runtime will execute applications on the device; SDK will define their
interfaces and development tools. Examples demonstrate usable scenarios;
Ports owns adaptations to third-party software. Agent-Skills helps developers
use these projects and contribute fixes. User-Skills guides OS installation,
device operation and personal software changes, including source patches that
follow upstream.
A repository's intended purpose is not a claim that all of its planned features
are implemented.

## Connect user skills

[EmberBSD-User-Skills](https://github.com/oxtech-ember/EmberBSD-User-Skills)
provides the `emberbsd-user` package with the `emberbsd-install` and
`emberbsd-user-guide` skills. Use it to install EmberBSD on a board, a desktop
VM or a USB-cabled phone from the host, then configure applications, diagnose
faults, or maintain personal Ports patches and publish them to your selected
GitHub repository. Installation methods stay device-general; per-board
procedures live in `ember/boards`. Personal source customization stays a user
workflow; it does not require a contribution PR.

Follow the [loading instructions](https://github.com/oxtech-ember/EmberBSD-User-Skills#load-the-package)
for your client. Both skill packages can coexist. Version 0.2.3 passed package
consistency, native Codex discovery and plugin/skill icon path checks; these
checks do not establish on-device agent behavior, cable-installer acceptance
or end-to-end personal Ports maintenance. The package
uses available tools and supplies no agent runtime or credentials.

## Connect developer skills

[Ember-Agent-Skills](https://github.com/oxtech-ember/Ember-Agent-Skills#use-in-your-development-environment)
provides portable Agent Skills packaged with Agent Plugins. Use the installation
method supported by your development environment. Codex is one verified host.

With a Codex CLI that supports plugins (commands checked with 0.159.2):

```sh
codex plugin marketplace add oxtech-ember/Ember-Agent-Skills --ref main
codex plugin list --marketplace ember-agent-skills --available --json
codex plugin add emberbsd-development@ember-agent-skills
```

Start a new conversation in your project and ask:

> Use $emberbsd-repository-guide to find the EmberBSD interfaces and examples
> for my application, test the result, and document its requirements.

See the [skills installation and update guide](https://github.com/oxtech-ember/Ember-Agent-Skills#use-in-your-development-environment)
for expected results, updates and other assistant environments. The plugin
provides developer instructions; it does not install software on a board.
Package 0.3.8 also passes native plugin/skill icon path checks. Desktop icon
rendering remains unverified for both packages.
Reusable fixes and ports are contributed through focused PRs after relevant
testing, following the accepting project's rules.

## What this fork adds to NetBSD 11

EmberBSD builds on NetBSD 11 to make useful device workflows reproducible:
local inference, robot telemetry, vision and positioning, and graphical
controls. This tree owns the OS changes; Ports and Examples supply optional
application stacks, adaptations and runnable checks. Small installations
can keep only the components they need. Upstream components retain their
own licenses and authorship.

Status updated **2026-10-10**; linked component documents record their own
validation dates. Application tests on an AArch64 VM do not establish support
on every board in the hardware catalog.

### Local AI and robotics

- **Local text and speech models:** pkgsrc recipes for llama.cpp 0.6.0 and
  whisper.cpp 1.9.4, with separately supplied models. Native AArch64 VM tests
  cover package installation, text generation, a loopback HTTP completion
  service and WAV transcription on CPU. This is a usable starting point for
  local assistants; microphone capture, image understanding and GPU/NPU
  inference still need validation. See the [AI packages](https://github.com/oxtech-ember/EmberBSD-Ports/tree/main/profiles/ai-cpu)
  and [model execution example](https://github.com/oxtech-ember/EmberBSD-Examples/tree/main/ai/local-inference).
- **Answers from local documents:** a C example uses SQLite 3.53.4 FTS5 to
  retrieve evidence and the existing llama.cpp CPU server to select a quotation.
  It checks the source and quoted text before displaying them, and handles
  missing evidence and server failures. Native AArch64 tests include actual
  model execution, SQLite transactions, concurrent readers and process-crash
  recovery. This is extractive retrieval, not arbitrary answer-quality or
  physical power-loss validation. See the [document example](https://github.com/oxtech-ember/EmberBSD-Examples/tree/main/ai/local-knowledge)
  and [SQLite checks](https://github.com/oxtech-ember/EmberBSD-Ports/tree/main/probes/sqlite).
- **Robot telemetry and commands:** Zenoh-Pico 1.10.1 packages and a C device
  controller exchange typed data and commands with ROS 2 Jazzy through a
  C++ bridge. The two-VM test covers acknowledgements, stale-command rejection
  and reconnection. This connects EmberBSD devices to ROS systems; it is
  bridge interoperability, not a native ROS 2 distribution. See the
  [Zenoh/ROS 2 example](https://github.com/oxtech-ember/EmberBSD-Examples/tree/main/robotics/zenoh-ros2).
- **Model APIs and speech processing:** ONNX Runtime 1.30.0 and ncnn 20260526
  provide installed C/C++ CPU inference libraries. RNNoise 0.2 processes audio,
  and Silero VAD 6.2.3 detects speech through the same ONNX Runtime library.
  Seven AArch64 VM checks cover numerical results, recurrent stream state,
  speech/silence boundaries and invalid inputs; ORT worker affinity is checked
  separately. Ports preserves the NetBSD adaptations and
  [build and test instructions](https://github.com/oxtech-ember/EmberBSD-Ports/tree/main/probes/ai-engines).
  These source probes support application development; microphone capture,
  real-world audio quality and GPU/NPU execution remain unverified.
- **Google AI Edge model execution:** LiteRT 2.2.0 provides a shared C/C++
  CPU runtime for `.tflite` applications; LiteRT-LM 0.18.0 adds SentencePiece
  language-model execution. Ports owns the [cross-build profile, adaptations and instructions](https://github.com/oxtech-ember/EmberBSD-Ports/tree/main/probes/litert).
  Installed C/C++ consumers, numerical/error contracts and real TinyLlama-1.1B
  text generation pass on a physical A733 board with NetBSD 11. Explicit
  metadata preparation preserves the model's weights and tokenizer; a
  disk-backed cache keeps the tested workflow within the board's 4 GiB RAM.
  These are source builds, with [recorded model and validation limits](https://github.com/oxtech-ember/EmberBSD-Ports/blob/main/probes/litert/VALIDATION.md);
  packaged delivery, GPU/NPU and multimodal workflows remain unverified.
- **Vision, geometry and positioning:** OpenCV 5.0.0, Eigen 5.0.1 and gpsd
  3.27.5 build and pass installed-consumer tests on AArch64. Checks include
  image processing, features and camera-pose recovery, numerical solvers,
  transforms, and a real gpsd process receiving synthetic GNSS data.
  These are [experimental source builds](https://github.com/oxtech-ember/EmberBSD-Ports/tree/main/probes/robotics-foundations);
  physical cameras/GNSS receivers and accelerated vision remain unverified.
- **Video processing for applications:** FFmpeg 9.0.2, GStreamer 1.28.7 and
  OpenCV 5.0.0 videoio share one tested media installation. AArch64 VM checks
  cover exact decoded frames and timestamps, application-buffer pipelines,
  file capture through both OpenCV backends, seeking, image processing and
  lossless output. NetBSD filesystem support and FFmpeg compatibility fixes
  are preserved in the [media port](https://github.com/oxtech-ember/EmberBSD-Ports/tree/main/probes/media).
  This is a CPU file-processing profile; camera capture, network streaming
  and hardware codecs remain outside its verified scope.
- **Recording experiments and finding visual markers:** MCAP C++ 2.1.3 records
  timestamped messages with indexed replay and LZ4/Zstd compression. AprilTag
  3.4.5 detects markers and estimates their pose. Installed AArch64 VM checks
  verify message content, selection and damaged structures, plus known marker
  geometry after image transformations. Ports owns the [source profiles and checks](https://github.com/oxtech-ember/EmberBSD-Ports/tree/main/probes/robotics-tools);
  camera hardware and real-world pose accuracy are unverified.
- **Signals and inertial measurements:** liquid-dsp 1.8.3, FFTW 3.3.11,
  VOLK 3.3.0 and Fusion 1.3.3 supply filtering, resampling, spectra, vector
  kernels and orientation estimation. Nineteen installed AArch64 VM cases
  verify synthetic signal/IMU results and execute generic/NEON VOLK kernels.
  Ports owns the [source profiles and instructions](https://github.com/oxtech-ember/EmberBSD-Ports/tree/main/probes/dsp),
  the FFTW integration fix and preserved pkgsrc adaptations. Physical SDR/IMU,
  measured SIMD speedups and real-time device operation remain unverified.
- **Software radio applications:** GNU Radio 3.10.12.0 supplies native C++
  flowgraphs using the same FFTW and VOLK. Ports owns the [headless recipe and three installed VM contracts](https://github.com/oxtech-ember/EmberBSD-Ports/tree/main/probes/gnuradio).
  Examples owns a [noisy BPSK channel demonstration](https://github.com/oxtech-ember/EmberBSD-Examples/tree/main/robotics/gnuradio-channel)
  that recovers 2,048 bits without error, restarts reproducibly and measures
  BER 0.516113 when carrier correction is removed. This verifies bounded
  software processing with known carrier and timing parameters; physical SDR,
  real-time deadlines, GUI and Python bindings remain unverified.
- **Offline visual SLAM:** ORB-SLAM3 runs with the common Eigen 5.0.1 and
  OpenCV 5.0.0 through a [headless Ports adaptation](https://github.com/oxtech-ember/EmberBSD-Ports/tree/main/probes/orb-slam3).
  Ports adapts current dependencies and repairs worker shutdown, cancellation
  and missing-pose export. The [standalone RGB-D example](https://github.com/oxtech-ember/EmberBSD-Examples/tree/main/robotics/orb-slam3-rgbd)
  tracks all 573 TUM fr1/desk image pairs in an AArch64 VM, with 1.71–1.76 cm
  translation ATE RMSE across two controlled runs and fixed-scale alignment. This enables
  recorded-data navigation experiments; live cameras, IMU fusion, boards,
  GUI, sustained operation and real-time performance remain unverified.
- **Further navigation and motion sources:** Ports prepares
  [GTSAM](https://github.com/oxtech-ember/EmberBSD-Ports/tree/main/probes/gtsam),
  [PCL](https://github.com/oxtech-ember/EmberBSD-Ports/tree/main/probes/pcl),
  [OpenVINS](https://github.com/oxtech-ember/EmberBSD-Ports/tree/main/probes/openvins)
  and [RTAB-Map](https://github.com/oxtech-ember/EmberBSD-Ports/tree/main/probes/rtabmap)
  with common current dependencies. [Ruckig](https://github.com/oxtech-ember/EmberBSD-Ports/tree/main/probes/ruckig)
  and [OSQP](https://github.com/oxtech-ember/EmberBSD-Ports/tree/main/probes/osqp)
  pass host numerical contracts; [OMPL](https://github.com/oxtech-ember/EmberBSD-Ports/tree/main/probes/ompl)
  has a prepared planning profile. Native acceptance of these additions remains pending.
- **Ethernet SDR development:** the [Ports SDR profile](https://github.com/oxtech-ember/EmberBSD-Ports/tree/main/probes/sdr)
  combines SoapySDR, libiio 1.0.0 with its official compatibility layer,
  AD9361/Pluto support and current libxml2. Synthetic IQ and loopback RX
  contracts pass on macOS. The [PlutoSky handoff](https://github.com/oxtech-ember/EmberBSD-Ports/blob/main/probes/sdr/HARDWARE.md)
  defines the next native and hardware checks; EmberBSD RX, physical RF
  performance and per-call stream deadlines remain unverified.
- **Numerical estimation and behavior logic:** Ceres 2.2.0 uses the common
  Eigen 5.0.1 for nonlinear fitting; BehaviorTree.CPP 4.9.0 coordinates
  asynchronous actions. Nine installed AArch64 VM cases verify numerical
  results, invalid inputs, cancellation, timeouts, restart and binary transition
  logging. Ports owns these [source profiles](https://github.com/oxtech-ember/EmberBSD-Ports/tree/main/probes/robotics-tools).
  Physical calibration, complete robot integration and hard-real-time behavior
  are unverified; optional Groot/ZeroMQ and SQLite logging are excluded.
- **Vehicle and industrial protocol development:** dbcppp 3.2.6 decodes DBC
  signals, iso14229 0.11.0 exchanges UDS messages through user-space ISO-TP,
  and libmodbus 3.2.0 provides TCP and RTU. Installed AArch64 VM consumers
  check exact data, fragmentation, error responses, timeouts and reconnection.
  Ports provides [build instructions and bounded software tests](https://github.com/oxtech-ember/EmberBSD-Ports/tree/main/probes/robotics-tools)
  using in-memory CAN frames, loopback TCP and pseudo-terminals. The DBC
  profile excludes KCD/XML; physical CAN/RS-485, ECUs and PLCs are unverified.
- **CAN FD without a controller:** this OS tree extends raw CAN sockets and
  `canlo` with 64-byte payloads, explicit FD opt-in and `canconfig` mode control.
  Native AArch64 rump checks execute the kernel socket path: 15 existing CAN
  cases and 11 new FD cases pass, including mixed Classical/FD traffic,
  filters, invalid records and interface lifecycle. See the
  [CAN FD guide and reproducible checks](ember/can/README.md).
  This is software-stack validation; physical drivers, data-phase timing,
  ISO-TP over FD and a booted kernel with this extension remain unverified.
- **MQTT for connected devices:** Ports provides a [Mosquitto 2.1.2 source probe](https://github.com/oxtech-ember/EmberBSD-Ports/tree/main/probes/mosquitto)
  built with common GCC 16.2, cJSON 1.7.19 and SQLite 3.53.4. Twelve installed
  AArch64 VM cases verify MQTT 3.1.1/5 QoS 0/1/2, authentication, ACL, TLS and
  retained-state recovery. Package and boot-service integration remain pending;
  this does not establish other smart-home applications or physical devices.

### Graphical interfaces

- **Desktop and touch-oriented applications:** [GNOME/X11](https://github.com/oxtech-ember/EmberBSD-Examples/tree/main/desktop/gnome-utm)
  provides a tested desktop scenario. [Phosh 0.58.0](https://github.com/oxtech-ember/EmberBSD-Ports/tree/main/probes/phosh)
  runs inside it with software-rendered Wayland, application switching,
  GTK applications and the Stevia English/Russian screen keyboard.
  These VM checks provide interface prototypes, not validated phone images.
- **Additional X11 desktops:** [Openbox 3.6.1](https://github.com/oxtech-ember/EmberBSD-Ports/tree/main/probes/openbox)
  and [Enlightenment 0.27.1/EFL 1.28.1](https://github.com/oxtech-ember/EmberBSD-Ports/tree/main/probes/enlightenment)
  pass software X11 window management, two application windows, keyboard
  input through XTEST, text editing/saving and clean session exit.
  The [common launcher and checks](https://github.com/oxtech-ember/EmberBSD-Ports/tree/main/probes/x11-desktops)
  isolate session configuration while preserving the user's HOME.
  [awesomeWM 4.3](https://github.com/oxtech-ember/EmberBSD-Ports/tree/main/probes/awesome)
  also builds and passes the same workflow with system Lua and patched LGI.
  [Xfce 4.20](https://github.com/oxtech-ember/EmberBSD-Ports/tree/main/probes/xfce)
  also passes a [NetBSD 11/AArch64 UTM workflow](https://github.com/oxtech-ember/EmberBSD-Ports/blob/main/probes/xfce/VALIDATION.md),
  including Thunar navigation, Mousepad save/reopen/edit, application launch
  through its panel menu and clean exit. Ports owns the source recipes and
  isolated Xvfb session checks. Physical input, touch and GPU acceleration
  are unverified; shared C++ toolchain migration remains separate.
- **Current KDE/Qt integration:** KWin 6.7.5 runs a nested Qt Wayland window
  with software rendering and tested keyboard input. Plasma Mobile 6.7.5
  builds and installs with checked library loading and QML components.
  Common [Qt 6.12 / Frameworks 6.31 source recipes](https://github.com/oxtech-ember/EmberBSD-Ports/tree/main/probes/plasma-mobile/toolkit)
  and [shared FFmpeg9 recipes](https://github.com/oxtech-ember/EmberBSD-Ports/tree/main/profiles/common-media)
  have checked source/dependency selection. The FFmpeg audio API and device
  registration pass in the AArch64 VM with GCC16; real metadata extraction
  passes on the host. Complete native packages and Qt playback remain pending.
  A complete mobile shell workflow, native display and power management
  still need validation. See the [Plasma Mobile port](https://github.com/oxtech-ember/EmberBSD-Ports/tree/main/probes/plasma-mobile).

### GPU foundations and NPU direction

- **Native graphics infrastructure:** experimental VirtIO DRM/KMS, UVM-backed
  GEM buffers and PRIME sharing make direct Wayland sessions possible.
  UTM tests cover visible KMS output, 32 cross-process buffer-lifetime cycles
  and labwc/Pixman displaying Kate without Xorg. The tree also includes DRM
  device-identity, fence-validation, bounded capset-query, context creation/retirement,
  console-recovery and partial-page memfd
  fixes; their individual build/runtime boundaries are documented separately.
  [Resource-lifetime fixes](sys/external/bsd/drm2/virtio/resource-lifetime.md)
  retain shared buffers across duplicate handles and failed host responses.
  [Asynchronous submission](sys/external/bsd/drm2/virtio/submit-ownership.md)
  reports immediate queue errors and publishes fence descriptors only after
  acceptance, with balanced cleanup on failure.
  [Completion ordering and reset](sys/external/bsd/drm2/virtio/completion-lifetime.md)
  prevent a later reply from prematurely signaling an older command and
  drain accepted requests before publishing terminal reset results.
  [Owned DMA backing checks](sys/external/bsd/drm2/virtio/dma-eligibility.md)
  reject unsuitable ARM64 maps before a future 3D context can use them,
  including resources shared through PRIME or duplicate handles.
  [Backing lifetime and retirement](sys/external/bsd/drm2/virtio/backing-lifetime.md)
  keep eligible memory pinned beyond individual request completion, until
  fenced resource retirement or completed reset makes release safe.
  [Whole-context EXEC ownership](sys/external/bsd/drm2/virtio/exec-ownership.md)
  covers every attached buffer even when an application supplies incomplete
  hints, and retains each pending operation through retries and overlapping
  completion. A [classic fence limit](sys/external/bsd/drm2/virtio/classic-fence-range.md)
  stops the device safely before sequence numbers can wrap or become ambiguous.
  [Explicit transfers and WAIT](sys/external/bsd/drm2/virtio/transfer-wait.md)
  retain per-buffer operations through completion and report their exact errors,
  even when a later reservation replaces the visible fence.
  [Controlled console and 2D uploads](sys/external/bsd/drm2/virtio/controlled-console.md)
  now pair finite DMA phases, retain host resources through failed cleanup,
  and reject private framebuffer handles before publication. Kernel copies
  wait for prior operations while retaining the buffer and its reservation.
  [Classic EXEC framing](sys/external/bsd/drm2/virtio/exec-framing.md) rejects
  misaligned or truncated packets before DMA or output-fence publication,
  preserving command bytes and Mesa transfer padding.
  Native contracts and object builds pass. A complete `EMBERGPU` kernel now
  cross-builds on macOS; its exact runtime boundary is recorded in the driver guide.
  Complete request bounds, arbitrary userspace CPU access and live DMA qualification
  remain pending.
  See the [VirtGPU implementation](sys/external/bsd/drm2/virtio/README.md),
  [DRM identity checks](ember/boot/drm-native-identity.md) and
  [graphics probes](https://github.com/oxtech-ember/EmberBSD-Examples/tree/main/desktop/wayland-utm).
  [Local socket fixes](ember/boot/local-socket-compatibility.md) restore
  per-call nonblocking sends and socketpair peer identity. All 37 regression
  cases pass on Zero 3W, followed by all 26 enabled upstream tests of the
  installed Ports Wayland 1.26.0nb1 package. This validates the IPC/library
  layer; a compositor and accelerated session need separate acceptance.
  Ports owns the [current Mesa 26.2.4 source adaptation](https://github.com/oxtech-ember/EmberBSD-Ports/tree/main/probes/wayland-utm),
  with native DSO-lifetime/numeric regressions under GCC 16.2 and
  common-toolchain staging rules. A [common graphics source profile](https://github.com/oxtech-ember/EmberBSD-Ports/tree/main/profiles/common-graphics)
  supplies canonical MesaLib 26.2.4nb3/libdrm 2.4.134nb2 packages with one
  shared LLVM 23.1.2 provider. The complete selected Mesa package cross-builds
  with GCC16 on macOS and is installed normally on Orange Pi Zero 3W (A733).
  [Installed-package acceptance](https://github.com/oxtech-ember/EmberBSD-Ports/blob/main/profiles/common-graphics/cross/mesa-package.md)
  verifies the package files, links and library closure, then passes four
  surfaceless EGL/llvmpipe shader/pixel lifecycles and all 37 upstream test
  invocations, including seven LLVM ORC tests. One internal upstream case
  skips and nine NIR cases remain disabled. The installed core-only libdrm
  passes hash, skip-list and exported-symbol checks; hardware enumeration
  skips because this board kernel has no DRM accelerator device.
  Revision nb2 corrects XCB pkg-config requirements with byte-identical Mesa
  libraries. Its first migrated consumer, [libepoxy 1.5.10nb2](https://github.com/oxtech-ember/EmberBSD-Ports/blob/main/profiles/common-graphics/cross/epoxy.md),
  passes four real dispatch/pixel lifecycles and four pure upstream tests
  against the installed providers. The common [LLVM23.1.2nb1 update](https://github.com/oxtech-ember/EmberBSD-Ports/blob/main/profiles/common-graphics/cross/mesa-runtime-rebind.md)
  also passes renewed Mesa and libepoxy rendering checks on Zero 3W,
  without rebuilding those libraries. The package includes X11/Wayland,
  EGL/GLES/GL and GBM. The same providers pass four guarded
  [GBM/PRIME/EGLImage pixel lifecycles](https://github.com/oxtech-ember/EmberBSD-Ports/blob/main/profiles/common-graphics/cross/gbm.md)
  through native VirtGPU buffers in an isolated AArch64 VM, using llvmpipe.
  [wlroots 0.20.2nb4](https://github.com/oxtech-ember/EmberBSD-Ports/blob/main/profiles/common-graphics/cross/wlroots-package.md)
  passes four GLES2/headless lifecycles both as root and after dropping
  privileges, checking 1024 pixels and buffer lifetime across a real KMS
  framebuffer import. Its [current DRM/input package](https://github.com/oxtech-ember/EmberBSD-Ports/blob/main/profiles/common-graphics/cross/drm-input.md)
  also presents four llvmpipe frames at 1280x800 through an active libseat
  session and enumerates two wscons devices. All 131 selected dependency
  invocations pass. Input events, VT switching, application surfaces and a
  complete accelerated Wayland session remain unverified.
  Ports also [cross-builds current text/image dependencies](https://github.com/oxtech-ember/EmberBSD-Ports/blob/main/profiles/common-graphics/cross/labwc-dependencies.md),
  including GLib 2.90.1, Pango 1.58.2, HarfBuzz 14.6.0 and Cairo 1.18.6, with
  normal package checks. Four installed AArch64 VM cycles pass exact font
  selection, text shaping, CPU rasterization and PNG roundtrip; two causal
  controls detect missing drawing and ligatures. Installed
  [libsfdo and session D-Bus](https://github.com/oxtech-ember/EmberBSD-Ports/blob/main/profiles/common-graphics/cross/desktop-support.md)
  also pass upstream lookup/parser tests, real method replies, name ownership
  and error/lifecycle checks in AArch64. The
  [Rust 1.99 cross-std adaptation](https://github.com/oxtech-ember/EmberBSD-Ports/tree/main/probes/rust-cross-std)
  also passes target threads, TLS destructors, unwinding and C `dlopen`
  consumers in AArch64 VM, preparing the current SVG path.
  The matching [Rust build-host package](https://github.com/oxtech-ember/EmberBSD-Ports/blob/main/profiles/common-build-tools/rust.md)
  and cargo-c pass normal macOS installation and a native Rust/C ABI consumer
  after a Ports correction to Mach-O dependency metadata. Its
  [packaged EmberBSD target std](https://github.com/oxtech-ember/EmberBSD-Ports/blob/main/profiles/common-build-tools/rust-target-std.md)
  installs into the same host compiler and passes target TLS/C consumers and
  an offline Cargo target with macOS proc macros in the AArch64 VM.
  The [GI 1.86.0 package](https://github.com/oxtech-ember/EmberBSD-Ports/blob/main/profiles/common-graphics/cross/introspection.md)
  also cross-builds with real GType queries and three unchanged upstream
  hash/search cases passing on EmberBSD/CM5.
  Matching GLib 2.90.1 metadata now supplies seven cross-built GIR/typelib
  pairs, with loading and introspection/libffi invocation verified on CM5.
  Ports' [complete GdkPixbuf package](https://github.com/oxtech-ember/EmberBSD-Ports/blob/main/profiles/common-graphics/cross/gdk-pixbuf.md)
  passes thirteen raster formats and four decoding cycles on CM5.
  The [librsvg 2.63.2 package](https://github.com/oxtech-ember/EmberBSD-Ports/blob/main/profiles/common-graphics/cross/librsvg.md)
  passes four CM5 CPU-rendering cycles: SVG/PNG pixels, dynamic SVG loading,
  embedded AVIF, text and typelib invocation. Ports repaired the isolated
  consumer's missing MIME database; this is CPU-rendering acceptance.
  Ports' [labwc 0.20.2nb2 package](https://github.com/oxtech-ember/EmberBSD-Ports/blob/main/profiles/common-graphics/cross/labwc.md)
  cross-builds on macOS with SVG, icons, translations and man pages.
  All 62 installed files/links and 80 target ELF files pass inspection.
  Ports' separate [labwc session check](https://github.com/oxtech-ember/EmberBSD-Ports/blob/main/profiles/common-graphics/cross/labwc-session.md)
  passes two VirGL/ANGLE Metal sessions on Apple M4: 66 captured EGL frames each,
  actual virtual USB input, keyboard-driven window/maximize/restore/fullscreen
  transitions, mouse dragging and clean restart. Sustained
  desktop use, VT switching and physical-board GPU acceleration remain unaccepted.
  A Ports
  [host-side VirGL 1.3.0 adaptation](https://github.com/oxtech-ember/EmberBSD-Ports/tree/main/probes/utm-virgl-host)
  preserves the upstream IOV-size correction and prevents resource publication
  after reported CREATE failures, with cleanup of owned partial allocations.
  Its [classic backing ledger](https://github.com/oxtech-ember/EmberBSD-Ports/blob/main/probes/utm-virgl-host/BACKING.md)
  retains guest mappings through renderer detach and all cleanup paths,
  including deferred UNREF, with causal ownership and sanitizer checks.
  An opt-in [classic lifecycle barrier](https://github.com/oxtech-ember/EmberBSD-Ports/blob/main/probes/utm-virgl-host/LIFECYCLE.md)
  orders CPU producer shutdown and all-resource detach before mapping release;
  source tests cover fault/reset, blocked display and command handoff.
  [Reported command and fence errors](https://github.com/oxtech-ember/EmberBSD-Ports/blob/main/probes/utm-virgl-host/COMPLETION.md)
  now enter that barrier before guest completion, with 683 source assertions
  passing in plain, sanitizer and NDEBUG runs.
  The paired [GL/EGL wait adaptation](https://github.com/oxtech-ember/EmberBSD-Ports/blob/main/probes/utm-virgl-host/wait-errors.md)
  also prevents failed waits from becoming successful fence callbacks;
  causal renderer-to-QEMU source checks pass in all three modes.
  A [full private renderer build](https://github.com/oxtech-ember/EmberBSD-Ports/tree/main/probes/utm-virgl-host/host)
  with current libepoxy 1.5.10 passes texture readback, decoded framebuffer clears,
  shader triangle pixels, native fences
  and three cleanup/reinit cycles on Apple M3/ANGLE Metal. It also fixes a
  reproduced absent-context cleanup error and rejects truncated command payloads
  with EINVAL; full native decoder checks preserve valid-command behavior.
  Reported surface/GL errors now reject classic submissions and poisoned contexts,
  tested on Metal with upstream GL checking both enabled and disabled.
  [Query-result checks](https://github.com/oxtech-ember/EmberBSD-Ports/blob/main/probes/utm-virgl-host/host/query-results.md)
  reject incomplete output backing and preserve delayed errors and fence ownership.
  Native Metal checks and 226 causal software assertions pass; concurrent access
  and guest execution remain outside that validation.
  Three [direct native resets](https://github.com/oxtech-ember/EmberBSD-Ports/blob/main/probes/utm-virgl-host/host/native-reset.md)
  with live 3D resources and unreported fences also pass, including old-object
  removal and shader pixels after ID reuse. This does not prove interruption
  of unfinished GPU work or guest 3D reset.
  The [full paired QEMU recipe](https://github.com/oxtech-ember/EmberBSD-Ports/tree/main/probes/utm-virgl-host/qemu)
  builds and passes an isolated 2D guest boot on ANGLE Metal, including libdrm
  and 32 GEM/PRIME lifetimes. A separate [live 2D backing check](https://github.com/oxtech-ember/EmberBSD-Ports/blob/main/probes/utm-virgl-host/qemu/reset.md)
  passes three QMP resets and four Metal renderer initializations.
  A separate [EMBERVIRGL kernel](ember/boot/utm-virgl-optin.md) now passes
  four [installed guest VirGL GLES lifecycles](https://github.com/oxtech-ember/EmberBSD-Ports/blob/main/profiles/common-graphics/cross/virgl-draw.md)
  with Mesa26, unchanged libepoxy and the common LLVM23 runtime. Shader
  rejection, triangle pixels and cleanup pass on the paired ANGLE Metal
  Apple M3 host. Ordinary EMBERGPU still requests no VirGL feature.
  The same stack now passes four [accelerated wlroots DRM presentations](https://github.com/oxtech-ember/EmberBSD-Ports/blob/main/profiles/common-graphics/cross/wlroots-virgl.md)
  at 1280x800, with pixel checks, live-provider verification and clean shutdown.
  This required a Ports Cocoa context repair and safe native PCI queue disable
  after child teardown. The latter also avoids the observed panic when the old
  host fails its scanout fence. Application surfaces, a complete accelerated
  Wayland session, in-flight guest reset, reliable console recovery and Vulkan
  Compute remain unverified.
- **Physical GPU and NPU porting targets:** CIX P1 is the first selected
  direction: Mali-G720 through Panthor/[Mesa PanVK](https://docs.mesa3d.org/drivers/panfrost.html),
  and Zhouyi v3/X2 through the [Compass driver/runtime sources](https://github.com/Arm-China/Compass_NPU_Driver).
  The [Compass Ports probe](https://github.com/oxtech-ember/EmberBSD-Ports/tree/main/probes/compass-umd)
  fixes descriptor ownership during initialization and cleanup, and rejects
  out-of-range partition/cluster queries while preserving legacy NPU counts.
  On NetBSD/AArch64 with GCC 16.2, 13 descriptor cases and 58 core-count
  cases pass against isolated production methods. The [complete 27-source UMD](https://github.com/oxtech-ember/EmberBSD-Ports/blob/main/probes/compass-umd/full-umd.md)
  cross-builds with GCC16; its actual DSO passes 85 no-device API checks over
  four cycles on the A733 Zero 3W. Native kernel transport, DMA and model
  execution remain unverified.
  The [A733 Ports audit](https://github.com/oxtech-ember/EmberBSD-Ports/tree/main/probes/a733-accelerators)
  identifies its Vivante NPU's missing Mesa TP path and pins the exact PowerVR
  firmware. The OS now supplies an [A733 PCK600 power-domain provider](ember/boot/a733-power-domains.md)
  with bounded transitions, error propagation and retained legacy FDT callbacks.
  Its 45 transition scenarios and GCC16 cross object builds pass. The
  [accelerator clock providers](ember/boot/a733-accelerator-clocks.md) add module
  clocks, interface gates and resets while preserving firmware-owned PLLs.
  They pass 199 software assertions and GCC12/GCC16 object builds. A complete
  GCC16.2 kernel boots on Zero 3W with both CCUs and PCK600 attached. The default read-only
  [GPU identification consumer](ember/boot/a733-gpu-identification.md) now
  attaches on Zero 3W. A stable physical CCU snapshot shows the GPU module
  and bus clocks gated and reset asserted, with DCDC4 programmed to 800 mV.
  A subsequent matched boot classifies the physical DCXO as 26 MHz while
  preserving the nominal DT clock. GPU_CORE's ON policy disagrees with its
  status, so the strict reader refuses readiness. The matched #7 kernel
  reads stable power-controller snapshots and identifies both GPU domains
  as single-Q-Channel PPUs; the inconsistent CORE state remains unresolved.
  The corrected [clock-only experiment](ember/boot/a733-gpu-identification.md#physical-result)
  completed GPU_CLK UPDATE on physical Zero 3W. CCU reported
  400/200 MHz configuration, but CORE ON/Q still timed out before GPU MMIO.
  Reboot with the restored normal DTB returned gated clocks and SSH.
  The observed post-PLL gates were open before preparation and after timeout.
  A [read-only preboot snapshot](ember/boot/a733-preboot-snapshot.md) finds
  CORE's ON-policy/OFF-status mismatch before the final firmware handoff and
  kernel entry. It persists through a confirmed power removal and the clock-only
  experiment. Its origin, GPU identity and acceleration remain unverified.
  The missing power handshake, general shared resource management, DMA/MMU
  and command submission still require porting. These are
  porting targets, not available EmberBSD acceleration. Board bring-up,
  driver/DMA integration, compatible runtime and real model execution must
  all pass before an accelerated AI workflow is claimed.
  For the Raspberry Pi firmware path, [native mailbox transactions](ember/boot/bcmmbox-transactions.md)
  now share a bounded TX/RX deadline and validate the echoed DMA address.
  Unknown completion retains the buffer and closes that channel until reboot,
  while unrelated channels can progress. Seventy software-contract cases pass
  on macOS and in an AArch64 VM. The complete GCC16 EMBER64 kernel and matched
  modules build; that kernel also boots and shuts down in an isolated AArch64 VM.
  On physical CM5, a normal reboot and 128 firmware clock reads through four
  processes plus 32 temperature/throttle checks pass with that matched build.
  This validates the ACPI polling/DMA success path; hardware IRQ/fault handling
  remains unverified. A [native BCM2712 V3D observer](ember/boot/bcm2712-v3d.md)
  validates firmware clock/reset state before fault-aware identification;
  186 actual-source cases pass on host and CM5 CPU with fake hardware.
  On physical CM5, the matched kernel identifies V3D 7.1 with one core and
  reads HUB/CORE/MMU registers after validated clock/reset and stable idle SMS
  checks. A separate opt-in [native reset probe](ember/boot/bcm2712-v3d-takeover.md)
  completes SMS and PM reset on that CM5 and validates the post-reset inventory.
  A further opt-in [first DMA/TFU experiment](ember/boot/bcm2712-v3d-dma.md)
  published the GPU MMU on that CM5 and executed one translated 64x64 R32F
  TFU copy through alias addresses with verified destination data, untouched
  canaries and an exactly-once completion counter. A second opt-in
  [bin/render queue experiment](ember/boot/bcm2712-v3d-binrender-plan.md)
  then executed both control lists on that CM5: the binner completed and the
  render stage cleared one 64x64 tile and stored the verified RGBA8 image
  through the published page table. A third opt-in
  [interrupt experiment](ember/boot/bcm2712-v3d-irq-plan.md) delivered the
  TFU and bin/render completions as real ACPI interrupts on both GPU lines,
  with handler acknowledgements and self-masking storm protection; the
  stand firmware's swapped core/hub line names were identified by wire
  evidence. The [acceptance guards](ember/boot/bcm2712-v3d-validation.md)
  add failure-propagation, completion-count and DMA-integrity regressions;
  their corrected DMA/queue and IRQ paths passed physical CM5 repeats
  on 2026-10-11. On the same board,
  the corrected [MMU fault experiment](ember/boot/bcm2712-v3d-fault-plan.md)
  passed on CM5: one invalid destination fault, bounded recovery and a
  verified good TFU copy in the same boot, with guarded scratch and intact
  protected buffers. Each submission must reprogram the complete TFU payload.
  A native [DRM/GEM render interface](ember/boot/bcm2712-v3d-render.md) now
  passes bounded userspace TFU acceptance on CM5: 420 verified copies,
  per-file mmap isolation, counter wrap, concurrent clients and submit/close
  races. The [native compute path](ember/boot/bcm2712-v3d-csd.md) also passed
  389 QPU dispatches with 6,224 verified integer results, changing uniforms,
  guarded memory, concurrent clients and CSD/TFU/CSD transitions. A subsequent
  [binary synchronization check](ember/boot/bcm2712-v3d-sync.md) passed standard
  syncobj operations, finite waits, 325 CSD submissions and 64 CSD/TFU chains,
  with 5,184 verified GPU results. The [native bin/render interface](ember/boot/bcm2712-v3d-gl.md)
  then passed 325 userspace CL pairs, 1,331,200 checked pixels and 32 TFU
  copies, including guards and binary fences. The common Ports
  [Mesa package](https://github.com/oxtech-ember/EmberBSD-Ports/blob/main/profiles/common-graphics/cross/mesa-cm5.md)
  now passes real V3D 7.1.10.2 shader/pixel rendering: EGL 1.5, GLES 3.0,
  four lifecycles at both 16x16 and 1280x800. Native platform identity
  makes this GPU discoverable without DRM master. All 38 Mesa upstream
  invocations pass with recorded internal skips; LLVM23 llvmpipe also passes.
  Same-device PRIME passes 80 cross-process lifecycles and four real
  GBM/EGLImage shader/pixel cycles after native DMA-BUF lifetime repairs.
  The ordinary kernel remains a passive observer. General job recovery,
  foreign-device sharing, Vulkan and remote accelerated Plasma remain unverified.

### Network diagnostics kit

- **Board-side capture, analysis and load tools:** EmberBSD-Ports carries a
  [network diagnostics profile](https://github.com/oxtech-ember/EmberBSD-Ports/tree/main/profiles/network-diagnostics)
  with libpcap 1.10.7, tcpdump 4.99.6, iperf3 3.21 and the Wireshark 4.6.8
  CLI set (dumpcap, tshark, capinfos, editcap, mergecap), cross-built from
  binary packages on the development host. On Raspberry Pi 5 (EmberBSD
  11.0, board `ember-rpi5-01`) tcpdump captured live Wi-Fi traffic through
  BPF without kernel drops, iperf3 measured TCP, reverse TCP and zero-loss
  UDP jitter against a LAN peer, and dumpcap completed a three-file
  ring-buffer capture under load with per-interface drop accounting.
- **Native Bluetooth HCI recording:** the `hcisnoop` recorder in the same
  profile reads the netbt HCI socket tap (direction from
  `SCM_HCI_DIRECTION`, timestamps from `SO_TIMESTAMP`) and writes BTSnoop
  traces that Wireshark opens as "Bluetooth HCI H4" without any Linux
  BlueZ dependency. It recorded a real controller inquiry on the same
  board with correct host/controller directions. Wi-Fi monitor mode with
  Radiotap, Lua dissectors, the Wireshark GUI on boards and Ethernet
  MAC/PHY telemetry remain separate queue items; see the profile README
  for the current boundaries.

### Board support and system builds

- **Allwinner A733** (Orange Pi Zero 4 and Zero 3W), a chip the base
  system has no support for: eight cores, the card at SDR104 speed,
  gigabit Ethernet, the AIC8800 Wi-Fi and Bluetooth module, processor
  frequency control, temperature sensors, watchdog and clock.
- **ESP32-S31**, a 32-bit RISC-V chip with 16 MB of memory and a new
  platform for NetBSD: the system boots from flash and works over Wi-Fi.
- **Raspberry Pi 5 and Compute Module 5**: UEFI firmware built from
  this tree, and drivers for the on-board Wi-Fi, Ethernet behind the
  RP1 chip, Bluetooth, the power button, the fan, the watchdog, I2C
  and a WM8960 audio codec. The board pages distinguish tested peripherals
  from shared driver availability.
- **WPA3-Personal on CM5:** upstream wpa_supplicant SAE now works through
  the fork's `bwfm` external-auth interface, with required protected
  management frames. A physical CM5 passed SAE group 19/H2E, bidirectional
  file transfer, three reconnects, wrong-password rejection, a WPA2
  regression and automatic reconnection after reboot. See the
  [configuration guide](ember/boot/bwfm-sae.md) and
  [hardware receipt](ember/boards/compute-module-5.md#hardware-check-2026-10-08).
  Other boards, access-point combinations and long-run operation need
  separate acceptance; this is not an 802.11r/802.11v roaming claim.
- **Zero 3W WPA3 and recovery:** the AIC8800D80 path supports SAE/H2E
  with required PMF. Protected SA Query recovers an AP/client state mismatch
  through the normal supplicant. Physical checks cover query/response,
  automatic reconnection and an 8 MiB transfer each way; see the
  [guide](ember/boot/aicwf-sae.md) and
  [hardware receipt](ember/boards/orange-pi-zero-3w.md#protected-association-recovery-2026-10-09).
- **Consistent Wi-Fi discovery and CM5 cooling:** `bwfm` retains firmware
  scan channels and signal ranking across both bands; the CM5 ACPI fan can
  [remain at maximum cooling](ember/boot/thermal-fan.md) across reboot.
  The [physical check](ember/boards/compute-module-5.md#home-mesh-and-cooling-check-2026-10-08)
  covers 5 GHz SAE/PMF traffic and operator-confirmed continuous rotation.
- **Raspberry Pi Zero 2 W**: Wi-Fi on its BCM43436 chip.
- **Headless base on Raspberry Pi 5:** the fork's kernel, matching board
  modules and reduced base userland are built and installed through the
  [base update procedure](ember/boot/aarch64-base-update.md). Kernel boot,
  16 memfd cases, three FP-state modes and candidate libc checks passed.
  The [board receipt](ember/boards/raspberry-pi-5.md#headless-base-update-2026-10-07)
  records operator-reported Ethernet access after the final reboot;
  Wi-Fi recovery and post-reboot library acceptance remain open.
- **Kernel fixes found on these boards** that are not specific to
  them: the `bwfm` Wi-Fi driver stalling on large transfers and after
  an access point asks the client to change band, idle cores on
  aarch64 missing a reschedule, and the SD host controller driver.
  Source changes preserve their provenance; `ember/patches` records the
  original board adaptations already applied to this tree.
- **Current kernel on Orange Pi Zero 3W:** a clean EMBER64 build boots on
  physical A733 hardware, passes all 16 memfd tests and a checked Wi-Fi
  round trip. The [board receipt](ember/boards/orange-pi-zero-3w.md#current-kernel-check-2026-10-07)
  records the kernel hashes, matching modules and remaining userland and
  accelerator limits.
- **AArch64 numerical correctness:** an [initial FP state correction](ember/boot/aarch64-fp-state.md)
  addresses lost subnormal values and NaN payloads on CPUs without AArch32.
  Production contracts and native object compilation pass. Three runtime
  modes also pass after booting the corrected kernel on Pi 5 Cortex-A76;
  that board already passed the baseline. A new minimal AArch64 UTM guest
  also passes all modes after cold-booting the GCC16-built kernel; the
  removed VM's full failing compiler suite has not been repeated.
- **AArch64 atomic correctness:** [narrow outlined CAS helpers](ember/boot/aarch64-outlined-cas.md)
  now normalize expected arguments so matching byte/halfword updates are
  not skipped. All 850 native production checks and an isolated unchanged
  GCC atomic regression pass with the repaired objects. The complete corrected
  shared/static libc is installed on physical Orange Pi Zero 3W; the installed
  shared library passes all 850 checks. Full compiler-suite acceptance remains
  separate.
  The same accepted static archive also passes 850 freshly cross-compiled
  checks on CM5. Ports now rejects divergent shared libc aliases in its
  [cross sysroot](https://github.com/oxtech-ember/EmberBSD-Ports/blob/main/profiles/common-build-tools/cross/sysroot.md);
  a complete current base userland/header release remains unaccepted.
- **AArch64 binary128 comparisons:** [libc exception-policy fixes](ember/tools/aarch64-binary128.md)
  preserve NaN comparison results while raising the required INVALID exception.
  Native raw-ABI, FP-mode and trap checks pass with GCC 12.5 and GCC 16.2
  candidate objects in UTM. The complete libc built and installed on Zero 3W
  passes the 3,600-row matrix and all 16 FP modes; hardware IOE is unavailable
  there, so trap cases skip. The kernel's FP signal classification is unchanged.
- **Reproducible builds and development:** pinned kernel/UEFI inputs and
  checked firmware assets, pkgsrc overlays, versioned source probes and
  standalone examples. The [Ports development toolchain](https://github.com/oxtech-ember/EmberBSD-Ports/tree/main/profiles/development-toolchain)
  provides GCC 16.2 built in the AArch64 VM and running there and on physical
  Orange Pi Zero 3W. New [development sessions](https://github.com/oxtech-ember/EmberBSD-Ports/blob/main/profiles/common-build-tools/development-defaults.md)
  and ordinary pkgsrc builds on Zero 3W select the repaired GCC16 nb1 package;
  the base compiler remains explicit bootstrap/recovery support.
  The [cross-build wrapper](ember/boot/cross-build.md) builds an AArch64 kernel,
  matched board modules and DTBs on Apple Silicon macOS and Debian/x86_64
  with GCC16.2. The Linux build passes all ten portable contracts and artifact
  checksum checks; native contracts and boot acceptance remain separate.
  The resulting kernel cold-boots in AArch64 UTM and passes FP process,
  signal and thread checks. The
  [Ports cross GCC16 recipe](https://github.com/oxtech-ember/EmberBSD-Ports/tree/main/profiles/development-toolchain/cross)
  also runs GCC16 on that host; cross-built C11 (plain/LTO) and C++20 DSO
  checks pass on Zero 3W with its installed GCC16 runtime. Its `--kernel`
  mode builds the cross frontends without an application sysroot; target
  libc and C++ runtime inputs remain required for application builds. The
  [CTF converter](ember/boot/ctf-external-types.md) preserves GCC/Clang types
  in DWARF4/5 and both offset widths, including COMDAT type units, standalone
  DWO, indexed DWP and combined split/supplementary input. Linked multi-CU
  fixtures retain distinct types; ambiguous static-symbol ownership is rejected
  before changing the ELF. [Live DTrace](ember/boot/dtrace-dwarf.md) passes
  typed FBT/syscall tracing in the AArch64 VM. These are checked type-conversion
  and tracing workflows; CTF does not represent general debugger location state.
  [Ports GDB 18.1nb1](https://github.com/oxtech-ember/EmberBSD-Ports/tree/main/profiles/development-toolchain/gdb)
  passes installed external-DWARF, live FP-register and signal-unwinding checks.
  Its 32 format cases cover DWARF2–5, DWARF32/64, DWP, type units and compressed
  sections. Another 24 expression, 14 agent-compiler and 72 entry-value checks
  pass, including reconstructed float/SIMD registers and unavailable history.
  The common [LLVM 23.1.2nb1 DWP tool](https://github.com/oxtech-ember/EmberBSD-Ports/blob/main/profiles/common-build-tools/cross/llvm-dwp-tests.md)
  passes 104 packaging/repacking cases with live GDB values, including mixed
  widths, shared CU/TU tables and string-offset promotion. Its existing
  C API/bitcode and ORC JIT consumers still pass.
  The OS-owned [development image](ember/image/README.md) installs the pinned
  GDB/LLVM package closure offline and includes CTF/libdwarf 2.2 and the
  corrected FFS primary-superblock selector. The same installed GDB, DWP and
  CTF matrices pass on first boot and after a normal AArch64 VM reboot.
  Full OS builds
  with GCC16 and general pkgsrc cross-package builds remain unvalidated.
  C11/C++20 threads, TLS and shared-library
  checks pass. On Zero 3W, current MPFR/MPC/libxml2 and actual pkgsrc wrapper
  compilation, package installation and loaded-runtime checks also pass;
  see the [native validation](https://github.com/oxtech-ember/EmberBSD-Ports/blob/main/profiles/development-toolchain/native-validation.md).
  The original atomic/binary128 LTO tests pass with its installed corrected libc.
  A [Ports allocation repair](https://github.com/oxtech-ember/EmberBSD-Ports/blob/main/profiles/development-toolchain/modules-portability.md)
  handles NetBSD's distinct EOPNOTSUPP value; the installed GCC16 nb1 package
  passes the original crashing C++ module test and a module import/run check
  on Zero 3W. The full upstream suite has exposed platform compatibility
  failures. An upstream
  [TSVC allocator backport](https://github.com/oxtech-ember/EmberBSD-Ports/blob/main/profiles/development-toolchain/testsuite-portability.md)
  passes focused native plain/LTO checks; the full suite remains unaccepted.
  Remaining repairs and a coherent Qt/LLVM runtime rebuild are required before
  adopting it as the default compiler in new images. The [common build-tools profile](https://github.com/oxtech-ember/EmberBSD-Ports/tree/main/profiles/common-build-tools)
  supplies [Python 3.14.8](https://github.com/oxtech-ember/EmberBSD-Ports/blob/main/profiles/common-build-tools/python.md)
  with Mac cross packaging and installed AArch64 VM acceptance: C/C++ embedding,
  extension loading and 21 selected upstream suites, including TLS and SQLite.
  Meson 1.12.1 and Ninja 1.13.2 cross packages also pass installed C/C++
  builds, Python embedding, incremental/error handling and install-RPATH checks
  in that VM with explicit current GNU as/ld. The profile prepares matching
  LLVM/Clang/LLD 23.1.2 with upstream lit. Portability patches, generated-header declarations and
  explicit interpreter/LLVM selection have source checks. Focused native
  macro/selection and GCC16 metadata checks pass. The complete LLVM23 core
  package passes normal package checks and installed C API/bitcode plus four
  default ORC JITLink lifecycles on Zero 3W. Upstream lit passes installed
  launcher and PASS/XFAIL/FAIL checks in AArch64 UTM. Clang/LLD packages and
  TinyGo consumer acceptance remains pending; Mesa26 uses the common LLVM23 provider. See the [LLVM family contract](https://github.com/oxtech-ember/EmberBSD-Ports/blob/main/profiles/common-build-tools/llvm-family.md).
  Ports also owns the [Mac cross-package workflow](https://github.com/oxtech-ember/EmberBSD-Ports/tree/main/profiles/common-build-tools/cross):
  pkgconf 3.0.7, GNU M4 1.4.21, Libtool 2.6.2 and
  [Binutils 2.47nb1](https://github.com/oxtech-ember/EmberBSD-Ports/blob/main/profiles/common-build-tools/binutils.md) pass normal package checks
  and installed AArch64 VM consumers with GCC16. Regression checks cover
  target ELF metadata, package replacement and extraction rollback on the host.
  The Binutils port also fixes mixed DWARF32/64 source lookup and passes
  installed GNU CTF and C++ DSO checks with explicit GNU tool selection.
  Migrating GCC16's hardcoded bootstrap as/ld defaults remains pending.
  [Developer skills](#connect-developer-skills) help
  AI coding assistants find the owning project, test changes and prepare
  contributions. [User skills](#connect-user-skills) guide OS installation, application use,
  device diagnosis and personal Ports maintenance with the available tools;
  package checks are verified, while on-device agent behavior remains untested.
  A general validated installation image is not yet released.

The costs are equally plain. Fixes from upstream are merged by hand.
Only what the linked board pages mark "Tested" is claimed on physical
hardware. Each application or graphics result has its own test environment
and limits in the linked documentation. Wi-Fi on the A733 boards and the ESP32-S31
needs vendor files that are not open.

## Supported boards

Choose a board below for its boot path, feature results, known limitations
and evidence. A physical test validates only the listed functions on the
listed revision; it does not imply full peripheral or long-run support.

| Board | Architecture / SoC | Confirmed scope and main limits |
| --- | --- | --- |
| [Raspberry Pi 5 (C1)](ember/boards/raspberry-pi-5.md) | AArch64 / BCM2712 | microSD/UEFI boot, Ethernet, Wi-Fi, classic Bluetooth, cooling and WM8960 audio; USB and graphics not validated here |
| [Compute Module 5 (D0)](ember/boards/compute-module-5.md) | AArch64 / BCM2712 | eMMC/UEFI boot, four cores, gigabit Ethernet, WPA2/WPA3-Personal and temperature sensing; persistent maximum cooling; other peripherals unvalidated |
| [Raspberry Pi Zero 2 W](ember/boards/raspberry-pi-zero-2-w.md) | AArch64 / BCM2710A1 | microSD boot, all cores, serial and 2.4 GHz Wi-Fi; large Wi-Fi transfers stall |
| [Orange Pi Zero 4](ember/boards/orange-pi-zero-4.md) | AArch64 / Allwinner A733 | Eight cores, SDR104, Ethernet, Wi-Fi, classic Bluetooth, thermal/frequency control and USB 2.0 data; SuperSpeed unconfirmed |
| [Orange Pi Zero 3W](ember/boards/orange-pi-zero-3w.md) | AArch64 / Allwinner A733 | Eight cores, SDR104, Wi-Fi, Bluetooth inquiry and thermal/frequency control; USB devices and Bluetooth pairing not tested |
| [ESP32-S31 development board](ember/boards/esp32-s31.md) | RISC-V 32 / ESP32-S31 | Flash boot, Ethernet and WPA2 Wi-Fi in 16 MB; one core, vendor radio libraries, no BLE/USB |

The [board catalog](ember/boards/README.md) defines validation terms and links
to VM and research targets. To contribute another board, follow
[adding a board](ember/boards/adding-a-board.md) and the
[developer skill](https://github.com/oxtech-ember/Ember-Agent-Skills#add-your-board).
Add a catalog row and a board page; keep detailed feature matrices on those pages.

## Wi-Fi status and remaining work

[External SAE with required PMF](ember/boot/bwfm-sae.md) is integrated and
tested on CM5 with CYW43455 firmware 7.45.265. The implementation also sends
firmware disassociation on disconnect, serializes firmware commands and
wraps the 16-bit command identifier. Three disconnect/reconnect cycles
passed without command timeouts. No WPA3 throughput improvement is claimed.

[Silent-disconnect recovery](ember/boot/bwfm-link-events.md) is physically
checked on CM5: the driver detects a firmware association lost without an
event, restarts host scanning and reconnects with SAE/required PMF. An 8 MiB
Wi-Fi round trip passed afterward. Long-run stability and seamless roaming
remain unverified.

- **Host-managed 802.11v roaming in `bwfm`** remains a separate local change,
  not integrated into this tree. With WPA2-PSK, a Raspberry Pi 5 local build
  moved from 2.4 to 5 GHz at the access point's request, reconnecting in
  0.20 s (4.22 s with the scan), and refused a prohibited transition.
  The published driver disables autonomous firmware WNM transitions.
  A seamless handover is not claimed.
- **802.11r fast transition** is not implemented by the external-SAE work.
- **Zero 3W Wi-Fi:** the AIC8800D80 connected-scan recovery is fixed and
  physically checked. [External SAE/H2E with required PMF](ember/boot/aicwf-sae.md)
  connects on 5 GHz and passes an 8 MiB transfer each way. Nonzero initial
  IGTK packet numbers are rejected. Protected SA Query and automatic recovery
  after an AP removes the station are physically checked; 802.11r/v and long-duration acceptance
  remain outside this result. The [board page](ember/boards/orange-pi-zero-3w.md#wi-fi-limits-and-connected-scan-regression)
  records the firmware, bundle and tests.
- **Short firmware replies** still need a command-transport fix and regression.

Changes to association and command handling should preserve the SAE and
WPA2 checks and account for the separate roaming work.

See [hardware support, build instructions, and limitations](README.ember.md)
for validation details and source provenance. Changes in this fork should
not be treated as changes accepted into upstream NetBSD.

This repository contains OS sources and hardware support. Applications and
device-specific configuration are added during deployment. Credentials and
personalized device images are not published in this repository.

The original NetBSD reference follows below. Links to official binary
releases refer to upstream NetBSD and do not include this fork's changes.

NetBSD
======

NetBSD is a free, fast, secure, and highly portable Unix-like Open
Source operating system.  It is available for a [wide range of
platforms](https://wiki.NetBSD.org/ports/), from large-scale servers
and powerful desktop systems to handheld and embedded devices.

Building
--------

You can cross-build NetBSD from most UNIX-like operating systems.
To build for amd64 (x86_64), in the src directory:

    ./build.sh -U -u -j4 -m amd64 -O ~/obj release

Additional build information available in the [BUILDING](BUILDING) file.

Binaries
--------

- [Daily builds](https://nycdn.NetBSD.org/pub/NetBSD-daily/HEAD/latest/)
- [Releases](https://cdn.NetBSD.org/pub/NetBSD/)

Testing
-------

On a running NetBSD system:

    cd /usr/tests; atf-run | atf-report

Troubleshooting
---------------

- Send bugs and patches [via web form](https://www.NetBSD.org/cgi-bin/sendpr.cgi?gndb=netbsd).
- Subscribe to the [mailing lists](https://www.NetBSD.org/mailinglists/).
  The [netbsd-users](https://www.NetBSD.org/mailinglists/#netbsd-users) list is a good choice for many problems; watch [current-users](https://www.NetBSD.org/mailinglists/#current-users) if you follow the bleeding edge of NetBSD-current.
- Join the community IRC channel [#netbsd @ libera.chat](https://web.libera.chat/#netbsd).

Latest sources
--------------

To fetch the main CVS repository:

    cvs -d anoncvs@anoncvs.NetBSD.org:/cvsroot checkout -P src

To work in the Git mirror, which is updated every few hours from CVS:

    git clone https://github.com/NetBSD/src.git

Additional Links
----------------

- [The NetBSD Guide](https://www.NetBSD.org/docs/guide/en/)
- [NetBSD manual pages](https://man.NetBSD.org/)
- [NetBSD Cross-Reference](https://nxr.NetBSD.org/)
