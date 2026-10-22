# YS-M33 copied environment for a one-shot trial

[Cable write stage](ys-m33-a133-cable-stage.md) ·
[Persistent recovery](ys-m33-a133-recovery-protection.md) ·
[Backup and recovery](ys-m33-a133-backup-recovery.md)

`A133Recovery.trial` in the [existing environment preparer](../../a133-recovery-env.rb)
constructs private 128 KiB copies for one normal boot with recovery selected
before kernel handoff. It performs no host file creation, device I/O, unlock,
reboot or confirmation. It is not connected to the cable write coordinator.

## Input and outputs

```ruby
require_relative '../tools/a133-recovery-env'

# Use this tablet's freshly verified original prefix, not its protected env.
copies = A133Recovery.trial(original_env.byteslice(0, 131072))
trial_prefix = copies.fetch(:trial_env)
expected_rearmed_prefix = copies.fetch(:rearmed_env)
```

The input must have the same CRC32 and factory boot profile required by
`A133Recovery.protect`. Existing hook/recovery helpers are refused, including
empty values. Existing `ember_trial_rearm` or `ember_trial_save` is also refused.
Foreign scripts, invalid size/CRC, duplicate variables and insufficient capacity
stop preparation. The original is copied; the returned Hash and both byte strings
are frozen. Errors carry bounded reasons without input contents or nested causes.

Both outputs preserve bootcmd, boot/recovery scripts, unrelated and opaque values,
empty values, leading-empty-entry format and padding. Reversing the additions
and boot_normal change must restore every original byte. The original env tail
is outside this prefix-only API; a future guarded transition must retain and
verify the entire env partition, not only these outputs.

## Proposed boot sequence

The trial prefix adds the existing USB recovery helper and two single-command
helpers, then sets:

```text
boot_normal=run ember_trial_rearm ember_trial_save boot_android
ember_trial_rearm=setenv boot_normal run ember_recovery_once
ember_trial_save=saveenv
```

The first stage selects persistent recovery in RAM. The second saves that state.
Only a successful save permits the third stage to call the unchanged factory
boot_android script. A single `run` list aborts at a failing variable; commands
separated by semicolons can continue after a failure. This distinction is stated
in the [upstream command-line contract](https://docs.u-boot.org/en/latest/usage/cmdline.html#general-rules).

`rearmed_env` encodes the proposed saved state: boot_normal becomes
`run ember_recovery_once`; the two unused trial helpers remain. When the
inspected bootcmd reaches that saved boot_normal entry, it selects recovery
under the stated parser contract. This is a byte
prediction, not a readback or evidence of factory saveenv formatting/durability.
No assumption that a later reset automatically removes the helpers is made.

The existing [first-boot guard](../../../boot/a133-firstboot-guard.sh) requests a clean
reset after an unconfirmed running kernel. Its marker must be installed and
armed by a future guarded trial stage. Kernel hangs require the independently
verified watchdog path; generating these env copies does not establish either.
Normal-boot release must follow authenticated, release-bound confirmation in a
separate stage. Removing a marker alone must not certify installation readiness.

## Host validation and remaining gates

```sh
ruby ember/tools/a133-trial-env-test.rb
ruby ember/tools/a133-recovery-env-test.rb
ruby ember/tools/a133-recovery-protect-test.rb
ruby ember/tools/a133-unlock-env-test.rb
ruby ember/tools/a133-env-edit-test.rb
sh ember/tools/a133-firstboot-guard-test.sh
```

The new test independently encodes expected binary env copies and checks all
four padding/leading-entry variants, input preservation, reversal and refusals.
It does not execute or emulate U-Boot. Existing environment/first-boot guard
tests remain separate contracts. No shell model is presented as vendor execution.

All outputs carry `installation_ready: false`. Before a physical trial, verify
images and retained recovery, pin the release/device/evidence, persist trial
intent before any env write/reboot, verify full write readback, install a bound
confirmation marker and test rearm/save failure behavior on the actual loader.
Host interruption, unknown/partial env, failed save, cold power loss and lost
host connectivity need explicit recovery policies. The current cable stage
continues to accept only its original/protected states and never adopts these
new states automatically. Accepted first boot, normal-boot release, restoration
and fleet readiness remain unimplemented by this preparer.


The standalone image Client keeps its existing prefix policy and can accept a
rearmed prefix with inert helpers. The stronger exact original/protected env
restriction described above belongs to the composed A133Cable flow.

Calls require a trusted Ruby process and ordinary byte inputs. They do not isolate hostile in-process code.
