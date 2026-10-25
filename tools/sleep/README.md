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
```

These checks execute no C. They do not establish native timing or signal behavior.
The existing [ATF cases](../../tests/bin/sleep/t_sleep.sh) remain unchanged.
Nonfinite durations and floating values beyond time_t are rejected before conversion.
The [conversion inputs](conversion-cases.json) cover invalid and short valid values.
They are future native test inputs, not a completed runtime test or SDK execution schema.

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
| Timing | A 0.5-second request must not finish before 0.5 seconds; a 5-second watchdog bounds the observation |
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
