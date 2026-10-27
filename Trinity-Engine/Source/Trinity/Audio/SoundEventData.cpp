#include "Trinity/Audio/SoundEventData.hpp"

#include "Trinity/Asset/CookedText.hpp"
#include "Trinity/Scene/SceneSerializer.hpp"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <format>
#include <initializer_list>
#include <limits>
#include <utility>

namespace Trinity
{
    namespace
    {
        std::atomic<std::uint64_t> s_NextSerial{ 1 };

        constexpr std::array<std::string_view, 5> c_ContainerTypeKeys{ "Clip", "Random", "Sequence", "Layer", "Switch" };
        constexpr std::array<std::string_view, 2> c_TriggerTimingKeys{ "Pulse", "Offset" };
        constexpr std::array<std::string_view, 3> c_StealModeKeys{ "Oldest", "Quietest", "None" };
        constexpr std::array<std::string_view, 2> c_ParameterKindKeys{ "Discrete", "Continuous" };
        constexpr std::array<std::string_view, 3> c_CurveTargetKeys{ "Volume", "Pitch", "Cutoff" };

        template<typename T, std::size_t N>
        std::string_view GetKey(const std::array<std::string_view, N>& keys, T value)
        {
            return static_cast<std::size_t>(value) < keys.size() ? keys[static_cast<std::size_t>(value)] : keys.front();
        }

        template<typename T, std::size_t N>
        std::optional<T> FindKey(const std::array<std::string_view, N>& keys, std::string_view key)
        {
            const auto a_Found = std::ranges::find(keys, key);

            return a_Found != keys.end() ? std::optional(static_cast<T>(a_Found - keys.begin())) : std::nullopt;
        }

        // By its name, or its ID when it has none, as an error names it
        std::string Describe(const SoundContainer& container)
        {
            return container.Name.empty() ? std::format("container {}", container.ID) : std::format("container '{}'", std::string_view(container.Name));
        }

        bool AreNumbers(std::initializer_list<float> values)
        {
            return std::ranges::all_of(values, [](float value) { return std::isfinite(value); });
        }


        // Whether the container is the one given or below it
        bool IsWithin(const SoundEventData& data, UUID container, UUID ancestor)
        {
            for (const SoundContainer* it_Container = data.FindContainer(container); it_Container != nullptr; it_Container = it_Container->Parent ? data.FindContainer(it_Container->Parent) : nullptr)
            {
                if (it_Container->ID == ancestor)
                {
                    return true;
                }
            }

            return false;
        }

        // The place after the last container that is the one given or below it
        std::size_t FindBranchEnd(const SoundEventData& data, UUID container)
        {
            std::size_t l_End = 0;
            for (std::size_t it_Container = 0; it_Container < data.Containers.size(); ++it_Container)
            {
                l_End = IsWithin(data, data.Containers[it_Container].ID, container) ? it_Container + 1 : l_End;
            }

            return l_End;
        }

        // The children of each Switch by the parameter, or of the one container given, play for every label again
        void ClearCases(SoundEventData& data, UUID switchContainer, UUID parameter = UUID(), std::string_view label = {})
        {
            for (SoundContainer& it_Child : data.Containers)
            {
                const SoundContainer* l_Parent = it_Child.Parent ? data.FindContainer(it_Child.Parent) : nullptr;
                const bool l_ByParameter = parameter && l_Parent != nullptr && l_Parent->Type == SoundContainerType::Switch && l_Parent->Parameter == parameter;
                if ((l_Parent != nullptr && l_Parent->ID == switchContainer) || (l_ByParameter && (label.empty() || std::string_view(it_Child.Case) == label)))
                {
                    it_Child.Case.clear();
                }
            }
        }

        // What the container's type never saves goes back to its default, so an edit leaves the event its file reloads as
        void ForgetUnsaved(SoundContainer& container)
        {
            const SoundContainer l_Default;
            const SoundContainerType l_Type = container.Type;
            container.Clip = l_Type == SoundContainerType::Clip ? container.Clip : l_Default.Clip;
            container.Loop = l_Type == SoundContainerType::Clip || l_Type == SoundContainerType::Sequence ? container.Loop : l_Default.Loop;
            container.Parameter = l_Type == SoundContainerType::Switch ? container.Parameter : l_Default.Parameter;
            if (l_Type != SoundContainerType::Random)
            {
                container.AvoidRepeat = l_Default.AvoidRepeat;
                container.AutoTrigger = l_Default.AutoTrigger;
                container.Timing = l_Default.Timing;
                container.IntervalMinimum = l_Default.IntervalMinimum;
                container.IntervalMaximum = l_Default.IntervalMaximum;
                container.TriggerCount = l_Default.TriggerCount;
            }
        }

        // What a curve's value is at where it changes nothing
        float GetCurveRest(SoundCurveTarget target)
        {
            switch (target)
            {
                case SoundCurveTarget::Pitch:
                {
                    return 1.0f;
                }
                case SoundCurveTarget::Cutoff:
                {
                    return SoundEventData::c_MaximumCutoff;
                }
                case SoundCurveTarget::Volume:
                {
                    break;
                }
            }

            return 0.0f;
        }

        bool IsLabelFree(const SoundParameter& parameter, std::string_view name, std::size_t except = std::numeric_limits<std::size_t>::max())
        {
            for (std::size_t it_Label = 0; it_Label < parameter.Labels.size(); ++it_Label)
            {
                if (it_Label != except && std::string_view(parameter.Labels[it_Label]) == name)
                {
                    return false;
                }
            }

            return !name.empty();
        }

        // A curve as a file gives it, its points each a pair of numbers
        std::string ReadCurve(const YAML::Node& node, SoundCurve& curve)
        {
            if (!node.IsMap())
            {
                return "a curve is not a map";
            }

            std::string l_Target(GetSoundCurveTargetKey(curve.Target));
            std::string l_Malformed;
            CookedText::ReadUUID(node, "Parameter", curve.Parameter, l_Malformed);
            CookedText::ReadString(node, "Target", l_Target, l_Malformed);
            const YAML::Node l_Points = node["Points"];
            if (!l_Malformed.empty() || (l_Points && !l_Points.IsSequence()))
            {
                return std::format("a curve's {} is malformed", l_Malformed.empty() ? std::string("Points") : l_Malformed);
            }

            for (std::size_t it_Point = 0; l_Points && it_Point < l_Points.size(); ++it_Point)
            {
                const YAML::Node l_Point = l_Points[it_Point];
                const std::optional<float> l_X = l_Point.IsSequence() && l_Point.size() == 2 ? CookedText::ParseNumber<float>(l_Point[0]) : std::nullopt;
                const std::optional<float> l_Y = l_Point.IsSequence() && l_Point.size() == 2 ? CookedText::ParseNumber<float>(l_Point[1]) : std::nullopt;
                if (!l_X || !l_Y)
                {
                    return "a curve's point is not two numbers";
                }

                curve.Points.emplace_back(*l_X, *l_Y);
            }

            const std::optional<SoundCurveTarget> l_FoundTarget = FindSoundCurveTarget(l_Target);
            if (!l_FoundTarget)
            {
                return std::format("a curve's Target is '{}', which is no value a curve moves", l_Target);
            }

            curve.Target = *l_FoundTarget;

            return {};
        }

