#include "PluginProcessor.h"
#include "PluginEditor.h"

using APVTS = juce::AudioProcessorValueTreeState;

juce::AudioProcessorValueTreeState::ParameterLayout DiscoSauceProcessor::createLayout()
{
    using namespace juce;
    APVTS::ParameterLayout layout;
    const Preset& p0 = presetTable()[0];

    layout.add(std::make_unique<AudioParameterChoice>(ParameterID { "preset", 1 }, "Preset",
               StringArray { presetTable()[0].name, presetTable()[1].name, presetTable()[2].name, presetTable()[3].name }, 0));

    auto knob = [&](const char* id, const char* name, float def)
    {
        layout.add(std::make_unique<AudioParameterFloat>(ParameterID { id, 1 }, name,
                   NormalisableRange<float>(0.0f, 1.0f, 0.001f), def));
    };
    knob("comp", "Compressor", p0.kComp);
    knob("reverb", "Reverb", p0.kRev);
    knob("delay", "Delay", p0.kDelay);
    knob("eq", "Equalizer", p0.kEq);
    knob("tune", "Auto-Tune", p0.kTune);
    knob("mix", "Dry/Wet", 1.0f);

    StringArray keys, scales;
    for (int i = 0; i < 12; ++i) { keys.add(keyNames()[i]); scales.add(scaleNames()[i]); }
    layout.add(std::make_unique<AudioParameterChoice>(ParameterID { "key", 1 }, "Key", keys, 0));
    layout.add(std::make_unique<AudioParameterChoice>(ParameterID { "scale", 1 }, "Scale", scales, 0));

    layout.add(std::make_unique<AudioParameterFloat>(ParameterID { "retune", 1 }, "Retune Speed",
               NormalisableRange<float>(0.0f, 300.0f, 0.1f, 0.4f), p0.retuneMs,
               AudioParameterFloatAttributes().withLabel("ms")));
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID { "output", 1 }, "Output",
               NormalisableRange<float>(-12.0f, 12.0f, 0.1f), 0.0f,
               AudioParameterFloatAttributes().withLabel("dB")));
    return layout;
}

DiscoSauceProcessor::DiscoSauceProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "DiscoSauce", createLayout())
{
}

bool DiscoSauceProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    return layouts.getMainInputChannelSet() == juce::AudioChannelSet::stereo()
        && layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void DiscoSauceProcessor::setParam(const juce::String& id, float plainValue)
{
    if (auto* p = apvts.getParameter(id))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost(p->convertTo0to1(plainValue));
        p->endChangeGesture();
    }
}

void DiscoSauceProcessor::applyPreset(int index)
{
    index = juce::jlimit(0, 3, index);
    const Preset& P = presetTable()[index];
    setParam("preset", (float) index);
    setParam("comp", P.kComp);
    setParam("reverb", P.kRev);
    setParam("delay", P.kDelay);
    setParam("eq", P.kEq);
    setParam("tune", P.kTune);
    setParam("retune", P.retuneMs);
    setParam("mix", 1.0f);
}

void DiscoSauceProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    sr = sampleRate;
    tune.prepare(sr);
    setLatencySamples(tune.getLatency());

    for (int c = 0; c < 2; ++c)
    {
        hpF[c].reset(); lowF[c].reset(); mudF[c].reset();
        presF[c].reset(); airF[c].reset(); essF[c].reset();
        essF[c].c = ds::makeHighPass(sr, 5500.0, 0.707);
    }
    essEnvHf = essEnvFull = 0.0;
    essGain = 1.0;

    comp1.prepare(sr);
    comp2.prepare(sr);
    delay.prepare(sr);
    reverb.prepare(sr);
    duckEnv = 0.0;

    smMix.reset(sr, 0.03);  smMix.setCurrentAndTargetValue(1.0f);
    smOut.reset(sr, 0.03);  smOut.setCurrentAndTargetValue(1.0f);
    smDly.reset(sr, 0.05);  smDly.setCurrentAndTargetValue(0.0f);
    smRev.reset(sr, 0.05);  smRev.setCurrentAndTargetValue(0.0f);

    const size_t n = (size_t) std::max(samplesPerBlock, 512);
    for (auto* v : { &dryL, &dryR, &mono, &dlyL, &dlyR, &revIn, &revL, &revR })
        v->assign(n, 0.f);
}

void DiscoSauceProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    if (buffer.getNumChannels() < 2 || n <= 0) return;

    if ((int) dryL.size() < n)
        for (auto* v : { &dryL, &dryR, &mono, &dlyL, &dlyR, &revIn, &revL, &revR })
            v->assign((size_t) n, 0.f);

    float* L = buffer.getWritePointer(0);
    float* R = buffer.getWritePointer(1);

    auto get = [this](const char* id) { return apvts.getRawParameterValue(id)->load(); };
    const int presetIdx = juce::jlimit(0, 3, (int) std::lround(get("preset")));
    const Preset& P = presetTable()[presetIdx];
    const float kC = get("comp"), kR = get("reverb"), kD = get("delay"), kE = get("eq"), kT = get("tune");
    const float mix = get("mix");

    // ---------------------------------------------------------------- 1. auto-tune
    ds::AutoTune::Settings ts;
    ts.amount = kT;
    ts.retuneMs = get("retune");
    ts.key = (int) std::lround(get("key"));
    ts.scale = (int) std::lround(get("scale"));
    tune.process(L, R, dryL.data(), dryR.data(), n, ts);

    // ---------------------------------------------------------------- 2. EQ stage A (cleanup)
    {
        const double hpHz = 20.0 + (P.hpHz - 20.0) * kE;
        const auto cHp = ds::makeHighPass(sr, hpHz, 0.707);
        const auto cLow = ds::makeLowShelf(sr, 120.0, 0.7, P.lowDb * kE);
        const auto cMud = ds::makePeak(sr, 300.0, 1.0, P.mudDb * kE);
        for (int c = 0; c < 2; ++c) { hpF[c].c = cHp; lowF[c].c = cLow; mudF[c].c = cMud; }
        for (int i = 0; i < n; ++i)
        {
            L[i] = mudF[0].process(lowF[0].process(hpF[0].process(L[i])));
            R[i] = mudF[1].process(lowF[1].process(hpF[1].process(R[i])));
        }
    }

    // ---------------------------------------------------------------- 3. dynamics + colour
    if (kC > 0.002f)
    {
        const float thr1 = -P.depthDb * kC;
        const float mk1 = -thr1 * (1.0f - 1.0f / P.ratio) * 0.4f;
        comp1.set(thr1, P.ratio, P.attMs, P.relMs, P.kneeDb, mk1);
        comp1.process(L, R, n);

        // fast peak catcher
        comp2.set(-3.0f - 4.0f * kC, 6.0f, 1.0f, 60.0f, 4.0f, 0.0f);
        comp2.process(L, R, n);

        const float drive = P.drive * kC;
        if (drive > 0.001f)
        {
            const float g = 1.0f + 6.0f * drive;
            const float inv = 1.0f / g;
            for (int i = 0; i < n; ++i)
            {
                L[i] = std::tanh(g * L[i]) * inv;
                R[i] = std::tanh(g * R[i]) * inv;
            }
        }
    }
    else
    {
        comp1.reset();
        comp2.reset();
    }

    // ---------------------------------------------------------------- 4. EQ stage B (presence / air) + de-esser
    {
        const auto cPres = ds::makePeak(sr, 3500.0, 0.8, P.presDb * kE);
        const auto cAir = ds::makeHighShelf(sr, 11000.0, 0.7, P.airDb * kE);
        for (int c = 0; c < 2; ++c) { presF[c].c = cPres; airF[c].c = cAir; }

        const double depth = P.deEss * kE;
        const double envAtt = std::exp(-1.0 / (0.001 * sr));
        const double envRel = std::exp(-1.0 / (0.040 * sr));
        for (int i = 0; i < n; ++i)
        {
            float l = airF[0].process(presF[0].process(L[i]));
            float r = airF[1].process(presF[1].process(R[i]));
            if (depth > 0.01)
            {
                const float hl = essF[0].process(l);
                const float hr = essF[1].process(r);
                const double hf = std::max(std::abs((double) hl), std::abs((double) hr));
                const double full = std::max(std::abs((double) l), std::abs((double) r)) + 1.0e-6;
                essEnvHf = (hf > essEnvHf ? envAtt : envRel) * essEnvHf + (1.0 - (hf > essEnvHf ? envAtt : envRel)) * hf;
                essEnvFull = (full > essEnvFull ? envAtt : envRel) * essEnvFull + (1.0 - (full > essEnvFull ? envAtt : envRel)) * full;
                const double ratio = essEnvHf / (essEnvFull + 1.0e-6);
                double target = 1.0;
                if (ratio > 0.45) target = std::pow(0.45 / ratio, depth * 1.5);
                target = std::max(0.25, target);
                essGain += (target - essGain) * 0.05;
                l -= (float) ((1.0 - essGain) * hl);
                r -= (float) ((1.0 - essGain) * hr);
            }
            L[i] = l;
            R[i] = r;
        }
    }

    // ---------------------------------------------------------------- 5. delay + reverb sends (ducked by the vocal)
    smDly.setTargetValue(kD * P.dlyWet);
    smRev.setTargetValue(kR * P.revWet);
    smMix.setTargetValue(mix);
    smOut.setTargetValue(juce::Decibels::decibelsToGain(get("output")));

    double bpm = 120.0;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (auto b = pos->getBpm())
                if (*b > 20.0 && *b < 400.0) bpm = *b;

    delay.set((float) (P.dlyBeats * 60.0 / bpm * sr), P.dlyFb, P.dlyLp);
    reverb.set(P.revSize, P.revDecay, P.revDamp, P.revPre);

    for (int i = 0; i < n; ++i) mono[(size_t) i] = 0.5f * (L[i] + R[i]);

    delay.process(mono.data(), dlyL.data(), dlyR.data(), n);
    for (int i = 0; i < n; ++i)
        revIn[(size_t) i] = mono[(size_t) i] + 0.5f * (dlyL[(size_t) i] + dlyR[(size_t) i]) * 0.5f;
    reverb.process(revIn.data(), revL.data(), revR.data(), n);

    const double duckAtt = std::exp(-1.0 / (0.005 * sr));
    const double duckRel = std::exp(-1.0 / (0.250 * sr));

    for (int i = 0; i < n; ++i)
    {
        const double lev = std::abs((double) mono[(size_t) i]);
        const double ca = lev > duckEnv ? duckAtt : duckRel;
        duckEnv = ca * duckEnv + (1.0 - ca) * lev;
        const float duck = 1.0f - 0.5f * (float) std::min(1.0, duckEnv * 4.0);

        const float dg = smDly.getNextValue();
        const float rg = smRev.getNextValue();
        const float m = smMix.getNextValue();
        const float og = smOut.getNextValue();

        float wl = L[i] + dlyL[(size_t) i] * dg * duck + revL[(size_t) i] * rg * (0.6f + 0.4f * duck);
        float wr = R[i] + dlyR[(size_t) i] * dg * duck + revR[(size_t) i] * rg * (0.6f + 0.4f * duck);
        wl *= og;
        wr *= og;

        // gentle safety limiter above -1 dBFS
        auto soft = [](float x)
        {
            const float a = std::abs(x), t = 0.89f;
            if (a <= t) return x;
            const float y = t + (1.0f - t) * std::tanh((a - t) / (1.0f - t));
            return x < 0 ? -y : y;
        };

        L[i] = dryL[(size_t) i] * (1.0f - m) + soft(wl) * m;
        R[i] = dryR[(size_t) i] * (1.0f - m) + soft(wr) * m;
    }
}

juce::AudioProcessorEditor* DiscoSauceProcessor::createEditor()
{
    return new DiscoSauceEditor(*this);
}

void DiscoSauceProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary(*xml, destData);
}

void DiscoSauceProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        if (xml->hasTagName(apvts.state.getType()))
            apvts.replaceState(juce::ValueTree::fromXml(*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new DiscoSauceProcessor();
}
