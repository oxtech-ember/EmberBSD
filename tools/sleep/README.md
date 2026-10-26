# Native sleep portability

Origin: EmberBSD contributors (AI-assisted).

The host recipe uses the canonical [sleep](../../bin/sleep/sleep.c), common
[compatibility](../Makefile.host), and its normal `-lm` dependency.
The Darwin helper uses public clock, nanosleep, and extended locale APIs.
It requires the accounted macOS clock_gettime ABI, available since macOS 10.12.
Other native hosts retain clock_nanosleep and LC_C_LOCALE.

Darwin computes each relative wait from the original CLOCK_MONOTONIC deadline.
It does not reuse nanosleep's remainder after report handling.
The helper returns zero or an error number, as clock_nanosleep does.
SIGINFO still requests a report. SIGALRM still exits successfully.
The normal error path still rejects other interrupted waits.
The deadline addition rejects seconds and nanosecond-carry overflow before addition.

Darwin creates a C numeric locale only when the current-locale parse is incomplete.
Both locale creation and release are checked. Conversion errno is preserved on success.
The locale object does not change the process locale.

Run source/data checks with the explicitly selected SDK Python:

```sh
python3 -B tools/sleep/test-source.py
python3 -B tools/sleep/test-conversion-source.py
python3 -B tools/sleep/test-bootstrap.py
```

These checks execute no C. They do not establish native timing or signal behavior.
The existing [ATF cases](../../tests/bin/sleep/t_sleep.sh) remain unchanged.
Nonfinite durations and floating values beyond time_t are rejected before conversion.
The [canonical initial contract](bootstrap.json) contains the conversion and CLI cases.
It replaces the separate conversion input list. These are planned checks, not completed native results.

## Initial native profile

The SDK `sleep-bootstrap` profile compiles the complete canonical C file and Darwin helper.
It uses the selected native GCC16, common annotation header, accounted host ABI and `-lm`.
The normal host recipe remains unchanged and keeps its compatibility library.
The 21 cases produce 24 commands, including one separate runtime observation of `sleep 0`.
Eight successful cases cover zero, fractional, scientific, hexadecimal and underflow conversion.
Thirteen negative cases have exact exit and stderr bytes in the C locale.
No system sleep, private C implementation or arbitrary process hook is used.

Only the `0.5` case requests an elapsed observation from 500000000 through 5000000000 nanoseconds.
The common runner measures the whole owned command call with its monotonic clock.
This includes startup, the selected shell pipeline, redirections, process drainage and log completion.
It excludes prior source validation, context-lock acquisition and later output verification.
The polling timeout is at most five seconds; startup and drainage can take longer.
An observation outside the interval fails after drainage. This is not a hard wall-clock kill deadline.
Scheduler and process-control overhead are included. The interval does not prove nanosleep accuracy
or prove that its internal wait lasted 0.5 seconds.
Retained evidence binds the exact case, start, end, difference and measurement scope.
Untimed cases have no elapsed record.

This finite profile is intended for initial configure consumers, including libarchive.
Signal behavior, alternate locales, injected failures, C analysis, memory and ordinary qualification
remain separate checks. Source/data tests do not establish any native result.

## Bounded native acceptance plan

Use a separate pinned SDK GCC16 producer, its owned child process, and full lifecycle drainage.
Keep host compatibility inputs, ABI, `-lm`, runtime closure, analysis, and memory evidence explicit.
No native acceptance or completed producer is provided by this source change.

| Group | Required observations |
|---|---|
| Parsing | Zero, fractional, scientific and hexadecimal inputs; existing malformed, negative, missing and extra arguments |
| Locale | C locale, an independently verified comma-radix locale, and C-radix fallback within that locale |
| Deadline | Zero and expired intervals, nanosecond borrow/carry, seconds overflow and carry overflow |
| Errors | Clock failure, nanosleep failure, locale allocation failure and locale release failure |
| Timing | The initial profile observes the whole owned 0.5-second command within 0.5–5 seconds; precise internal timing remains separate |
| SIGINFO | A 2-second request receives SIGINFO after 0.25 seconds; it reports progress and retains the original deadline |
| SIGALRM | A 4-second request receives SIGALRM after 0.25 seconds; it exits zero before the original deadline |

Record spawn, attempted signal delivery, observed output and exit using the same monotonic clock.
Signal cases need an SDK process-owner operation bound to its own leader identity.
That operation must take no caller-supplied PID and must revalidate the owned lifetime.
Only named SIGINFO and SIGALRM events are needed. No shell hooks or process-name lookup is needed.
Unsupported signal delivery is an explicit missing capability, not a skipped successful case.
An elapsed delay alone does not prove that handlers are installed.
SIGINFO output can confirm a handler invocation; early delivery races must remain visible as failures.
Do not claim precise delivery time from a requested schedule or impose a hard scheduling latency guarantee.
Native signal coverage is pending a reviewed owner API, startup treatment, and actual observations.

Injected syscall failures and handler-delay/remainder tests need a separate diagnostic fixture.
They must exercise the actual helper, not a second implementation of sleep.
The fixture is not a production executable or ordinary capability proof.