        // A container as a file gives it, each key that is missing at its default. Every key is read whatever its type, which the file writes only those of
        std::string ReadContainer(const YAML::Node& node, SoundContainer& container)
        {
            if (!node.IsMap())
            {
                return "a container is not a map";
            }

            std::string l_Name;
            std::string l_Type;
            std::string l_Timing(GetSoundTriggerTimingKey(container.Timing));
            std::string l_Case;
            std::string l_Malformed;
            CookedText::ReadUUID(node, "ID", container.ID, l_Malformed);
            CookedText::ReadUUID(node, "Parent", container.Parent, l_Malformed);
            CookedText::ReadString(node, "Name", l_Name, l_Malformed);
            CookedText::ReadString(node, "Type", l_Type, l_Malformed);
            CookedText::ReadNumber(node, "Volume", container.Volume, l_Malformed);
            CookedText::ReadNumber(node, "VolumeRandom", container.VolumeRandom, l_Malformed);
            CookedText::ReadNumber(node, "Pitch", container.Pitch, l_Malformed);
            CookedText::ReadNumber(node, "PitchRandom", container.PitchRandom, l_Malformed);
            CookedText::ReadNumber(node, "Delay", container.Delay, l_Malformed);
            CookedText::ReadNumber(node, "DelayRandom", container.DelayRandom, l_Malformed);
            CookedText::ReadNumber(node, "Weight", container.Weight, l_Malformed);
            CookedText::ReadString(node, "Case", l_Case, l_Malformed);
            CookedText::ReadUUID(node, "Clip", container.Clip, l_Malformed);
            CookedText::ReadBool(node, "Loop", container.Loop, l_Malformed);
            CookedText::ReadNumber(node, "AvoidRepeat", container.AvoidRepeat, l_Malformed);
            CookedText::ReadBool(node, "AutoTrigger", container.AutoTrigger, l_Malformed);
            CookedText::ReadString(node, "Timing", l_Timing, l_Malformed);
            CookedText::ReadNumber(node, "IntervalMinimum", container.IntervalMinimum, l_Malformed);
            CookedText::ReadNumber(node, "IntervalMaximum", container.IntervalMaximum, l_Malformed);
            CookedText::ReadNumber(node, "TriggerCount", container.TriggerCount, l_Malformed);
            CookedText::ReadUUID(node, "Parameter", container.Parameter, l_Malformed);
            container.Name = TaggedString<MemoryTag::Audio>(std::string_view(l_Name));
            container.Case = TaggedString<MemoryTag::Audio>(std::string_view(l_Case));
            const YAML::Node l_Curves = node["Curves"];
            if (!l_Malformed.empty() || (l_Curves && !l_Curves.IsSequence()))
            {
                return std::format("{}: {} is malformed", Describe(container), l_Malformed.empty() ? std::string("Curves") : l_Malformed);
            }

            for (std::size_t it_Curve = 0; l_Curves && it_Curve < l_Curves.size(); ++it_Curve)
            {
                if (std::string l_Error = ReadCurve(l_Curves[it_Curve], container.Curves.emplace_back()); !l_Error.empty())
                {
                    return std::format("{}: {}", Describe(container), l_Error);
                }
            }

            const std::optional<SoundContainerType> l_FoundType = FindSoundContainerType(l_Type);
            if (!l_FoundType)
            {
                return std::format("{} is of no known type{}", Describe(container), l_Type.empty() ? std::string() : std::format(", '{}'", l_Type));
            }

            const std::optional<SoundTriggerTiming> l_FoundTiming = FindSoundTriggerTiming(l_Timing);
            if (!l_FoundTiming)
            {
                return std::format("{}: its Timing is '{}', which is no timing", Describe(container), l_Timing);
            }

            container.Type = *l_FoundType;
            container.Timing = *l_FoundTiming;

            return {};
        }

        // A parameter as a file gives it, with its labels in their order. One with no Kind is discrete, as every parameter was before there were continuous ones
        std::string ReadParameter(const YAML::Node& node, SoundParameter& parameter)
        {
            if (!node.IsMap())
            {
                return "a parameter is not a map";
            }

            std::string l_Name;
            std::string l_Kind(GetSoundParameterKindKey(parameter.Kind));
            std::string l_Malformed;
            CookedText::ReadUUID(node, "ID", parameter.ID, l_Malformed);
            CookedText::ReadString(node, "Name", l_Name, l_Malformed);
            CookedText::ReadString(node, "Kind", l_Kind, l_Malformed);
            CookedText::ReadBool(node, "Global", parameter.Global, l_Malformed);
            CookedText::ReadNumber(node, "Minimum", parameter.Minimum, l_Malformed);
            CookedText::ReadNumber(node, "Maximum", parameter.Maximum, l_Malformed);
            CookedText::ReadNumber(node, "Default", parameter.Default, l_Malformed);
            CookedText::ReadNumber(node, "SeekSpeed", parameter.SeekSpeed, l_Malformed);
            parameter.Name = TaggedString<MemoryTag::Audio>(std::string_view(l_Name));
            const YAML::Node l_Labels = node["Labels"];
            if (!l_Malformed.empty() || (l_Labels && !l_Labels.IsSequence()))
            {
                return std::format("parameter '{}': {} is malformed", l_Name, l_Malformed.empty() ? std::string("Labels") : l_Malformed);
            }

            for (std::size_t it_Label = 0; l_Labels && it_Label < l_Labels.size(); ++it_Label)
            {
                std::string l_Label;
                if (l_Labels[it_Label].IsMap())
                {
                    CookedText::ReadString(l_Labels[it_Label], "Name", l_Label, l_Malformed);
                }

                if (!l_Labels[it_Label].IsMap() || !l_Malformed.empty())
                {
                    return std::format("parameter '{}': a label is malformed", l_Name);
                }

                parameter.Labels.emplace_back(std::string_view(l_Label));
            }

            const std::optional<SoundParameterKind> l_FoundKind = FindSoundParameterKind(l_Kind);
            if (!l_FoundKind)
            {
                return std::format("parameter '{}': its Kind is '{}', which is no kind", l_Name, l_Kind);
            }

            parameter.Kind = *l_FoundKind;

            return {};
        }

