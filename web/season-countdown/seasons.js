/*
 * Equinox and solstice moments, computed in UTC.
 *
 * Algorithm: Jean Meeus, "Astronomical Algorithms" (2nd ed.), chapter 27.
 * Mean instants from the year-2000..3000 polynomials, corrected by the 24
 * periodic terms of table 27.C. Meeus quotes a worst-case error of ~51 s over
 * 1951-2050. The result is in Terrestrial Time; ΔT converts it to UTC.
 *
 * Shared by index.html (browser) and build-ics.mjs (Node).
 */
(function (root) {
  'use strict';

  var MIN_YEAR = 2000;
  var MAX_YEAR = 2200;

  // [A, B (deg), C (deg per Julian century)]
  var PERIODIC = [
    [485, 324.96, 1934.136], [203, 337.23, 32964.467], [199, 342.08, 20.186],
    [182, 27.85, 445267.112], [156, 73.14, 45036.886], [136, 171.52, 22518.443],
    [77, 222.54, 65928.934], [74, 296.72, 3034.906], [70, 243.58, 9037.513],
    [58, 119.81, 33718.147], [52, 297.17, 150.678], [50, 21.02, 2281.226],
    [45, 247.54, 29929.562], [44, 325.15, 31555.956], [29, 60.93, 4443.417],
    [18, 155.12, 67555.328], [17, 288.79, 4562.452], [16, 198.04, 62894.029],
    [14, 199.76, 31436.921], [12, 95.39, 14577.848], [12, 287.11, 31931.756],
    [12, 320.81, 34777.259], [9, 227.73, 1222.114], [8, 15.45, 16859.074]
  ];

  // Mean JDE polynomials in Y = (year - 2000) / 1000, table 27.B.
  var MEAN = [
    [2451623.80984, 365242.37404, 0.05169, -0.00411, -0.00057], // March equinox
    [2451716.56767, 365241.62603, 0.00325, 0.00888, -0.00030],  // June solstice
    [2451810.21715, 365242.01767, -0.11575, 0.00337, 0.00078],  // September equinox
    [2451900.05952, 365242.74049, -0.06223, -0.00823, 0.00032]  // December solstice
  ];

  // Index 0..3 matches MEAN. `north`/`south` are the astronomical seasons that
  // begin at that moment in each hemisphere.
  var EVENTS = [
    { key: 'march-equinox', name: 'March Equinox', north: 'spring', south: 'autumn' },
    { key: 'june-solstice', name: 'June Solstice', north: 'summer', south: 'winter' },
    { key: 'september-equinox', name: 'September Equinox', north: 'autumn', south: 'spring' },
    { key: 'december-solstice', name: 'December Solstice', north: 'winter', south: 'summer' }
  ];

  var DEG = Math.PI / 180;

  function polyval(c, x) {
    var r = 0;
    for (var i = c.length - 1; i >= 0; i--) r = r * x + c[i];
    return r;
  }

  // ΔT = TT - UT in seconds. Observed values have sat near 69 s since 2017;
  // beyond 2030 blend into the Espenak & Meeus 2050-2150 extrapolation.
  // Future ΔT is uncertain by tens of seconds, which is inside the
  // algorithm's own error budget.
  function deltaT(year) {
    function longRange(y) {
      var u = (y - 1820) / 100;
      return -20 + 32 * u * u - 0.5628 * (2150 - y);
    }
    if (year <= 2030) return 69.2;
    if (year <= 2050) return 69.2 + (longRange(2050) - 69.2) * (year - 2030) / 20;
    if (year <= 2150) return longRange(year);
    var u = (year - 1820) / 100;
    return -20 + 32 * u * u;
  }

  function jdeOf(year, index) {
    var jde0 = polyval(MEAN[index], (year - 2000) / 1000);
    var t = (jde0 - 2451545.0) / 36525;
    var w = (35999.373 * t - 2.47) * DEG;
    var dl = 1 + 0.0334 * Math.cos(w) + 0.0007 * Math.cos(2 * w);
    var s = 0;
    for (var i = 0; i < PERIODIC.length; i++) {
      var p = PERIODIC[i];
      s += p[0] * Math.cos((p[1] + p[2] * t) * DEG);
    }
    return jde0 + 0.00001 * s / dl;
  }

  // UTC Date of the given event (0 = March equinox .. 3 = December solstice).
  function momentOf(year, index) {
    if (year < MIN_YEAR || year > MAX_YEAR) {
      throw new RangeError('year must be within ' + MIN_YEAR + '..' + MAX_YEAR);
    }
    var jdTT = jdeOf(year, index);
    var ms = (jdTT - 2440587.5) * 86400000 - deltaT(year) * 1000;
    return new Date(Math.round(ms / 1000) * 1000);
  }

  // Every event within [fromYear, toYear], chronological.
  function eventsBetween(fromYear, toYear) {
    var out = [];
    for (var y = fromYear; y <= toYear; y++) {
      for (var i = 0; i < 4; i++) {
        var e = EVENTS[i];
        out.push({ year: y, index: i, key: e.key, name: e.name,
                   north: e.north, south: e.south, date: momentOf(y, i) });
      }
    }
    return out;
  }

  // The most recent event at or before `now`, and the next one after it.
  function around(now) {
    var y = now.getUTCFullYear();
    var list = eventsBetween(y - 1, y + 1);
    for (var i = 0; i < list.length; i++) {
      if (list[i].date > now) return { previous: list[i - 1], next: list[i], upcoming: list.slice(i) };
    }
    throw new Error('unreachable');
  }

  var api = { EVENTS: EVENTS, MIN_YEAR: MIN_YEAR, MAX_YEAR: MAX_YEAR,
              momentOf: momentOf, eventsBetween: eventsBetween, around: around,
              deltaT: deltaT };

  if (typeof module === 'object' && module.exports) module.exports = api;
  else root.Seasons = api;
})(this);
