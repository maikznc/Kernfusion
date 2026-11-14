/* =============================================================================
   NOVA Kernfusion – DSP
   Läuft an drei Stellen mit identischem Code:
     1. als AudioWorklet (Echtzeit beim Abhören),
     2. im OfflineAudioContext (Assistent, Messen, Export),
     3. im Hauptfenster (EQ-Kurven zeichnen, Lautheit messen).
   Kette: Eingang → Pre-EQ → Multiband-Dynamik → Exciter → Imager → Post-EQ
          → Maximizer (True-Peak-Limiter mit Lookahead) → Ausgang
   ============================================================================= */
(function (root) {
  "use strict";

  const TAU = Math.PI * 2;
  const dbToGain = (db) => Math.pow(10, db / 20);
  const gainToDb = (g) => 20 * Math.log10(Math.max(g, 1e-12));
  const clamp = (v, lo, hi) => (v < lo ? lo : v > hi ? hi : v);

  // ---------------------------------------------------------------- Biquad (RBJ)
  function biquadCoeffs(type, fs, f, q, gainDb) {
    f = clamp(f, 10, fs * 0.49);
    const w0 = TAU * f / fs, cw = Math.cos(w0), sw = Math.sin(w0);
    const A = Math.pow(10, (gainDb || 0) / 40);
    const al = sw / (2 * (q || 0.7071));
    let b0, b1, b2, a0, a1, a2;
    switch (type) {
      case "lowpass":  b0 = (1 - cw) / 2; b1 = 1 - cw; b2 = (1 - cw) / 2; a0 = 1 + al; a1 = -2 * cw; a2 = 1 - al; break;
      case "highpass": b0 = (1 + cw) / 2; b1 = -(1 + cw); b2 = (1 + cw) / 2; a0 = 1 + al; a1 = -2 * cw; a2 = 1 - al; break;
      case "allpass":  b0 = 1 - al; b1 = -2 * cw; b2 = 1 + al; a0 = 1 + al; a1 = -2 * cw; a2 = 1 - al; break;
      case "peak":     b0 = 1 + al * A; b1 = -2 * cw; b2 = 1 - al * A; a0 = 1 + al / A; a1 = -2 * cw; a2 = 1 - al / A; break;
      case "lowshelf": {
        const s = 2 * Math.sqrt(A) * al;
        b0 = A * ((A + 1) - (A - 1) * cw + s); b1 = 2 * A * ((A - 1) - (A + 1) * cw); b2 = A * ((A + 1) - (A - 1) * cw - s);
        a0 = (A + 1) + (A - 1) * cw + s; a1 = -2 * ((A - 1) + (A + 1) * cw); a2 = (A + 1) + (A - 1) * cw - s; break;
      }
      case "highshelf": {
        const s = 2 * Math.sqrt(A) * al;
        b0 = A * ((A + 1) + (A - 1) * cw + s); b1 = -2 * A * ((A - 1) + (A + 1) * cw); b2 = A * ((A + 1) + (A - 1) * cw - s);
        a0 = (A + 1) - (A - 1) * cw + s; a1 = 2 * ((A - 1) - (A + 1) * cw); a2 = (A + 1) - (A - 1) * cw - s; break;
      }
      default: return [1, 0, 0, 0, 0];
    }
    return [b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0];
  }

  /** Betrag eines Biquads bei Frequenz f (für EQ-Kurven in der Oberfläche). */
  function biquadMagnitude(c, fs, f) {
    const w = TAU * f / fs, c1 = Math.cos(w), s1 = Math.sin(w), c2 = Math.cos(2 * w), s2 = Math.sin(2 * w);
    const nr = c[0] + c[1] * c1 + c[2] * c2, ni = -(c[1] * s1 + c[2] * s2);
    const dr = 1 + c[3] * c1 + c[4] * c2, di = -(c[3] * s1 + c[4] * s2);
    return Math.sqrt((nr * nr + ni * ni) / (dr * dr + di * di));
  }

  /** Stereo-Biquad, Transposed Direct Form II. */
  class Biquad {
    constructor(c) { this.set(c || [1, 0, 0, 0, 0]); this.reset(); }
    set(c) { this.b0 = c[0]; this.b1 = c[1]; this.b2 = c[2]; this.a1 = c[3]; this.a2 = c[4]; }
    reset() { this.l1 = this.l2 = this.r1 = this.r2 = 0; }
    L(x) { const y = this.b0 * x + this.l1; this.l1 = this.b1 * x - this.a1 * y + this.l2; this.l2 = this.b2 * x - this.a2 * y; return y; }
    R(x) { const y = this.b0 * x + this.r1; this.r1 = this.b1 * x - this.a1 * y + this.r2; this.r2 = this.b2 * x - this.a2 * y; return y; }
    flush() {   // sehr kleine Zustände auf 0 (vermeidet langsame Subnormal-Rechnung nach Stille)
      if (Math.abs(this.l1) < 1e-25) this.l1 = 0; if (Math.abs(this.l2) < 1e-25) this.l2 = 0;
      if (Math.abs(this.r1) < 1e-25) this.r1 = 0; if (Math.abs(this.r2) < 1e-25) this.r2 = 0;
    }
  }

  // ---------------------------------------------------------------- EQ (5 Bänder)
  const EQ_DEFAULT = () => ({
    on: true,
    bands: [
      { type: "highpass", on: false, freq: 25, q: 0.71, gain: 0 },
      { type: "lowshelf", on: true, freq: 100, q: 0.71, gain: 0 },
      { type: "peak", on: true, freq: 350, q: 0.9, gain: 0 },
      { type: "peak", on: true, freq: 2500, q: 0.9, gain: 0 },
      { type: "highshelf", on: true, freq: 10000, q: 0.71, gain: 0 },
    ],
  });

  function eqBandActive(b) {
    return b.on && (b.type === "highpass" || b.type === "lowpass" || Math.abs(b.gain) > 0.01);
  }

  class EQ {
    constructor() { this.filters = []; this.active = []; }
    set(p, fs) {
      this.on = !!p.on;
      this.filters.length = p.bands.length;
      this.active = [];
      p.bands.forEach((b, i) => {
        const c = biquadCoeffs(b.type, fs, b.freq, b.q, b.gain);
        if (!this.filters[i]) this.filters[i] = new Biquad(c); else this.filters[i].set(c);
        if (eqBandActive(b)) this.active.push(this.filters[i]);
      });
    }
    L(x) { if (!this.on) return x; for (let i = 0; i < this.active.length; i++) x = this.active[i].L(x); return x; }
    R(x) { if (!this.on) return x; for (let i = 0; i < this.active.length; i++) x = this.active[i].R(x); return x; }
    flush() { for (const f of this.filters) f && f.flush(); }
  }

  /** Gesamtkurve eines EQs in dB (für die Anzeige). */
  function eqResponseDb(p, fs, freqs) {
    const cs = p.bands.filter(eqBandActive).map((b) => biquadCoeffs(b.type, fs, b.freq, b.q, b.gain));
    return freqs.map((f) => { let m = 1; for (const c of cs) m *= biquadMagnitude(c, fs, f); return gainToDb(m); });
  }

  // ---------------------------------------------------------------- 3-Band-Weiche (Linkwitz-Riley 4. Ordnung)
  class Crossover3 {
    constructor() {
      this.lp1a = new Biquad(); this.lp1b = new Biquad(); this.hp1a = new Biquad(); this.hp1b = new Biquad();
      this.lp2a = new Biquad(); this.lp2b = new Biquad(); this.hp2a = new Biquad(); this.hp2b = new Biquad();
      this.ap2 = new Biquad();   // Phasenausgleich des Bass-Bands an der oberen Trennfrequenz
      this.out = new Float64Array(3);
    }
    set(fs, f1, f2) {
      const q = Math.SQRT1_2;
      const lp1 = biquadCoeffs("lowpass", fs, f1, q), hp1 = biquadCoeffs("highpass", fs, f1, q);
      const lp2 = biquadCoeffs("lowpass", fs, f2, q), hp2 = biquadCoeffs("highpass", fs, f2, q);
      this.lp1a.set(lp1); this.lp1b.set(lp1); this.hp1a.set(hp1); this.hp1b.set(hp1);
      this.lp2a.set(lp2); this.lp2b.set(lp2); this.hp2a.set(hp2); this.hp2b.set(hp2);
      this.ap2.set(biquadCoeffs("allpass", fs, f2, q));
    }
    L(x) {
      const o = this.out;
      o[0] = this.ap2.L(this.lp1b.L(this.lp1a.L(x)));
      const rest = this.hp1b.L(this.hp1a.L(x));
      o[1] = this.lp2b.L(this.lp2a.L(rest));
      o[2] = this.hp2b.L(this.hp2a.L(rest));
      return o;
    }
    R(x) {
      const o = this.out;
      o[0] = this.ap2.R(this.lp1b.R(this.lp1a.R(x)));
      const rest = this.hp1b.R(this.hp1a.R(x));
      o[1] = this.lp2b.R(this.lp2a.R(rest));
      o[2] = this.hp2b.R(this.hp2a.R(rest));
      return o;
    }
    flush() { for (const k of ["lp1a", "lp1b", "hp1a", "hp1b", "lp2a", "lp2b", "hp2a", "hp2b", "ap2"]) this[k].flush(); }
  }

  // ---------------------------------------------------------------- Multiband-Kompressor
  const DYN_DEFAULT = () => ({
    on: true, xover: [120, 2500], attack: 25, release: 160, amount: 1,
    bands: [
      { threshold: 0, ratio: 1.5, makeup: 0 },
      { threshold: 0, ratio: 1.5, makeup: 0 },
      { threshold: 0, ratio: 1.5, makeup: 0 },
    ],
  });

  class BandCompressor {
    constructor() { this.ms = 0; this.gr = 0; this.grMax = 0; }
    set(fs, b, attackMs, releaseMs, amount) {
      this.thr = b.threshold; this.ratio = Math.max(1, b.ratio); this.makeup = b.makeup; this.knee = 6;
      this.amount = amount === undefined ? 1 : clamp(amount, 0, 2);   // Intensität (Punch-Regler)
      this.aRms = 1 - Math.exp(-1 / (fs * 0.010));                  // 10-ms-RMS-Fenster
      this.aAtt = Math.exp(-1 / (fs * attackMs / 1000));
      this.aRel = Math.exp(-1 / (fs * releaseMs / 1000));
    }
    /** Stereo-gekoppelt: RMS-Pegel → Kennlinie (Soft Knee) → Attack/Release auf die Reduktion. */
    gain(l, r) {
      const p = Math.max(l * l, r * r);
      this.ms += this.aRms * (p - this.ms);
      const lvl = 10 * Math.log10(this.ms + 1e-20);
      const over = lvl - this.thr, k = this.knee, slope = (1 / this.ratio - 1) * this.amount;
      let target = 0;
      if (over > k / 2) target = slope * over;
      else if (over > -k / 2) target = slope * (over + k / 2) * (over + k / 2) / (2 * k);
      // target ≤ 0: mehr Reduktion → Attack, weniger → Release
      this.gr = target < this.gr ? this.aAtt * this.gr + (1 - this.aAtt) * target
                                 : this.aRel * this.gr + (1 - this.aRel) * target;
      if (-this.gr > this.grMax) this.grMax = -this.gr;
      return dbToGain(this.gr + this.makeup);
    }
  }

  class Dynamics {
    constructor() { this.xo = new Crossover3(); this.comps = [new BandCompressor(), new BandCompressor(), new BandCompressor()]; }
    set(p, fs) {
      this.on = !!p.on;
      this.xo.set(fs, p.xover[0], p.xover[1]);
      p.bands.forEach((b, i) => this.comps[i].set(fs, b, p.attack, p.release, p.amount));
    }
    process(l, r, out) {
      if (!this.on) { out[0] = l; out[1] = r; return; }
      const bl = this.xo.L(l), b0 = bl[0], b1 = bl[1], b2 = bl[2];
      const br = this.xo.R(r), c0 = br[0], c1 = br[1], c2 = br[2];
      const g0 = this.comps[0].gain(b0, c0), g1 = this.comps[1].gain(b1, c1), g2 = this.comps[2].gain(b2, c2);
      out[0] = b0 * g0 + b1 * g1 + b2 * g2;
      out[1] = c0 * g0 + c1 * g1 + c2 * g2;
    }
    flush() { this.xo.flush(); }
  }

  // ---------------------------------------------------------------- Exciter (Oberton-Erzeugung je Band)
  const EXC_DEFAULT = () => ({ on: true, mode: "tape", xover: [250, 4000], bands: [{ amount: 0 }, { amount: 0 }, { amount: 0 }] });

  const SATURATORS = {
    // Band-Ton → Obertöne. Bei kleinen Pegeln ≈ linear, darum normiert auf den Eingang.
    tape: (x) => Math.tanh(x),                                   // ungerade Obertöne, weich
    tube: (x) => Math.tanh(x + 0.25 * x * x),                    // gerade + ungerade Obertöne
    warm: (x) => x / (1 + Math.abs(x)) + 0.12 * x * x,           // betont 2. Oberton
  };

  class Exciter {
    constructor() { this.xo = new Crossover3(); this.dcL = 0; this.dcR = 0; this.dcX = [0, 0]; }
    set(p, fs) {
      this.on = !!p.on && p.bands.some((b) => b.amount > 0.001);
      this.xo.set(fs, p.xover[0], p.xover[1]);
      this.sat = SATURATORS[p.mode] || SATURATORS.tape;
      this.mix = p.bands.map((b) => clamp(b.amount, 0, 1));
      this.drive = p.bands.map((b) => 1.5 + 10 * clamp(b.amount, 0, 1));
      this.dcCoef = Math.exp(-TAU * 12 / fs);
    }
    band(x, i) {
      const m = this.mix[i];
      if (m <= 0.001) return x;
      const d = this.drive[i];
      return x + m * (this.sat(x * d) / d - x);
    }
    process(l, r, out) {
      if (!this.on) { out[0] = l; out[1] = r; return; }
      const bl = this.xo.L(l);
      let yl = this.band(bl[0], 0) + this.band(bl[1], 1) + this.band(bl[2], 2);
      const br = this.xo.R(r);
      let yr = this.band(br[0], 0) + this.band(br[1], 1) + this.band(br[2], 2);
      // Gleichanteil aus geraden Obertönen entfernen (Hochpass 12 Hz)
      const c = this.dcCoef;
      const ol = yl - this.dcX[0] + c * this.dcL; this.dcX[0] = yl; this.dcL = ol;
      const or = yr - this.dcX[1] + c * this.dcR; this.dcX[1] = yr; this.dcR = or;
      out[0] = ol; out[1] = or;
    }
    flush() { this.xo.flush(); if (Math.abs(this.dcL) < 1e-25) this.dcL = 0; if (Math.abs(this.dcR) < 1e-25) this.dcR = 0; }
  }

  // ---------------------------------------------------------------- Imager (Stereobreite je Band)
  const IMG_DEFAULT = () => ({ on: true, xover: [150, 5000], bands: [{ width: 1 }, { width: 1 }, { width: 1 }] });

  class Imager {
    constructor() { this.xo = new Crossover3(); this.w = [1, 1, 1]; }
    set(p, fs) {
      this.on = !!p.on && p.bands.some((b) => Math.abs(b.width - 1) > 0.001);
      this.xo.set(fs, p.xover[0], p.xover[1]);
      this.w = p.bands.map((b) => clamp(b.width, 0, 2));
    }
    process(l, r, out) {
      if (!this.on) { out[0] = l; out[1] = r; return; }
      const bl = this.xo.L(l), a0 = bl[0], a1 = bl[1], a2 = bl[2];
      const br = this.xo.R(r), c0 = br[0], c1 = br[1], c2 = br[2];
      let yl = 0, yr = 0;
      const w = this.w;
      for (let i = 0; i < 3; i++) {
        const L = i === 0 ? a0 : i === 1 ? a1 : a2, R = i === 0 ? c0 : i === 1 ? c1 : c2;
        const m = 0.5 * (L + R), s = 0.5 * (L - R) * w[i];
        yl += m + s; yr += m - s;
      }
      out[0] = yl; out[1] = yr;
    }
    flush() { this.xo.flush(); }
  }

  // ---------------------------------------------------------------- True-Peak (4x Interpolation, wie BS.1770 Anhang 2)
  function makeTruePeakFilter() {
    const phases = 4, taps = 12, N = phases * taps;   // 48-Tap-Fenster-Sinc
    const h = new Float64Array(N);
    for (let n = 0; n < N; n++) {
      const t = (n - (N - 1) / 2) / phases;
      const sinc = Math.abs(t) < 1e-9 ? 1 : Math.sin(Math.PI * t) / (Math.PI * t);
      const w = 0.42 - 0.5 * Math.cos(TAU * n / (N - 1)) + 0.08 * Math.cos(2 * TAU * n / (N - 1));
      h[n] = sinc * w;
    }
    const poly = [];
    for (let p = 0; p < phases; p++) {
      const c = new Float64Array(taps);
      for (let k = 0; k < taps; k++) c[k] = h[k * phases + p];
      poly.push(c);
    }
    return { poly, taps, phases, delay: Math.round((N - 1) / 2 / phases) };
  }
  const TP = makeTruePeakFilter();

  class TruePeakDetector {
    constructor() { this.buf = new Float64Array(TP.taps); this.pos = 0; }
    /** Höchster Betrag der 4 Zwischenwerte zur aktuellen (verzögerten) Position. */
    push(x) {
      const b = this.buf, n = TP.taps;
      b[this.pos] = x;
      let peak = 0;
      for (let p = 0; p < TP.phases; p++) {
        const c = TP.poly[p];
        let s = 0, j = this.pos;
        for (let k = 0; k < n; k++) { s += c[k] * b[j]; j = j === 0 ? n - 1 : j - 1; }
        const a = s < 0 ? -s : s;
        if (a > peak) peak = a;
      }
      this.pos = this.pos + 1 === n ? 0 : this.pos + 1;
      return peak;
    }
  }

  // ---------------------------------------------------------------- Maximizer (Lookahead-Limiter auf True Peak)
  const MAX_DEFAULT = () => ({ on: true, gain: 0, ceiling: -1, release: 120 });

  class Maximizer {
    constructor(fs) {
      this.fs = fs;
      this.look = Math.max(8, Math.round(fs * 0.0015));        // 1,5 ms Lookahead
      this.delay = this.look - 1 + TP.delay;
      this.dl = new Float64Array(this.delay + 1); this.dr = new Float64Array(this.delay + 1); this.dpos = 0;
      this.req = new Float64Array(this.look); this.rpos = 0;  // benötigte Verstärkung (Fenster für Minimum)
      this.mins = new Float64Array(this.look); this.mpos = 0; this.msum = this.look;
      this.mins.fill(1);
      this.req.fill(1);
      this.tpl = new TruePeakDetector(); this.tpr = new TruePeakDetector();
      this.g = 1; this.grMax = 0; this.gainSm = 1;
    }
    set(p) {
      this.on = !!p.on;
      this.targetGain = dbToGain(p.gain || 0);
      this.ceil = dbToGain(clamp(p.ceiling, -12, 0));
      this.rel = 1 - Math.exp(-1 / (this.fs * Math.max(5, p.release) / 1000));
      this.smooth = 1 - Math.exp(-1 / (this.fs * 0.03));
    }
    process(l, r, out) {
      if (!this.on) { out[0] = l; out[1] = r; return; }
      this.gainSm += (this.targetGain - this.gainSm) * this.smooth;
      l *= this.gainSm; r *= this.gainSm;

      const pk = Math.max(this.tpl.push(l), this.tpr.push(r));
      const need = pk > this.ceil ? this.ceil / pk : 1;

      // gleitendes Minimum über das Lookahead-Fenster (einfach, Fenster ist klein)
      this.req[this.rpos] = need; this.rpos = (this.rpos + 1) % this.look;
      let m = 1;
      for (let i = 0; i < this.look; i++) if (this.req[i] < m) m = this.req[i];
      // Rechteck-Mittel über das Fenster → glatter Verlauf, der das Ziel rechtzeitig erreicht
      this.msum += m - this.mins[this.mpos]; this.mins[this.mpos] = m; this.mpos = (this.mpos + 1) % this.look;
      const box = this.msum / this.look;
      // Release: nach oben langsam, nach unten sofort
      this.g = box < this.g ? box : this.g + (box - this.g) * this.rel;
      if (this.g > box) this.g = box;
      const gr = -gainToDb(this.g);
      if (gr > this.grMax) this.grMax = gr;

      // Audio verzögern (Lookahead + Laufzeit des True-Peak-Filters)
      const n = this.dl.length;
      this.dl[this.dpos] = l; this.dr[this.dpos] = r;
      const rp = (this.dpos + 1) % n;
      this.dpos = rp;
      let ol = this.dl[rp] * this.g, or = this.dr[rp] * this.g;
      // Sicherheits-Clipper auf Sample-Ebene (greift praktisch nie)
      const c = this.ceil;
      if (ol > c) ol = c; else if (ol < -c) ol = -c;
      if (or > c) or = c; else if (or < -c) or = -c;
      out[0] = ol; out[1] = or;
    }
  }

  // ---------------------------------------------------------------- Lautheit (ITU-R BS.1770 / EBU R128)
  function kWeightingCoeffs(fs) {
    let f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
    let K = Math.tan(Math.PI * f0 / fs);
    const Vh = Math.pow(10, G / 20), Vb = Math.pow(Vh, 0.4996667741545416);
    let a0 = 1 + K / Q + K * K;
    const pre = [(Vh + Vb * K / Q + K * K) / a0, 2 * (K * K - Vh) / a0, (Vh - Vb * K / Q + K * K) / a0, 2 * (K * K - 1) / a0, (1 - K / Q + K * K) / a0];
    f0 = 38.13547087602444; Q = 0.5003270373238773; K = Math.tan(Math.PI * f0 / fs);
    a0 = 1 + K / Q + K * K;
    const rlb = [1, -2, 1, 2 * (K * K - 1) / a0, (1 - K / Q + K * K) / a0];
    return [pre, rlb];
  }

  class LoudnessMeter {
    constructor(fs) {
      const [pre, rlb] = kWeightingCoeffs(fs);
      this.f1 = new Biquad(pre); this.f2 = new Biquad(rlb);
      this.step = Math.round(fs * 0.1);   // 100-ms-Teilblöcke
      this.acc = 0; this.n = 0;
      this.ring = new Float64Array(30); this.rpos = 0; this.filled = 0;
      this.blocks = [];                    // 400-ms-Blöcke (75 % Überlappung) für Integrated
      this.shortTerms = [];                // 3-s-Werte alle 100 ms für LRA
      this.momentary = -Infinity; this.shortTerm = -Infinity;
      this.tpl = new TruePeakDetector(); this.tpr = new TruePeakDetector(); this.truePeak = 0; this.samplePeak = 0;
    }
    reset() {
      this.f1.reset(); this.f2.reset(); this.acc = 0; this.n = 0; this.ring.fill(0); this.rpos = 0; this.filled = 0;
      this.blocks = []; this.shortTerms = []; this.momentary = this.shortTerm = -Infinity; this.truePeak = 0; this.samplePeak = 0;
    }
    push(l, r) {
      const kl = this.f2.L(this.f1.L(l)), kr = this.f2.R(this.f1.R(r));
      this.acc += kl * kl + kr * kr;
      const pk = Math.max(this.tpl.push(l), this.tpr.push(r));
      if (pk > this.truePeak) this.truePeak = pk;
      const sp = Math.max(Math.abs(l), Math.abs(r));
      if (sp > this.samplePeak) this.samplePeak = sp;
      if (++this.n >= this.step) {
        this.ring[this.rpos] = this.acc / this.n; this.rpos = (this.rpos + 1) % 30; this.filled = Math.min(30, this.filled + 1);
        this.acc = 0; this.n = 0;
        const ms = (k) => { let s = 0; for (let i = 1; i <= k; i++) s += this.ring[(this.rpos - i + 30) % 30]; return s / k; };
        if (this.filled >= 4) { const m = ms(4); this.momentary = -0.691 + 10 * Math.log10(m + 1e-20); this.blocks.push(m); }
        if (this.filled >= 30) { const s = ms(30); this.shortTerm = -0.691 + 10 * Math.log10(s + 1e-20); this.shortTerms.push(this.shortTerm); }
        else if (this.filled >= 4) this.shortTerm = -0.691 + 10 * Math.log10(ms(this.filled) + 1e-20);
        return true;
      }
      return false;
    }
    integrated() {
      const abs = this.blocks.filter((m) => -0.691 + 10 * Math.log10(m + 1e-20) > -70);
      if (!abs.length) return -Infinity;
      const mean = abs.reduce((a, b) => a + b, 0) / abs.length;
      const relGate = -0.691 + 10 * Math.log10(mean) - 10;
      const rel = abs.filter((m) => -0.691 + 10 * Math.log10(m) > relGate);
      if (!rel.length) return -Infinity;
      return -0.691 + 10 * Math.log10(rel.reduce((a, b) => a + b, 0) / rel.length);
    }
    loudnessRange() {
      const abs = this.shortTerms.filter((v) => v > -70);
      if (abs.length < 2) return 0;
      const mean = 10 * Math.log10(abs.reduce((a, v) => a + Math.pow(10, v / 10), 0) / abs.length);
      const rel = abs.filter((v) => v > mean - 20).sort((a, b) => a - b);
      if (rel.length < 2) return 0;
      const q = (p) => rel[Math.min(rel.length - 1, Math.max(0, Math.round(p * (rel.length - 1))))];
      return q(0.95) - q(0.10);
    }
  }

  /** Komplette Messung eines Stereo-Puffers (Hauptthread). */
  function measure(L, R, fs) {
    const m = new LoudnessMeter(fs);
    R = R || L;
    for (let i = 0; i < L.length; i++) m.push(L[i], R[i]);
    const I = m.integrated(), tp = gainToDb(m.truePeak);
    let maxST = m.shortTerm;
    for (const v of m.shortTerms) if (v > maxST) maxST = v;
    return { integrated: I, truePeak: tp, samplePeak: gainToDb(m.samplePeak), lra: m.loudnessRange(),
             plr: Number.isFinite(I) ? tp - I : NaN, maxShortTerm: maxST };
  }

  // ---------------------------------------------------------------- Standard-Parameter
  function defaultParams() {
    return {
      bypass: false, matchBypass: true, inputGain: 0, monitorGain: 0,
      preEq: EQ_DEFAULT(), dyn: DYN_DEFAULT(), exc: EXC_DEFAULT(), img: IMG_DEFAULT(),
      postEq: EQ_DEFAULT(), max: MAX_DEFAULT(),
    };
  }

  // ---------------------------------------------------------------- Komplette Kette
  class Chain {
    constructor(fs) {
      this.fs = fs;
      this.preEq = new EQ(); this.dyn = new Dynamics(); this.exc = new Exciter(); this.img = new Imager();
      this.postEq = new EQ(); this.max = new Maximizer(fs);
      this.tmp = new Float64Array(2);
      this.inGain = 1; this.inGainTarget = 1; this.smooth = 1 - Math.exp(-1 / (fs * 0.03));
      this.set(defaultParams());
    }
    set(p) {
      this.p = p;
      this.inGainTarget = dbToGain(p.inputGain || 0);
      this.preEq.set(p.preEq, this.fs); this.dyn.set(p.dyn, this.fs); this.exc.set(p.exc, this.fs);
      this.img.set(p.img, this.fs); this.postEq.set(p.postEq, this.fs); this.max.set(p.max);
    }
    process(l, r, out) {
      this.inGain += (this.inGainTarget - this.inGain) * this.smooth;
      l = this.preEq.L(l * this.inGain); r = this.preEq.R(r * this.inGain);
      const t = this.tmp;
      this.dyn.process(l, r, t); this.exc.process(t[0], t[1], t); this.img.process(t[0], t[1], t);
      l = this.postEq.L(t[0]); r = this.postEq.R(t[1]);
      this.max.process(l, r, out);
    }
    flush() { this.preEq.flush(); this.dyn.flush(); this.exc.flush(); this.img.flush(); this.postEq.flush(); }
    /** Latenz in Samples (für den Vorher/Nachher-Vergleich). */
    latency() { return this.max.on ? this.max.delay : 0; }
  }

  // ---------------------------------------------------------------- Echtzeit-Engine (Kette + Meter + Vorher/Nachher)
  /** Unabhängig vom Audio-API: läuft im AudioWorklet und – als Ersatz – in einem ScriptProcessor. */
  class Engine {
    constructor(fs, params) {
      this.fs = fs;
      this.chain = new Chain(fs);
      this.meterIn = new LoudnessMeter(fs); this.meterOut = new LoudnessMeter(fs);
      this.p = params || defaultParams();
      this.chain.set(this.p);
      this.monitor = dbToGain(this.p.monitorGain || 0);
      // Bypass-Verzögerung = Latenz der Kette, damit Vorher/Nachher deckungsgleich ist
      this.byL = new Float64Array(4096); this.byR = new Float64Array(4096); this.byPos = 0;
      this.matchDb = 0; this.bypassGain = 1;
      // Waveform: eine Spalte alle 5 ms
      this.colLen = Math.max(32, Math.round(fs * 0.005));
      this.col = 0; this.cMin = 0; this.cMax = 0; this.iMin = 0; this.iMax = 0; this.eLo = 0; this.eMid = 0; this.eHi = 0;
      this.lpLo = 0; this.lpLo2 = 0; this.lpHi = 0; this.lpHi2 = 0;
      this.aLo = 1 - Math.exp(-TAU * 200 / fs); this.aHi = 1 - Math.exp(-TAU * 3500 / fs);
      this.wave = []; this.scope = []; this.corrNum = 0; this.corrDen1 = 0; this.corrDen2 = 0;
      this.sinceReport = 0; this.reportEvery = Math.round(fs / 30);
      this.t = new Float64Array(2);
      this.iCache = -Infinity; this.iCacheIn = -Infinity; this.iCount = 0;
    }
    message(m) {
      if (m.type === "params") {
        this.p = m.params;
        this.chain.set(this.p);
        this.monitor = dbToGain(this.p.monitorGain || 0);
      } else if (m.type === "resetMeters") {
        this.meterIn.reset(); this.meterOut.reset(); this.chain.max.grMax = 0; this.iCache = this.iCacheIn = -Infinity;
      }
    }
    /** Verarbeitet einen Block. Gibt etwa 30-mal pro Sekunde einen Meter-Bericht zurück, sonst null. */
    process(inL, inR, outL, outR, n) {
      const t = this.t, chain = this.chain, lat = chain.latency(), byLen = this.byL.length, bypass = this.p.bypass;
      // Lautheitsangleich für Vorher/Nachher: Ausgang ↔ Eingang (Kurzzeit)
      if (this.p.matchBypass && Number.isFinite(this.meterOut.shortTerm) && Number.isFinite(this.meterIn.shortTerm)) {
        const d = this.meterOut.shortTerm - this.meterIn.shortTerm;
        this.matchDb += (d - this.matchDb) * 0.02;
      }
      const byGain = this.p.matchBypass ? dbToGain(this.matchDb) : 1;
      this.bypassGain += (byGain - this.bypassGain) * 0.05;
      for (let i = 0; i < n; i++) {
        const l = inL ? inL[i] : 0, r = inR ? inR[i] : 0;
        chain.process(l, r, t);
        this.byL[this.byPos] = l; this.byR[this.byPos] = r;
        const dpos = (this.byPos - lat + byLen) % byLen;
        const dl = this.byL[dpos], dr = this.byR[dpos];
        this.byPos = (this.byPos + 1) % byLen;
        const ol = t[0], or = t[1];
        this.meterIn.push(dl, dr);
        this.meterOut.push(ol, or);
        this.trackWave(ol, or, dl, dr);
        const yl = bypass ? dl * this.bypassGain : ol, yr = bypass ? dr * this.bypassGain : or;
        outL[i] = yl * this.monitor; if (outR !== outL) outR[i] = yr * this.monitor;
      }
      chain.flush();
      if ((this.sinceReport += n) >= this.reportEvery) { this.sinceReport = 0; return this.report(); }
      return null;
    }
    trackWave(l, r, il, ir) {
      const m = 0.5 * (l + r), im = 0.5 * (il + ir);
      if (m < this.cMin) this.cMin = m; if (m > this.cMax) this.cMax = m;
      if (im < this.iMin) this.iMin = im; if (im > this.iMax) this.iMax = im;
      this.lpLo += this.aLo * (m - this.lpLo); this.lpLo2 += this.aLo * (this.lpLo - this.lpLo2);
      this.lpHi += this.aHi * (m - this.lpHi); this.lpHi2 += this.aHi * (this.lpHi - this.lpHi2);
      const lo = this.lpLo2, hi = m - this.lpHi2, mid = this.lpHi2 - this.lpLo2;
      this.eLo += lo * lo; this.eMid += mid * mid; this.eHi += hi * hi;
      this.corrNum += l * r; this.corrDen1 += l * l; this.corrDen2 += r * r;
      if ((this.col & 7) === 0 && this.scope.length < 512) this.scope.push(l, r);
      if (++this.col >= this.colLen) {
        this.wave.push(this.cMin, this.cMax, this.iMin, this.iMax, Math.sqrt(this.eLo / this.col), Math.sqrt(this.eMid / this.col), Math.sqrt(this.eHi / this.col));
        this.col = 0; this.cMin = this.cMax = this.iMin = this.iMax = 0; this.eLo = this.eMid = this.eHi = 0;
      }
    }
    report() {
      const mi = this.meterIn, mo = this.meterOut, c = this.chain;
      const corr = this.corrDen1 > 1e-12 && this.corrDen2 > 1e-12 ? this.corrNum / Math.sqrt(this.corrDen1 * this.corrDen2) : 1;
      this.corrNum *= 0.5; this.corrDen1 *= 0.5; this.corrDen2 *= 0.5;
      const wave = Float32Array.from(this.wave), scope = Float32Array.from(this.scope);
      if (this.iCount++ % 10 === 0) { this.iCache = mo.integrated(); this.iCacheIn = mi.integrated(); }   // ~3x pro Sekunde reicht
      const rep = {
        type: "meter",
        inM: mi.momentary, inS: mi.shortTerm, inI: this.iCacheIn,
        outM: mo.momentary, outS: mo.shortTerm, outI: this.iCache, outTP: gainToDb(mo.truePeak),
        grMax: c.max.on ? c.max.grMax : 0, grBands: c.dyn.comps.map((k) => (c.dyn.on ? k.grMax : 0)),
        corr, wave, scope, matchDb: this.matchDb,
      };
      this.wave = []; this.scope = [];
      c.max.grMax = 0; c.dyn.comps.forEach((k) => (k.grMax = 0));
      return rep;
    }
  }

  const api = { biquadCoeffs, biquadMagnitude, eqResponseDb, defaultParams, Chain, Engine, LoudnessMeter, measure,
                TruePeakDetector, Crossover3, dbToGain, gainToDb, kWeightingCoeffs };
  root.KF = api;

  // ---------------------------------------------------------------- AudioWorklet
  if (typeof root.registerProcessor === "function" && typeof root.AudioWorkletProcessor === "function") {
    class KernfusionProcessor extends root.AudioWorkletProcessor {
      constructor(options) {
        super();
        const params = options && options.processorOptions && options.processorOptions.params;
        this.engine = new Engine(root.sampleRate, params);
        this.port.onmessage = (e) => this.engine.message(e.data);
      }
      process(inputs, outputs) {
        const input = inputs[0], output = outputs[0];
        const inL = input && input[0] ? input[0] : null, inR = input && input[1] ? input[1] : inL;
        const rep = this.engine.process(inL, inR, output[0], output[1] || output[0], output[0].length);
        if (rep) this.port.postMessage(rep, [rep.wave.buffer, rep.scope.buffer]);
        return true;
      }
    }
    root.registerProcessor("nova-kernfusion", KernfusionProcessor);
  }
})(typeof globalThis !== "undefined" ? globalThis : this);
