#include "TriOsc.h"

using namespace juce;

static constexpr float kPi = MathConstants<float>::pi;
static constexpr float kTwoPi = MathConstants<float>::twoPi;

static float blep (float t, float dt)
{
    if (t < dt)         { t /= dt; return t + t - t * t - 1.f; }
    if (t > 1.f - dt)   { t = (t - 1.f) / dt; return t * t + t + t + 1.f; }
    return 0.f;
}

static const StringArray kSrcNames { "None", "LFO", "Amp Env", "Filter Env", "Velocity", "Key Track", "Random" };
static const StringArray kDstNames { "None", "Pitch", "Cutoff", "Resonance", "Volume",
                                     "Osc 1 Level", "Osc 2 Level", "Osc 3 Level", "FM Amount" };

// ======================================================================
//                              PROCESSOR
// ======================================================================
AudioProcessorValueTreeState::ParameterLayout TriOscProcessor::createLayout()
{
    std::vector<std::unique_ptr<RangedAudioParameter>> p;

    auto F = [&] (String id, String name, float lo, float hi, float def,
                  float skew = 1.f, String unit = {}, int dec = 2)
    {
        p.push_back (std::make_unique<AudioParameterFloat> (
            ParameterID { id, 1 }, name, NormalisableRange<float> (lo, hi, 0.f, skew), def,
            AudioParameterFloatAttributes().withStringFromValueFunction (
                [unit, dec] (float v, int) { return String (v, dec) + unit; })));
    };
    auto I = [&] (String id, String name, int lo, int hi, int def)
    {
        p.push_back (std::make_unique<AudioParameterInt> (ParameterID { id, 1 }, name, lo, hi, def));
    };
    auto C = [&] (String id, String name, StringArray items, int def)
    {
        p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { id, 1 }, name, items, def));
    };
    auto B = [&] (String id, String name, bool def)
    {
        p.push_back (std::make_unique<AudioParameterBool> (ParameterID { id, 1 }, name, def));
    };

    for (int i = 1; i <= 3; ++i)
    {
        String s = "o" + String (i) + "_";
        String n = "Osc " + String (i) + " ";
        B (s + "on", n + "On", i < 3);
        C (s + "wave", n + "Wave", { "Sine", "Triangle", "Saw", "Square", "Noise" }, i == 3 ? 3 : 2);
        I (s + "oct", n + "Octave", -3, 3, i == 3 ? -1 : 0);
        I (s + "semi", n + "Semi", -12, 12, 0);
        F (s + "fine", n + "Fine", -100.f, 100.f, i == 2 ? 7.f : 0.f, 1.f, " ct", 0);
        F (s + "level", n + "Level", 0.f, 1.f, i == 1 ? 0.8f : (i == 2 ? 0.6f : 0.5f));
        F (s + "pan", n + "Pan", -1.f, 1.f, 0.f);
        F (s + "pw", n + "PW", 0.05f, 0.95f, 0.5f);
        F (s + "phase", n + "Phase", 0.f, 1.f, 0.f);
        I (s + "uni", n + "Voices", 1, 7, 1);
        F (s + "det", n + "Detune", 0.f, 100.f, 15.f, 1.f, " ct", 0);
        F (s + "spread", n + "Spread", 0.f, 1.f, 0.5f);
    }

    F ("fm", "Voice FM", 0.f, 1.f, 0.f);
    F ("ring", "Voice Ring Mod", 0.f, 1.f, 0.f);
    F ("glide", "Voice Glide", 0.f, 1000.f, 0.f, 0.4f, " ms", 0);

    F ("drive", "Output Drive", 0.f, 1.f, 0.f);
    F ("master", "Output Master", 0.f, 1.f, 0.7f);

    C ("ftype", "Filter Type", { "Lowpass", "Highpass", "Bandpass", "Off" }, 0);
    F ("cutoff", "Filter Cutoff", 20.f, 20000.f, 20000.f, 0.3f, " Hz", 0);
    F ("res", "Filter Resonance", 0.f, 1.f, 0.f);
    F ("fenv", "Filter Env Amount", -1.f, 1.f, 0.f);
    F ("keytrack", "Filter Key Track", 0.f, 1.f, 0.f);

    F ("a_a", "Amp Env Attack", 0.0005f, 10.f, 0.005f, 0.3f, " s", 3);
    F ("a_d", "Amp Env Decay", 0.001f, 10.f, 0.3f, 0.3f, " s", 3);
    F ("a_s", "Amp Env Sustain", 0.f, 1.f, 0.7f);
    F ("a_r", "Amp Env Release", 0.001f, 10.f, 0.2f, 0.3f, " s", 3);
    F ("f_a", "Filter Env Attack", 0.0005f, 10.f, 0.005f, 0.3f, " s", 3);
    F ("f_d", "Filter Env Decay", 0.001f, 10.f, 0.3f, 0.3f, " s", 3);
    F ("f_s", "Filter Env Sustain", 0.f, 1.f, 0.f);
    F ("f_r", "Filter Env Release", 0.001f, 10.f, 0.2f, 0.3f, " s", 3);

    C ("lfo_shape", "LFO Shape", { "Sine", "Triangle", "Saw", "Square", "S&H" }, 0);
    F ("lfo_rate", "LFO Rate", 0.05f, 30.f, 4.f, 0.4f, " Hz", 2);
    F ("lfo_pitch", "LFO Pitch", 0.f, 12.f, 0.f, 1.f, " st", 1);
    F ("lfo_cut", "LFO Cutoff", 0.f, 1.f, 0.f);
    F ("lfo_amp", "LFO Amp", 0.f, 1.f, 0.f);

    for (int k = 1; k <= 4; ++k)
    {
        String s = "m" + String (k) + "_";
        String n = "Slot " + String (k) + " ";
        C (s + "src", n + "Source", kSrcNames, 0);
        C (s + "dst", n + "Dest", kDstNames, 0);
        F (s + "amt", n + "Amount", -1.f, 1.f, 0.f);
    }

    F ("ch_rate", "Chorus Rate", 0.05f, 5.f, 0.6f, 0.5f, " Hz", 2);
    F ("ch_depth", "Chorus Depth", 0.f, 1.f, 0.5f);
    F ("ch_mix", "Chorus Mix", 0.f, 1.f, 0.f);

    F ("dl_time", "Delay Time", 10.f, 1000.f, 350.f, 0.5f, " ms", 0);
    F ("dl_fb", "Delay Feedback", 0.f, 0.95f, 0.4f);
    F ("dl_mix", "Delay Mix", 0.f, 1.f, 0.f);

    F ("rv_size", "Reverb Size", 0.f, 1.f, 0.5f);
    F ("rv_damp", "Reverb Damping", 0.f, 1.f, 0.5f);
    F ("rv_mix", "Reverb Mix", 0.f, 1.f, 0.f);

    F ("eq_lf", "Low Freq", 20.f, 600.f, 120.f, 0.4f, " Hz", 0);
    F ("eq_lg", "Low Gain", -15.f, 15.f, 0.f, 1.f, " dB", 1);
    F ("eq_mf", "Mid Freq", 150.f, 10000.f, 1000.f, 0.35f, " Hz", 0);
    F ("eq_mg", "Mid Gain", -15.f, 15.f, 0.f, 1.f, " dB", 1);
    F ("eq_mq", "Mid Q", 0.2f, 8.f, 1.f, 0.5f, "", 2);
    F ("eq_hf", "High Freq", 1500.f, 18000.f, 6000.f, 0.4f, " Hz", 0);
    F ("eq_hg", "High Gain", -15.f, 15.f, 0.f, 1.f, " dB", 1);

    return { p.begin(), p.end() };
}

