/**********************************************************************
 *
 * GEOS - Geometry Engine Open Source
 * http://geos.osgeo.org
 *
 * Copyright (C) 2013 Sandro Santilli <strk@kbt.io>
 * Copyright (C) 2005-2006 Refractions Research Inc.
 * Copyright (C) 2001-2002 Vivid Solutions Inc.
 *
 * This is free software; you can redistribute and/or modify it under
 * the terms of the GNU Lesser General Public Licence as published
 * by the Free Software Foundation.
 * See the COPYING file for more information.
 *
 **********************************************************************
 *
 * Last port: algorithm/MinimumBoundingCircle.java 2019-01-24
 *
 **********************************************************************/

#include <geos/algorithm/Angle.h>
#include <geos/algorithm/MinimumBoundingCircle.h>
#include <geos/geom/Coordinate.h>
#include <geos/geom/CoordinateSequenceFactory.h>
#include <geos/geom/Envelope.h>
#include <geos/geom/Geometry.h>
#include <geos/geom/GeometryCollection.h>
#include <geos/geom/GeometryFactory.h>
#include <geos/geom/LineString.h>
#include <geos/geom/Polygon.h>
#include <geos/geom/Triangle.h>
#include <geos/util/GEOSException.h>

#include <math.h> // sqrt
#include <memory> // for unique_ptr
#include <typeinfo>
#include <vector>

using namespace geos::geom;

