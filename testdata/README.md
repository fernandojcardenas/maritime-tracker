# Test data

## pyais-nmea-sample.nmea

85,194 lines of recorded AIS NMEA sentences (`!AIVDM`), about 4.2 MB, covering
11,075 vessels (MMSIs) worldwide, mostly European waters.

- Source: `tests/nmea-sample` in [pyais](https://github.com/M0r13n/pyais),
  copied unmodified at commit `4e4950ac726db26937202f9d51abbb09203c2c8c`
  (2026-09-27). SHA-256
  `af1aebe3bb400500c4aa7b5298dae57bde468f927fca4bb3b6c9f8990589cd5d`.
- Licence: MIT, copyright (c) 2019 M0r13n. See [LICENSE.pyais](LICENSE.pyais).
- The file has been in pyais since 2020. pyais does not document which
  receiver or network recorded it.

It is used by:

- `tests/capture_test.cpp`: exact decoder counts on real input, including the
  malformed sentences it contains;
- `tools/crosscheck.py` in CI: field-by-field comparison with pyais;
- the CI fuzz job: the first 2,000 lines seed the stream-decoder fuzzer.

Live data from the Norwegian Coastal Administration feed (see
[docs/data-sources.md](../docs/data-sources.md)) is the input for milestone M2.
