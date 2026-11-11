#include "Visuals.h"

#include "DecayModel.h"
#include "SillageLookAndFeel.h"

namespace ui
{

namespace
{
float raw (SillageAudioProcessor& processor, const char* id)
{
    return processor.apvts.getRawParameterValue (id)->load();
}

uint32_t mix (uint32_t hash, float value) noexcept
{
    uint32_t bits = 0;
    std::memcpy (&bits, &value, sizeof (bits));
    return (hash ^ bits) * 16777619u;
}
} // namespace

// ---- MeterFeed -----------------------------------------------------------------

void MeterFeed::push (const SillageAudioProcessor::Meter& meter) noexcept
{
    constexpr float kFall = 0.035f; // of full scale per frame: about a second

    const auto follow = [] (float& held, float now)
    {
        held = juce::jmax (now, held - kFall);
    };

    const auto inputNow = toScale (meter.input), wetNow = toScale (meter.wet);
    follow (input, inputNow);
    follow (wet, wetNow);
    follow (output, toScale (meter.output));

    const auto reduction = -juce::Decibels::gainToDecibels (juce::jlimit (1.0e-4f, 1.0f, meter.limiterGain));
    limiterDb = juce::jmax (reduction, limiterDb - 0.5f);

    inputHistory[(size_t) head] = inputNow;
    wetHistory[(size_t) head]   = wetNow;
    head = (head + 1) % kHistory;
}

// ---- TailView ------------------------------------------------------------------

void TailView::paint (juce::Graphics& g)
{
    const auto area    = getLocalBounds().toFloat().reduced (2.0f, 4.0f);
    const auto centreY = area.getCentreY();
    const auto half    = area.getHeight() * 0.5f;

    constexpr float kStep = 3.0f;
    const auto bars = juce::jmax (1, (int) (area.getWidth() / kStep));

    for (int bar = 0; bar < bars; ++bar)
    {
        // The newest frame is at the right; each bar takes the loudest of the
        // frames it stands for.
        const auto newest = (bars - 1 - bar) * MeterFeed::kHistory / bars;
        const auto oldest = (bars - bar) * MeterFeed::kHistory / bars;

        float in = 0.0f, wet = 0.0f;
        for (int age = newest; age < juce::jmax (newest + 1, oldest); ++age)
        {
            const auto index = (size_t) (((feed.head - 1 - age) % MeterFeed::kHistory + MeterFeed::kHistory) % MeterFeed::kHistory);
            in  = juce::jmax (in, feed.inputHistory[index]);
            wet = juce::jmax (wet, feed.wetHistory[index]);
        }

        const auto x = area.getX() + (float) bar * kStep;

        g.setColour (colour::faint);
        g.fillRect (x, centreY - juce::jmax (0.5f, in * half), 2.0f, juce::jmax (1.0f, in * half * 2.0f));

        if (wet > 0.0f)
        {
            g.setColour (colour::text.withAlpha (0.9f));
            g.fillRect (x + 0.5f, centreY - wet * half, 1.0f, juce::jmax (1.0f, wet * half * 2.0f));
        }
    }

    // Now.
    g.setColour (colour::accent);
    g.fillRect (area.getRight() - 1.0f, area.getY(), 1.0f, area.getHeight());
}

// ---- CloudView -----------------------------------------------------------------

uint32_t CloudView::signature() const noexcept
{
    namespace id = params::id;
    uint32_t hash = 2166136261u;
    for (auto* parameter : { id::time, id::timeSync, id::timeDivision, id::density, id::size, id::spread, id::decay,
                             id::feedback, id::pitch, id::pitchSpread, id::reverse, id::sync, id::grainDivision })
        hash = mix (hash, raw (processor, parameter));
    return mix (hash, (float) processor.getEffectiveBpm());
}

void CloudView::paint (juce::Graphics& g)
{
    namespace id = params::id;

    const auto area   = getLocalBounds().toFloat().reduced (2.0f, 4.0f);
    const auto model  = processor.getDecayModelForDisplay();
    const auto spread = (double) raw (processor, id::spread) * 0.01;
    const auto size   = (double) raw (processor, id::size) * 0.001;

    auto time = (double) raw (processor, id::time) * 0.001;
    if (raw (processor, id::timeSync) >= 0.5f)
        time = params::divisionSeconds ((int) raw (processor, id::timeDivision), processor.getEffectiveBpm(), 4.0);

    auto density = (double) raw (processor, id::density);
    if (raw (processor, id::sync) >= 0.5f)
        density = 1.0 / juce::jmax (1.0e-3, params::divisionSeconds ((int) raw (processor, id::grainDivision), processor.getEffectiveBpm(), 4.0));

    const auto decayOn = model.windowSeconds > 0.0;
    const auto window  = decayOn ? model.windowSeconds : kBufferSeconds;

    // How far back the picture goes: the part of the cloud's reach that is
    // within 30 dB of its loudest, with room for a grain.
    const auto reach = decayOn ? time + spread * (size + window * 0.5) : time * (1.0 - spread) + spread * kBufferSeconds;
    const auto span  = juce::jmax (0.05, reach + size) * 1.05;

    const auto xOf = [&] (double seconds) { return area.getX() + (float) (seconds / span) * area.getWidth(); };

    // The centre line is the played pitch; the edges are two octaves away.
    const auto centreY = area.getCentreY();
    g.setColour (colour::faint.withAlpha (0.5f));
    g.fillRect (area.getX(), centreY, area.getWidth(), 1.0f);

    const auto pitch       = (double) raw (processor, id::pitch);
    const auto pitchSpread = (double) raw (processor, id::pitchSpread) * 0.01;
    const auto reverse     = (double) raw (processor, id::reverse) * 0.01;

    // As many dashes as grains start in the picture's span, within reason. The
    // seed is fixed: the picture moves with the knobs, not with the clock.
    const auto count = juce::jlimit (4, 72, (int) std::lround (density * juce::jmin (span, 1.5)));
    juce::Random rng (0x51a6e);

    for (int i = 0; i < count; ++i)
    {
        double behind = 0.0, level = 1.0;
        const auto draw = rng.nextDouble();

        if (decayOn)
        {
            const auto total    = 2.0 * decay::kRt60 / window;
            const auto oddsRate = total * decay::kDensityShare;
            const auto atEnd    = std::exp (-oddsRate * window);
            behind = juce::jmin (window, -std::log (1.0 - draw * (1.0 - atEnd)) / oddsRate);
            level  = 1.0 + juce::jmin (1.0, spread * 4.0) * (std::exp (-0.5 * (total - oddsRate) * behind) - 1.0);
        }
        else
        {
            behind = draw * kBufferSeconds;
        }

        const auto offset = decayOn ? time + spread * (size + behind) : time + spread * (behind - time);
        const auto pitchAt = pitch + pitchSpread * 12.0 * (rng.nextDouble() * 2.0 - 1.0);
        const auto reversed = rng.nextDouble() < reverse;

        // Grains at one pitch would all sit on one line; a few lanes keep
        // them apart so how many there are can be seen.
        const auto lane = (float) (i % 7 - 3) * 3.0f;

        const auto x = xOf (offset);
        const auto y = centreY + lane
                     - (float) juce::jlimit (-1.0, 1.0, pitchAt / 24.0) * (area.getHeight() * 0.5f - 12.0f);
        const auto w = juce::jmax (2.0f, (float) (size / span) * area.getWidth());

        g.setColour ((reversed ? colour::accent : colour::text).withAlpha ((float) (0.2 + 0.75 * level)));
        g.fillRect (juce::Rectangle<float> (x, y - 0.75f, w, 1.5f).getIntersection (area.expanded (0.0f, 2.0f)));
    }

    // Time: where the sound arrives.
    g.setColour (colour::dim);
    g.fillRect (xOf (time), area.getY(), 1.0f, area.getHeight());
}

// ---- LoopView ------------------------------------------------------------------

uint32_t LoopView::signature() const noexcept
{
    namespace id = params::id;
    uint32_t hash = 2166136261u;
    for (auto* parameter : { id::fbHighpass, id::fbLowpass, id::fbResonance, id::shimmerAmount, id::shimmerInterval,
                             id::loopOn, id::feedback })
        hash = mix (hash, raw (processor, parameter));
    return hash;
}

void LoopView::paint (juce::Graphics& g)
{
    namespace id = params::id;

    const auto area   = getLocalBounds().toFloat().reduced (2.0f, 4.0f);
    const auto loopOn = raw (processor, id::loopOn) >= 0.5f;

    const auto highpass  = loopOn ? (double) raw (processor, id::fbHighpass) : 20.0;
    const auto lowpass   = (double) raw (processor, id::fbLowpass);
    const auto q         = (double) FeedbackPath::resonanceFor (loopOn ? raw (processor, id::fbResonance) * 0.01f : 0.0f);

    constexpr double kLowHz = 20.0, kHighHz = 20000.0, kTopDb = 14.0, kBottomDb = -36.0;
    constexpr int kSteps = 96;

    const auto response = [&] (double hz)
    {
        const auto section = [q] (double ratio, bool isHighpass)
        {
            const auto r2 = ratio * ratio;
            const auto denominator = std::sqrt ((1.0 - r2) * (1.0 - r2) + r2 / (q * q));
            return (isHighpass ? r2 : 1.0) / juce::jmax (1.0e-9, denominator);
        };
        return 20.0 * std::log10 (juce::jmax (1.0e-6, section (hz / highpass, true) * section (hz / lowpass, false)));
    };

    const auto curve = [&] (double shiftOctaves)
    {
        juce::Path path;
        for (int i = 0; i <= kSteps; ++i)
        {
            const auto t  = (double) i / kSteps;
            const auto hz = kLowHz * std::pow (kHighHz / kLowHz, t) / std::pow (2.0, shiftOctaves);
            const auto db = juce::jlimit (kBottomDb, kTopDb, response (hz));
            const auto point = juce::Point<float> (area.getX() + (float) t * area.getWidth(),
                                                   area.getBottom() - (float) ((db - kBottomDb) / (kTopDb - kBottomDb)) * area.getHeight());
            if (i == 0) path.startNewSubPath (point);
            else        path.lineTo (point);
        }
        return path;
    };

    // Shimmer: each trip the loop's content steps by the interval, so its
    // copies sit beside the response, fainter each time.
    static constexpr float kIntervals[] { -12.0f, -7.0f, -5.0f, 0.0f, 5.0f, 7.0f, 12.0f, 19.0f, 24.0f };
    const auto shimmer  = loopOn ? raw (processor, id::shimmerAmount) * 0.01f : 0.0f;
    const auto interval = kIntervals[juce::jlimit (0, 8, (int) raw (processor, id::shimmerInterval))] / 12.0f;

    if (shimmer > 0.01f && std::abs (interval) > 0.01f)
        for (int trip = 3; trip >= 1; --trip)
        {
            g.setColour (colour::accent.withAlpha (shimmer * (0.9f - 0.22f * (float) trip)));
            g.strokePath (curve ((double) interval * trip), juce::PathStrokeType (1.0f));
        }

    g.setColour (colour::text.withAlpha (loopOn ? 0.95f : 0.4f));
    g.strokePath (curve (0.0), juce::PathStrokeType (1.5f));
}

// ---- LevelMeter ----------------------------------------------------------------

void LevelMeter::paint (juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat();

    const auto captionH = compact ? 0.0f : 14.0f;
    const auto bars     = area.withTrimmedBottom (captionH).reduced (compact ? 0.0f : 6.0f, compact ? 2.0f : 6.0f);

    struct Bar { const char* name; float level; };
    const Bar levels[] { { "In", feed.input }, { "Wet", feed.wet }, { "Out", feed.output } };

    const auto slots  = compact ? 3.5f : 4.0f; // the last slot is the limiter light
    const auto slotW  = bars.getWidth() / slots;
    const auto barW   = compact ? 3.0f : 4.0f;

    for (int i = 0; i < 3; ++i)
    {
        const auto cx = bars.getX() + slotW * ((float) i + 0.5f);

        g.setColour (colour::track);
        g.fillRect (cx - 0.5f, bars.getY(), 1.0f, bars.getHeight());

        const auto h = levels[i].level * bars.getHeight();
        g.setColour (i == 0 ? colour::dim : colour::text);
        g.fillRect (cx - barW * 0.5f, bars.getBottom() - h, barW, juce::jmax (1.0f, h));

        if (! compact)
        {
            g.setColour (colour::dim);
            g.setFont (font (10.5f));
            g.drawText (levels[i].name, juce::Rectangle<float> (cx - slotW * 0.5f, area.getBottom() - captionH, slotW, captionH),
                        juce::Justification::centred);
        }
    }

    // The loop limiter: lit while it is holding the loop back.
    const auto working = feed.limiterDb > 0.5f;
    const auto cx = bars.getX() + slotW * (compact ? 3.25f : 3.5f);
    const auto dot = juce::Rectangle<float> (0.0f, 0.0f, 7.0f, 7.0f).withCentre ({ cx, compact ? bars.getCentreY() : bars.getY() + 5.0f });

    if (working)
    {
        g.setColour (colour::accent);
        g.fillEllipse (dot);
    }
    else
    {
        g.setColour (colour::faint);
        g.drawEllipse (dot.reduced (0.5f), 1.0f);
    }

    if (! compact)
    {
        g.setColour (working ? colour::text : colour::dim);
        g.setFont (font (10.5f));
        g.drawText ("Limit", juce::Rectangle<float> (cx - slotW * 0.5f, area.getBottom() - captionH, slotW, captionH),
                    juce::Justification::centred);
    }
}

} // namespace ui
