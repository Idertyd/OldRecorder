#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>
#include <cmath>
#include <vector>

namespace
{
    struct Biquad
    {
        float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;

        void set (bool highpass, double sr, double f, double q)
        {
            f = juce::jlimit (10.0, sr * 0.45, f);
            const double w = juce::MathConstants<double>::twoPi * f / sr;
            const double c = std::cos (w), s = std::sin (w), al = s / (2.0 * q);
            const double a0 = 1.0 + al;
            double nb0, nb1, nb2;
            if (! highpass) { nb0 = (1.0 - c) * 0.5; nb1 = 1.0 - c;    nb2 = nb0; }
            else            { nb0 = (1.0 + c) * 0.5; nb1 = -(1.0 + c); nb2 = nb0; }
            b0 = (float) (nb0 / a0); b1 = (float) (nb1 / a0); b2 = (float) (nb2 / a0);
            a1 = (float) (-2.0 * c / a0); a2 = (float) ((1.0 - al) / a0);
        }

        float process (float x)
        {
            const float y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }

        void reset() { z1 = z2 = 0; }
    };

    constexpr double centreDelayMs = 4.0;
}

class OldRecorderProcessor : public juce::AudioProcessor
{
public:
    OldRecorderProcessor()
        : AudioProcessor (BusesProperties()
                              .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                              .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
          apvts (*this, nullptr, "STATE", createLayout())
    {
    }

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
    {
        using P = juce::AudioParameterFloat;
        using ID = juce::ParameterID;
        juce::AudioProcessorValueTreeState::ParameterLayout l;
        l.add (std::make_unique<P> (ID { "amount", 1 }, "Amount", 0.0f, 1.0f, 0.7f));
        l.add (std::make_unique<P> (ID { "tone", 1 },   "Tone (LP Hz)",
                                    juce::NormalisableRange<float> (2000.0f, 16000.0f, 1.0f, 0.5f), 7500.0f));
        l.add (std::make_unique<P> (ID { "lowcut", 1 }, "Low Cut (Hz)",
                                    juce::NormalisableRange<float> (20.0f, 300.0f, 1.0f, 0.6f), 90.0f));
        l.add (std::make_unique<P> (ID { "wow", 1 },    "Wow/Flutter", 0.0f, 1.0f, 0.4f));
        l.add (std::make_unique<P> (ID { "noise", 1 },  "Hiss", 0.0f, 1.0f, 0.3f));
        l.add (std::make_unique<P> (ID { "drive", 1 },  "Saturation", 0.0f, 1.0f, 0.4f));
        l.add (std::make_unique<P> (ID { "output", 1 }, "Output (dB)", -12.0f, 12.0f, 0.0f));
        return l;
    }

    void prepareToPlay (double sampleRate, int) override
    {
        sr = sampleRate;
        bufSize = (int) (sr * 0.05) + 8;
        for (auto& d : delayBuf) d.assign ((size_t) bufSize, 0.0f);
        writePos = 0;
        for (int c = 0; c < 2; ++c) { hp[c].reset(); lp1[c].reset(); lp2[c].reset(); prevNoise[c] = 0; }
        amountSm.reset (sr, 0.05);
        amountSm.setCurrentAndTargetValue (*apvts.getRawParameterValue ("amount"));
        setLatencySamples ((int) std::round (centreDelayMs * sr / 1000.0));
    }

    void releaseResources() override {}

    bool isBusesLayoutSupported (const BusesLayout& l) const override
    {
        return l.getMainInputChannelSet() == l.getMainOutputChannelSet()
            && (l.getMainOutputChannelSet() == juce::AudioChannelSet::stereo()
                || l.getMainOutputChannelSet() == juce::AudioChannelSet::mono());
    }

    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        juce::ScopedNoDenormals noDenormals;
        const int nCh = juce::jmin (2, buffer.getNumChannels());
        const int n = buffer.getNumSamples();

        const float amt      = *apvts.getRawParameterValue ("amount");
        const float tone     = *apvts.getRawParameterValue ("tone");
        const float lowcut   = *apvts.getRawParameterValue ("lowcut");
        const float wowP     = *apvts.getRawParameterValue ("wow");
        const float noiseP   = *apvts.getRawParameterValue ("noise");
        const float driveP   = *apvts.getRawParameterValue ("drive");
        const float outGain  = juce::Decibels::decibelsToGain ((float) *apvts.getRawParameterValue ("output"));

        amountSm.setTargetValue (amt);

        // Filter cutoffs sweep from "transparent" (20k / 10 Hz) to the set values as Amount rises
        const double lpF = std::exp (juce::jmap ((double) amt, std::log (20000.0), std::log ((double) tone)));
        const double hpF = juce::jmap ((double) amt, 10.0, (double) lowcut);
        for (int c = 0; c < nCh; ++c)
        {
            hp[c].set (true, sr, hpF, 0.707);
            lp1[c].set (false, sr, lpF, 0.5412);   // 4th-order Butterworth pair
            lp2[c].set (false, sr, lpF, 1.3066);
        }

        const double twoPi = juce::MathConstants<double>::twoPi;
        const int centreSamples = (int) std::round (centreDelayMs * sr / 1000.0);

        for (int i = 0; i < n; ++i)
        {
            const float a = amountSm.getNextValue();
            const float mix = juce::jmin (1.0f, a * 2.5f);

            wowPhase  += twoPi * 0.8 / sr;   if (wowPhase  > twoPi) wowPhase  -= twoPi;
            flutPhase += twoPi * 6.5 / sr;   if (flutPhase > twoPi) flutPhase -= twoPi;
            drift += 0.0005f * (rng.nextFloat() - 0.5f);
            drift *= 0.9998f;

            const double modMs = (double) (a * wowP) * (1.2 * std::sin (wowPhase) + 0.12 * std::sin (flutPhase))
                                 + (double) (a * wowP * drift * 40.0f);
            const double delaySamples = juce::jlimit (1.0, (double) bufSize - 4.0,
                                                      (centreDelayMs + modMs) * sr / 1000.0);

            const float g = 1.0f + driveP * a * 5.0f;
            const float gNorm = std::tanh (g);
            const float noiseLevel = noiseP * a * 0.004f;

            for (int c = 0; c < nCh; ++c)
            {
                float* data = buffer.getWritePointer (c);
                auto& d = delayBuf[c];
                const float x = data[i];
                d[(size_t) writePos] = x;

                // dry, delayed by the fixed centre delay (keeps dry/wet phase aligned)
                int dryIdx = writePos - centreSamples; if (dryIdx < 0) dryIdx += bufSize;
                const float dry = d[(size_t) dryIdx];

                // wet, delayed by the wobbling delay (linear interpolation)
                double rp = (double) writePos - delaySamples; if (rp < 0) rp += bufSize;
                const int i0 = (int) rp; const int i1 = (i0 + 1) % bufSize;
                const float fr = (float) (rp - i0);
                float y = d[(size_t) i0] * (1.0f - fr) + d[(size_t) i1] * fr;

                y = std::tanh (g * y) / gNorm;          // tape-ish soft saturation
                y = hp[c].process (y);
                y = lp2[c].process (lp1[c].process (y));

                const float r = rng.nextFloat() * 2.0f - 1.0f;   // hiss (high-passed white noise)
                y += (r - prevNoise[c]) * 0.5f * noiseLevel;
                prevNoise[c] = r;

                data[i] = (dry * (1.0f - mix) + y * mix) * outGain;
            }
            if (++writePos >= bufSize) writePos = 0;
        }
    }

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Old Recorder"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& dest) override
    {
        if (auto xml = apvts.copyState().createXml()) copyXmlToBinary (*xml, dest);
    }
    void setStateInformation (const void* data, int size) override
    {
        if (auto xml = getXmlFromBinary (data, size))
            if (xml->hasTagName (apvts.state.getType())) apvts.replaceState (juce::ValueTree::fromXml (*xml));
    }

