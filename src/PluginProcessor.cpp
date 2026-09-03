#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
// Shimmer intervals, matching the order of the parameter's choice list.
constexpr std::array<float, 9> kShimmerIntervals { -12.0f, -7.0f, -5.0f, 0.0f,
                                                    5.0f, 7.0f, 12.0f, 19.0f, 24.0f };

// Chaos reach per destination at 100 % (5.4).
constexpr float kChaosFeedbackBoost   = 0.30f; // up to +30 % above the set value
constexpr float kChaosDensityRange    = 0.50f; // ±50 %
constexpr float kChaosPitchSemitones  = 12.0f;
constexpr float kChaosShimmerCents    = 50.0f;

// Rewind threshold hysteresis: the tail has to come back up this far above the
// threshold before a falling crossing can fire again.
constexpr float kRewindRearmDb = 6.0f;

const juce::Identifier kPresetNameProperty { "presetName" };
} // namespace

SillageAudioProcessor::SillageAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", params::createLayout())
{
    apvts.addParameterListener (params::id::panic, this);
    apvts.addParameterListener (params::id::rewindManual, this);

    // Modulation destinations resolve through their parameter's own range.
    for (int d = 0; d < mod::kNumDestinations; ++d)
        if (const auto* id = mod::destinationParameterId ((mod::Destination) d))
            destinationParams[(size_t) d] = apvts.getParameter (id);

    ensureCurvesInState();
    publishCurves();
}

SillageAudioProcessor::~SillageAudioProcessor()
{
    apvts.removeParameterListener (params::id::panic, this);
    apvts.removeParameterListener (params::id::rewindManual, this);
}

void SillageAudioProcessor::parameterChanged (const juce::String& parameterId, float newValue)
{
    // Momentary parameters: a rising edge from the UI pulse or host automation
    // arms an action that the audio thread performs at the top of its next block.
    if (newValue < 0.5f)
        return;

    if (parameterId == params::id::panic)
        panicRequested.store (true);
    else if (parameterId == params::id::rewindManual)
        rewindManualRequested.store (true);
}

// ---- Lifetime Curves state ---------------------------------------------------

void SillageAudioProcessor::ensureCurvesInState()
{
    if (! apvts.state.getChildWithName (lifetime::kCurvesType).isValid())
        apvts.state.appendChild (lifetime::toValueTree (lifetime::defaultCurveSet()), nullptr);
}

lifetime::CurveSet SillageAudioProcessor::getCurves() const
{
    return lifetime::fromValueTree (apvts.state.getChildWithName (lifetime::kCurvesType));
}

void SillageAudioProcessor::setCurves (const lifetime::CurveSet& curves)
{
    auto existing = apvts.state.getChildWithName (lifetime::kCurvesType);
    if (existing.isValid())
        apvts.state.removeChild (existing, nullptr);
    apvts.state.appendChild (lifetime::toValueTree (curves), nullptr);
    publishCurves();
}

void SillageAudioProcessor::publishCurves()
{
    curveStore.set (getCurves());
}

float SillageAudioProcessor::getAverageAgeNormalised() const noexcept
{
    const auto lifetimeSeconds = juce::jmax (0.001f, parameterValue (params::id::lifetime) * 0.001f);
    return juce::jlimit (0.0f, 1.0f, wake.getAverageAgeSeconds() / lifetimeSeconds);
}

// ---- Presets -----------------------------------------------------------------

juce::File SillageAudioProcessor::getPresetDirectory()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
               .getChildFile ("Elan Vital Studios").getChildFile ("Sillage").getChildFile ("Presets");
}

juce::String SillageAudioProcessor::getPresetName() const
{
    return apvts.state.getProperty (kPresetNameProperty, "Init").toString();
}

void SillageAudioProcessor::setPresetName (const juce::String& name)
{
    apvts.state.setProperty (kPresetNameProperty, name, nullptr);
}