        std::string ValidateContainers(const SoundEventData& data)
        {
            if (data.Containers.empty() || data.Containers.front().Parent)
            {
                return "it has no root container, one without a Parent, first";
            }

            for (std::size_t it_Container = 0; it_Container < data.Containers.size(); ++it_Container)
            {
                const SoundContainer& l_Container = data.Containers[it_Container];
                const auto a_Before = data.Containers.begin() + static_cast<std::ptrdiff_t>(it_Container);
                if (!l_Container.ID || std::ranges::find(data.Containers.begin(), a_Before, l_Container.ID, &SoundContainer::ID) != a_Before)
                {
                    return std::format("{} has no ID of its own", Describe(l_Container));
                }

                if (it_Container > 0)
                {
                    const auto a_Parent = std::ranges::find(data.Containers.begin(), a_Before, l_Container.Parent, &SoundContainer::ID);
                    if (a_Parent == a_Before)
                    {
                        return std::format("{} is not after a parent of the event's", Describe(l_Container));
                    }

                    if (a_Parent->Type == SoundContainerType::Clip)
                    {
                        return std::format("{} is below {}, a Clip, which holds no containers", Describe(l_Container), Describe(*a_Parent));
                    }
                }

                if (static_cast<std::size_t>(l_Container.Type) >= c_ContainerTypeKeys.size() || static_cast<std::size_t>(l_Container.Timing) >= c_TriggerTimingKeys.size())
                {
                    return std::format("{} is of no known type or timing", Describe(l_Container));
                }

                if (!AreNumbers({ l_Container.Volume, l_Container.VolumeRandom, l_Container.Pitch, l_Container.PitchRandom, l_Container.Delay, l_Container.DelayRandom, l_Container.Weight, l_Container.IntervalMinimum, l_Container.IntervalMaximum }))
                {
                    return std::format("{} has a value that is no number", Describe(l_Container));
                }
            }

            return {};
        }

        std::string ValidateParameters(const SoundEventData& data)
        {
            for (std::size_t it_Parameter = 0; it_Parameter < data.Parameters.size(); ++it_Parameter)
            {
                const SoundParameter& l_Parameter = data.Parameters[it_Parameter];
                const std::string_view l_Name(l_Parameter.Name);
                const auto a_Before = data.Parameters.begin() + static_cast<std::ptrdiff_t>(it_Parameter);
                if (l_Name.empty())
                {
                    return "a parameter has no name";
                }

                if (!l_Parameter.ID || std::ranges::find(data.Parameters.begin(), a_Before, l_Parameter.ID, &SoundParameter::ID) != a_Before)
                {
                    return std::format("parameter '{}' has no ID of its own", l_Name);
                }

                if (std::ranges::any_of(data.Parameters.begin(), a_Before, [l_Name](const SoundParameter& other) { return std::string_view(other.Name) == l_Name; }))
                {
                    return std::format("two parameters are named '{}'", l_Name);
                }

                if (static_cast<std::size_t>(l_Parameter.Kind) >= c_ParameterKindKeys.size())
                {
                    return std::format("parameter '{}' is of no known kind", l_Name);
                }

                if (!std::isfinite(l_Parameter.Default))
                {
                    return std::format("parameter '{}' has a default that is no number", l_Name);
                }

                if (!AreNumbers({ l_Parameter.Minimum, l_Parameter.Maximum, l_Parameter.SeekSpeed }))
                {
                    return std::format("parameter '{}' has a range or seek speed that is no number", l_Name);
                }

                if (l_Parameter.SeekSpeed < 0.0f)
                {
                    return std::format("parameter '{}' seeks at a speed below 0", l_Name);
                }

                if (l_Parameter.Kind == SoundParameterKind::Continuous)
                {
                    if (!l_Parameter.Labels.empty())
                    {
                        return std::format("parameter '{}' is continuous, and has labels", l_Name);
                    }

                    if (l_Parameter.Minimum >= l_Parameter.Maximum)
                    {
                        return std::format("parameter '{}' has a minimum that is not below its maximum", l_Name);
                    }

                    continue;
                }

                if (l_Parameter.Labels.empty())
                {
                    return std::format("parameter '{}' has no labels", l_Name);
                }

                for (auto it_Label = l_Parameter.Labels.begin(); it_Label != l_Parameter.Labels.end(); ++it_Label)
                {
                    if (it_Label->empty())
                    {
                        return std::format("parameter '{}' has a label with no name", l_Name);
                    }

                    if (std::find(l_Parameter.Labels.begin(), it_Label, *it_Label) != it_Label)
                    {
                        return std::format("parameter '{}' has two labels named '{}'", l_Name, std::string_view(*it_Label));
                    }
                }
            }

            return {};
        }

        // Once the parameters keep to their rules
        std::string ValidateCurves(const SoundEventData& data)
        {
            for (const SoundContainer& it_Container : data.Containers)
            {
                for (auto it_Curve = it_Container.Curves.begin(); it_Curve != it_Container.Curves.end(); ++it_Curve)
                {
                    const SoundParameter* l_Parameter = data.FindParameter(it_Curve->Parameter);
                    if (l_Parameter == nullptr)
                    {
                        return std::format("{} has a curve by no parameter the event has", Describe(it_Container));
                    }

                    if (static_cast<std::size_t>(it_Curve->Target) >= c_CurveTargetKeys.size())
                    {
                        return std::format("{} has a curve of no value a curve moves", Describe(it_Container));
                    }

                    const std::string_view l_Target = GetSoundCurveTargetKey(it_Curve->Target);
                    if (std::any_of(it_Container.Curves.begin(), it_Curve, [&it_Curve](const SoundCurve& other) { return other.Parameter == it_Curve->Parameter && other.Target == it_Curve->Target; }))
                    {
                        return std::format("{} has two curves of its {} by parameter '{}'", Describe(it_Container), l_Target, std::string_view(l_Parameter->Name));
                    }

                    for (std::size_t it_Point = 0; it_Point < it_Curve->Points.size(); ++it_Point)
                    {
                        const glm::vec2& l_Point = it_Curve->Points[it_Point];
                        if (!AreNumbers({ l_Point.x, l_Point.y }))
                        {
                            return std::format("{}'s {} curve by '{}' has a point that is no number", Describe(it_Container), l_Target, std::string_view(l_Parameter->Name));
                        }

                        if (it_Point > 0 && l_Point.x <= it_Curve->Points[it_Point - 1].x)
                        {
                            return std::format("{}'s {} curve by '{}' has its points out of order, or two at one value", Describe(it_Container), l_Target, std::string_view(l_Parameter->Name));
                        }
                    }
                }
            }

            return {};
        }

