//
// Created by 谢雨 on 25-5-2.
//

#ifndef CEREALUTILS_HPP
#define CEREALUTILS_HPP
#include "Eigen/src/Core/Matrix.h"
#include "Eigen/src/Geometry/AlignedBox.h"

#define CEREAL_AR_PTR_SAVE(FIELD) \
ar(FIELD == nullptr); \
if (FIELD != nullptr) { \
    ar(*FIELD); \
}
#define CEREAL_AR_PTR_LOAD(CLASS, FIELD) \
{ \
    bool isNull; \
    ar(isNull); \
    if (!isNull) { \
        CLASS data; \
        ar(data); \
        FIELD = &data; \
    } \
}

#define CEREAL_AR_VEC_PTR_SAVE(FIELD) \
ar(FIELD.size()); \
for (const auto v: FIELD) { \
    ar(*v); \
}
#define CEREAL_AR_VEC_PTR_LOAD(CLASS, FIELD) \
{ \
    size_t size; \
    ar(size); \
    for (int i = 0; i < size; ++i) { \
        CLASS data; \
        ar(data); \
        FIELD.emplace_back(&data); \
    } \
}

namespace cereal
{
    template<class Archive>
    void serialize(Archive & archive,
                   Eigen::AlignedBox<float, 3> & m)
    {
        archive(m.min().x(), m.min().y(), m.min().z());
        archive(m.max().x(), m.max().y(), m.max().z());
    }

}
#endif //CEREALUTILS_HPP
