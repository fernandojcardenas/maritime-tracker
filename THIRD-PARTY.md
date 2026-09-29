# Third-party material

Everything else in this repository is MIT licensed (see [LICENSE](LICENSE)).

| What | Where | Licence | Notes |
|---|---|---|---|
| Leaflet 1.9.4 | `web/vendor/leaflet/` (built into `mt-ingest`) | BSD 2-Clause, copyright (c) 2010-2023 Volodymyr Agafonkin, (c) 2010-2011 CloudMade; see [its LICENSE](web/vendor/leaflet/LICENSE) | From the npm package, checked by SHA-256; provenance in [README](web/vendor/leaflet/README.md) |
| Natural Earth land polygons | `web/land.json` (built into `mt-ingest`) | Public domain ([terms](https://www.naturalearthdata.com/about/terms-of-use/)) | Made with Natural Earth. Built by `tools/make_land.py` from a pinned commit, inputs checked by SHA-256 |
| pyais NMEA sample | `testdata/pyais-nmea-sample.nmea` | MIT, copyright (c) 2019 M0r13n; see [LICENSE.pyais](testdata/LICENSE.pyais) | Test data only; see [testdata/README.md](testdata/README.md) |
| BarentsWatch Live AIS records | `testdata/barentswatch-live-2026-09-29.jsonl` (also in the Docker image) | Norwegian licence for Open Government data (NLOD) | Norwegian Coastal Administration via BarentsWatch |
| nlohmann/json, GoogleTest | downloaded at configure time, not stored here | MIT; BSD 3-Clause | nlohmann/json is compiled into the binaries |