bool SillageAudioProcessor::savePreset (const juce::File& file)
{
    setPresetName (file.getFileNameWithoutExtension());

    auto xml = apvts.copyState().createXml();
    if (xml == nullptr)
        return false;

    file.getParentDirectory().createDirectory();
    return xml->writeTo (file);
}

bool SillageAudioProcessor::loadPreset (const juce::File& file)
{
    auto xml = juce::XmlDocument::parse (file);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return false;

    apvts.replaceState (juce::ValueTree::fromXml (*xml));
    ensureCurvesInState();
    publishCurves();
    setPresetName (file.getFileNameWithoutExtension());
    return true;
}

void SillageAudioProcessor::resetToDefaults()
{
    for (auto* parameter : getParameters())
    {
        parameter->beginChangeGesture();
        parameter->setValueNotifyingHost (parameter->getDefaultValue());
        parameter->endChangeGesture();
    }

    setCurves (lifetime::defaultCurveSet());
    setPresetName ("Init");
}

// ---- Lifecycle ---------------------------------------------------------------

void SillageAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;

    wetBuffer.setSize (2, samplesPerBlock);

    mixSmoothed.reset (sampleRate, 0.02);
    outputGainSmoothed.reset (sampleRate, 0.02);
    widthSmoothed.reset (sampleRate, 0.02);

    const auto channels = juce::jmax (1, getTotalNumOutputChannels());
    wake.prepare (sampleRate, channels, samplesPerBlock);
    onsetDetector.prepare (sampleRate);
    envelopeFollower.prepare (sampleRate);
    chaosModulator.prepare (sampleRate);
    duck.prepare (sampleRate);

    for (auto& lfo : lfos)
        lfo.prepare (sampleRate);
    transientEnvelope.prepare (sampleRate);
    modRandom.prepare (sampleRate, modRng);

    juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) juce::jmax (1, samplesPerBlock), (juce::uint32) channels };
    wetHighpass.prepare (spec);
    wetHighpass.setType (juce::dsp::StateVariableTPTFilterType::highpass);
    wetLowpass.prepare (spec);
    wetLowpass.setType (juce::dsp::StateVariableTPTFilterType::lowpass);

    rewindTimerLeft      = 0.0;
    rewindThresholdArmed = false;
    wetLevelDb           = -120.0f;

    // Zero latency: the granular loop shifter reads from a delay line rather
    // than looking ahead, and the onset detector's reaction time delays the
    // responses, not the dry path.
    setLatencySamples (0);
}

void SillageAudioProcessor::releaseResources() {}

bool SillageAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& in  = layouts.getMainInputChannelSet();
    const auto& out = layouts.getMainOutputChannelSet();

    if (out != juce::AudioChannelSet::stereo() && out != juce::AudioChannelSet::mono())
        return false;

    // Mono-in upmixes cleanly to stereo-out.
    return in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
}

double SillageAudioProcessor::getTailLengthSeconds() const
{
    // The feedback buffer holds up to 10 s of audio.
    return kBufferSeconds;
}

void SillageAudioProcessor::updateTransport()
{
    auto bpm = (double) parameterValue (params::id::fallbackBpm);
    auto beatsPerBar = 4.0;
    auto hostTime = -1.0;

    if (auto* playhead = getPlayHead())
    {
        if (auto position = playhead->getPosition(); position.hasValue())
        {
            if (auto hostBpm = position->getBpm())
                bpm = *hostBpm;

            // The bar division is only correct in odd metres if we ask.
            if (auto signature = position->getTimeSignature())
                if (signature->numerator > 0 && signature->denominator > 0)
                    beatsPerBar = 4.0 * (double) signature->numerator
                                      / (double) signature->denominator;

            // Synced LFOs lock to the transport while it runs.
            if (position->getIsPlaying())
            {
                if (auto seconds = position->getTimeInSeconds())
                    hostTime = *seconds;
                else if (auto ppq = position->getPpqPosition())
                    hostTime = *ppq * 60.0 / juce::jmax (1.0, bpm);
            }
        }
    }

    effectiveBpm.store (bpm);
    barBeats.store (beatsPerBar);
    hostTimeSeconds.store (hostTime);
}

