#pragma once

namespace TrackQuestFromMap::StarMapSelection::Detail
{
	constexpr std::size_t kMaximumUIChildren = 4096;
	constexpr std::size_t kMaximumQuestTargetTextBytes = 4096;

	[[nodiscard]] inline std::optional<std::uint32_t> ReadGFxUInt(
		const RE::Scaleform::GFx::Value& a_value) noexcept
	{
		if (a_value.IsUInt())
		{
			return a_value.GetUInt();
		}
		if (a_value.IsInt())
		{
			const auto value = a_value.GetInt();
			return value >= 0
				? std::optional{static_cast<std::uint32_t>(value)}
				: std::nullopt;
		}
		if (a_value.IsNumber())
		{
			const double number = a_value.GetNumber();
			if (std::isfinite(number) && number >= 0.0 &&
				std::trunc(number) == number &&
				number <= static_cast<double>(std::numeric_limits<std::uint32_t>::max()))
			{
				return static_cast<std::uint32_t>(number);
			}
		}
		return std::nullopt;
	}

	[[nodiscard]] inline bool ReadGFxBooleanMember(
		const RE::Scaleform::GFx::Value& a_object,
		const std::string_view a_name,
		bool& a_result)
	{
		if (!a_object.IsObject())
		{
			return false;
		}
		RE::Scaleform::GFx::Value value;
		if (!a_object.GetMember(a_name, std::addressof(value)) || !value.IsBoolean())
		{
			return false;
		}
		a_result = value.GetBoolean();
		return true;
	}

	[[nodiscard]] inline bool ReadGFxStringMember(
		const RE::Scaleform::GFx::Value& a_object,
		const std::string_view a_name,
		std::string& a_result)
	{
		if (!a_object.IsObject())
		{
			return false;
		}
		RE::Scaleform::GFx::Value value;
		if (!a_object.GetMember(a_name, std::addressof(value)) || !value.IsString())
		{
			return false;
		}

		// GetString may point into a managed GFx value. Copy it while `value` is
		// alive and never retain the pointer beyond this call.
		const auto text = value.GetString();
		if (!text)
		{
			return false;
		}
		std::size_t length = 0;
		while (length <= kMaximumQuestTargetTextBytes && text[length] != '\0')
		{
			++length;
		}
		if (length > kMaximumQuestTargetTextBytes)
		{
			return false;
		}
		a_result.assign(text, length);
		return true;
	}

	[[nodiscard]] inline std::optional<std::size_t> ReadDisplayChildCount(
		const RE::Scaleform::GFx::Value& a_object)
	{
		RE::Scaleform::GFx::Value countValue;
		if (!a_object.IsObject() ||
			!a_object.GetMember("numChildren", std::addressof(countValue)))
		{
			return std::nullopt;
		}
		const auto count = ReadGFxUInt(countValue);
		if (!count || *count > kMaximumUIChildren)
		{
			return std::nullopt;
		}
		return *count;
	}
}
