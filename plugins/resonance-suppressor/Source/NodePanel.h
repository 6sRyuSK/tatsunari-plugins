#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "PluginProcessor.h"
#include "factory_ui/FactoryLookAndFeel.h"
#include "factory_ui/FactoryChrome.h"

#include <memory>

//
// A small, bright inline editor for the currently-selected reduction node,
// shown over the analyser (bottom-centre) when a node is selected. It rebinds
// its APVTS attachments to that node on selection (Pro-Q-style). Cut nodes show
// On + Slope + Freq; band nodes show On + Type + Freq + Sens. Horizontal
// "kawaii" card styling (factory_ui). GUI-thread only.
//
class NodePanel : public juce::Component
{
public:
    explicit NodePanel (juce::AudioProcessorValueTreeState& s) : apvts (s)
    {
        title.setJustificationType (juce::Justification::centredLeft);
        title.setFont (juce::Font (juce::FontOptions (13.0f, juce::Font::bold)));
        addAndMakeVisible (title);

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

    int  currentNode()    const noexcept { return nodeId; }
    bool isCutNode()      const noexcept { return isCut; }
    int  preferredWidth() const noexcept { return isCut ? 318 : 384; }
    static constexpr int kHeight = 76;

    // Rebind to node `id` (0 = low cut, 1 = high cut, 2.. = band). Drops the old
    // attachments first so syncing the new ones can't write back to the old node.
    void setNode (int id)
    {
        nodeId = id;
        isCut  = (id < 2);

        onAtt.reset(); choiceAtt.reset(); freqAtt.reset(); sensAtt.reset();

        title.setText (nodeName(), juce::dontSendNotification);
        title.setColour (juce::Label::textColourId, nodeColour());

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

        // Fixed text precision (must run after the attachments — see #26).
        factory_ui::setSliderDecimals (freqSlider, 0);
        if (! isCut) factory_ui::setSliderDecimals (sensSlider, 1);

        resized();
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        factory_ui::paintCard (g, r, 12.0f);
        // A little colour tab so the panel reads as "this node".
        g.setColour (nodeColour());
        g.fillRoundedRectangle (r.reduced (6.0f).removeFromLeft (4.0f), 2.0f);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (12, 8);
        r.removeFromLeft (6); // room for the colour tab

        title.setBounds (r.removeFromLeft (52));
        r.removeFromLeft (6);
        onButton.setBounds (r.removeFromLeft (50).withSizeKeepingCentre (50, 22));
        r.removeFromLeft (8);
        choiceBox.setBounds (r.removeFromLeft (100).withSizeKeepingCentre (100, 26));
        r.removeFromLeft (10);

        auto knob = [&r] (juce::Slider& s, juce::Label& l)
        {
            auto c = r.removeFromLeft (58);
            l.setBounds (c.removeFromTop (14));
            s.setBounds (c);
            r.removeFromLeft (4);
        };
        knob (freqSlider, freqLabel);
        if (! isCut) knob (sensSlider, sensLabel);
    }

private:
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

    juce::AudioProcessorValueTreeState& apvts;
    int  nodeId = 0;
    bool isCut  = true;

    juce::Label title;
    juce::ToggleButton onButton;
    juce::ComboBox choiceBox;
    juce::Slider freqSlider, sensSlider;
    juce::Label  freqLabel,  sensLabel;

    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>   onAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> choiceAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>   freqAtt, sensAtt;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NodePanel)
};
