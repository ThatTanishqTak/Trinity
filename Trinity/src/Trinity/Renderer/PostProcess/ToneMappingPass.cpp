#include "Trinity/trpch.h"

#include "Trinity/Renderer/PostProcess/ToneMappingPass.h"
#include "Trinity/Renderer/ShaderCompiler.h"
#include "Trinity/Utilities/Utilities.h"

namespace Trinity
{
    bool ToneMappingPass::Initialize()
    {
        m_VertexSpv = ShaderCompiler::CompileToSpv("Resources/DefaultAssets/Shaders/ToneMapping.vert");
        m_FragmentSpv = ShaderCompiler::CompileToSpv("Resources/DefaultAssets/Shaders/ToneMapping.frag");

        return !m_VertexSpv.empty() && !m_FragmentSpv.empty();
    }

    void ToneMappingPass::Execute()
    {
#if _DEBUG
        TR_CORE_TRACE("Executing Tone Mapping pass");
#endif
    }
}