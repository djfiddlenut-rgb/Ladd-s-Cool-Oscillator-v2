#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <vector>
#include <memory>
#include <array>
#include <atomic>
#include <cmath>

// ---------------------------------------------------------------- envelope
struct Env
{
    enum Stage { Idle, Att, Dec, Sus, Rel } stage = Idle;
    float level = 0.f;

    void trigger() { stage = Att; }
    void release() { if (stage != Idle) stage = Rel; }

    float next (float a, float d, float s, float r, float sr)
    {
        switch (stage)
        {
            case Att:
                level += 1.f / (juce::jmax (a, 0.0005f) * sr);
                if (level >= 1.f) { level = 1.f; stage = Dec; }
                break;
            case Dec:
                level += (s - level) * (1.f - std::exp (-4.f / (juce::jmax (d, 0.001f) * sr)));
                if (level <= s + 0.0005f) stage = Sus;
                break;
            case Sus:
                level = s;
                break;
            case Rel:
                level -= level * (1.f - std::exp (-4.f / (juce::jmax (r, 0.001f) * sr)));
                if (level < 0.0003f) { level = 0.f; stage = Idle; }
                break;
            default: break;
        }
        return level;
    }
};

// ------------------------------------------------------------------ voice
struct Voice
{
    bool active = false, gate = false;
    int note = 0;
    float vel = 0.f, curNote = 60.f, rnd = 0.f;
    uint32_t age = 0;
    float ph[3][7] {};
    Env aenv, fenv;
    float ic1[2] {}, ic2[2] {};
};

// ----------------------------------------------------------------- biquad
struct Biquad
{
    float b0 = 1.f, b1 = 0.f, b2 = 0.f, a1 = 0.f, a2 = 0.f;
    float z1[2] {}, z2[2] {};

    void setCoeffs (float nb0, float nb1, float nb2, float na0, float na1, float na2)
    {
        b0 = nb0 / na0; b1 = nb1 / na0; b2 = nb2 / na0;
        a1 = na1 / na0; a2 = na2 / na0;
    }

    void makePeak (double sr, float f, float gDb, float q)
    {
        f = juce::jlimit (10.f, (float) (sr * 0.45), f);
        const float A = std::pow (10.f, gDb / 40.f);
        const float w0 = juce::MathConstants<float>::twoPi * f / (float) sr;
        const float c = std::cos (w0), s = std::sin (w0);
        const float al = s / (2.f * juce::jmax (q, 0.1f));
        setCoeffs (1.f + al * A, -2.f * c, 1.f - al * A, 1.f + al / A, -2.f * c, 1.f - al / A);
    }

    void makeLowShelf (double sr, float f, float gDb)
    {
        f = juce::jlimit (10.f, (float) (sr * 0.45), f);
        const float A = std::pow (10.f, gDb / 40.f);
        const float w0 = juce::MathConstants<float>::twoPi * f / (float) sr;
        const float c = std::cos (w0), s = std::sin (w0);
        const float tsa = 2.f * std::sqrt (A) * (s * 0.70710678f);
        setCoeffs (A * ((A + 1.f) - (A - 1.f) * c + tsa),
                   2.f * A * ((A - 1.f) - (A + 1.f) * c),
                   A * ((A + 1.f) - (A - 1.f) * c - tsa),
                   (A + 1.f) + (A - 1.f) * c + tsa,
                   -2.f * ((A - 1.f) + (A + 1.f) * c),
                   (A + 1.f) + (A - 1.f) * c - tsa);
    }

    void makeHighShelf (double sr, float f, float gDb)
    {
        f = juce::jlimit (10.f, (float) (sr * 0.45), f);
        const float A = std::pow (10.f, gDb / 40.f);
        const float w0 = juce::MathConstants<float>::twoPi * f / (float) sr;
        const float c = std::cos (w0), s = std::sin (w0);
        const float tsa = 2.f * std::sqrt (A) * (s * 0.70710678f);
        setCoeffs (A * ((A + 1.f) + (A - 1.f) * c + tsa),
                   -2.f * A * ((A - 1.f) + (A + 1.f) * c),
                   A * ((A + 1.f) + (A - 1.f) * c - tsa),
                   (A + 1.f) - (A - 1.f) * c + tsa,
                   2.f * ((A - 1.f) - (A + 1.f) * c),
                   (A + 1.f) - (A - 1.f) * c - tsa);
    }