TriOscProcessor::TriOscProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createLayout())
{
    for (auto& a : scope) a.store (0.f);
}

void TriOscProcessor::prepareToPlay (double sampleRate, int)
{
    sr = sampleRate;
    for (auto& v : voices) v = Voice();

    const int chSize = (int) (sr * 0.05) + 8;
    const int dlSize = (int) (sr * 2.0) + 8;
    for (int c = 0; c < 2; ++c)
    {
        chBuf[c].assign ((size_t) chSize, 0.f);
        dlBuf[c].assign ((size_t) dlSize, 0.f);
    }
    chW = dlW = 0;
    chPhase = 0.f;
    dlSmooth = 350.f * 0.001f * (float) sr;
    reverb.setSampleRate (sr);
    reverb.reset();
}

void TriOscProcessor::readParams()
{
    auto g = [this] (const String& id) { return apvts.getRawParameterValue (id)->load(); };

    for (int i = 0; i < 3; ++i)
    {
        String s = "o" + String (i + 1) + "_";
        auto& o = P.osc[i];
        o.on = g (s + "on") > 0.5f;
        o.wave = (int) g (s + "wave");
        o.oct = (int) g (s + "oct");
        o.semi = (int) g (s + "semi");
        o.fine = g (s + "fine");
        o.level = g (s + "level");
        o.pan = g (s + "pan");
        o.pw = g (s + "pw");
        o.phase = g (s + "phase");
        o.uni = jlimit (1, 7, (int) g (s + "uni"));
        o.det = g (s + "det");
        o.spread = g (s + "spread");
    }
    for (int k = 0; k < 4; ++k)
    {
        String s = "m" + String (k + 1) + "_";
        P.mod[k].src = (int) g (s + "src");
        P.mod[k].dst = (int) g (s + "dst");
        P.mod[k].amt = g (s + "amt");
    }

    P.fm = g ("fm");           P.ring = g ("ring");       P.drive = g ("drive");
    P.glideMs = g ("glide");   P.master = g ("master");   P.ftype = (int) g ("ftype");
    P.cutoff = g ("cutoff");   P.res = g ("res");         P.fenvAmt = g ("fenv");
    P.keytrack = g ("keytrack");
    P.aA = g ("a_a"); P.aD = g ("a_d"); P.aS = g ("a_s"); P.aR = g ("a_r");
    P.fA = g ("f_a"); P.fD = g ("f_d"); P.fS = g ("f_s"); P.fR = g ("f_r");
    P.lfoRate = g ("lfo_rate");   P.lfoShape = (int) g ("lfo_shape");
    P.lfoPitch = g ("lfo_pitch"); P.lfoCut = g ("lfo_cut"); P.lfoAmp = g ("lfo_amp");

    P.chRate = g ("ch_rate"); P.chDepth = g ("ch_depth"); P.chMix = g ("ch_mix");
    P.dlTime = g ("dl_time"); P.dlFb = g ("dl_fb");       P.dlMix = g ("dl_mix");
    P.rvSize = g ("rv_size"); P.rvDamp = g ("rv_damp");   P.rvMix = g ("rv_mix");
    P.eqLf = g ("eq_lf"); P.eqLg = g ("eq_lg");
    P.eqMf = g ("eq_mf"); P.eqMg = g ("eq_mg"); P.eqMq = g ("eq_mq");
    P.eqHf = g ("eq_hf"); P.eqHg = g ("eq_hg");
}

void TriOscProcessor::getScope (float* dest, int n) const
{
    const int w = scopeW.load (std::memory_order_relaxed);
    for (int i = 0; i < n; ++i)
        dest[i] = scope[(size_t) ((w - n + i + kScopeSize * 2) & (kScopeSize - 1))].load (std::memory_order_relaxed);
}

// ------------------------------------------------------------------- midi
void TriOscProcessor::noteOn (int note, float vel)
{
    Voice* v = nullptr;
    for (auto& x : voices) if (! x.active) { v = &x; break; }
    if (v == nullptr)
    {
        v = &voices[0];
        for (auto& x : voices) if (x.age < v->age) v = &x;
    }

    v->active = true;
    v->gate = true;
    v->note = note;
    v->vel = vel;
    v->rnd = rng.nextFloat() * 2.f - 1.f;
    v->age = ++counter;
    v->curNote = (P.glideMs > 0.f && lastNote >= 0) ? (float) lastNote : (float) note;
    lastNote = note;

    for (int i = 0; i < 3; ++i)
        for (int u = 0; u < 7; ++u)
        {
            float ph = P.osc[i].phase + (P.osc[i].uni > 1 ? rng.nextFloat() : 0.f);
            v->ph[i][u] = ph - std::floor (ph);
        }

    for (int c = 0; c < 2; ++c) v->ic1[c] = v->ic2[c] = 0.f;
    v->aenv.trigger();
    v->fenv.trigger();
}

void TriOscProcessor::noteOff (int note)
{
    for (auto& v : voices)
        if (v.active && v.gate && v.note == note)
        {
            v.gate = false;
            v.aenv.release();
            v.fenv.release();
        }
}

void TriOscProcessor::handleMidi (const MidiMessage& m)
{
    if (m.isNoteOn())            noteOn (m.getNoteNumber(), m.getFloatVelocity());
    else if (m.isNoteOff())      noteOff (m.getNoteNumber());
    else if (m.isAllNotesOff() || m.isAllSoundOff())
    {
        for (auto& v : voices) { v.gate = false; v.aenv.release(); v.fenv.release(); }
    }
    else if (m.isPitchWheel())   bend = (m.getPitchWheelValue() - 8192) / 8192.f;
}

// -------------------------------------------------------------------- dsp
float TriOscProcessor::wave (int w, float t, float dt, float pw)
{
    switch (w)
    {
        case 0: return std::sin (kTwoPi * t);
        case 1: return 4.f * std::abs (t - 0.5f) - 1.f;
        case 2: return 2.f * t - 1.f - blep (t, dt);
        case 3:
        {
            float v = t < pw ? 1.f : -1.f;
            v += blep (t, dt);
            float t2 = t + 1.f - pw;
            t2 -= std::floor (t2);
            v -= blep (t2, dt);
            return v;
        }
        default: return rng.nextFloat() * 2.f - 1.f;
    }
}

