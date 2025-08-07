#pragma once

namespace Trinity
{
    class Panel
    {
    public:
        virtual ~Panel() = default;
        virtual void OnUIRender() = 0;
    };
}