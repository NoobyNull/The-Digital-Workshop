// Regenerates the subscribable calendar feeds next to this script:
//   node build-ics.mjs
// Files use a .txt extension because the mini-app host only serves an
// allowlist of web file types; calendar clients go by content, not extension.
// Feeds are static, so they cover a span (FIRST_YEAR..LAST_YEAR) and ask
// clients to re-poll weekly; UIDs are stable so re-publishing never duplicates.
import { createRequire } from 'node:module';
import { writeFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const Seasons = createRequire(import.meta.url)('./seasons.js');
const here = dirname(fileURLToPath(import.meta.url));

const FIRST_YEAR = 2025;
const LAST_YEAR = 2050;
// Fixed so output is deterministic; bump when the algorithm changes.
const DTSTAMP = '20260923T000000Z';

const cap = (s) => s[0].toUpperCase() + s.slice(1);

const FEEDS = [
  {
    id: 'north',
    file: 'seasons-north.txt',
    calName: 'Seasons (Northern Hemisphere)',
    summary: (e) => `${cap(e.north)} begins · ${e.name}`,
  },
  {
    id: 'south',
    file: 'seasons-south.txt',
    calName: 'Seasons (Southern Hemisphere)',
    summary: (e) => `${cap(e.south)} begins · ${e.name}`,
  },
];

const stamp = (d) => d.toISOString().replace(/[-:]/g, '').replace(/\.\d{3}/, '');
const escape = (s) => s.replace(/\\/g, '\\\\').replace(/;/g, '\;').replace(/,/g, '\\,').replace(/\n/g, '\\n');

// RFC 5545 §3.1: fold lines longer than 75 octets.
function fold(line) {
  const bytes = Buffer.from(line, 'utf8');
  if (bytes.length <= 75) return line;
  const parts = [];
  let chunk = '';
  let size = 0;
  for (const ch of line) {
    const n = Buffer.byteLength(ch, 'utf8');
    if (size + n > (parts.length ? 74 : 75)) {
      parts.push(chunk);
      chunk = '';
      size = 0;
    }
    chunk += ch;
    size += n;
  }
  parts.push(chunk);
  return parts.join('\r\n ');
}

function build(feed) {
  const lines = [
    'BEGIN:VCALENDAR',
    'VERSION:2.0',
    'PRODID:-//The Digital Workshop//Season Countdown//EN',
    'CALSCALE:GREGORIAN',
    'METHOD:PUBLISH',
    `X-WR-CALNAME:${escape(feed.calName)}`,
    'X-WR-CALDESC:Exact moments of each equinox and solstice (accurate to about a minute).',
    'REFRESH-INTERVAL;VALUE=DURATION:P1W',
    'X-PUBLISHED-TTL:P1W',
  ];
  for (const e of Seasons.eventsBetween(FIRST_YEAR, LAST_YEAR)) {
    const at = stamp(e.date);
    lines.push(
      'BEGIN:VEVENT',
      `UID:${e.year}-${e.key}-${feed.id}@season-countdown`,
      `DTSTAMP:${DTSTAMP}`,
      `DTSTART:${at}`,
      `DTEND:${at}`,
      `SUMMARY:${escape(feed.summary(e))}`,
      'TRANSP:TRANSPARENT',
      'END:VEVENT',
    );
  }
  lines.push('END:VCALENDAR');
  return lines.map(fold).join('\r\n') + '\r\n';
}

for (const feed of FEEDS) {
  writeFileSync(join(here, feed.file), build(feed));
  console.log(`wrote ${feed.file}`);
}
