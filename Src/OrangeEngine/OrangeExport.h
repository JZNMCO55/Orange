#ifndef ORANGEEXPORT_H
#define ORANGEEXPORT_H

#ifdef ORANGE_EXPORTS
#define ORANGE_API __declspec(dllexport)
#else
#define ORANGE_API __declspec(dllimport)
#endif

#define BIT(x) (1 << x)

#define ORANGE_BIND_EVENT_FN(fn) std::bind(&fn, this, std::placeholders::_1)

#endif // ORANGEEXPORT_H