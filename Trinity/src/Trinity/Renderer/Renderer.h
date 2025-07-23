#pragma once

namespace Trinity
{
	class Renderer
	{
	public:
		Renderer() = default;
		~Renderer() = default;

		bool Initialize();
		void Shutdown();

		void DrawFrame();

	private:
		void CreateGraphicsPipeline();
	};
}