        // Once the containers and the parameters keep to their rules, as a Switch names one and a case another's label
        std::string ValidateSwitches(const SoundEventData& data)
        {
            for (const SoundContainer& it_Container : data.Containers)
            {
                if (it_Container.Type == SoundContainerType::Switch && it_Container.Parameter)
                {
                    const SoundParameter* l_Parameter = data.FindParameter(it_Container.Parameter);
                    if (l_Parameter == nullptr)
                    {
                        return std::format("{} is a Switch by no parameter the event has", Describe(it_Container));
                    }

                    if (l_Parameter->Kind != SoundParameterKind::Discrete)
                    {
                        return std::format("{} is a Switch by parameter '{}', which is continuous and has no labels", Describe(it_Container), std::string_view(l_Parameter->Name));
                    }
                }

                const SoundContainer* l_Parent = it_Container.Parent ? data.FindContainer(it_Container.Parent) : nullptr;
                if (it_Container.Case.empty() || l_Parent == nullptr || l_Parent->Type != SoundContainerType::Switch)
                {
                    continue;
                }

                const SoundParameter* l_Parameter = l_Parent->Parameter ? data.FindParameter(l_Parent->Parameter) : nullptr;
                if (l_Parameter == nullptr)
                {
                    return std::format("{} plays for '{}', and {} picks by no parameter", Describe(it_Container), std::string_view(it_Container.Case), Describe(*l_Parent));
                }

                if (std::ranges::find(l_Parameter->Labels, it_Container.Case) == l_Parameter->Labels.end())
                {
                    return std::format("{} plays for '{}', which is no label of parameter '{}'", Describe(it_Container), std::string_view(it_Container.Case), std::string_view(l_Parameter->Name));
                }
            }

            return {};
        }
    }

    // Between the two points either side of the value, found by halves, since points are in order
    float SoundCurve::Evaluate(float value) const
    {
        if (Points.empty())
        {
            return 0.0f;
        }

        if (!(value > Points.front().x))
        {
            return Points.front().y;
        }

        if (value >= Points.back().x)
        {
            return Points.back().y;
        }

        const auto a_After = std::ranges::upper_bound(Points, value, {}, [](const glm::vec2& point) { return point.x; });
        const glm::vec2& l_Before = *(a_After - 1);

        return l_Before.y + (a_After->y - l_Before.y) * (value - l_Before.x) / (a_After->x - l_Before.x);
    }

    float SoundParameter::GetMinimum() const
    {
        return Kind == SoundParameterKind::Continuous ? Minimum : 0.0f;
    }

    float SoundParameter::GetMaximum() const
    {
        return Kind == SoundParameterKind::Continuous ? Maximum : static_cast<float>(std::max<std::size_t>(Labels.size(), 1) - 1);
    }

    float SoundParameter::Clamp(float value) const
    {
        const float l_Value = std::clamp(std::isfinite(value) ? value : (std::isfinite(Default) ? Default : 0.0f), GetMinimum(), std::max(GetMinimum(), GetMaximum()));

        return Kind == SoundParameterKind::Discrete ? std::round(l_Value) : l_Value;
    }

    SoundEventData SoundEventData::MakeDefault()
    {
        SoundEventData l_Data;
        SoundContainer& l_Root = l_Data.Containers.emplace_back();
        l_Root.ID = UUID::Generate();
        l_Root.Name = TaggedString<MemoryTag::Audio>(std::string_view("Root"));
        l_Root.Type = SoundContainerType::Random;

        return l_Data;
    }

    const SoundContainer& SoundEventData::GetRoot() const
    {
        return Containers.front();
    }

    const SoundContainer* SoundEventData::FindContainer(UUID id) const
    {
        const auto a_Found = id ? std::ranges::find(Containers, id, &SoundContainer::ID) : Containers.end();

        return a_Found != Containers.end() ? &*a_Found : nullptr;
    }

    SoundContainer* SoundEventData::FindContainer(UUID id)
    {
        return const_cast<SoundContainer*>(std::as_const(*this).FindContainer(id));
    }

    const SoundParameter* SoundEventData::FindParameter(UUID id) const
    {
        const auto a_Found = id ? std::ranges::find(Parameters, id, &SoundParameter::ID) : Parameters.end();

        return a_Found != Parameters.end() ? &*a_Found : nullptr;
    }


    SoundParameter* SoundEventData::FindParameter(UUID id)
    {
        return const_cast<SoundParameter*>(std::as_const(*this).FindParameter(id));
    }

    const SoundParameter* SoundEventData::FindParameter(std::string_view name) const
    {
        const auto a_Found = std::ranges::find_if(Parameters, [name](const SoundParameter& parameter) { return std::string_view(parameter.Name) == name; });

        return a_Found != Parameters.end() ? &*a_Found : nullptr;
    }

    std::vector<UUID, TaggedAllocator<UUID, MemoryTag::Audio>> SoundEventData::GetClips() const
    {
        std::vector<UUID, TaggedAllocator<UUID, MemoryTag::Audio>> l_Clips;
        for (const SoundContainer& it_Container : Containers)
        {
            if (it_Container.Type == SoundContainerType::Clip && it_Container.Clip && std::ranges::find(l_Clips, it_Container.Clip) == l_Clips.end())
            {
                l_Clips.push_back(it_Container.Clip);
            }
        }

        return l_Clips;
    }

    const SoundCurve* SoundEventData::FindCurve(UUID container, UUID parameter, SoundCurveTarget target) const
    {
        const SoundContainer* l_Container = FindContainer(container);
        if (l_Container == nullptr)
        {
            return nullptr;
        }

        const auto a_Found = std::ranges::find_if(l_Container->Curves, [parameter, target](const SoundCurve& curve) { return curve.Parameter == parameter && curve.Target == target; });

        return a_Found != l_Container->Curves.end() ? &*a_Found : nullptr;
    }

    SoundCurve* SoundEventData::FindCurve(UUID container, UUID parameter, SoundCurveTarget target)
    {
        return const_cast<SoundCurve*>(std::as_const(*this).FindCurve(container, parameter, target));
    }