double SillageAudioProcessor::resolveTimeSeconds() const
{
    double timeSeconds = (double) modulated (mod::Destination::time) * 0.001;
    if (parameterValue (params::id::timeSync) >= 0.5f)
        timeSeconds = params::divisionSeconds ((int) parameterValue (params::id::timeDivision),
                                               effectiveBpm.load(), barBeats.load());

    // Never ask for more delay than the buffer holds.
    return juce::jlimit (0.001, kBufferSeconds * 0.98, timeSeconds);
}

// ---- Modulation --------------------------------------------------------------

float SillageAudioProcessor::modulated (mod::Destination destination) const noexcept
{
    const auto* param = destinationParams[(size_t) destination];
    if (param == nullptr)
        return 0.0f;

    const auto offset = modOffsets[(size_t) destination];
    if (std::abs (offset) < 1.0e-6f)
        return param->convertFrom0to1 (param->getValue());

    return param->convertFrom0to1 (juce::jlimit (0.0f, 1.0f, param->getValue() + offset));
}

void SillageAudioProcessor::updateModulation (int numSamples, float envelope, int numOnsets)
{
    // Sources, all 0..1 at the end of this block.
    const auto lifetimeSeconds = juce::jmax (0.001f, parameterValue (params::id::lifetime) * 0.001f);
    sources.ageNormalised = juce::jlimit (0.0f, 1.0f, wake.getAverageAgeSeconds() / lifetimeSeconds);
    sources.envelope      = juce::jlimit (0.0f, 1.0f, envelope);

    if (numOnsets > 0)
        transientEnvelope.trigger();
    sources.transient = transientEnvelope.advance (numSamples, (double) parameterValue (params::id::modTransientDecay) * 0.001);

    sources.chaos = modRandom.advance (numSamples, modRng) * 0.5f + 0.5f;

    const auto bpm      = effectiveBpm.load();
    const auto bar      = barBeats.load();
    const auto hostTime = hostTimeSeconds.load();

    for (size_t l = 0; l < lfos.size(); ++l)
    {
        const auto synced = parameterValue (params::id::lfoSync[l]) >= 0.5f;
        auto hz = (double) parameterValue (params::id::lfoRate[l]);
        if (synced)
        {
            const auto seconds = juce::jmax (1.0e-3, params::divisionSeconds ((int) parameterValue (params::id::lfoDivision[l]), bpm, bar));
            hz = 1.0 / seconds;
            if (hostTime >= 0.0)
                lfos[l].setPhase (hostTime / seconds);
        }

        const auto shape = (mod::LfoShape) juce::jlimit (0, (int) mod::LfoShape::count - 1, (int) parameterValue (params::id::lfoShape[l]));
        const auto value = lfos[l].advance (numSamples, hz, parameterValue (params::id::lfoPhase[l]) / 360.0f, shape);
        if (l == 0) sources.lfo1 = value;
        else        sources.lfo2 = value;
    }

    // Slots: global destinations sum into normalised offsets; per-grain Age on
    // a per-grain destination is handed to the engine to resolve at birth.
    modOffsets.fill (0.0f);
    numAgeMods = 0;

    for (size_t s = 0; s < params::id::modSource.size(); ++s)
    {
        const auto source      = (mod::Source) juce::jlimit (0, mod::kNumSources - 1, (int) parameterValue (params::id::modSource[s]));
        const auto destination = (mod::Destination) juce::jlimit (0, mod::kNumDestinations - 1, (int) parameterValue (params::id::modDestination[s]));
        const auto amount      = parameterValue (params::id::modAmount[s]) * 0.01f;
        const auto curve       = (mod::Curve) juce::jlimit (0, (int) mod::Curve::count - 1, (int) parameterValue (params::id::modCurve[s]));

        if (source == mod::Source::none || destination == mod::Destination::none || std::abs (amount) < 1.0e-6f)
            continue;

        const auto perGrain = mod::perGrainIndex (destination);
        if (source == mod::Source::agePerGrain && perGrain >= 0)
        {
            ageMods[(size_t) numAgeMods++] = { perGrain, amount, curve };
            continue;
        }

        modOffsets[(size_t) destination] += amount * mod::shape (sources.get (source), curve);
    }
}

