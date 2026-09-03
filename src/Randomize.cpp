#include "Randomize.h"

namespace randomize
{

bool isExcluded (const juce::String& parameterId)
{
    for (const auto* excluded : kExcluded)
        if (parameterId == excluded)
            return true;
    return false;
}

void apply (juce::AudioProcessorValueTreeState& apvts, float amount, juce::Random& rng)
{
    amount = juce::jlimit (0.0f, 1.0f, amount);
    if (amount <= 0.0f)
        return;

    for (auto* parameter : apvts.processor.getParameters())
    {
        auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*> (parameter);
        if (withId == nullptr || isExcluded (withId->paramID))
            continue;

        const auto isDiscrete = dynamic_cast<juce::AudioParameterChoice*> (parameter) != nullptr
                             || dynamic_cast<juce::AudioParameterBool*> (parameter) != nullptr;

        float next;
        if (isDiscrete)
        {
            // A switch either flips or it doesn't; "a little" means "less often".
            if (rng.nextFloat() >= amount)
                continue;

            const auto steps = juce::jmax (2, parameter->getNumSteps());
            next = (float) rng.nextInt (steps) / (float) (steps - 1);
        }
        else
        {
            // Lerp in normalised space so skewed ranges move perceptually.
            const auto current = parameter->getValue();
            next = current + amount * (rng.nextFloat() - current);
        }

        parameter->beginChangeGesture();
        parameter->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, next));
        parameter->endChangeGesture();
    }
}

} // namespace randomize