    SoundContainer* SoundEventData::AddContainer(UUID parent, SoundContainerType type, std::string_view name, UUID clip)
    {
        const SoundContainer* l_Parent = FindContainer(parent);
        if (l_Parent == nullptr || l_Parent->Type == SoundContainerType::Clip)
        {
            return nullptr;
        }

        SoundContainer l_Container;
        l_Container.ID = UUID::Generate();
        l_Container.Parent = parent;
        l_Container.Name = TaggedString<MemoryTag::Audio>(name);
        l_Container.Type = type;
        l_Container.Clip = type == SoundContainerType::Clip ? clip : UUID();
        const auto a_At = Containers.begin() + static_cast<std::ptrdiff_t>(FindBranchEnd(*this, parent));

        return &*Containers.insert(a_At, std::move(l_Container));
    }

    SoundContainer* SoundEventData::DuplicateContainer(UUID container, std::string_view name)
    {
        if (!container || Containers.empty() || Containers.front().ID == container || FindContainer(container) == nullptr)
        {
            return nullptr;
        }

        std::vector<SoundContainer, TaggedAllocator<SoundContainer, MemoryTag::Audio>> l_Copies;
        std::vector<std::pair<UUID, UUID>, TaggedAllocator<std::pair<UUID, UUID>, MemoryTag::Audio>> l_IDs;
        for (const SoundContainer& it_Container : Containers)
        {
            if (IsWithin(*this, it_Container.ID, container))
            {
                l_IDs.emplace_back(it_Container.ID, UUID::Generate());
                l_Copies.push_back(it_Container);
            }
        }

        for (SoundContainer& it_Copy : l_Copies)
        {
            const auto a_Parent = std::ranges::find(l_IDs, it_Copy.Parent, &std::pair<UUID, UUID>::first);
            it_Copy.Parent = a_Parent != l_IDs.end() ? a_Parent->second : it_Copy.Parent;
            it_Copy.ID = std::ranges::find(l_IDs, it_Copy.ID, &std::pair<UUID, UUID>::first)->second;
        }

        l_Copies.front().Name = TaggedString<MemoryTag::Audio>(name);
        const auto a_At = Containers.begin() + static_cast<std::ptrdiff_t>(FindBranchEnd(*this, container));

        return &*Containers.insert(a_At, l_Copies.begin(), l_Copies.end());
    }

    bool SoundEventData::RemoveContainer(UUID container)
    {
        if (!container || Containers.empty() || Containers.front().ID == container || FindContainer(container) == nullptr)
        {
            return false;
        }

        std::vector<UUID, TaggedAllocator<UUID, MemoryTag::Audio>> l_Gone;
        for (const SoundContainer& it_Container : Containers)
        {
            if (IsWithin(*this, it_Container.ID, container))
            {
                l_Gone.push_back(it_Container.ID);
            }
        }

        std::erase_if(Containers, [&l_Gone](const SoundContainer& it_Container) { return std::ranges::find(l_Gone, it_Container.ID) != l_Gone.end(); });

        return true;
    }

    // Taken out with everything below it in their order, then put back in one piece where it goes
    bool SoundEventData::MoveContainer(UUID container, UUID parent, UUID before)
    {
        const SoundContainer* l_Container = FindContainer(container);
        const SoundContainer* l_Parent = FindContainer(parent);
        const SoundContainer* l_Before = before ? FindContainer(before) : nullptr;
        if (l_Container == nullptr || l_Container == &Containers.front() || l_Parent == nullptr || l_Parent->Type == SoundContainerType::Clip || IsWithin(*this, parent, container) || (before && (l_Before == nullptr || l_Before->Parent != parent || before == container)))
        {
            return false;
        }

        const SoundContainer* l_From = FindContainer(l_Container->Parent);
        const bool l_KeepsCase = l_From != nullptr && l_From->Type == SoundContainerType::Switch && l_Parent->Type == SoundContainerType::Switch && l_From->Parameter == l_Parent->Parameter;
        const bool l_KeepsWeight = l_Parent->Type == SoundContainerType::Random;
        std::vector<SoundContainer, TaggedAllocator<SoundContainer, MemoryTag::Audio>> l_Branch;
        for (const SoundContainer& it_Container : Containers)
        {
            if (IsWithin(*this, it_Container.ID, container))
            {
                l_Branch.push_back(it_Container);
            }
        }

        std::erase_if(Containers, [&l_Branch](const SoundContainer& it_Container) { return std::ranges::find(l_Branch, it_Container.ID, &SoundContainer::ID) != l_Branch.end(); });
        l_Branch.front().Parent = parent;
        if (!l_KeepsCase)
        {
            l_Branch.front().Case.clear();
        }

        if (!l_KeepsWeight)
        {
            l_Branch.front().Weight = SoundContainer().Weight;
        }

        const std::size_t l_At = before ? static_cast<std::size_t>(std::ranges::find(Containers, before, &SoundContainer::ID) - Containers.begin()) : FindBranchEnd(*this, parent);
        Containers.insert(Containers.begin() + static_cast<std::ptrdiff_t>(l_At), l_Branch.begin(), l_Branch.end());

        return true;
    }

    bool SoundEventData::SetContainerType(UUID container, SoundContainerType type)
    {
        SoundContainer* l_Container = FindContainer(container);
        if (l_Container == nullptr || (type == SoundContainerType::Clip && std::ranges::any_of(Containers, [container](const SoundContainer& it_Child) { return it_Child.Parent == container; })))
        {
            return false;
        }

        if (l_Container->Type == SoundContainerType::Switch && type != SoundContainerType::Switch)
        {
            ClearCases(*this, container);
        }

        if (l_Container->Type == SoundContainerType::Random && type != SoundContainerType::Random)
        {
            for (SoundContainer& it_Child : Containers)
            {
                it_Child.Weight = it_Child.Parent == container ? SoundContainer().Weight : it_Child.Weight;
            }
        }

        const SoundParameter* l_Parameter = FindParameter(l_Container->Parameter);
        if (type == SoundContainerType::Switch && (l_Parameter == nullptr || l_Parameter->Kind != SoundParameterKind::Discrete))
        {
            l_Container->Parameter = UUID();
        }

        l_Container->Type = type;
        ForgetUnsaved(*l_Container);

        return true;
    }

    bool SoundEventData::SetSwitchParameter(UUID container, UUID parameter)
    {
        SoundContainer* l_Container = FindContainer(container);
        const SoundParameter* l_Parameter = parameter ? FindParameter(parameter) : nullptr;
        if (l_Container == nullptr || l_Container->Type != SoundContainerType::Switch || (parameter && (l_Parameter == nullptr || l_Parameter->Kind != SoundParameterKind::Discrete)))
        {
            return false;
        }

        if (l_Container->Parameter != parameter)
        {
            ClearCases(*this, container);
        }

        l_Container->Parameter = parameter;

        return true;
    }

