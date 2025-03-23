#ifndef UUID_H
#define UUID_H

#include "OrangeExport.h"

namespace Orange
{
    class ORANGE_API UUID
    {
    public:
        UUID();
        UUID(uint64_t uuid);
        UUID(const UUID& other) = default;

        operator uint64_t() const { return mUUID; };

    private:
        uint64_t mUUID;
    };
}

namespace std
{
    template <>
    struct hash<Orange::UUID>
    {
        size_t operator()(const Orange::UUID& uuid) const
        {
            return hash<uint64_t>()(uuid);
        }
    };
}

#endif // UUID_H