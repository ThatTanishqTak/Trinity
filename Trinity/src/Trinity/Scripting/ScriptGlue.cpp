#include "trpch.h"
#include "ScriptGlue.h"

#include <glm/glm.hpp>
#include <glm/vec3.hpp>

#include "mono/metadata/object.h"

namespace Trinity
{

#define TR_ADD_INTERNAL_CALL(Name) mono_add_internal_call("Trinity.InternalCalls::" #Name, Name)

	static void NativeLog(MonoString* string, int parameter)
	{
		char* cStr = mono_string_to_utf8(string);
		std::string str(cStr);
		mono_free(cStr);
		std::cout << str << ", " << parameter << std::endl;
	}

	static void NativeLog_Vector(glm::vec3* parameter, glm::vec3* outResult)
	{
		//TR_CORE_WARN("Value: {0}", *parameter);
		*outResult = glm::normalize(*parameter);
	}

	static float NativeLog_VectorDot(glm::vec3* parameter)
	{
		//TR_CORE_WARN("Value: {0}", *parameter);
		return glm::dot(*parameter, *parameter);
	}

	void ScriptGlue::RegisterFunctions()
	{
		TR_ADD_INTERNAL_CALL(NativeLog);
		TR_ADD_INTERNAL_CALL(NativeLog_Vector);
		TR_ADD_INTERNAL_CALL(NativeLog_VectorDot);
	}

}