// ---- Settings ----------------------------------------------------------------

GrainEngine::Settings SillageAudioProcessor::resolveGrainSettings (const ChaosValues& chaos,
                                                                   float envelope) const
{
    GrainEngine::Settings settings;

    const auto bpm       = effectiveBpm.load();
    const auto bar       = barBeats.load();
    const auto chaosAmt  = parameterValue (params::id::chaos) * 0.01f;
    const auto bipolar   = envelope * 2.0f - 1.0f; // -1 quiet .. +1 loud

    settings.timeSamples    = resolveTimeSeconds() * currentSampleRate;
    settings.timeModSamples = (double) (chaosAmt * chaos.position) * settings.timeSamples;

    // Density: Chaos wanders it ±50 %, the input envelope scales it up to an
    // octave either way — positive means hits dense, sustains sparse.
    auto density = (double) modulated (mod::Destination::density);
    density *= 1.0 + (double) (chaosAmt * chaos.density * kChaosDensityRange);
    if (const auto envDensity = parameterValue (params::id::envDensity) * 0.01f; std::abs (envDensity) > 1.0e-6f)
        density *= std::pow (2.0, (double) (envDensity * bipolar));
    settings.density = juce::jlimit (0.5, 500.0, density);

    auto spread = modulated (mod::Destination::spread) * 0.01f;
    spread += parameterValue (params::id::envSpread) * 0.01f * bipolar * 0.5f;
    settings.spread = juce::jlimit (0.0f, 1.0f, spread);

    settings.sizeSamples = (double) modulated (mod::Destination::size) * 0.001 * currentSampleRate;
    settings.window      = (WindowShape) juce::jlimit (
        0, (int) WindowShape::numShapes - 1, (int) parameterValue (params::id::window));

    // Chaos only ever pushes feedback up; at 100 % the loop goes past unity on
    // purpose and the limiter is what keeps that musical.
    settings.feedback = modulated (mod::Destination::feedback) * 0.01f
                      + chaosAmt * kChaosFeedbackBoost * (chaos.feedback + 1.0f) * 0.5f;

    settings.pitchSemitones    = modulated (mod::Destination::pitch)
                               + parameterValue (params::id::pitchFine) * 0.01f;
    settings.pitchModSemitones = chaosAmt * chaos.pitch * kChaosPitchSemitones;
    settings.pitchSpread       = modulated (mod::Destination::pitchSpread) * 0.01f;
    settings.quantize          = (scales::Scale) juce::jlimit (
        0, (int) scales::Scale::numScales - 1, (int) parameterValue (params::id::quantize));
    settings.quantizeRoot      = juce::jlimit (0, 11, (int) parameterValue (params::id::quantizeRoot));
    settings.panSpread         = modulated (mod::Destination::panSpread) * 0.01f;
    settings.reverseProbability = modulated (mod::Destination::reverse) * 0.01f;

    settings.freeze            = parameterValue (params::id::freeze) >= 0.5f;
    settings.freezeFadeSamples = (double) parameterValue (params::id::freezeFade) * 0.001 * currentSampleRate;

    settings.sync = parameterValue (params::id::sync) >= 0.5f;
    settings.syncIntervalSamples = params::divisionSeconds (
        (int) parameterValue (params::id::grainDivision), bpm, bar) * currentSampleRate;
    settings.swing = parameterValue (params::id::swing) * 0.01f;

    // Age & Lifetime Curves (5.6).
    settings.lifetimeSeconds = juce::jmax (0.001, (double) parameterValue (params::id::lifetime) * 0.001);
    settings.curves          = &activeCurves;
    for (size_t d = 0; d < params::id::curveEnable.size(); ++d)
        settings.curveEnabled[d] = parameterValue (params::id::curveEnable[d]) >= 0.5f;

    // Rewind (5.7).
    settings.rewindOn            = parameterValue (params::id::rewindOn) >= 0.5f;
    settings.rewindLengthSamples = (double) parameterValue (params::id::rewindLength) * 0.001 * currentSampleRate;
    settings.rewindLevel         = modulated (mod::Destination::rewindLevel) * 0.01f;
    settings.rewindRate          = std::pow (2.0, (double) parameterValue (params::id::rewindPitch) / 12.0);

    // Per-grain Age modulation (5.9).
    settings.ageMods    = ageMods;
    settings.numAgeMods = numAgeMods;
    const auto rangeOf = [this] (mod::Destination d) -> const juce::NormalisableRange<float>*
    {
        const auto* param = destinationParams[(size_t) d];
        return param != nullptr ? &param->getNormalisableRange() : nullptr;
    };
    settings.perGrainRanges[(size_t) mod::PerGrain::size]        = rangeOf (mod::Destination::size);
    settings.perGrainRanges[(size_t) mod::PerGrain::pitch]       = rangeOf (mod::Destination::pitch);
    settings.perGrainRanges[(size_t) mod::PerGrain::pitchSpread] = rangeOf (mod::Destination::pitchSpread);
    settings.perGrainRanges[(size_t) mod::PerGrain::panSpread]   = rangeOf (mod::Destination::panSpread);
    settings.perGrainRanges[(size_t) mod::PerGrain::reverse]     = rangeOf (mod::Destination::reverse);

    return settings;
}