void TriOscProcessor::processBlock (AudioBuffer<float>& buf, MidiBuffer& midi)
{
    ScopedNoDenormals noDenormals;
    buf.clear();
    readParams();

    const int chSize = (int) chBuf[0].size();
    const int dlSize = (int) dlBuf[0].size();
    if (chSize == 0 || dlSize == 0) return; // not prepared yet

    const int ns = buf.getNumSamples();
    float* outL = buf.getWritePointer (0);
    float* outR = buf.getWritePointer (1);
    const float fsr = (float) sr;
    const float glideCoef = P.glideMs > 0.f ? 1.f - std::exp (-1.f / (P.glideMs * 0.001f * fsr)) : 1.f;
    const float drive = 1.f + P.drive * 8.f;
    const float maxCut = 0.45f * fsr;

    eqLo.makeLowShelf (sr, P.eqLf, P.eqLg);
    eqMid.makePeak (sr, P.eqMf, P.eqMg, P.eqMq);
    eqHi.makeHighShelf (sr, P.eqHf, P.eqHg);

    const float chStep = P.chRate / fsr;
    const float dlTarget = jlimit (1.f, (float) (dlSize - 3), P.dlTime * 0.001f * fsr);

    auto renderOsc = [&] (int i, Voice& v, float baseSemis, float fmOff, float level,
                          float& oL, float& oR) -> float
    {
        const auto& o = P.osc[i];
        oL = oR = 0.f;
        if (! o.on) return 0.f;

        const int uni = o.uni;
        const float norm = 1.f / std::sqrt ((float) uni);
        float mono = 0.f;

        for (int u = 0; u < uni; ++u)
        {
            const float pos = uni > 1 ? ((float) u / (float) (uni - 1)) * 2.f - 1.f : 0.f;
            const float semis = baseSemis + (float) (o.oct * 12 + o.semi) + o.fine * 0.01f + pos * o.det * 0.01f;
            const float freq = 440.f * std::exp2 ((semis - 69.f) / 12.f);
            const float dt = jlimit (1.0e-5f, 0.45f, freq / fsr);

            float t = v.ph[i][u] + fmOff;
            t -= std::floor (t);
            const float s = wave (o.wave, t, dt, o.pw);

            v.ph[i][u] += dt;
            if (v.ph[i][u] >= 1.f) v.ph[i][u] -= 1.f;

            const float pn = jlimit (-1.f, 1.f, o.pan + pos * o.spread);
            const float ang = (pn + 1.f) * 0.25f * kPi;
            oL += s * std::cos (ang) * 1.4142f * norm;
            oR += s * std::sin (ang) * 1.4142f * norm;
            mono += s * norm;
        }
        oL *= level;
        oR *= level;
        return mono;
    };

    auto it = midi.begin();

    for (int n = 0; n < ns; ++n)
    {
        while (it != midi.end() && (*it).samplePosition <= n)
        {
            handleMidi ((*it).getMessage());
            ++it;
        }

        // LFO
        lfoPhase += P.lfoRate / fsr;
        if (lfoPhase >= 1.f) { lfoPhase -= 1.f; sh = rng.nextFloat() * 2.f - 1.f; }
        float lfo;
        switch (P.lfoShape)
        {
            case 0:  lfo = std::sin (kTwoPi * lfoPhase); break;
            case 1:  lfo = 1.f - 4.f * std::abs (lfoPhase - 0.5f); break;
            case 2:  lfo = 2.f * lfoPhase - 1.f; break;
            case 3:  lfo = lfoPhase < 0.5f ? 1.f : -1.f; break;
            default: lfo = sh; break;
        }

        float accL = 0.f, accR = 0.f;

        for (auto& v : voices)
        {
            if (! v.active) continue;

            v.curNote += ((float) v.note - v.curNote) * glideCoef;

            float ae = v.aenv.next (P.aA, P.aD, P.aS, P.aR, fsr);
            const float fe = v.fenv.next (P.fA, P.fD, P.fS, P.fR, fsr);
            if (v.aenv.stage == Env::Idle) { v.active = false; continue; }

            // modulation matrix
            const float src[7] = { 0.f, lfo, ae, fe, v.vel,
                                   jlimit (-1.f, 1.f, (v.curNote - 60.f) / 36.f), v.rnd };
            float md[9] = {};
            for (auto& m : P.mod)
                if (m.src > 0 && m.dst > 0 && m.src < 7 && m.dst < 9)
                    md[m.dst] += src[m.src] * m.amt;

            const float baseSemis = v.curNote + bend * 2.f + lfo * P.lfoPitch + md[1] * 12.f;
            ae *= 1.f - P.lfoAmp * (0.5f + 0.5f * lfo);
            ae *= jlimit (0.f, 2.f, 1.f + md[4]);
            const float fmAmt = jlimit (0.f, 2.f, P.fm + md[8]);
            const float res = jlimit (0.f, 1.f, P.res + md[3] * 0.5f);

            float lv[3];
            for (int i = 0; i < 3; ++i) lv[i] = jlimit (0.f, 1.5f, P.osc[i].level + md[5 + i]);

            float l0, r0, l1, r1, l2, r2;
            const float m1 = renderOsc (1, v, baseSemis, 0.f, lv[1], l1, r1);
            const float m0 = renderOsc (0, v, baseSemis, fmAmt * m1 * 2.f, lv[0], l0, r0);
            renderOsc (2, v, baseSemis, 0.f, lv[2], l2, r2);

            float xl = l0 + l1 + l2;
            float xr = r0 + r1 + r2;
            const float rm = P.ring * m0 * m1;
            xl += rm;
            xr += rm;

            if (P.ftype != 3)
            {
                float cut = P.cutoff * std::exp2 (P.fenvAmt * 5.f * fe
                                                  + (P.lfoCut * lfo + md[2]) * 4.f
                                                  + P.keytrack * (v.curNote - 60.f) / 12.f);
                cut = jlimit (20.f, maxCut, cut);
                const float g = std::tan (kPi * cut / fsr);
                const float k = 2.f - 1.95f * res;
                const float a1 = 1.f / (1.f + g * (g + k));
                const float a2 = g * a1;
                const float a3 = g * a2;

                auto filt = [&] (float x, int ch)
                {
                    const float v3 = x - v.ic2[ch];
                    const float v1 = a1 * v.ic1[ch] + a2 * v3;
                    const float v2 = v.ic2[ch] + a2 * v.ic1[ch] + a3 * v3;
                    v.ic1[ch] = 2.f * v1 - v.ic1[ch];
                    v.ic2[ch] = 2.f * v2 - v.ic2[ch];
                    if (P.ftype == 0) return v2;
                    if (P.ftype == 1) return x - k * v1 - v2;
                    return v1 * k;
                };
                xl = filt (xl, 0);
                xr = filt (xr, 1);
            }

            const float amp = ae * v.vel;
            accL += xl * amp;
            accR += xr * amp;
        }

        float l = std::tanh (accL * 0.3f * drive);
        float r = std::tanh (accR * 0.3f * drive);

        // EQ
        l = eqHi.process (eqMid.process (eqLo.process (l, 0), 0), 0);
        r = eqHi.process (eqMid.process (eqLo.process (r, 1), 1), 1);

        // chorus
        chBuf[0][(size_t) chW] = l;
        chBuf[1][(size_t) chW] = r;
        if (P.chMix > 0.001f)
        {
            float wet[2];
            for (int c = 0; c < 2; ++c)
            {
                float ph = chPhase + 0.25f * (float) c;
                ph -= std::floor (ph);
                const float d = (0.010f + 0.008f * P.chDepth * (0.5f + 0.5f * std::sin (kTwoPi * ph))) * fsr;
                float rp = (float) chW - d;
                if (rp < 0.f) rp += (float) chSize;
                int i0 = (int) rp;
                const float fr = rp - (float) i0;
                if (i0 >= chSize) i0 -= chSize;
                const int i1 = (i0 + 1) % chSize;
                wet[c] = chBuf[c][(size_t) i0] * (1.f - fr) + chBuf[c][(size_t) i1] * fr;
            }
            l = l * (1.f - P.chMix * 0.4f) + wet[0] * P.chMix * 0.6f;
            r = r * (1.f - P.chMix * 0.4f) + wet[1] * P.chMix * 0.6f;
        }
        chW = (chW + 1) % chSize;
        chPhase += chStep;
        if (chPhase >= 1.f) chPhase -= 1.f;

        // delay (stereo ping-pong)
        dlSmooth += (dlTarget - dlSmooth) * 0.0005f;
        float rp = (float) dlW - dlSmooth;
        if (rp < 0.f) rp += (float) dlSize;
        int i0 = (int) rp;
        const float fr = rp - (float) i0;
        if (i0 >= dlSize) i0 -= dlSize;
        const int i1 = (i0 + 1) % dlSize;
        const float dL = dlBuf[0][(size_t) i0] * (1.f - fr) + dlBuf[0][(size_t) i1] * fr;
        const float dR = dlBuf[1][(size_t) i0] * (1.f - fr) + dlBuf[1][(size_t) i1] * fr;
        dlBuf[0][(size_t) dlW] = l + P.dlFb * dR;
        dlBuf[1][(size_t) dlW] = r + P.dlFb * dL;
        dlW = (dlW + 1) % dlSize;
        l += dL * P.dlMix;
        r += dR * P.dlMix;

        outL[n] = l;
        outR[n] = r;
    }
    midi.clear();

    // reverb
    if (P.rvMix > 0.001f)
    {
        Reverb::Parameters rp;
        rp.roomSize = P.rvSize;
        rp.damping = P.rvDamp;
        rp.wetLevel = P.rvMix * 0.3f;
        rp.dryLevel = 0.5f;
        rp.width = 1.f;
        rp.freezeMode = 0.f;
        reverb.setParameters (rp);
        reverb.processStereo (outL, outR, ns);
    }

    // master + scope feed
    const int w = scopeW.load (std::memory_order_relaxed);
    for (int n = 0; n < ns; ++n)
    {
        outL[n] *= P.master;
        outR[n] *= P.master;
        scope[(size_t) ((w + n) & (kScopeSize - 1))].store (0.5f * (outL[n] + outR[n]), std::memory_order_relaxed);
    }
    scopeW.store ((w + ns) & (kScopeSize - 1), std::memory_order_relaxed);
}