    float process (float x, int ch)
    {
        const float y = b0 * x + z1[ch];
        z1[ch] = b1 * x - a1 * y + z2[ch];
        z2[ch] = b2 * x - a2 * y;
        return y;
    }

    float magnitude (float f, double sr) const
    {
        const float w = juce::MathConstants<float>::twoPi * f / (float) sr;
        const float cw = std::cos (w), sw = std::sin (w);
        const float c2 = std::cos (2.f * w), s2 = std::sin (2.f * w);
        const float nr = b0 + b1 * cw + b2 * c2;
        const float ni = -(b1 * sw + b2 * s2);
        const float dr = 1.f + a1 * cw + a2 * c2;
        const float di = -(a1 * sw + a2 * s2);
        return std::sqrt ((nr * nr + ni * ni) / juce::jmax (dr * dr + di * di, 1.0e-12f));
    }
};

// ------------------------------------------------------------- parameters
struct OscP
{
    bool on = true;
    int wave = 2, oct = 0, semi = 0, uni = 1;
    float fine = 0, level = 1, pan = 0, pw = 0.5f, phase = 0, det = 0, spread = 0;
};

struct ModSlot
{
    int src = 0, dst = 0;
    float amt = 0.f;
};

struct Params
{
    OscP osc[3];
    ModSlot mod[4];
    float fm = 0, ring = 0, drive = 0, glideMs = 0, master = 0.7f;
    float cutoff = 20000.f, res = 0, fenvAmt = 0, keytrack = 0;
    int ftype = 0;
    float aA = 0.005f, aD = 0.3f, aS = 0.7f, aR = 0.2f;
    float fA = 0.005f, fD = 0.3f, fS = 0.f, fR = 0.2f;
    float lfoRate = 4.f, lfoPitch = 0, lfoCut = 0, lfoAmp = 0;
    int lfoShape = 0;
    float chRate = 0.6f, chDepth = 0.5f, chMix = 0.f;
    float dlTime = 350.f, dlFb = 0.4f, dlMix = 0.f;
    float rvSize = 0.5f, rvDamp = 0.5f, rvMix = 0.f;
    float eqLf = 120.f, eqLg = 0.f, eqMf = 1000.f, eqMg = 0.f, eqMq = 1.f, eqHf = 6000.f, eqHg = 0.f;
};

// -------------------------------------------------------------- processor
class TriOscProcessor : public juce::AudioProcessor
{
public:
    TriOscProcessor();

    void prepareToPlay (double sampleRate, int) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& l) const override
    {
        return l.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
    }
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Ladds Cool Oscillator"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 3.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& dest) override;
    void setStateInformation (const void* data, int size) override;

    // Copies the most recent n output samples (mono) for the oscilloscope.
    void getScope (float* dest, int n) const;

    juce::AudioProcessorValueTreeState apvts;

    static constexpr int kScopeSize = 4096;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void readParams();
    void handleMidi (const juce::MidiMessage& m);
    void noteOn (int note, float vel);
    void noteOff (int note);
    float wave (int w, float t, float dt, float pw);

    Params P;
    Voice voices[16];
    double sr = 44100.0;
    float lfoPhase = 0.f, sh = 0.f, bend = 0.f;
    int lastNote = -1;
    uint32_t counter = 0;
    juce::Random rng;

    // effects
    Biquad eqLo, eqMid, eqHi;
    std::vector<float> chBuf[2], dlBuf[2];
    int chW = 0, dlW = 0;
    float chPhase = 0.f, dlSmooth = 350.f;
    juce::Reverb reverb;

    // scope
    std::array<std::atomic<float>, kScopeSize> scope;
    std::atomic<int> scopeW { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TriOscProcessor)
};
