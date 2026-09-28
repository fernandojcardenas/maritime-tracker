# Data sources

All input is public AIS data. No hardware and no paid service is required.

| Source | What it is | Access | Licence |
|---|---|---|---|
| Norwegian Coastal Administration (Kystverket) open AIS feed | Live raw NMEA sentences with IEC 62320-1 tag blocks, streamed over TCP | `153.44.253.27:5631`, no registration | Norwegian Licence for Open Government Data (NLOD) |
| Danish historical AIS data | Decoded AIS positions and static data as daily zipped CSV files | aisdata.ais.dk, free | Published free of charge by the Danish authorities; see their site for terms |

Notes:

- The Norwegian open feed excludes fishing vessels under 15 m and recreational
  craft under 45 m (Kystverket, checked 2026-09-28).
- The Danish files are used offline for repeatable evaluation of the tracker
  and anomaly rules (milestones M3–M4).
- Everything in this repository comes from these public sources. No data from
  any government or military system is used.

Sources: [Kystverket, access to AIS data](https://www.kystverket.no/en/sea-transport-and-ports/ais/access-to-ais-data/),
[Danish Maritime Authority, AIS data](https://www.dma.dk/safety-at-sea/navigational-information/ais-data).