// ------------------------------------------------------------------ state
void TriOscProcessor::getStateInformation (MemoryBlock& dest)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, dest);
}

void TriOscProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (ValueTree::fromXml (*xml));
}

// ======================================================================
//                                 UI
// ======================================================================
namespace ui
{
    static const Colour bgTop    (0xff0b0e15);
    static const Colour bgBottom (0xff151a26);
    static const Colour card     (0xff1a1f2e);
    static const Colour edge     (0xff2a3146);
    static const Colour accent   (0xff4de2ff);
    static const Colour accent2  (0xffa07bff);
    static const Colour text     (0xffe8ebf4);
    static const Colour textDim  (0xff8a93aa);
}

static Font uiFont (float h, bool bold = false)
{
    return Font (FontOptions ("Segoe UI", h, bold ? Font::bold : Font::plain));
}

// ------------------------------------------------------------ look & feel
class KnobLookAndFeel : public LookAndFeel_V4
{
public:
    KnobLookAndFeel()
    {
        setColour (ComboBox::textColourId, ui::text);
        setColour (ComboBox::backgroundColourId, Colour (0xff10131b));
        setColour (ComboBox::outlineColourId, ui::edge);
        setColour (ComboBox::arrowColourId, ui::accent);
        setColour (PopupMenu::backgroundColourId, Colour (0xff151926));
        setColour (PopupMenu::textColourId, ui::text);
        setColour (PopupMenu::highlightedBackgroundColourId, ui::accent.withAlpha (0.25f));
        setColour (PopupMenu::highlightedTextColourId, Colours::white);
        setColour (Label::textColourId, ui::text);
        setColour (TextEditor::backgroundColourId, Colour (0xff10131b));
        setColour (TextEditor::textColourId, ui::text);
        setColour (TextEditor::highlightColourId, ui::accent.withAlpha (0.35f));
        setColour (TextEditor::outlineColourId, ui::edge);
        setColour (TextEditor::focusedOutlineColourId, ui::accent);
        setColour (CaretComponent::caretColourId, ui::accent);
    }

    Font getComboBoxFont (ComboBox&) override { return uiFont (13.f); }
    Font getPopupMenuFont() override { return uiFont (13.f); }
    Font getLabelFont (Label& l) override
    {
        return uiFont (l.getFont().getHeight(), l.getFont().isBold());
    }