FeedbackPath::Settings SillageAudioProcessor::resolveFeedbackSettings (const ChaosValues& chaos) const
{
    FeedbackPath::Settings settings;

    settings.highpassHz = modulated (mod::Destination::loopHighpass);
    settings.lowpassHz  = modulated (mod::Destination::loopLowpass);
    settings.resonance  = parameterValue (params::id::fbResonance) * 0.01f;

    const auto index    = juce::jlimit (0, (int) kShimmerIntervals.size() - 1,
                                        (int) parameterValue (params::id::shimmerInterval));
    const auto chaosAmt = parameterValue (params::id::chaos) * 0.01f;

    auto shimmer = kShimmerIntervals[(size_t) index]
                 + (modulated (mod::Destination::shimmerFine)
                    + chaosAmt * chaos.shimmerFine * kChaosShimmerCents) * 0.01f;

    // Handoff open question 5: when Quantize is on the loop shimmer snaps too,
    // otherwise a +7 against a minor scale stops being musical after two passes.
    const auto scale = (scales::Scale) juce::jlimit (
        0, (int) scales::Scale::numScales - 1, (int) parameterValue (params::id::quantize));
    if (scale != scales::Scale::off)
        shimmer = scales::snapToScale (shimmer, scale,
                                       juce::jlimit (0, 11, (int) parameterValue (params::id::quantizeRoot)));

    settings.shimmerSemitones = shimmer;
    settings.shimmerAmount    = modulated (mod::Destination::shimmerAmount) * 0.01f;
    settings.diffuse          = modulated (mod::Destination::diffuse) * 0.01f;
    settings.satType          = juce::jlimit (0, 2, (int) parameterValue (params::id::satType));
    settings.drive            = modulated (mod::Destination::drive) * 0.01f;

    // Global Lifetime Curve destinations resolve from the average age of the
    // live grains (5.9): the loop filters are replaced while their curve is
    // enabled, and Bit depth / Sample rate set the base that per-pass Degrade
    // then lowers further.
    using lifetime::Destination;
    const auto ageNorm = getAverageAgeNormalised();
    const auto curveOn = [this] (Destination d) { return parameterValue (params::id::curveEnable[(size_t) d]) >= 0.5f; };
    const auto curveAt = [&] (Destination d) { return activeCurves.curves[(size_t) d].evaluate (ageNorm); };

    if (curveOn (Destination::lowpass))    settings.lowpassHz  = lifetime::lowpassHz (curveAt (Destination::lowpass));
    if (curveOn (Destination::highpass))   settings.highpassHz = lifetime::highpassHz (curveAt (Destination::highpass));
    if (curveOn (Destination::bitDepth))   settings.baseBits   = lifetime::bitDepth (curveAt (Destination::bitDepth));
    if (curveOn (Destination::sampleRate)) settings.baseSampleRateHz = lifetime::sampleRateHz (curveAt (Destination::sampleRate), currentSampleRate);

    // Per-pass Degrade (5.6 A).
    settings.degradeBitsPerPass   = modulated (mod::Destination::degradeBits);
    settings.degradeRatePerPass   = modulated (mod::Destination::degradeRate) * 0.01f;
    settings.degradeNoise         = modulated (mod::Destination::degradeNoise) * 0.01f;
    settings.degradeTiltHzPerPass = modulated (mod::Destination::degradeTilt);
    settings.degradeDriftCents    = modulated (mod::Destination::degradeDrift);
    settings.driftDirection       = juce::jlimit (0, 2, (int) parameterValue (params::id::degradeDriftDir));
    settings.passSeconds          = resolveTimeSeconds();

    return settings;
}