    SoundParameter* SoundEventData::AddParameter(std::string_view name, SoundParameterKind kind)
    {
        if (name.empty() || FindParameter(name) != nullptr)
        {
            return nullptr;
        }

        SoundParameter& l_Parameter = Parameters.emplace_back();
        l_Parameter.ID = UUID::Generate();
        l_Parameter.Name = TaggedString<MemoryTag::Audio>(name);
        l_Parameter.Kind = kind;
        if (kind == SoundParameterKind::Discrete)
        {
            l_Parameter.Labels.emplace_back(std::string_view("Label 1"));
        }

        return &l_Parameter;
    }

    bool SoundEventData::RemoveParameter(UUID parameter)
    {
        if (FindParameter(parameter) == nullptr)
        {
            return false;
        }

        ClearCases(*this, UUID(), parameter);
        for (SoundContainer& it_Container : Containers)
        {
            std::erase_if(it_Container.Curves, [parameter](const SoundCurve& curve) { return curve.Parameter == parameter; });
            it_Container.Parameter = it_Container.Parameter == parameter ? UUID() : it_Container.Parameter;
        }

        std::erase_if(Parameters, [parameter](const SoundParameter& it_Parameter) { return it_Parameter.ID == parameter; });

        return true;
    }

    bool SoundEventData::RenameParameter(UUID parameter, std::string_view name)
    {
        const SoundParameter* l_Other = FindParameter(name);
        SoundParameter* l_Parameter = FindParameter(parameter);
        if (l_Parameter == nullptr || name.empty() || (l_Other != nullptr && l_Other != l_Parameter))
        {
            return false;
        }

        l_Parameter->Name = TaggedString<MemoryTag::Audio>(name);

        return true;
    }

    bool SoundEventData::SetParameterKind(UUID parameter, SoundParameterKind kind)
    {
        SoundParameter* l_Parameter = FindParameter(parameter);
        if (l_Parameter == nullptr)
        {
            return false;
        }

        if (l_Parameter->Kind == kind)
        {
            return true;
        }

        if (kind == SoundParameterKind::Continuous)
        {
            ClearCases(*this, UUID(), parameter);
            for (SoundContainer& it_Container : Containers)
            {
                it_Container.Parameter = it_Container.Type == SoundContainerType::Switch && it_Container.Parameter == parameter ? UUID() : it_Container.Parameter;
            }

            l_Parameter->Minimum = 0.0f;
            l_Parameter->Maximum = static_cast<float>(std::max<std::size_t>(l_Parameter->Labels.size(), 2) - 1);
            l_Parameter->Labels.clear();
        }
        else
        {
            l_Parameter->Labels = { TaggedString<MemoryTag::Audio>(std::string_view("Label 1")) };
        }

        l_Parameter->Kind = kind;
        l_Parameter->Default = l_Parameter->Clamp(l_Parameter->Default);

        return true;
    }

    bool SoundEventData::AddLabel(UUID parameter, std::string_view name)
    {
        SoundParameter* l_Parameter = FindParameter(parameter);
        if (l_Parameter == nullptr || l_Parameter->Kind != SoundParameterKind::Discrete || !IsLabelFree(*l_Parameter, name))
        {
            return false;
        }

        l_Parameter->Labels.emplace_back(name);

        return true;
    }

    bool SoundEventData::RenameLabel(UUID parameter, std::size_t index, std::string_view name)
    {
        SoundParameter* l_Parameter = FindParameter(parameter);
        if (l_Parameter == nullptr || l_Parameter->Kind != SoundParameterKind::Discrete || index >= l_Parameter->Labels.size() || !IsLabelFree(*l_Parameter, name, index))
        {
            return false;
        }

        const TaggedString<MemoryTag::Audio> l_Old = l_Parameter->Labels[index];
        for (SoundContainer& it_Child : Containers)
        {
            const SoundContainer* l_Switch = it_Child.Parent ? FindContainer(it_Child.Parent) : nullptr;
            if (l_Switch != nullptr && l_Switch->Type == SoundContainerType::Switch && l_Switch->Parameter == parameter && it_Child.Case == l_Old)
            {
                it_Child.Case = TaggedString<MemoryTag::Audio>(name);
            }
        }

        l_Parameter->Labels[index] = TaggedString<MemoryTag::Audio>(name);

        return true;
    }

    bool SoundEventData::RemoveLabel(UUID parameter, std::size_t index)
    {
        SoundParameter* l_Parameter = FindParameter(parameter);
        if (l_Parameter == nullptr || l_Parameter->Kind != SoundParameterKind::Discrete || index >= l_Parameter->Labels.size() || l_Parameter->Labels.size() == 1)
        {
            return false;
        }

        ClearCases(*this, UUID(), parameter, std::string_view(l_Parameter->Labels[index]));
        l_Parameter->Labels.erase(l_Parameter->Labels.begin() + static_cast<std::ptrdiff_t>(index));
        l_Parameter->Default = l_Parameter->Clamp(l_Parameter->Default);

        return true;
    }

    SoundCurve* SoundEventData::AddCurve(UUID container, UUID parameter, SoundCurveTarget target)
    {
        SoundContainer* l_Container = FindContainer(container);
        const SoundParameter* l_Parameter = FindParameter(parameter);
        if (l_Container == nullptr || l_Parameter == nullptr)
        {
            return nullptr;
        }

        if (SoundCurve* l_Found = FindCurve(container, parameter, target))
        {
            return l_Found;
        }

        SoundCurve& l_Curve = l_Container->Curves.emplace_back();
        l_Curve.Parameter = parameter;
        l_Curve.Target = target;
        const float l_Rest = GetCurveRest(target);
        l_Curve.Points.emplace_back(l_Parameter->GetMinimum(), l_Rest);
        if (l_Parameter->GetMaximum() > l_Parameter->GetMinimum())
        {
            l_Curve.Points.emplace_back(l_Parameter->GetMaximum(), l_Rest);
        }

        return &l_Curve;
    }

    bool SoundEventData::RemoveCurve(UUID container, UUID parameter, SoundCurveTarget target)
    {
        SoundContainer* l_Container = FindContainer(container);

        return l_Container != nullptr && std::erase_if(l_Container->Curves, [parameter, target](const SoundCurve& curve) { return curve.Parameter == parameter && curve.Target == target; }) != 0;
    }