    void drawRotarySlider (Graphics& g, int x, int y, int w, int h, float pos,
                           float a0, float a1, Slider& s) override
    {
        auto bounds = Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (3.f);
        const float d = jmin (bounds.getWidth(), bounds.getHeight(), 88.f);
        const auto c = bounds.getCentre();
        const float r = d * 0.5f;
        const bool hot = s.isMouseOverOrDragging();
        const float thick = jmax (3.f, d * 0.085f);
        const float trackR = r - thick * 0.5f - 1.f;
        const float bodyR = trackR - thick - 2.f;
        const float ang = a0 + pos * (a1 - a0);

        // knob body
        g.setGradientFill (ColourGradient (Colour (0xff2e354b), c.x - bodyR * 0.5f, c.y - bodyR,
                                           Colour (0xff12151f), c.x + bodyR * 0.4f, c.y + bodyR, false));
        g.fillEllipse (c.x - bodyR, c.y - bodyR, bodyR * 2.f, bodyR * 2.f);
        g.setColour (Colours::white.withAlpha (hot ? 0.14f : 0.07f));
        g.drawEllipse (c.x - bodyR, c.y - bodyR, bodyR * 2.f, bodyR * 2.f, 1.f);

        // track
        Path track;
        track.addCentredArc (c.x, c.y, trackR, trackR, 0.f, a0, a1, true);
        g.setColour (Colour (0xff262c40));
        g.strokePath (track, PathStrokeType (thick, PathStrokeType::curved, PathStrokeType::rounded));

        // value arc (bipolar params fill from the centre)
        float zeroAng = a0;
        if (s.getMinimum() < 0.0 && s.getMaximum() > 0.0)
            zeroAng = a0 + (float) s.valueToProportionOfLength (0.0) * (a1 - a0);
        const float va = jmin (zeroAng, ang), vb = jmax (zeroAng, ang);

        if (vb - va > 0.001f)
        {
            Path val;
            val.addCentredArc (c.x, c.y, trackR, trackR, 0.f, va, vb, true);
            ColourGradient grad (ui::accent2, c.x - trackR, c.y + trackR,
                                 ui::accent, c.x + trackR, c.y - trackR, false);
            for (int k = 3; k >= 1; --k)
            {
                auto gg = grad;
                gg.multiplyOpacity (hot ? 0.16f : 0.09f);
                g.setGradientFill (gg);
                g.strokePath (val, PathStrokeType (thick + (float) k * 3.f,
                                                   PathStrokeType::curved, PathStrokeType::rounded));
            }
            g.setGradientFill (grad);
            g.strokePath (val, PathStrokeType (thick, PathStrokeType::curved, PathStrokeType::rounded));
        }

        // pointer
        Path pp;
        pp.startNewSubPath (c.x + std::sin (ang) * bodyR * 0.30f, c.y - std::cos (ang) * bodyR * 0.30f);
        pp.lineTo (c.x + std::sin (ang) * bodyR * 0.88f, c.y - std::cos (ang) * bodyR * 0.88f);
        g.setColour (hot ? Colours::white : ui::text);
        g.strokePath (pp, PathStrokeType (2.2f, PathStrokeType::curved, PathStrokeType::rounded));
    }

    void drawToggleButton (Graphics& g, ToggleButton& b, bool hover, bool) override
    {
        auto r = b.getLocalBounds().toFloat().reduced (1.f);
        const bool on = b.getToggleState();
        g.setColour (on ? ui::accent.withAlpha (0.92f) : Colour (0xff262c40));
        g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
        if (hover)
        {
            g.setColour (Colours::white.withAlpha (0.08f));
            g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
        }
        const float kd = r.getHeight() - 6.f;
        const float kx = on ? r.getRight() - kd - 3.f : r.getX() + 3.f;
        g.setColour (on ? Colour (0xff0b0e15) : ui::textDim);
        g.fillEllipse (kx, r.getY() + 3.f, kd, kd);
    }

    void drawComboBox (Graphics& g, int w, int h, bool, int, int, int, int, ComboBox& box) override
    {
        auto r = Rectangle<float> (0.f, 0.f, (float) w, (float) h).reduced (0.5f);
        g.setColour (Colour (0xff10131b));
        g.fillRoundedRectangle (r, 7.f);
        g.setColour (box.isMouseOver (true) ? ui::accent.withAlpha (0.7f) : ui::edge);
        g.drawRoundedRectangle (r, 7.f, 1.f);
        Path arrow;
        const float ax = (float) w - 16.f, ay = (float) h * 0.5f;
        arrow.addTriangle (ax - 4.f, ay - 2.f, ax + 4.f, ay - 2.f, ax, ay + 3.f);
        g.setColour (ui::accent);
        g.fillPath (arrow);
    }

    Label* createSliderTextBox (Slider& s) override
    {
        auto* l = LookAndFeel_V4::createSliderTextBox (s);
        l->setFont (uiFont (12.f));
        l->setJustificationType (Justification::centred);
        return l;
    }
};

// -------------------------------------------------------------- param cell
class ParamCell : public Component
{
public:
    ParamCell (AudioProcessorValueTreeState& apvts, const String& id, const String& displayName)
    {
        auto* param = apvts.getParameter (id);

        label.setText (displayName, dontSendNotification);
        label.setJustificationType (Justification::centred);
        label.setFont (uiFont (11.5f));
        label.setColour (Label::textColourId, ui::textDim);
        label.setInterceptsMouseClicks (false, false);
        addAndMakeVisible (label);

        if (auto* ch = dynamic_cast<AudioParameterChoice*> (param))
        {
            auto cb = std::make_unique<ComboBox>();
            cb->addItemList (ch->choices, 1);
            att = std::make_shared<AudioProcessorValueTreeState::ComboBoxAttachment> (apvts, id, *cb);
            ctl = std::move (cb);
            kind = Combo;
        }
        else if (dynamic_cast<AudioParameterBool*> (param) != nullptr)
        {
            auto tb = std::make_unique<ToggleButton>();
            att = std::make_shared<AudioProcessorValueTreeState::ButtonAttachment> (apvts, id, *tb);
            ctl = std::move (tb);
            kind = Toggle;
        }
        else
        {
            auto s = std::make_unique<Slider> (Slider::RotaryHorizontalVerticalDrag, Slider::TextBoxBelow);
            s->setRotaryParameters (kPi * 1.25f, kPi * 2.75f, true);
            s->setTextBoxStyle (Slider::TextBoxBelow, false, 84, 16);
            s->setColour (Slider::textBoxTextColourId, ui::text.withAlpha (0.85f));
            s->setColour (Slider::textBoxOutlineColourId, Colours::transparentBlack);
            s->setColour (Slider::textBoxBackgroundColourId, Colours::transparentBlack);
            s->setColour (Slider::textBoxHighlightColourId, ui::accent.withAlpha (0.35f));
            att = std::make_shared<AudioProcessorValueTreeState::SliderAttachment> (apvts, id, *s);
            ctl = std::move (s);
            kind = Knob;
        }
        addAndMakeVisible (*ctl);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        label.setBounds (r.removeFromTop (16));
        switch (kind)
        {
            case Knob:   ctl->setBounds (r); break;
            case Combo:  ctl->setBounds (r.withSizeKeepingCentre (jmin (r.getWidth() - 8, 150), 26)); break;
            case Toggle: ctl->setBounds (r.withSizeKeepingCentre (44, 24)); break;
        }
    }

private:
    enum Kind { Knob, Combo, Toggle };
    Kind kind = Knob;
    Label label;
    std::unique_ptr<Component> ctl;
    std::shared_ptr<void> att; // declared last so it is destroyed first
};

// -------------------------------------------------------------------- card
class Card : public Component
{
public:
    Card (AudioProcessorValueTreeState& apvts, const String& t, const String& prefix,
          const std::vector<String>& ids, int c)
        : cols (c), title (t)
    {
        for (auto& id : ids)
        {
            auto* param = apvts.getParameter (id);
            if (param == nullptr) continue;
            String name = param->getName (40);
            if (prefix.isNotEmpty() && name.startsWith (prefix))
                name = name.substring (prefix.length());
            cells.push_back (std::make_unique<ParamCell> (apvts, id, name));
            addAndMakeVisible (*cells.back());
        }
    }