    juce::AudioProcessorValueTreeState apvts;

private:
    double sr = 44100.0;
    int bufSize = 4096, writePos = 0;
    std::vector<float> delayBuf[2];
    Biquad hp[2], lp1[2], lp2[2];
    float prevNoise[2] { 0, 0 };
    double wowPhase = 0, flutPhase = 0;
    float drift = 0;
    juce::Random rng;
    juce::SmoothedValue<float> amountSm;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OldRecorderProcessor)
};

class OldRecorderEditor : public juce::AudioProcessorEditor
{
public:
    explicit OldRecorderEditor (OldRecorderProcessor& p) : AudioProcessorEditor (&p)
    {
        const char* ids[]   = { "amount", "tone", "lowcut", "wow", "noise", "drive", "output" };
        const char* names[] = { "AMOUNT", "TONE", "LOW CUT", "WOW", "HISS", "SATURATE", "OUTPUT" };
        for (int i = 0; i < 7; ++i)
        {
            knobs[i].setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
            knobs[i].setTextBoxStyle (juce::Slider::TextBoxBelow, false, 78, 18);
            addAndMakeVisible (knobs[i]);
            labels[i].setText (names[i], juce::dontSendNotification);
            labels[i].setJustificationType (juce::Justification::centred);
            labels[i].setColour (juce::Label::textColourId, juce::Colour (0xffe8d3a8));
            addAndMakeVisible (labels[i]);
            att[i] = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (p.apvts, ids[i], knobs[i]);
        }
        knobs[0].setColour (juce::Slider::rotarySliderFillColourId, juce::Colour (0xffe0a040));
        setSize (680, 250);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (0xff2b2118));
        g.setColour (juce::Colour (0xffe8d3a8));
        g.setFont (juce::FontOptions (22.0f, juce::Font::bold));
        g.drawText ("OLD RECORDER", 20, 8, 300, 30, juce::Justification::left);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (10);
        area.removeFromTop (40);
        const int w = area.getWidth() / 7;
        for (int i = 0; i < 7; ++i)
        {
            auto col = area.removeFromLeft (w);
            labels[i].setBounds (col.removeFromTop (20));
            knobs[i].setBounds (col);
        }
    }

private:
    juce::Slider knobs[7];
    juce::Label labels[7];
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> att[7];
};

juce::AudioProcessorEditor* OldRecorderProcessor::createEditor() { return new OldRecorderEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new OldRecorderProcessor(); }