GrainEngine::TransientResponse SillageAudioProcessor::resolveTransientResponse() const
{
    GrainEngine::TransientResponse response;

    const auto synced = parameterValue (params::id::sync) >= 0.5f;

    response.retrigger  = parameterValue (params::id::retriggerOn) >= 0.5f;
    response.burstCount = (int) parameterValue (params::id::retriggerCount);
    response.burstIntervalSamples = synced
        ? params::divisionSeconds ((int) parameterValue (params::id::retriggerDivision),
                                   effectiveBpm.load(), barBeats.load()) * currentSampleRate
        : (double) parameterValue (params::id::retriggerRate) * 0.001 * currentSampleRate;

    // Look back past the detector's reaction time to where the hit landed,
    // plus the user's pre-roll (handoff open question 4).
    response.burstOffsetSamples = (double) OnsetDetector::kDetectionLagSamples
                                + (double) parameterValue (params::id::retriggerOffset) * 0.001 * currentSampleRate;
    response.burstAmount = parameterValue (params::id::retriggerAmount) * 0.01f;

    response.choke            = parameterValue (params::id::chokeOn) >= 0.5f;
    response.chokeAmount      = parameterValue (params::id::chokeAmount) * 0.01f;
    response.chokeFadeSamples = (double) parameterValue (params::id::chokeFade) * 0.001 * currentSampleRate;
    response.chokeProtectSamples = (double) OnsetDetector::kDetectionLagSamples;

    response.rewind = parameterValue (params::id::rewindOn) >= 0.5f
                   && (params::RewindTrigger) (int) parameterValue (params::id::rewindTrigger)
                          == params::RewindTrigger::transient;

    return response;
}

WakeEngine::Settings SillageAudioProcessor::resolveWakeSettings() const
{
    WakeEngine::Settings settings;
    settings.isolated            = (params::WakeMode) (int) parameterValue (params::id::wakeMode) == params::WakeMode::isolated;
    settings.displace            = juce::jlimit (0.0f, 1.0f, modulated (mod::Destination::displace) * 0.01f);
    settings.displaceFadeSamples = (double) parameterValue (params::id::chokeFade) * 0.001 * currentSampleRate;
    return settings;
}