    void paint (Graphics& g) override
    {
        auto b = getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (ui::card);
        g.fillRoundedRectangle (b, 12.f);
        g.setGradientFill (ColourGradient (Colours::white.withAlpha (0.05f), 0.f, 0.f,
                                           Colours::transparentWhite, 0.f, 70.f, false));
        g.fillRoundedRectangle (b, 12.f);
        g.setColour (ui::edge);
        g.drawRoundedRectangle (b, 12.f, 1.f);

        g.setColour (ui::accent);
        g.fillRoundedRectangle (14.f, 15.f, 3.f, 12.f, 1.5f);
        g.setColour (ui::text.withAlpha (0.92f));
        g.setFont (uiFont (12.f, true));
        g.drawText (title.toUpperCase(), 24, 10, getWidth() - 30, 22, Justification::centredLeft);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (10, 8);
        area.removeFromTop (26);
        const int n = (int) cells.size();
        if (n == 0) return;
        const int rows = (n + cols - 1) / cols;
        const int cellW = area.getWidth() / cols;
        const int cellH = jmin (area.getHeight() / rows, 132);
        const int y0 = area.getY() + (area.getHeight() - cellH * rows) / 2;
        for (int i = 0; i < n; ++i)
            cells[(size_t) i]->setBounds (area.getX() + (i % cols) * cellW, y0 + (i / cols) * cellH, cellW, cellH);
    }

    int cols;

private:
    String title;
    std::vector<std::unique_ptr<ParamCell>> cells;
};

// -------------------------------------------------------------------- page
class Page : public Component
{
public:
    void addCard (std::unique_ptr<Card> c)
    {
        addAndMakeVisible (*c);
        cards.push_back (std::move (c));
    }

    void setExtra (std::unique_ptr<Component> e, float frac)
    {
        extra = std::move (e);
        extraFrac = frac;
        addAndMakeVisible (*extra);
    }

    Component* getExtra() { return extra.get(); }

    void resized() override
    {
        auto r = getLocalBounds();
        const int gap = 12;
        if (extra != nullptr)
        {
            extra->setBounds (r.removeFromTop ((int) ((float) r.getHeight() * extraFrac)));
            r.removeFromTop (gap);
        }
        if (cards.empty()) return;

        int total = 0;
        for (auto& c : cards) total += c->cols;
        const int availW = r.getWidth() - gap * ((int) cards.size() - 1);
        int x = r.getX();
        for (size_t i = 0; i < cards.size(); ++i)
        {
            const int w = (i + 1 == cards.size()) ? r.getRight() - x : availW * cards[i]->cols / total;
            cards[i]->setBounds (x, r.getY(), w, r.getHeight());
            x += w + gap;
        }
    }

private:
    std::vector<std::unique_ptr<Card>> cards;
    std::unique_ptr<Component> extra;
    float extraFrac = 0.f;
};

// ------------------------------------------------------------------ tab bar
class TabBar : public Component, private Timer
{
public:
    std::function<void (int)> onChange;

    explicit TabBar (const StringArray& n) : names (n) { startTimerHz (60); }

    void paint (Graphics& g) override
    {
        const float tw = (float) getWidth() / (float) names.size();
        auto full = getLocalBounds().toFloat();
        g.setColour (Colour (0xff10131b));
        g.fillRoundedRectangle (full, 10.f);
        g.setColour (ui::edge);
        g.drawRoundedRectangle (full.reduced (0.5f), 10.f, 1.f);

        // sliding highlight
        auto hl = Rectangle<float> (indicator * tw, 0.f, tw, full.getHeight()).reduced (3.f);
        g.setGradientFill (ColourGradient (ui::accent2.withAlpha (0.30f), hl.getX(), 0.f,
                                           ui::accent.withAlpha (0.30f), hl.getRight(), 0.f, false));
        g.fillRoundedRectangle (hl, 8.f);
        g.setColour (ui::accent.withAlpha (0.8f));
        g.fillRoundedRectangle (hl.getX() + hl.getWidth() * 0.3f, hl.getBottom() - 2.f,
                                hl.getWidth() * 0.4f, 2.f, 1.f);

        g.setFont (uiFont (11.5f, true));
        for (int i = 0; i < names.size(); ++i)
        {
            const bool sel = (i == selected);
            g.setColour (sel ? Colours::white : (i == hover ? ui::text : ui::textDim));
            g.drawText (names[i], Rectangle<float> ((float) i * tw, 0.f, tw, full.getHeight()),
                        Justification::centred);
        }
    }

    void mouseDown (const MouseEvent& e) override
    {
        const int i = indexAt (e.x);
        if (i >= 0 && i != selected)
        {
            selected = i;
            if (onChange) onChange (i);
        }
    }
    void mouseMove (const MouseEvent& e) override { hover = indexAt (e.x); repaint(); }
    void mouseExit (const MouseEvent&) override { hover = -1; repaint(); }

private:
    int indexAt (int x) const
    {
        if (getWidth() <= 0) return -1;
        return jlimit (0, names.size() - 1, x * names.size() / getWidth());
    }

    void timerCallback() override
    {
        const float target = (float) selected;
        if (std::abs (target - indicator) > 0.0005f)
        {
            indicator += (target - indicator) * 0.2f;
            if (std::abs (target - indicator) < 0.002f) indicator = target;
            repaint();
        }
    }

    StringArray names;
    int selected = 0, hover = -1;
    float indicator = 0.f;
};

// -------------------------------------------------------------- oscilloscope
class Scope : public Component
{
public:
    explicit Scope (TriOscProcessor& p) : proc (p) {}