    std::string_view GetSoundContainerTypeKey(SoundContainerType type)
    {
        return GetKey(c_ContainerTypeKeys, type);
    }

    std::optional<SoundContainerType> FindSoundContainerType(std::string_view key)
    {
        return FindKey<SoundContainerType>(c_ContainerTypeKeys, key);
    }

    std::string_view GetSoundTriggerTimingKey(SoundTriggerTiming timing)
    {
        return GetKey(c_TriggerTimingKeys, timing);
    }

    std::optional<SoundTriggerTiming> FindSoundTriggerTiming(std::string_view key)
    {
        return FindKey<SoundTriggerTiming>(c_TriggerTimingKeys, key);
    }

    std::string_view GetSoundStealModeKey(SoundStealMode mode)
    {
        return GetKey(c_StealModeKeys, mode);
    }

    std::optional<SoundStealMode> FindSoundStealMode(std::string_view key)
    {
        return FindKey<SoundStealMode>(c_StealModeKeys, key);
    }

    std::string_view GetSoundParameterKindKey(SoundParameterKind kind)
    {
        return GetKey(c_ParameterKindKeys, kind);
    }

    std::optional<SoundParameterKind> FindSoundParameterKind(std::string_view key)
    {
        return FindKey<SoundParameterKind>(c_ParameterKindKeys, key);
    }

    std::string_view GetSoundCurveTargetKey(SoundCurveTarget target)
    {
        return GetKey(c_CurveTargetKeys, target);
    }

    std::optional<SoundCurveTarget> FindSoundCurveTarget(std::string_view key)
    {
        return FindKey<SoundCurveTarget>(c_CurveTargetKeys, key);
    }

    std::string ValidateSoundEvent(const SoundEventData& data)
    {
        if (std::string l_Error = ValidateContainers(data); !l_Error.empty())
        {
            return l_Error;
        }

        if (std::string l_Error = ValidateParameters(data); !l_Error.empty())
        {
            return l_Error;
        }

        if (std::string l_Error = ValidateSwitches(data); !l_Error.empty())
        {
            return l_Error;
        }

        if (std::string l_Error = ValidateCurves(data); !l_Error.empty())
        {
            return l_Error;
        }

        if (static_cast<std::size_t>(data.Steal) >= c_StealModeKeys.size() || static_cast<std::size_t>(data.Rolloff) >= c_AudioRolloffKeys.size())
        {
            return "it steals or rolls off in no known way";
        }

        if (!AreNumbers({ data.Cooldown, data.DopplerLevel, data.Spread, data.MinDistance, data.MaxDistance, data.OcclusionVolume, data.OcclusionCutoff }))
        {
            return "it has a value that is no number";
        }

        return {};
    }

    SoundEventAsset::SoundEventAsset(SoundEventData data) : m_Data(std::move(data)), m_Clips(m_Data.GetClips()), m_Serial(s_NextSerial.fetch_add(1, std::memory_order_relaxed))
    {

    }

    Expected<Asset*, std::string> SoundEventLoader::Load(std::span<const std::byte> data) const
    {
        Expected<SoundEventData, std::string> l_Data = ParseSoundEvent(std::string_view(reinterpret_cast<const char*>(data.data()), data.size()));
        if (!l_Data)
        {
            return Unexpected{ l_Data.GetError() };
        }

        return Memory::New<SoundEventAsset>(MemoryTag::Audio, std::move(*l_Data));
    }

    std::string WriteSoundEvent(const SoundEventData& data)
    {
        std::string l_Text = std::format("Format: {}", SoundEventData::c_FormatVersion);
        ComponentWriter l_Writer(l_Text, 0);
        if (data.Output)
        {
            l_Writer.Write("Output", data.Output);
        }

        l_Writer.Write("Priority", data.Priority);
        l_Writer.Write("MaxInstances", data.MaxInstances);
        l_Writer.Write("Steal", GetSoundStealModeKey(data.Steal));
        l_Writer.Write("Cooldown", data.Cooldown);
        l_Writer.Write("Spatialize", data.Spatialize);
        l_Writer.Write("DopplerLevel", data.DopplerLevel);
        l_Writer.Write("Spread", data.Spread);
        l_Writer.Write("MinDistance", data.MinDistance);
        l_Writer.Write("MaxDistance", data.MaxDistance);
        l_Writer.Write("Rolloff", GetAudioRolloffKey(data.Rolloff));
        l_Writer.Write("Occlusion", data.Occlusion);
        l_Writer.Write("OcclusionVolume", data.OcclusionVolume);
        l_Writer.Write("OcclusionCutoff", data.OcclusionCutoff);
        if (!data.Parameters.empty())
        {
            l_Writer.BeginList("Parameters");
            for (const SoundParameter& it_Parameter : data.Parameters)
            {
                l_Writer.BeginItem();
                l_Writer.Write("ID", it_Parameter.ID);
                l_Writer.Write("Name", std::string_view(it_Parameter.Name));
                l_Writer.Write("Kind", GetSoundParameterKindKey(it_Parameter.Kind));
                l_Writer.Write("Global", it_Parameter.Global);
                if (it_Parameter.Kind == SoundParameterKind::Continuous)
                {
                    l_Writer.Write("Minimum", it_Parameter.Minimum);
                    l_Writer.Write("Maximum", it_Parameter.Maximum);
                    l_Writer.Write("Default", it_Parameter.Default);
                    l_Writer.Write("SeekSpeed", it_Parameter.SeekSpeed);

                    continue;
                }

                l_Writer.Write("Default", it_Parameter.Default);
                l_Writer.BeginList("Labels");
                for (const TaggedString<MemoryTag::Audio>& it_Label : it_Parameter.Labels)
                {
                    l_Writer.BeginItem();
                    l_Writer.Write("Name", std::string_view(it_Label));
                }

                l_Writer.End();
            }

            l_Writer.End();
        }

        l_Writer.BeginList("Containers");
        for (const SoundContainer& it_Container : data.Containers)
        {
            l_Writer.BeginItem();
            l_Writer.Write("ID", it_Container.ID);
            l_Writer.Write("Name", std::string_view(it_Container.Name));
            if (it_Container.Parent)
            {
                l_Writer.Write("Parent", it_Container.Parent);
            }

            l_Writer.Write("Type", GetSoundContainerTypeKey(it_Container.Type));
            l_Writer.Write("Volume", it_Container.Volume);
            l_Writer.Write("VolumeRandom", it_Container.VolumeRandom);
            l_Writer.Write("Pitch", it_Container.Pitch);
            l_Writer.Write("PitchRandom", it_Container.PitchRandom);
            l_Writer.Write("Delay", it_Container.Delay);
            l_Writer.Write("DelayRandom", it_Container.DelayRandom);
            const SoundContainer* l_Parent = it_Container.Parent ? data.FindContainer(it_Container.Parent) : nullptr;
            if (l_Parent != nullptr && l_Parent->Type == SoundContainerType::Random)
            {
                l_Writer.Write("Weight", it_Container.Weight);
            }

            if (l_Parent != nullptr && l_Parent->Type == SoundContainerType::Switch && !it_Container.Case.empty())
            {
                l_Writer.Write("Case", std::string_view(it_Container.Case));
            }

            switch (it_Container.Type)
            {
                case SoundContainerType::Clip:
                {
                    if (it_Container.Clip)
                    {
                        l_Writer.Write("Clip", it_Container.Clip);
                    }

                    l_Writer.Write("Loop", it_Container.Loop);
                    break;
                }
                case SoundContainerType::Random:
                {
                    l_Writer.Write("AvoidRepeat", it_Container.AvoidRepeat);
                    l_Writer.Write("AutoTrigger", it_Container.AutoTrigger);
                    l_Writer.Write("Timing", GetSoundTriggerTimingKey(it_Container.Timing));
                    l_Writer.Write("IntervalMinimum", it_Container.IntervalMinimum);
                    l_Writer.Write("IntervalMaximum", it_Container.IntervalMaximum);
                    l_Writer.Write("TriggerCount", it_Container.TriggerCount);
                    break;
                }
                case SoundContainerType::Sequence:
                {
                    l_Writer.Write("Loop", it_Container.Loop);
                    break;
                }
                case SoundContainerType::Switch:
                {
                    if (it_Container.Parameter)
                    {
                        l_Writer.Write("Parameter", it_Container.Parameter);
                    }

                    break;
                }
                case SoundContainerType::Layer:
                {
                    break;
                }
            }

            if (!it_Container.Curves.empty())
            {
                l_Writer.BeginList("Curves");
                for (const SoundCurve& it_Curve : it_Container.Curves)
                {
                    l_Writer.BeginItem();
                    l_Writer.Write("Parameter", it_Curve.Parameter);
                    l_Writer.Write("Target", GetSoundCurveTargetKey(it_Curve.Target));
                    l_Writer.Write("Points", std::span<const glm::vec2>(it_Curve.Points));
                }

                l_Writer.End();
            }
        }

        l_Writer.End();

        return l_Text + "\n";
    }

