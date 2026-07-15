#pragma once

#include <memory>

#include <hb.h>
#include <hb-subset.h>

namespace FontSubset
{

template<typename T, void (*Destroy)(T*)>
using HbPtr = std::unique_ptr<T, decltype(Destroy)>;

template<typename T, void (*Destroy)(T*)>
HbPtr<T, Destroy> TakeHb(T* pointer)
{
    return HbPtr<T, Destroy>(pointer, Destroy);
}

}
