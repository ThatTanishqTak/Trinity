#pragma once

namespace Trinity
{
	using MouseCode = uint16_t;

	namespace Mouse
	{
		enum : MouseCode
		{
			Button1       = 0,
			Button2       = 1,
			Button3       = 2,
			Button4       = 3,
			Button5       = 4,
			Button6       = 5,
			Button7       = 6,
			Button8       = 7,

			MouseLast     = Button8,
			MouseLeft     = Button1,
			MouseLRight   = Button2,
			MouseMiddle   = Button3,
		};
	}
}