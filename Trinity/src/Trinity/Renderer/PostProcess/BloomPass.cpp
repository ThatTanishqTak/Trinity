#include "Trinity/trpch.h"

#include "Trinity/Renderer/PostProcess/BloomPass.h"
#include "Trinity/Renderer/ShaderCompiler.h"
#include "Trinity/Utilities/Utilities.h"

namespace Trinity
{
    bool BloomPass::Initialize()
    {
        m_VertexSpv = ShaderCompiler::CompileToSpv("Resources/Shaders/Bloom.vert");
        m_FragmentSpv = ShaderCompiler::CompileToSpv("Resources/Shaders/Bloom.frag");

        return !m_VertexSpv.empty() && !m_FragmentSpv.empty();
    }

    void BloomPass::Execute()
    {
#if _DEBUG
        TR_CORE_TRACE("Executing Bloom pass");
#endif
    }
}