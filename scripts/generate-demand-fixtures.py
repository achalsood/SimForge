"""Deterministic descriptive fixtures, not random Poisson realizations.

Each declared hour is continuously observed, including hours without arrivals.
Events are spaced evenly inside the hour, never at the exposure end.
Training is Jan 5-6 UTC; validation is Jan 7 UTC. No inferred service data.
"""
from datetime import datetime, timedelta, timezone
from pathlib import Path
import json

root = Path(__file__).resolve().parents[1] / "examples" / "demand-calibration"
root.mkdir(exist_ok=True)
start = datetime(2026, 1, 5, tzinfo=timezone.utc)
for name in ("constant", "morning-surge", "sparse", "distribution-shift"):
    arrivals = []
    for day in range(3):
        for hour in range(24):
            if name == "constant":
                count = 4
            elif name == "morning-surge":
                count = 30 if 8 <= hour < 12 else 6
            elif name == "sparse":
                count = int(hour == 8)
            else:
                count = 6 if day < 2 else 18
            for index in range(count):
                time = start + timedelta(days=day, hours=hour, seconds=(index * 3600 // count))
                arrivals.append(time.strftime("%Y-%m-%dT%H:%M:%SZ"))
    (root / f"{name}.csv").write_text("timestamp\n" + "\n".join(arrivals) + "\n", encoding="ascii")
    request = dict(calibration_request_version=1, csv=f"{name}.csv", timestamp_format="iso8601_utc_seconds",
                   observation_start="2026-01-05T00:00:00Z", observation_end="2026-01-08T00:00:00Z",
                   continuous=True, split="2026-01-07T00:00:00Z",
                   model="daily_profile" if name == "morning-surge" else "constant",
                   daily_bin_seconds=3600, diagnostic_bin_seconds=3600)
    (root / f"{name}.json").write_text(json.dumps(request, indent=2) + "\n", encoding="ascii")
(root / "malformed.csv").write_text("timestamp\n2026-02-30T00:00:00Z\n", encoding="ascii")