    Expected<SoundEventData, std::string> ParseSoundEvent(std::string_view text)
    {
        const Expected<YAML::Node, std::string> l_Root = CookedText::LoadRoot(text, "a sound event file", SoundEventData::c_FormatVersion);
        if (!l_Root)
        {
            return Unexpected{ l_Root.GetError() };
        }

        SoundEventData l_Data;
        std::string l_Steal(GetSoundStealModeKey(l_Data.Steal));
        std::string l_Rolloff(GetAudioRolloffKey(l_Data.Rolloff));
        std::string l_Malformed;
        CookedText::ReadUUID(*l_Root, "Output", l_Data.Output, l_Malformed);
        CookedText::ReadNumber(*l_Root, "Priority", l_Data.Priority, l_Malformed);
        CookedText::ReadNumber(*l_Root, "MaxInstances", l_Data.MaxInstances, l_Malformed);
        CookedText::ReadString(*l_Root, "Steal", l_Steal, l_Malformed);
        CookedText::ReadNumber(*l_Root, "Cooldown", l_Data.Cooldown, l_Malformed);
        CookedText::ReadBool(*l_Root, "Spatialize", l_Data.Spatialize, l_Malformed);
        CookedText::ReadNumber(*l_Root, "DopplerLevel", l_Data.DopplerLevel, l_Malformed);
        CookedText::ReadNumber(*l_Root, "Spread", l_Data.Spread, l_Malformed);
        CookedText::ReadNumber(*l_Root, "MinDistance", l_Data.MinDistance, l_Malformed);
        CookedText::ReadNumber(*l_Root, "MaxDistance", l_Data.MaxDistance, l_Malformed);
        CookedText::ReadString(*l_Root, "Rolloff", l_Rolloff, l_Malformed);
        CookedText::ReadBool(*l_Root, "Occlusion", l_Data.Occlusion, l_Malformed);
        CookedText::ReadNumber(*l_Root, "OcclusionVolume", l_Data.OcclusionVolume, l_Malformed);
        CookedText::ReadNumber(*l_Root, "OcclusionCutoff", l_Data.OcclusionCutoff, l_Malformed);
        if (!l_Malformed.empty())
        {
            return Unexpected{ std::format("{} is malformed", l_Malformed) };
        }

        const std::optional<SoundStealMode> l_FoundSteal = FindSoundStealMode(l_Steal);
        const std::optional<AudioRolloff> l_FoundRolloff = FindAudioRolloff(l_Rolloff);
        if (!l_FoundSteal || !l_FoundRolloff)
        {
            return Unexpected{ !l_FoundSteal ? std::format("Steal is '{}', which is no steal mode", l_Steal) : std::format("Rolloff is '{}', which is no rolloff", l_Rolloff) };
        }

        l_Data.Steal = *l_FoundSteal;
        l_Data.Rolloff = *l_FoundRolloff;
        const YAML::Node l_Parameters = (*l_Root)["Parameters"];
        const YAML::Node l_Containers = (*l_Root)["Containers"];
        if ((l_Parameters && !l_Parameters.IsSequence()) || !l_Containers || !l_Containers.IsSequence())
        {
            return Unexpected{ std::string(l_Parameters && !l_Parameters.IsSequence() ? "Parameters is not a list" : "Containers is not a list") };
        }

        for (std::size_t it_Parameter = 0; l_Parameters && it_Parameter < l_Parameters.size(); ++it_Parameter)
        {
            if (std::string l_Error = ReadParameter(l_Parameters[it_Parameter], l_Data.Parameters.emplace_back()); !l_Error.empty())
            {
                return Unexpected{ std::move(l_Error) };
            }
        }

        for (std::size_t it_Container = 0; it_Container < l_Containers.size(); ++it_Container)
        {
            if (std::string l_Error = ReadContainer(l_Containers[it_Container], l_Data.Containers.emplace_back()); !l_Error.empty())
            {
                return Unexpected{ std::move(l_Error) };
            }
        }

        if (std::string l_Error = ValidateSoundEvent(l_Data); !l_Error.empty())
        {
            return Unexpected{ std::move(l_Error) };
        }

        return l_Data;
    }
}