void SillageAudioProcessor::updateRewindTriggers (int numSamples)
{
    const auto rewindOn = parameterValue (params::id::rewindOn) >= 0.5f;
    const auto mode     = (params::RewindTrigger) juce::jlimit (0, 3, (int) parameterValue (params::id::rewindTrigger));

    // The manual button fires in every mode; it is a button.
    auto fire = rewindManualRequested.exchange (false);

    if (mode == params::RewindTrigger::timer)
    {
        const auto synced = parameterValue (params::id::sync) >= 0.5f;
        const auto intervalSamples = juce::jmax (1.0,
            (synced ? params::longDivisionSeconds ((int) parameterValue (params::id::rewindDivision),
                                                   effectiveBpm.load(), barBeats.load())
                    : (double) parameterValue (params::id::rewindInterval)) * currentSampleRate);

        rewindTimerLeft -= (double) numSamples;
        if (rewindTimerLeft <= 0.0)
        {
            fire = true;
            rewindTimerLeft = intervalSamples;
        }
    }
    else
    {
        rewindTimerLeft = 0.0; // start at once when the mode is switched to Timer
    }

    if (mode == params::RewindTrigger::threshold)
    {
        // Falling crossing with hysteresis: the tail has to climb back above
        // threshold + 6 dB before another rewind can fire.
        const auto threshold = parameterValue (params::id::rewindThreshold);
        if (wetLevelDb > threshold + kRewindRearmDb)
            rewindThresholdArmed = true;
        else if (rewindThresholdArmed && wetLevelDb < threshold)
        {
            rewindThresholdArmed = false;
            fire = true;
        }
    }

    if (fire && rewindOn)
        wake.triggerRewind();
}

// ---- Processing --------------------------------------------------------------

void SillageAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const auto numSamples = buffer.getNumSamples();
    const auto numOut     = getTotalNumOutputChannels();
    const auto numIn      = getTotalNumInputChannels();

    for (int channel = numIn; channel < numOut; ++channel)
        buffer.copyFrom (channel, 0, buffer, 0, 0, numSamples); // mono -> stereo upmix

    if (panicRequested.exchange (false))
    {
        wake.reset();
        duck.reset();
        onsetDetector.reset();
        envelopeFollower.reset();
        transientEnvelope.reset();
        wetHighpass.reset();
        wetLowpass.reset();
        wetLevelDb           = -120.0f;
        rewindThresholdArmed = false;
    }

    updateTransport();
    curveStore.copyTo (activeCurves);
    updateRewindTriggers (numSamples);

    // Transient detection and the input envelope run on the dry input, before
    // anything the engine does to it.
    onsetDetector.setSensitivity (parameterValue (params::id::sensitivity) * 0.01f);
    const auto numOnsets = onsetDetector.process (buffer.getArrayOfReadPointers(), numOut, numSamples,
                                                  onsetOffsets.data(), kMaxOnsetsPerBlock);
    const auto envelope  = envelopeFollower.process (buffer.getArrayOfReadPointers(), numOut, numSamples);
    const auto chaos     = chaosModulator.advance (numSamples);

    updateModulation (numSamples, envelope, numOnsets);

    wetBuffer.setSize (numOut, numSamples, false, false, true);

    wake.setFeedbackSettings (resolveFeedbackSettings (chaos));
    wake.process (buffer.getArrayOfReadPointers(),
                  wetBuffer.getArrayOfWritePointers(),
                  numOut, numSamples,
                  resolveGrainSettings (chaos, envelope),
                  resolveWakeSettings(),
                  onsetOffsets.data(), numOnsets,
                  resolveTransientResponse());

    // Wet level for the Rewind threshold trigger, smoothed over ~50 ms.
    {
        double sum = 0.0;
        for (int channel = 0; channel < numOut; ++channel)
        {
            const auto* data = wetBuffer.getReadPointer (channel);
            for (int i = 0; i < numSamples; ++i)
                sum += (double) data[i] * (double) data[i];
        }
        const auto rmsDb   = juce::Decibels::gainToDecibels ((float) std::sqrt (sum / juce::jmax (1, numSamples * numOut)), -120.0f);
        const auto blockCoeff = (float) (1.0 - std::exp (-(double) numSamples / (0.05 * currentSampleRate)));
        wetLevelDb += (rmsDb - wetLevelDb) * blockCoeff;
    }

    // Post-loop wet stage (5.11): HP/LP outside the loop, then Width. Either
    // filter at its end stop is bypassed outright rather than left to colour
    // the tail with a near-transparent pole.
    const auto wetHp = parameterValue (params::id::wetHighpass);
    const auto wetLp = parameterValue (params::id::wetLowpass);
    const auto useHp = wetHp > 20.5f;
    const auto useLp = wetLp < 19990.0f;
    if (useHp) wetHighpass.setCutoffFrequency (juce::jlimit (20.0f, (float) (currentSampleRate * 0.45), wetHp));
    if (useLp) wetLowpass.setCutoffFrequency  (juce::jlimit (20.0f, (float) (currentSampleRate * 0.45), wetLp));

    mixSmoothed.setTargetValue (modulated (mod::Destination::mix) * 0.01f);
    outputGainSmoothed.setTargetValue (
        juce::Decibels::decibelsToGain (parameterValue (params::id::output)));
    widthSmoothed.setTargetValue (parameterValue (params::id::width) * 0.01f);

    const auto ducking   = parameterValue (params::id::duckOn) >= 0.5f;
    const auto duckDepth = parameterValue (params::id::duckDepth) * 0.01f;
    duck.setTimes (parameterValue (params::id::duckAttack), parameterValue (params::id::duckRelease));

    int onsetIndex = 0;

    for (int i = 0; i < numSamples; ++i)
    {
        while (onsetIndex < numOnsets && onsetOffsets[(size_t) onsetIndex] <= i)
        {
            if (ducking)
                duck.trigger();
            ++onsetIndex;
        }

        // Duck sits on the wet signal ahead of Mix, per the signal-flow diagram.
        const auto duckGain = ducking ? duck.next (duckDepth) : duck.next (0.0f);

        float wet[2] { wetBuffer.getSample (0, i), numOut > 1 ? wetBuffer.getSample (1, i) : 0.0f };

        for (int channel = 0; channel < numOut; ++channel)
        {
            if (useHp) wet[channel] = wetHighpass.processSample (channel, wet[channel]);
            if (useLp) wet[channel] = wetLowpass.processSample (channel, wet[channel]);
        }

        const auto width = widthSmoothed.getNextValue();
        if (numOut > 1)
        {
            const auto mid  = (wet[0] + wet[1]) * 0.5f;
            const auto side = (wet[0] - wet[1]) * 0.5f * width;
            wet[0] = mid + side;
            wet[1] = mid - side;
        }

        const auto mix     = mixSmoothed.getNextValue();
        const auto dryGain = std::cos (mix * juce::MathConstants<float>::halfPi);
        const auto wetGain = std::sin (mix * juce::MathConstants<float>::halfPi) * duckGain;
        const auto outGain = outputGainSmoothed.getNextValue();

        for (int channel = 0; channel < numOut; ++channel)
        {
            auto* data = buffer.getWritePointer (channel);
            data[i] = (data[i] * dryGain + wet[juce::jmin (channel, 1)] * wetGain) * outGain;
        }
    }
}

juce::AudioProcessorEditor* SillageAudioProcessor::createEditor()
{
    return new SillageAudioProcessorEditor (*this);
}

void SillageAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void SillageAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        if (xml->hasTagName (apvts.state.getType()))
        {
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
            ensureCurvesInState();
            publishCurves();
        }
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SillageAudioProcessor();
}
