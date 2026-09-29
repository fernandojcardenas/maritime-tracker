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

## barentswatch-live-2026-09-29.jsonl

5,000 consecutive records (13:59–14:01 UTC, 3,036 vessels) from the one-hour
live run of 2026-09-29, recorded from the BarentsWatch Live AIS API exactly as
received, one JSON object per line. SHA-256
`5eee807193ee58cba7a36726082e4cb33a15e69939f478b7a8348cfe49366151`.

Contains data under the Norwegian licence for Open Government data (NLOD),
made available by the Norwegian Coastal Administration (Kystverket) via
BarentsWatch. The open data excludes fishing vessels under 15 m and leisure
craft under 45 m.

Used by `tests/barentswatch_test.cpp` (parser and tracker counts on live data)
and `tests/anomaly_test.cpp` (the anomaly rules on live data).
