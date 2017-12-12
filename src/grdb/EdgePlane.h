/*
 * EdgePlane.h
 *
 *  Created on: Nov 28, 2017
 *      Author: florian
 */

#ifndef SRC_GRDB_EDGEPLANE_H_
#define SRC_GRDB_EDGEPLANE_H_

#include <boost/multi_array/base.hpp>
#include <boost/multi_array.hpp>
#include <boost/range/adaptor/argument_fwd.hpp>
#include <boost/range/adaptor/strided.hpp>
#include <boost/range/adaptor/sliced.hpp>
#include <boost/range/iterator_range_core.hpp>
#include <array>
#include <cstddef>
#include <exception>

#include "../misc/geometry.h"

///@brief The data structure for presenting the routing edges in global routing area.
///@details User can specify the data structure of routing edges by their own, and
///         the default data structure of routing edges is a integer.
template<class T>
class EdgePlane {

    enum EdgeDir {
        EAST = 0, SOUTH = 1
    };

    struct HandleT {

        HandleT() :
                v { }, //
                e { } {
        }

        Coordinate_2d& vertex() {
            return v;
        }
        const Coordinate_2d& vertex() const {
            return v;
        }

        T& edge() {
            return *e;
        }
        const T& edge() const {
            return *e;
        }

        friend typename EdgePlane<T>::IteratorExpression;
    private:

        Coordinate_2d v;
        T* e;

    };
    class RangeExpression;

    class IteratorExpression {

    public:

        IteratorExpression(int index, RangeExpression& rangeExpression) :
                index { index - 1 }, //
                range { rangeExpression }, handle { } {
            operator ++();
        }

        bool operator !=(const IteratorExpression& it) {
            return index != it.index;
        }

        HandleT& operator *() {
            return handle;
        }

        IteratorExpression& operator ++() { //prefix increment
            const std::array<Coordinate_2d, 4>& around = Coordinate_2d::dir_array();

            do {
                ++index;
                handle.v = range.c + around[index % 4];
            } while (index < 4 && !isVertexInside(handle.v));

            if (index < 4) {

                switch (index) {
                case 0: {
                    handle.e = &range.edgePlane[range.c.x][range.c.y][EAST];
                    break;
                }
                case 1: {
                    handle.e = &range.edgePlane[range.c.x][range.c.y][SOUTH];
                    break;
                }
                case 2: {
                    handle.e = &range.edgePlane[range.c.x - 1][range.c.y][EAST];
                    break;
                }
                case 3: {
                    handle.e = &range.edgePlane[range.c.x][range.c.y - 1][SOUTH];
                    break;
                }
                }

            }
            return *this;
        }

    private:
        bool isVertexInside(const Coordinate_2d& c) const {
            return (c.x >= 0 && c.y >= 0 && c.x < (int) range.edgePlane.size() && //
                    c.y * 2 < (int) range.edgePlane[0].size());
        }
        int index;
        RangeExpression& range;

        HandleT handle;

    };

    class RangeExpression {

    public:

        RangeExpression(const Coordinate_2d& c, boost::multi_array<T, 3>& edgePlane_) :
                c { c }, //
                edgePlane { edgePlane_ }  //
        {
        }

        IteratorExpression begin() {
            return IteratorExpression { 0, *this };
        }

        IteratorExpression end() {
            return IteratorExpression { 4, *this };
        }

        Coordinate_2d c;
        boost::multi_array<T, 3>& edgePlane;
    };

public:

    typedef HandleT Handle;

    EdgePlane(const int xSize, const int ySize);

    EdgePlane(const Coordinate_2d& size);

    EdgePlane(const EdgePlane&);

    ~EdgePlane();

    void operator=(const EdgePlane&);

    ///@brief Get the map size in x-axis and y-axis
    Coordinate_2d getSize() const;

///@brief Get the map size in x-axis
    int getXSize() const;

///@brief Get the map size in y-axis
    int getYSize() const;

///@brief Get the neighbors

    RangeExpression neighbors(const Coordinate_2d& c);

    boost::iterator_range<T*> all();

    ///@brief Get the specified edge between 2 vertices
    T& edge(const Coordinate_2d& c1, const Coordinate_2d& c2);

    ///@brief Get the specified edge between 2 vertices, and the edge is read-only.
    const T& edge(const Coordinate_2d& c1, const Coordinate_2d& c2) const;

    const std::size_t num_elements() const;

    auto allVertical();

    auto allHorizontal();

private:
///The real data structure of plane
    boost::multi_array<T, 3> edgePlane_;

};

template<class T>
EdgePlane<T>::EdgePlane(int xSize, int ySize) :
        edgePlane_(boost::extents[xSize][ySize][2]) {

}

template<class T>
EdgePlane<T>::EdgePlane(const Coordinate_2d& size) :
        edgePlane_(boost::extents[size.x][size.y][2]) {

}

template<class T>
EdgePlane<T>::EdgePlane(const EdgePlane& original) :
        edgePlane_(original.edgePlane_) {

}

template<class T>
EdgePlane<T>::~EdgePlane() {

}

template<class T>
inline
int EdgePlane<T>::getXSize() const {
    return edgePlane_.size();
}

template<class T>
inline
int EdgePlane<T>::getYSize() const {
    return edgePlane_[0].size();
}

template<class T>
inline Coordinate_2d EdgePlane<T>::getSize() const {
    return Coordinate_2d(getXSize(), getYSize());
}

template<class T>
boost::iterator_range<T*> EdgePlane<T>::all() {
    return boost::iterator_range<T*>(edgePlane_.data(), &edgePlane_.data()[edgePlane_.num_elements()]);
}

template<class T>
auto EdgePlane<T>::allVertical() {
    return all() | boost::adaptors::sliced(1, edgePlane_.num_elements()) | boost::adaptors::strided(2);
}

template<class T>
auto EdgePlane<T>::allHorizontal() {
    return all() | boost::adaptors::strided(2);
}

template<class T>
typename EdgePlane<T>::RangeExpression EdgePlane<T>::neighbors(const Coordinate_2d& c) {
    return RangeExpression(c, edgePlane_);
}

template<class T>
T& EdgePlane<T>::edge(const Coordinate_2d& c1, const Coordinate_2d& c2) {
    if (c1.x < c2.x) {
        return edgePlane_[c1.x][c1.y][EAST];
    }
    if (c1.x > c2.x) {
        return edgePlane_[c2.x][c2.y][EAST];
    }
    if (c1.y < c2.y) {
        return edgePlane_[c1.x][c1.y][SOUTH];
    }
    if (c1.y > c2.y) {
        return edgePlane_[c2.x][c2.y][SOUTH];
    }
    throw std::exception();
}

template<class T>
const T& EdgePlane<T>::edge(const Coordinate_2d& c1, const Coordinate_2d& c2) const {
    return edge(c1, c2);
}
template<class T>
const std::size_t EdgePlane<T>::num_elements() const {
    return edgePlane_.num_elements();
}
#endif /* SRC_GRDB_EDGEPLANE_H_ */