namespace geos {
namespace algorithm { // geos.algorithm

/*public*/
Geometry*
MinimumBoundingCircle::getCircle()
{
    //TODO: ensure the output circle contains the extermal points.
    //TODO: or maybe even ensure that the returned geometry contains ALL the input points?

    compute();
    if(centre.isNull()) {
        return input->getFactory()->createPolygon();
    }
    Point* centrePoint = input->getFactory()->createPoint(centre);
    if(radius == 0.0) {
        return centrePoint;
    }
    return centrePoint->buffer(radius);
}

/*public*/
Geometry*
MinimumBoundingCircle::getFarthestPoints()
{
    compute();
    switch(extremalPts.size()) {
    case 0:
        return input->getFactory()->createLineString();
    case 1:
        return input->getFactory()->createPoint(centre);
    }

    size_t dims = input->getDimension();
    size_t len = 2;
    CoordinateSequence* cs = input->getFactory()->getCoordinateSequenceFactory()->create(len, dims);
    cs->add(extremalPts[0], true);
    cs->add(extremalPts[extremalPts.size() - 1], true);
    return input->getFactory()->createLineString(cs);
}

/*public*/
Geometry*
MinimumBoundingCircle::getDiameter()
{
    compute();
    switch(extremalPts.size()) {
    case 0:
        return input->getFactory()->createLineString();
    case 1:
        return input->getFactory()->createPoint(centre);
    }
    size_t dims = input->getDimension();
    size_t len = 2;
    CoordinateSequence* cs = input->getFactory()->getCoordinateSequenceFactory()->create(len, dims);
    // TODO: handle case of 3 extremal points, by computing a line from one of
    // them through the centre point with len = 2*radius
    cs->add(extremalPts[0], true);
    cs->add(extremalPts[1], true);
    return input->getFactory()->createLineString(cs);
}

/*public*/
std::vector<Coordinate>
MinimumBoundingCircle::getExtremalPoints()
{
    compute();
    return extremalPts;
}

/*public*/
Coordinate
MinimumBoundingCircle::getCentre()
{
    compute();
    return centre;
}

/*public*/
double
MinimumBoundingCircle::getRadius()
{
    compute();
    return radius;
}

/*private*/
void
MinimumBoundingCircle::computeCentre()
{
    switch(extremalPts.size()) {
    case 0: {
        centre.setNull();
        break;
    }
    case 1: {
        centre = extremalPts[0];
        break;
    }
    case 2: {
        double xAvg = (extremalPts[0].x + extremalPts[1].x) / 2.0;
        double yAvg = (extremalPts[0].y + extremalPts[1].y) / 2.0;
        Coordinate c(xAvg, yAvg);
        centre = c;
        break;
    }
    case 3: {
        centre = Triangle::circumcentre(extremalPts[0], extremalPts[1], extremalPts[2]);
        break;
    }
    default: {
        util::GEOSException("Logic failure in MinimumBoundingCircle algorithm!");
        \
    }
    }
}

/*private*/
void
MinimumBoundingCircle::compute()
{
    if(!extremalPts.empty()) {
        return;
    }

    computeCirclePoints();
    computeCentre();
    if(!centre.isNull()) {
        radius = centre.distance(extremalPts[0]);
    }
}

/*private*/
void
MinimumBoundingCircle::computeCirclePoints()
{
    // handle degenerate or trivial cases
    if(input->isEmpty()) {
        return;
    }
    if(input->getNumPoints() == 1) {
        extremalPts.push_back(*(input->getCoordinate()));
        return;
    }

    /**
    * The problem is simplified by reducing to the convex hull.
    * Computing the convex hull also has the useful effect of eliminating duplicate points
    */
    Geometry* convexHull = input->convexHull();

    CoordinateSequence* cs = convexHull->getCoordinates();
    std::vector<Coordinate> pts;
    cs->toVector(pts);

    // strip duplicate final point, if any
    if(pts.front().equals2D(pts.back())) {
        pts.pop_back();
    }

    /**
    * Optimization for the trivial case where the CH has fewer than 3 points
    */
    if(pts.size() <= 2) {
        extremalPts = pts;
        return;
    }

    // find a point P with minimum Y ordinate
    Coordinate P = lowestPoint(pts);

    // find a point Q such that the angle that PQ makes with the x-axis is minimal
    Coordinate Q = pointWitMinAngleWithX(pts, P);

    /**
    * Iterate over the remaining points to find
    * a pair or triplet of points which determine the minimal circle.
    * By the design of the algorithm,
    * at most <tt>pts.length</tt> iterations are required to terminate
    * with a correct result.
    */
    size_t i = 0, n = pts.size();
    while(i++ < n) {
        Coordinate R = pointWithMinAngleWithSegment(pts, P, Q);

        // if PRQ is obtuse, then MBC is determined by P and Q
        if(algorithm::Angle::isObtuse(P, R, Q)) {
            extremalPts.push_back(P);
            extremalPts.push_back(Q);
            return;
        }
        // if RPQ is obtuse, update baseline and iterate
        if(algorithm::Angle::isObtuse(R, P, Q)) {
            P = R;
            continue;
        }
        // if RQP is obtuse, update baseline and iterate
        if(algorithm::Angle::isObtuse(R, Q, P)) {
            Q = R;
            continue;
        }
        // otherwise all angles are acute, and the MBC is determined by the triangle PQR
        extremalPts.push_back(P);
        extremalPts.push_back(Q);
        extremalPts.push_back(R);
        return;
    }
    // never get here
    util::GEOSException("Logic failure in MinimumBoundingCircle algorithm!");
}

/*private*/
Coordinate
MinimumBoundingCircle::lowestPoint(std::vector<Coordinate>& pts)
{
    Coordinate min = pts[0];
    for(auto pt : pts) {
        if(pt.y < min.y) {
            min = pt;
        }
    }
    return min;
}


/*private*/
Coordinate
MinimumBoundingCircle::pointWitMinAngleWithX(std::vector<Coordinate>& pts, Coordinate& P)
{
    double minSin = std::numeric_limits<double>::max();
    Coordinate minAngPt;
    minAngPt.setNull();
    for(auto p : pts) {

        if(p == P) {
            continue;
        }

        /**
        * The sin of the angle is a simpler proxy for the angle itself
        */
        double dx = p.x - P.x;
        double dy = p.y - P.y;
        if(dy < 0) {
            dy = -dy;
        }
        double len = sqrt(dx * dx + dy * dy);
        double sin = dy / len;

        if(sin < minSin) {
            minSin = sin;
            minAngPt = p;
        }
    }
    return minAngPt;
}


/*private*/
Coordinate
MinimumBoundingCircle::pointWithMinAngleWithSegment(std::vector<Coordinate>& pts, Coordinate& P, Coordinate& Q)
{
    double minAng = std::numeric_limits<double>::max();
    Coordinate minAngPt;
    minAngPt.setNull();
    for(auto p : pts) {
        if(p == P) {
            continue;
        }
        if(p == Q) {
            continue;
        }

        double ang = Angle::angleBetween(P, p, Q);
        if(ang < minAng) {
            minAng = ang;
            minAngPt = p;
        }
    }
    return minAngPt;
}


} // namespace geos.algorithm
} // namespace geos
