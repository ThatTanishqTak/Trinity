#include "Trinity/Time/Timer.hpp"

#include "Trinity/Core/Log.hpp"

namespace Trinity
{
	ScopedTimer::~ScopedTimer()
	{
		TR_CORE_TRACE("{} took {:.3f} ms", m_Name, m_Timer.GetElapsedMilliseconds());
	}
}