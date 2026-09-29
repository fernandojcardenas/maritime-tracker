# Data sources

All input is public AIS data. No hardware and no paid service is required.

| Source | What it is | Access | Licence |
|---|---|---|---|
| BarentsWatch Live AIS API (Norway) | The same Norwegian open AIS data, decoded, one JSON record per line over HTTPS | `live.ais.barentswatch.no`, free account and API client (see below); `tools/barentswatch_stream.sh` | NLOD |
| Norwegian Coastal Administration (Kystverket) open AIS feed | Live raw NMEA sentences with IEC 62320-1 tag blocks, streamed over TCP | `153.44.253.27:5631`, no registration. It did not accept connections from two US networks (a home connection and a GitHub Actions runner) on 2026-09-28/29 | NLOD |
| Danish historical AIS data | Decoded AIS positions and static data as daily zipped CSV files | aisdata.ais.dk, free; fetched by `tools/fetch_dk_slice.sh` with a SHA-256 check | Published free of charge by the Danish authorities. Their site states no redistribution terms, so the data is downloaded, not committed |

## BarentsWatch credentials

1. Create a free user at barentswatch.no and, under your user's page, create
   an API client. It gives a client ID and a client secret.
2. For local runs, export them as `BW_CLIENT_ID` and `BW_CLIENT_SECRET`.
   For the `Live feed run` workflow, add them as repository secrets with those
   names (Settings > Secrets and variables > Actions).

The stream script requests a token with scope `ais`, reads the secret only
from the environment, and never prints it or passes it on a command line.

Notes:

- The Norwegian open feed excludes fishing vessels under 15 m and recreational
  craft under 45 m (Kystverket, checked 2026-09-28).
- The Danish files are used offline for repeatable evaluation of the tracker
  and anomaly rules (milestones M3–M4). The M3 evaluation uses 2026-04-22.
- Everything in this repository comes from these public sources. No data from
  any government or military system is used.

Sources: [Kystverket, access to AIS data](https://www.kystverket.no/en/sea-transport-and-ports/ais/access-to-ais-data/),
[Danish Maritime Authority, AIS data](https://www.dma.dk/safety-at-sea/navigational-information/ais-data).
