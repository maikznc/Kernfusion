// Tests für die Kernfusion-DSP-Kette (Node): node tests/dsp-tests.js
require("../web/kernfusion-dsp.js");
const KF = globalThis.KF;

let failures = 0;
const check = (ok, what) => { console.log((ok ? "  ok    " : "  FAIL  ") + what); if (!ok) failures++; };
const fmt = (v, d = 2) => (Number.isFinite(v) ? v.toFixed(d) : String(v));

const fs = 48000;
function sine(freq, amp, seconds, phase = 0) {
  const n = Math.round(fs * seconds), d = new Float32Array(n);
  for (let i = 0; i < n; i++) d[i] = amp * Math.sin(2 * Math.PI * freq * i / fs + phase);
  return d;
}
function pink(seconds, seed, gain) {
  const n = Math.round(fs * seconds), d = new Float32Array(n);
  let s = seed >>> 0, b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;
  for (let i = 0; i < n; i++) {
    s = (s * 1664525 + 1013904223) >>> 0; const w = s / 4294967296 * 2 - 1;
    b0 = 0.99886 * b0 + w * 0.0555179; b1 = 0.99332 * b1 + w * 0.0750759; b2 = 0.969 * b2 + w * 0.153852;
    b3 = 0.8665 * b3 + w * 0.3104856; b4 = 0.55 * b4 + w * 0.5329522; b5 = -0.7616 * b5 - w * 0.016898;
    d[i] = (b0 + b1 + b2 + b3 + b4 + b5 + b6 + w * 0.5362) * 0.11 * gain; b6 = w * 0.115926;
  }
  return d;
}
function run(params, L, R) {
  const chain = new KF.Chain(fs);
  chain.set(params);
  const oL = new Float32Array(L.length), oR = new Float32Array(L.length), t = new Float64Array(2);
  for (let i = 0; i < L.length; i++) {
    chain.process(L[i], R[i], t); oL[i] = t[0]; oR[i] = t[1];
    if ((i & 127) === 127) chain.flush();
  }
  return { L: oL, R: oR, chain };
}
const rms = (d, from = 0) => { let s = 0; for (let i = from; i < d.length; i++) s += d[i] * d[i]; return Math.sqrt(s / (d.length - from)); };

console.log("\nLautheit (BS.1770)");
{
  const s = sine(1000, 1, 10);
  const m = KF.measure(s, s, fs);
  check(Math.abs(m.integrated) < 0.1, `1 kHz, 0 dBFS stereo → ${fmt(m.integrated)} LUFS (Soll 0,0)`);
  const q = sine(1000, KF.dbToGain(-23), 10);
  const mq = KF.measure(q, q, fs);
  check(Math.abs(mq.integrated + 23) < 0.1, `1 kHz, −23 dBFS stereo → ${fmt(mq.integrated)} LUFS (Soll −23,0)`);
  // Intersample-Peak: Sinus bei fs/4 mit 45° Phase → Samples bei −3 dB, wahrer Spitzenwert 0 dB
  const ip = sine(fs / 4, 1, 2, Math.PI / 4);
  const mi = KF.measure(ip, ip, fs);
  check(Math.abs(mi.samplePeak + 3.01) < 0.05 && Math.abs(mi.truePeak) < 0.3,
        `Intersample-Peak erkannt: Sample-Peak ${fmt(mi.samplePeak)} dB, True Peak ${fmt(mi.truePeak)} dBTP (Soll ≈ 0)`);
}

console.log("\nNeutralstellung ist transparent");
{
  const p = KF.defaultParams(); p.max.on = false;
  for (const f of [40, 1000, 12000]) {
    const s = sine(f, 0.25, 1);
    const o = run(p, s, s);
    const d = 20 * Math.log10(rms(o.L, fs / 2) / rms(s, fs / 2));
    check(Math.abs(d) < 0.05, `${f} Hz: Pegeländerung ${fmt(d, 3)} dB`);
  }
}

console.log("\nFrequenzweiche summiert flach");
{
  const xo = new KF.Crossover3(); xo.set(fs, 120, 2500);
  let worst = 0;
  for (const f of [30, 80, 120, 200, 1000, 2500, 5000, 15000]) {
    const s = sine(f, 0.5, 0.5);
    const out = new Float32Array(s.length);
    for (let i = 0; i < s.length; i++) { const b = xo.L(s[i]); out[i] = b[0] + b[1] + b[2]; }
    const d = 20 * Math.log10(rms(out, fs / 4) / rms(s, fs / 4));
    worst = Math.max(worst, Math.abs(d));
  }
  check(worst < 0.05, `größte Abweichung über 30 Hz–15 kHz: ${fmt(worst, 3)} dB`);
}

