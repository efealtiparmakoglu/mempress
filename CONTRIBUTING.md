# Contributing to mempress

Thanks for your interest!

## Ground rules

- **Zero runtime dependencies.** Standard library only; helper code lives in
  the repo and is unit-tested.
- **Deterministic behavior.** Same input, same output — every time.
- **Clear errors.** Usage mistakes exit 2 with a precise message; runtime
  failures exit 1.

## Workflow

Run the project's test suite (see README) before every push.

## Pull requests

1. One feature or fix per PR.
2. New behavior needs tests.
3. Keep README and usage text in sync.

## Reporting bugs

Open an issue with the exact command line, minimal input, and the output
you got vs. expected.
