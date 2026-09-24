# Season Countdown

A static mini app that counts down to the exact moment of the next equinox or
solstice and offers a subscribable calendar feed.

| File | Purpose |
| --- | --- |
| `index.html` | The countdown page (hemisphere toggle, upcoming list, subscribe links). |
| `seasons.js` | Equinox/solstice math (Meeus ch. 27 + ΔT), shared by the page and the build script. |
| `build-ics.mjs` | Regenerates the iCalendar feeds: `node build-ics.mjs`. |
| `seasons-north.txt`, `seasons-south.txt` | Generated iCalendar feeds, named for each hemisphere's seasons. |

The page computes everything in the browser, so it never goes stale. The feeds
are static files covering `FIRST_YEAR..LAST_YEAR` in `build-ics.mjs`; to extend
them, raise `LAST_YEAR`, rebuild, and republish. Event UIDs are stable, so
subscribers see the new years appear without duplicates.

Times are accurate to within about a minute (checked against USNO values for
2024–2027).

## Publishing

Deploy the five files above as a static site. The subscribe links are built
from the page's own URL, so nothing is hard-coded to a host. Feeds use a `.txt`
extension because the mini-app host only allows common web file types;
calendar apps parse the content regardless of extension.