console.log("\nKompressor");
{
  const p = KF.defaultParams(); p.max.on = false;
  p.dyn.bands[1] = { threshold: -20, ratio: 4, makeup: 0 };
  const s = sine(1000, KF.dbToGain(-10), 2);
  const o = run(p, s, s);
  const d = 20 * Math.log10(rms(o.L, fs) / rms(s, fs));
  // Pegel (RMS) −13 dB, Schwelle −20 → 7 dB drüber, Ratio 4 → 5,25 dB Reduktion
  check(Math.abs(d + 5.25) < 0.4, `Reduktion ${fmt(-d)} dB (Soll ≈ 5,25 dB)`);
}

console.log("\nImager");
{
  const p = KF.defaultParams(); p.max.on = false; p.dyn.on = false;
  p.img.bands = [{ width: 0 }, { width: 0 }, { width: 0 }];
  const L = pink(1, 1, 0.5), R = pink(1, 2, 0.5);
  const o = run(p, L, R);
  let diff = 0; for (let i = fs / 4; i < L.length; i++) diff = Math.max(diff, Math.abs(o.L[i] - o.R[i]));
  check(diff < 1e-6, `Breite 0 → Mono (L−R max. ${diff.toExponential(1)})`);
}

console.log("\nMaximizer (True-Peak-Limiter)");
{
  for (const [name, L, R] of [["rosa Rauschen", pink(10, 3, 0.9), pink(10, 4, 0.9)],
                               ["Sinus mit Intersample-Peaks", sine(fs / 4, 0.9, 5, Math.PI / 4), sine(fs / 4, 0.9, 5, Math.PI / 4)]]) {
    const p = KF.defaultParams(); p.dyn.on = false;
    p.max = { on: true, gain: 12, ceiling: -1, release: 100 };
    const o = run(p, L, R);
    // True Peak über den ganzen Ausgang messen (Detektor eingeschwungen), gewertet ab 1 s
    const dl = new KF.TruePeakDetector(), dr = new KF.TruePeakDetector();
    let tp = 0;
    for (let i = 0; i < o.L.length; i++) { const v = Math.max(dl.push(o.L[i]), dr.push(o.R[i])); if (i > fs) tp = Math.max(tp, v); }
    check(KF.gainToDb(tp) <= -0.95, `${name}: +12 dB → True Peak ${fmt(KF.gainToDb(tp))} dBTP (Grenze −1,0)`);
  }
  const p = KF.defaultParams(); p.dyn.on = false; p.max = { on: true, gain: 8, ceiling: -1, release: 100 };
  const L = pink(10, 5, 0.25), R = pink(10, 6, 0.25);
  const before = KF.measure(L, R, fs), after = KF.measure(run(p, L, R).L, run(p, L, R).R, fs);
  check(after.integrated - before.integrated > 4, `Lautheit steigt: ${fmt(before.integrated)} → ${fmt(after.integrated)} LUFS`);
}

console.log("\nExciter");
{
  const p = KF.defaultParams(); p.max.on = false; p.dyn.on = false;
  p.exc.bands[2].amount = 0.6;
  const s = sine(5000, 0.3, 1);
  const o = run(p, s, s);
  // Energie bei 15 kHz (3. Oberton) per Goertzel
  const goertzel = (d, f) => { const k = 2 * Math.cos(2 * Math.PI * f / fs); let s0 = 0, s1 = 0, s2 = 0;
    for (let i = fs / 2; i < d.length; i++) { s0 = d[i] + k * s1 - s2; s2 = s1; s1 = s0; } return Math.sqrt(s1 * s1 + s2 * s2 - k * s1 * s2); };
  const h3 = 20 * Math.log10(goertzel(o.L, 15000) / goertzel(o.L, 5000));
  const h3dry = 20 * Math.log10((goertzel(s, 15000) + 1e-9) / goertzel(s, 5000));
  check(h3 > -45 && h3dry < -100, `3. Oberton erzeugt: ${fmt(h3, 1)} dB unter dem Grundton (ohne Exciter ${fmt(h3dry, 0)} dB)`);
}

console.log("\nRechenzeit");
{
  const p = KF.defaultParams();
  p.preEq.bands[1].gain = 2; p.postEq.bands[4].gain = 1.5;
  p.dyn.bands.forEach((b) => { b.threshold = -24; b.ratio = 2; });
  p.exc.bands[2].amount = 0.3; p.img.bands[2].width = 1.2; p.max.gain = 6;
  const L = pink(20, 7, 0.4), R = pink(20, 8, 0.4);
  const t0 = process.hrtime.bigint();
  run(p, L, R);
  const ms = Number(process.hrtime.bigint() - t0) / 1e6;
  check(ms < 20000 / 3, `20 s Audio mit allen Modulen in ${fmt(ms / 1000, 2)} s (${fmt(20000 / ms, 1)}× Echtzeit)`);
}

console.log(failures ? `\n${failures} Test(s) fehlgeschlagen` : "\nAlle DSP-Tests bestanden");
process.exit(failures ? 1 : 0);
