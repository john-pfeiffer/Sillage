#include "LifetimeCurves.h"

#include <algorithm>
#include <cmath>

namespace lifetime
{

float Curve::evaluate (float x) const noexcept
{
    const auto n = juce::jlimit (2, kMaxPoints, numPoints);
    x = juce::jlimit (0.0f, 1.0f, x);

    if (x <= points[0].x)
        return juce::jlimit (0.0f, 1.0f, points[0].y);

    for (int i = 0; i + 1 < n; ++i)
    {
        const auto& a = points[(size_t) i];
        const auto& b = points[(size_t) i + 1];

        if (x <= b.x)
        {
            const auto span = b.x - a.x;
            if (span <= 1.0e-6f)
                return juce::jlimit (0.0f, 1.0f, b.y);

            // bend > 0 eases in (slow start), bend < 0 eases out.
            const auto t      = (x - a.x) / span;
            const auto shaped = std::pow (t, std::exp2 (juce::jlimit (-1.0f, 1.0f, a.bend) * 3.0f));
            return juce::jlimit (0.0f, 1.0f, a.y + (b.y - a.y) * shaped);
        }
    }

    return juce::jlimit (0.0f, 1.0f, points[(size_t) n - 1].y);
}

void Curve::sortPoints() noexcept
{
    const auto n = juce::jlimit (2, kMaxPoints, numPoints);
    std::sort (points.begin(), points.begin() + n,
               [] (const Point& a, const Point& b) { return a.x < b.x; });
    points[0].x = 0.0f;
    points[(size_t) n - 1].x = 1.0f;
}

float neutralY (Destination destination) noexcept
{
    switch (destination)
    {
        case Destination::lowpass:     return 1.0f;   // 20 kHz
        case Destination::highpass:    return 0.0f;   // 20 Hz
        case Destination::bitDepth:    return 1.0f;   // 24 bits
        case Destination::sampleRate:  return 1.0f;   // full rate
        case Destination::grainSize:   return 0.625f; // ~150 ms, the Size default
        case Destination::pitchOffset: return 0.5f;   // 0 st
        case Destination::reverse:     return 0.0f;
        case Destination::panSpread:   return 0.0f;
        case Destination::level:       return 1.0f;
        case Destination::count:
        default:                       return 0.5f;
    }
}

Curve flatCurve (float y) noexcept
{
    Curve curve;
    curve.numPoints = 2;
    curve.points[0] = { 0.0f, y, 0.0f };
    curve.points[1] = { 1.0f, y, 0.0f };
    return curve;
}

CurveSet defaultCurveSet() noexcept
{
    CurveSet set;
    for (int d = 0; d < kNumDestinations; ++d)
        set.curves[(size_t) d] = flatCurve (neutralY ((Destination) d));
    return set;
}

float lowpassHz (float y) noexcept        { return 200.0f * std::pow (100.0f, juce::jlimit (0.0f, 1.0f, y)); }
float highpassHz (float y) noexcept       { return 20.0f * std::pow (200.0f, juce::jlimit (0.0f, 1.0f, y)); }
float bitDepth (float y) noexcept         { return 4.0f + 20.0f * juce::jlimit (0.0f, 1.0f, y); }
double grainSizeMs (float y) noexcept     { return 2.0 * std::pow (1000.0, (double) juce::jlimit (0.0f, 1.0f, y)); }
float pitchSemitones (float y) noexcept   { return (juce::jlimit (0.0f, 1.0f, y) - 0.5f) * 48.0f; }

float sampleRateHz (float y, double sr) noexcept
{
    const auto top = juce::jmax (4000.0, sr);
    return (float) (4000.0 * std::pow (top / 4000.0, (double) juce::jlimit (0.0f, 1.0f, y)));
}

juce::ValueTree toValueTree (const CurveSet& set)
{
    juce::ValueTree tree (kCurvesType);
    for (int d = 0; d < kNumDestinations; ++d)
    {
        const auto& curve = set.curves[(size_t) d];
        juce::ValueTree curveTree (kCurveType);
        curveTree.setProperty ("dest", d, nullptr);

        const auto n = juce::jlimit (2, kMaxPoints, curve.numPoints);
        for (int i = 0; i < n; ++i)
        {
            juce::ValueTree point (kPointType);
            point.setProperty ("x", curve.points[(size_t) i].x, nullptr);
            point.setProperty ("y", curve.points[(size_t) i].y, nullptr);
            point.setProperty ("bend", curve.points[(size_t) i].bend, nullptr);
            curveTree.appendChild (point, nullptr);
        }
        tree.appendChild (curveTree, nullptr);
    }
    return tree;
}

CurveSet fromValueTree (const juce::ValueTree& tree)
{
    auto set = defaultCurveSet();
    if (! tree.isValid() || tree.getType() != kCurvesType)
        return set;

    for (const auto& curveTree : tree)
    {
        if (curveTree.getType() != kCurveType)
            continue;

        const auto d = (int) curveTree.getProperty ("dest", -1);
        if (d < 0 || d >= kNumDestinations)
            continue;

        Curve curve;
        curve.numPoints = 0;
        for (const auto& point : curveTree)
        {
            if (point.getType() != kPointType || curve.numPoints >= kMaxPoints)
                continue;

            curve.points[(size_t) curve.numPoints++] = {
                juce::jlimit (0.0f, 1.0f, (float) point.getProperty ("x", 0.0f)),
                juce::jlimit (0.0f, 1.0f, (float) point.getProperty ("y", 0.0f)),
                juce::jlimit (-1.0f, 1.0f, (float) point.getProperty ("bend", 0.0f))
            };
        }

        if (curve.numPoints < 2)
            continue;

        curve.sortPoints();
        set.curves[(size_t) d] = curve;
    }

    return set;
}

void randomise (CurveSet& set, float amount, juce::Random& rng)
{
    amount = juce::jlimit (0.0f, 1.0f, amount);
    if (amount <= 0.0f)
        return;

    for (auto& curve : set.curves)
    {
        // A full reroll may also change how many points there are.
        if (rng.nextFloat() < amount)
        {
            const auto target = 2 + rng.nextInt (4); // 2..5 points
            const auto current = juce::jlimit (2, kMaxPoints, curve.numPoints);
            if (target > current)
            {
                for (int i = current; i < target; ++i)
                    curve.points[(size_t) i] = { rng.nextFloat(), rng.nextFloat(), 0.0f };
                curve.numPoints = target;
            }
            else
            {
                curve.numPoints = target;
            }
        }

        const auto n = juce::jlimit (2, kMaxPoints, curve.numPoints);
        for (int i = 0; i < n; ++i)
        {
            auto& p = curve.points[(size_t) i];
            p.y    += amount * (rng.nextFloat() - p.y);
            p.bend += amount * ((rng.nextFloat() * 2.0f - 1.0f) - p.bend);
            if (i != 0 && i != n - 1)
                p.x += amount * (rng.nextFloat() - p.x);
        }
        curve.sortPoints();
    }
}

CurveStore::CurveStore()
{
    slots[0] = slots[1] = defaultCurveSet();
}

void CurveStore::set (const CurveSet& set)
{
    const auto next = 1 - published.load (std::memory_order_acquire);
    slots[(size_t) next] = set;
    published.store (next, std::memory_order_release);
}

void CurveStore::copyTo (CurveSet& out) const noexcept
{
    out = slots[(size_t) published.load (std::memory_order_acquire)];
}

} // namespace lifetime