    void paint (Graphics& g) override
    {
        auto b = getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (Colour (0xff0a0d13));
        g.fillRoundedRectangle (b, 12.f);
        g.setColour (ui::edge);
        g.drawRoundedRectangle (b, 12.f, 1.f);

        auto area = b.reduced (14.f, 12.f);

        g.setColour (Colours::white.withAlpha (0.045f));
        for (int i = 1; i < 8; ++i)
            g.drawVerticalLine ((int) (area.getX() + area.getWidth() * (float) i / 8.f), area.getY(), area.getBottom());
        for (int j = 0; j <= 4; ++j)
        {
            g.setColour (Colours::white.withAlpha (j == 2 ? 0.12f : 0.045f));
            g.drawHorizontalLine ((int) (area.getY() + area.getHeight() * (float) j / 4.f), area.getX(), area.getRight());
        }

        g.setColour (ui::textDim.withAlpha (0.6f));
        g.setFont (uiFont (10.f, true));
        g.drawText ("OSCILLOSCOPE", (int) b.getX() + 14, (int) b.getY() + 6, 120, 12, Justification::centredLeft);

        constexpr int total = 3584, N = 1536;
        float buf[total];
        proc.getScope (buf, total);

        int start = 0;
        for (int i = 0; i < 2048; ++i)
            if (buf[i] <= 0.f && buf[i + 1] > 0.f) { start = i; break; }

        float peak = 0.f;
        for (int i = 0; i < N; ++i) peak = jmax (peak, std::abs (buf[start + i]));

        const float targetScale = jlimit (1.f, 6.f, 0.8f / jmax (peak, 0.001f));
        scale += (targetScale - scale) * 0.12f;
        activity += ((peak > 0.001f ? 1.f : 0.25f) - activity) * 0.12f;

        const int px = jmax (2, (int) area.getWidth());
        Path path;
        for (int x = 0; x <= px; ++x)
        {
            const int idx = start + (int) ((float) x / (float) px * (float) (N - 1));
            const float v = jlimit (-1.f, 1.f, buf[idx] * scale);
            const float y = area.getCentreY() - v * area.getHeight() * 0.48f;
            if (x == 0) path.startNewSubPath (area.getX(), y);
            else        path.lineTo (area.getX() + (float) x, y);
        }

        ColourGradient grad (ui::accent2, area.getX(), 0.f, ui::accent, area.getRight(), 0.f, false);
        const float widths[3] = { 7.f, 4.f, 1.8f };
        const float alphas[3] = { 0.08f, 0.16f, 1.f };
        for (int k = 0; k < 3; ++k)
        {
            auto gg = grad;
            gg.multiplyOpacity (alphas[k] * activity);
            g.setGradientFill (gg);
            g.strokePath (path, PathStrokeType (widths[k], PathStrokeType::curved, PathStrokeType::rounded));
        }
    }

private:
    TriOscProcessor& proc;
    float scale = 1.f, activity = 0.25f;
};

// ----------------------------------------------------------------- eq curve
class EqCurve : public Component
{
public:
    explicit EqCurve (TriOscProcessor& p) : proc (p) {}

    void paint (Graphics& g) override
    {
        auto b = getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (Colour (0xff0a0d13));
        g.fillRoundedRectangle (b, 12.f);
        g.setColour (ui::edge);
        g.drawRoundedRectangle (b, 12.f, 1.f);

        auto a = b.reduced (18.f, 16.f);
        const double sr = proc.getSampleRate() > 0.0 ? proc.getSampleRate() : 44100.0;
        auto v = [this] (const char* id) { return proc.apvts.getRawParameterValue (id)->load(); };

        Biquad lo, mid, hi;
        lo.makeLowShelf (sr, v ("eq_lf"), v ("eq_lg"));
        mid.makePeak (sr, v ("eq_mf"), v ("eq_mg"), v ("eq_mq"));
        hi.makeHighShelf (sr, v ("eq_hf"), v ("eq_hg"));

        auto freqToX = [&] (float f) { return a.getX() + a.getWidth() * std::log10 (f / 20.f) / 3.f; };
        auto dbToY = [&] (float db) { return a.getCentreY() - db / 15.f * (a.getHeight() * 0.5f); };

        g.setFont (uiFont (10.f));
        const float fl[] = { 50.f, 100.f, 200.f, 500.f, 1000.f, 2000.f, 5000.f, 10000.f };
        const char* ft[] = { "50", "100", "200", "500", "1k", "2k", "5k", "10k" };
        for (int i = 0; i < 8; ++i)
        {
            const float x = freqToX (fl[i]);
            g.setColour (Colours::white.withAlpha (0.05f));
            g.drawVerticalLine ((int) x, a.getY(), a.getBottom());
            g.setColour (ui::textDim.withAlpha (0.6f));
            g.drawText (ft[i], (int) x - 16, (int) a.getBottom() - 12, 32, 12, Justification::centred);
        }
        for (int db = -12; db <= 12; db += 6)
        {
            g.setColour (Colours::white.withAlpha (db == 0 ? 0.14f : 0.05f));
            g.drawHorizontalLine ((int) dbToY ((float) db), a.getX(), a.getRight());
            g.setColour (ui::textDim.withAlpha (0.6f));
            g.drawText (String (db), (int) a.getX() + 2, (int) dbToY ((float) db) - 12, 30, 12, Justification::centredLeft);
        }

        Path curve;
        const int px = jmax (2, (int) a.getWidth());
        for (int x = 0; x <= px; x += 2)
        {
            const float f = 20.f * std::pow (10.f, 3.f * (float) x / (float) px);
            const float mag = lo.magnitude (f, sr) * mid.magnitude (f, sr) * hi.magnitude (f, sr);
            const float db = jlimit (-15.f, 15.f, 20.f * std::log10 (jmax (mag, 1.0e-6f)));
            const float y = dbToY (db);
            if (x == 0) curve.startNewSubPath (a.getX(), y);
            else        curve.lineTo (a.getX() + (float) x, y);
        }

        Path fill (curve);
        fill.lineTo (a.getRight(), a.getCentreY());
        fill.lineTo (a.getX(), a.getCentreY());
        fill.closeSubPath();
        g.setGradientFill (ColourGradient (ui::accent.withAlpha (0.20f), 0.f, a.getY(),
                                           ui::accent2.withAlpha (0.04f), 0.f, a.getBottom(), false));
        g.fillPath (fill);

        ColourGradient grad (ui::accent2, a.getX(), 0.f, ui::accent, a.getRight(), 0.f, false);
        auto glow = grad;
        glow.multiplyOpacity (0.18f);
        g.setGradientFill (glow);
        g.strokePath (curve, PathStrokeType (6.f, PathStrokeType::curved, PathStrokeType::rounded));
        g.setGradientFill (grad);
        g.strokePath (curve, PathStrokeType (2.f, PathStrokeType::curved, PathStrokeType::rounded));
    }

private:
    TriOscProcessor& proc;
};

