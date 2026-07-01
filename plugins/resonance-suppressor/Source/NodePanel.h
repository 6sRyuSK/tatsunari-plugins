#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "PluginProcessor.h"
#include "factory_ui/FactoryLookAndFeel.h"
#include "factory_ui/FactoryChrome.h"

#include <cmath>
#include <memory>

//
// A small, bright inline editor for the currently-selected reduction node, shown
// over the analyser (bottom-centre) when a node is selected. It rebinds its APVTS
// attachments to that node on selection (Pro-Q-style). Layout: On (top-left) with
// the node name painted to its right, the slope/type selector below, and the
// Freq [+ Sens] knobs filling the full height on the right. Frequencies read in
// kHz at/above 1 kHz. Horizontal "kawaii" card styling (factory_ui).
// GUI-thread only.
//
class NodePanel : public juce::Component
{
public:
    explicit NodePanel (juce::AudioProcessorValueTreeState& s) : apvts (s)
    {
        setLookAndFeel (&lnf); // knob captions / value boxes / combo text at 11 px

        onButton.setButtonText ("On");
        addAndMakeVisible (onButton);

        choiceBox.setColour (juce::ComboBox::textColourId, FactoryLookAndFeel::text());
        choiceBox.setJustificationType (juce::Justification::centred);
        addAndMakeVisible (choiceBox);

        factory_ui::styleKnob (freqSlider, freqLabel, "Freq", " Hz");
        addAndMakeVisible (freqSlider); addAndMakeVisible (freqLabel);
        factory_ui::styleKnob (sensSlider, sensLabel, "Sens", " dB");
        addAndMakeVisible (sensSlider); addAndMakeVisible (sensLabel);
    }

    ~NodePanel() override { setLookAndFeel (nullptr); }

    int  currentNode()    const noexcept { return nodeId; }
    bool isCutNode()      const noexcept { return isCut; }
    int  preferredWidth() const noexcept { return isCut ? 290 : 360; }
    static constexpr int kHeight = 104;

    // Rebind to node `id` (0 = low cut, 1 = high cut, 2.. = band). Drops the old
    // attachments first so syncing the new ones can't write back to the old node.
    void setNode (int id)
    {
        nodeId = id;
        isCut  = (id < 2);

        onAtt.reset(); choiceAtt.reset(); freqAtt.reset(); sensAtt.reset();

        nameText = nodeName();
        nameCol  = nodeColour();

        choiceBox.clear (juce::dontSendNotification);
        if (isCut) choiceBox.addItemList ({ "6 dB/oct", "12 dB/oct", "24 dB/oct", "48 dB/oct" }, 1);
        else       choiceBox.addItemList ({ "Bell", "Low Shelf", "High Shelf", "Band Shelf", "Band Reject", "Tilt" }, 1);

        sensSlider.setVisible (! isCut);
        sensLabel.setVisible (! isCut);

        using SA = juce::AudioProcessorValueTreeState::SliderAttachment;
        using BA = juce::AudioProcessorValueTreeState::ButtonAttachment;
        using CA = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
        onAtt     = std::make_unique<BA> (apvts, pid ("on"),                     onButton);
        choiceAtt = std::make_unique<CA> (apvts, pid (isCut ? "slope" : "type"), choiceBox);
        freqAtt   = std::make_unique<SA> (apvts, pid ("freq"),                   freqSlider);
        if (! isCut) sensAtt = std::make_unique<SA> (apvts, pid ("sens"), sensSlider);

        // Freq reads in kHz at/above 1 kHz, Hz below. Must run after the
        // attachment, which installs its own textFromValueFunction (see #26).
        freqSlider.textFromValueFunction = [] (double v)
        {
            if (v >= 1000.0)
            {
                const double k = v / 1000.0;
                const bool whole = std::abs (k - std::round (k)) < 0.05;
                return (whole ? juce::String ((int) std::round (k)) : juce::String (k, 1)) + " kHz";
            }
            return juce::String (juce::roundToInt (v)) + " Hz";
        };
        freqSlider.setTextValueSuffix ({}); // the lambda already prints Hz/kHz — avoid a doubled suffix
        freqSlider.updateText();
        if (! isCut) factory_ui::setSliderDecimals (sensSlider, 1);

        resized();
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        factory_ui::paintCard (g, getLocalBounds().toFloat(), 12.0f);
        // Floating node name, painted to the right of the On toggle.
        g.setColour (nameCol);
        g.setFont (juce::Font (juce::FontOptions (14.0f, juce::Font::bold)));
        g.drawText (nameText, nameArea, juce::Justification::centredLeft);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (10, 8);

        // Right: Freq (+ Sens) knobs filling the full height.
        constexpr int knobW = 64, kgap = 8;
        auto knobs = r.removeFromRight (isCut ? knobW : knobW * 2 + kgap);
        r.removeFromRight (12); // gap before the knobs

        // Left-top: On (top-left) + node name to its right.
        auto topRow = r.removeFromTop (24);
        onButton.setBounds (topRow.removeFromLeft (46).withSizeKeepingCentre (46, 22));
        topRow.removeFromLeft (6);
        nameArea = topRow; // name painted here
        r.removeFromTop (8);
        choiceBox.setBounds (r.removeFromTop (26));

        auto col = [] (juce::Rectangle<int> c, juce::Slider& s, juce::Label& l)
        {
            l.setBounds (c.removeFromTop (13));
            s.setBounds (c); // rotary + value box fill the rest (full height)
        };
        if (isCut) col (knobs, freqSlider, freqLabel);
        else
        {
            col (knobs.removeFromLeft (knobW), freqSlider, freqLabel);
            knobs.removeFromLeft (kgap);
            col (knobs, sensSlider, sensLabel);
        }
    }

private:
    // FactoryLookAndFeel but with slightly smaller (11 px) label text for the knob
    // captions / value boxes / combo — FactoryLookAndFeel::getLabelFont is fixed
    // at 13 px, so we override just that.
    struct PanelLnF : FactoryLookAndFeel
    {
        juce::Font getLabelFont (juce::Label&) override { return juce::Font (juce::FontOptions (11.0f)); }
    };

    juce::String pid (const char* s) const
    {
        return isCut ? ResonanceSuppressorAudioProcessor::cutPid  (nodeId, s)
                     : ResonanceSuppressorAudioProcessor::bandPid (nodeId - 2, s);
    }
    juce::String nodeName() const
    {
        return isCut ? (nodeId == 0 ? "Low Cut" : "High Cut")
                     : "Band " + juce::String (nodeId - 1);
    }
    juce::Colour nodeColour() const
    {
        return isCut ? FactoryLookAndFeel::accent() : FactoryLookAndFeel::bandColour (nodeId - 2);
    }

    PanelLnF lnf;
    juce::AudioProcessorValueTreeState& apvts;
    int  nodeId = 0;
    bool isCut  = true;
    juce::String nameText;
    juce::Colour nameCol { FactoryLookAndFeel::accent() };
    juce::Rectangle<int> nameArea;

    juce::ToggleButton onButton;
    juce::ComboBox choiceBox;
    juce::Slider freqSlider, sensSlider;
    juce::Label  freqLabel,  sensLabel;

    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>   onAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> choiceAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>   freqAtt, sensAtt;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NodePanel)
};
