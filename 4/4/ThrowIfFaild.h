#ifndef THROWIFFAILD_HPP
#define THROWIFFAILD_HPP

#include <stdexcept>

template<typename T> inline void ThrowIfFailed(T hr, const char* message = "DirectX operation failed")
{
	if (FAILED(hr))
	{
		throw std::runtime_error(message);
	}
}


#endif // !THROWIFFAILD_HPP
