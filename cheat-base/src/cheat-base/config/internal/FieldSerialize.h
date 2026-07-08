#pragma once

#include "FieldEntry.h"
#include <cheat-base/config/converters.h>

#include <type_traits>
#include <utility>

namespace config::internal
{
	namespace CHECK
	{
		template<typename T, typename = void>
		struct EqualExists : std::false_type {};

		template<typename T>
		struct EqualExists<T, std::void_t<decltype(std::declval<T>() == std::declval<T>())>>
			: std::true_type {};
	}

	template<typename T>
	class FieldSerialize : public FieldEntry
	{
	public:
		FieldSerialize(const std::string& friendlyName, const std::string& name, const std::string& sectionName, const T& defaultValue, bool multiProfile = false) :
			FieldEntry(friendlyName, name, sectionName, multiProfile), m_Value(defaultValue), m_DefaultValue(defaultValue) { }

		nlohmann::json ToJson() override
		{
			if constexpr (CHECK::EqualExists<T>::value)
			{
				if (m_Value == m_DefaultValue)
					return {};
			}


			return converters::ToJson(m_Value);
		}

		void FromJson(const nlohmann::json& jObject) override
		{
			if (jObject.empty())
			{
				m_Value = m_DefaultValue;
				return;
			}

			converters::FromJson(m_Value, jObject);
		}
		
		void Reset() override
		{
			m_Value = m_DefaultValue;
		}

		T m_Value;
		T m_DefaultValue;
	};
}