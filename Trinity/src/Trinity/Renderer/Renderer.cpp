#include "Trinity/trpch.h"

#include "Trinity/Renderer/Renderer.h"
#include "Trinity/Utilities/Utilities.h"

namespace Trinity
{
	bool Renderer::Initialize()
	{
		TR_CORE_INFO("-------INITIALIZING RENDERER-------");

		CreateGraphicsPipeline();

		TR_CORE_INFO("-------RENDERER INITIALIZED-------");

		return true;
	}

	void Renderer::Shutdown()
	{
		TR_CORE_INFO("-------SHUTTING DOWN RENDERER-------");

		TR_CORE_INFO("-------RENDERER SHUTDOWN COMPLETE-------");
	}

	void Renderer::DrawFrame()
	{

	}

	//----------------------------------------------------------------------------------------------------------------------------------------------------//

	void Renderer::CreateGraphicsPipeline()
	{
		TR_CORE_TRACE("Creating graphics pipeline");

		TR_CORE_TRACE("Graphics pipeline created");
	}
}