// ------------------------------------------------------------------ editor
class TriOscEditor : public AudioProcessorEditor, private Timer
{
public:
    explicit TriOscEditor (TriOscProcessor& p)
        : AudioProcessorEditor (&p), proc (p),
          tabs (StringArray { "OSCILLATORS", "FILTER", "MOD", "MATRIX", "EFFECTS", "EQ" }),
          scope (p)
    {
        setLookAndFeel (&laf);
        auto& a = proc.apvts;
        auto card = [&] (const String& title, const String& prefix, std::vector<String> ids, int cols)
        {
            return std::make_unique<Card> (a, title, prefix, ids, cols);
        };

        // 1. oscillators
        {
            auto pg = std::make_unique<Page>();
            for (int i = 1; i <= 3; ++i)
            {
                String s = "o" + String (i) + "_";
                pg->addCard (card ("Oscillator " + String (i), "Osc " + String (i) + " ",
                                   { s + "on", s + "wave", s + "oct", s + "semi", s + "fine", s + "level",
                                     s + "pan", s + "pw", s + "phase", s + "uni", s + "det", s + "spread" }, 4));
            }
            pages.push_back (std::move (pg));
        }
        // 2. filter
        {
            auto pg = std::make_unique<Page>();
            pg->addCard (card ("Filter", "Filter ", { "ftype", "cutoff", "res", "fenv", "keytrack" }, 3));
            pg->addCard (card ("Filter Env", "Filter Env ", { "f_a", "f_d", "f_s", "f_r" }, 2));
            pg->addCard (card ("Amp Env", "Amp Env ", { "a_a", "a_d", "a_s", "a_r" }, 2));
            pages.push_back (std::move (pg));
        }
        // 3. modulation
        {
            auto pg = std::make_unique<Page>();
            pg->addCard (card ("LFO", "LFO ", { "lfo_shape", "lfo_rate", "lfo_pitch", "lfo_cut", "lfo_amp" }, 3));
            pg->addCard (card ("Voice", "Voice ", { "fm", "ring", "glide" }, 3));
            pages.push_back (std::move (pg));
        }
        // 4. matrix
        {
            auto pg = std::make_unique<Page>();
            pg->addCard (card ("Slots 1 - 2", "", { "m1_src", "m1_dst", "m1_amt", "m2_src", "m2_dst", "m2_amt" }, 3));
            pg->addCard (card ("Slots 3 - 4", "", { "m3_src", "m3_dst", "m3_amt", "m4_src", "m4_dst", "m4_amt" }, 3));
            pages.push_back (std::move (pg));
        }
        // 5. effects
        {
            auto pg = std::make_unique<Page>();
            pg->addCard (card ("Chorus", "Chorus ", { "ch_rate", "ch_depth", "ch_mix" }, 3));
            pg->addCard (card ("Delay", "Delay ", { "dl_time", "dl_fb", "dl_mix" }, 3));
            pg->addCard (card ("Reverb", "Reverb ", { "rv_size", "rv_damp", "rv_mix" }, 3));
            pg->addCard (card ("Output", "Output ", { "drive", "master" }, 2));
            pages.push_back (std::move (pg));
        }
        // 6. eq
        {
            auto pg = std::make_unique<Page>();
            auto curve = std::make_unique<EqCurve> (proc);
            eqCurve = curve.get();
            pg->setExtra (std::move (curve), 0.48f);
            pg->addCard (card ("Low Shelf", "Low ", { "eq_lf", "eq_lg" }, 2));
            pg->addCard (card ("Mid Peak", "Mid ", { "eq_mf", "eq_mg", "eq_mq" }, 3));
            pg->addCard (card ("High Shelf", "High ", { "eq_hf", "eq_hg" }, 2));
            pages.push_back (std::move (pg));
        }

        for (size_t i = 0; i < pages.size(); ++i)
        {
            addAndMakeVisible (*pages[i]);
            pages[i]->setVisible (i == 0);
        }
        addAndMakeVisible (tabs);
        addAndMakeVisible (scope);
        tabs.onChange = [this] (int i) { switchTo (i); };

        setSize (1040, 660);
        startTimerHz (60);
    }

    ~TriOscEditor() override
    {
        stopTimer();
        animator.cancelAllAnimations (false);
        setLookAndFeel (nullptr);
    }

    void paint (Graphics& g) override
    {
        g.setGradientFill (ColourGradient (ui::bgTop, 0.f, 0.f, ui::bgBottom, 0.f, (float) getHeight(), false));
        g.fillAll();
        g.setGradientFill (ColourGradient (ui::accent.withAlpha (0.08f), 120.f, 0.f,
                                           Colours::transparentBlack, 120.f, 340.f, true));
        g.fillRect (getLocalBounds());
        g.setGradientFill (ColourGradient (ui::accent2.withAlpha (0.07f), (float) getWidth(), (float) getHeight(),
                                           Colours::transparentBlack, (float) getWidth() - 420.f, (float) getHeight(), true));
        g.fillRect (getLocalBounds());

        g.setColour (ui::text);
        g.setFont (uiFont (19.f, true));
        g.drawText ("LADDS COOL OSCILLATOR", 20, 10, 320, 26, Justification::centredLeft);
        g.setColour (ui::accent);
        g.setFont (uiFont (10.f, true));
        g.drawText ("3-OSC SYNTHESIZER", 21, 35, 320, 14, Justification::centredLeft);

        g.setColour (ui::edge.withAlpha (0.6f));
        g.drawHorizontalLine (58, 16.f, (float) getWidth() - 16.f);

        // footer credits
        g.setColour (Colours::white.withAlpha (0.32f));
        g.setFont (uiFont (10.5f));
        g.drawText ("made by yours truly liam mcconnell :)", 16, getHeight() - 22, 400, 18, Justification::centredLeft);
        g.drawText ("vibecoded with claude sonnet 5.5", getWidth() - 416, getHeight() - 22, 400, 18, Justification::centredRight);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        auto header = r.removeFromTop (58);
        tabs.setBounds (header.withTrimmedLeft (340).reduced (0, 12).withTrimmedRight (20));
        r.removeFromBottom (24);
        scope.setBounds (r.removeFromBottom (132).reduced (16, 0).withTrimmedBottom (4));
        contentBounds = r.reduced (16, 6);
        for (auto& pg : pages) pg->setBounds (contentBounds);
    }

private:
    void timerCallback() override
    {
        scope.repaint();
        if (eqCurve != nullptr && eqCurve->isShowing()) eqCurve->repaint();
    }

    void switchTo (int idx)
    {
        if (idx == current || idx < 0 || idx >= (int) pages.size()) return;

        const int shift = 60 * (idx > current ? 1 : -1);
        auto* outPage = pages[(size_t) current].get();
        auto* inPage = pages[(size_t) idx].get();

        animator.cancelAnimation (outPage, false);
        animator.cancelAnimation (inPage, false);

        inPage->setBounds (contentBounds.translated (shift, 0));
        inPage->setAlpha (0.f);
        inPage->setVisible (true);

        animator.animateComponent (outPage, contentBounds.translated (-shift, 0), 0.f, 220, false, 1.0, 1.0);
        animator.animateComponent (inPage, contentBounds, 1.f, 320, false, 3.0, 0.0);
        current = idx;

        Component::SafePointer<TriOscEditor> safe (this);
        Timer::callAfterDelay (360, [safe, outPage]
        {
            if (safe != nullptr && safe->pages[(size_t) safe->current].get() != outPage)
            {
                outPage->setVisible (false);
                outPage->setAlpha (1.f);
                outPage->setBounds (safe->contentBounds);
            }
        });
    }

    KnobLookAndFeel laf; // first member = destroyed last
    TriOscProcessor& proc;
    TabBar tabs;
    Scope scope;
    ComponentAnimator animator;
    std::vector<std::unique_ptr<Page>> pages;
    EqCurve* eqCurve = nullptr;
    Rectangle<int> contentBounds;
    int current = 0;
};

AudioProcessorEditor* TriOscProcessor::createEditor() { return new TriOscEditor (*this); }

// Entry point that JUCE's plugin wrapper calls to create the plugin
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new TriOscProcessor();
}
