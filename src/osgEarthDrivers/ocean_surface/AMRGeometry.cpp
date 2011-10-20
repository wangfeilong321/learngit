/* -*-c++-*- */
/* osgEarth - Dynamic map generation toolkit for OpenSceneGraph
 * Copyright 2008-2010 Pelican Mapping
 * http://osgearth.org
 *
 * osgEarth is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>
 */
#include "Common"
#include "AMRGeometry"
#include <osg/State>
#include <osg/Uniform>
#include <osgEarth/Notify>

#define LC "[AMRGeometry] "

// --------------------------------------------------------------------------

#include "AMRShaders.h"

// --------------------------------------------------------------------------

AMRTriangle::AMRTriangle()
{
    _stateSet = new osg::StateSet();

    _stateSet->getOrCreateUniform( "tex0", osg::Uniform::SAMPLER_2D )->set( 0 );
}

#define SET_UNIFORM(X,Y,Z) \
    _stateSet->getOrCreateUniform( X , Y )->set( Z )

//#define LOCALIZE_VERTS 1


AMRTriangle::AMRTriangle(const MeshNode& n0, const osg::Vec2& t0,
                         const MeshNode& n1, const osg::Vec2& t1, 
                         const MeshNode& n2, const osg::Vec2& t2) :
_node0(n0), _node1(n1), _node2(n2)
{

    _stateSet = new osg::StateSet();
    // should this be INT_SAMPLER_2D?
    SET_UNIFORM( "tex0", osg::Uniform::INT, 0 );

    SET_UNIFORM( "c0", osg::Uniform::FLOAT_VEC3, _node0._geodeticCoord );
    SET_UNIFORM( "c1", osg::Uniform::FLOAT_VEC3, _node1._geodeticCoord );
    SET_UNIFORM( "c2", osg::Uniform::FLOAT_VEC3, _node2._geodeticCoord );

#ifdef LOCALIZE_VERTS
    _local2world = osg::Matrix::translate( 
        - ( _node0._vertex + _node1._vertex + _node2._vertex ) / 3.0 );
    _world2local = osg::Matrix::inverse(_local2world);

    SET_UNIFORM( "v0", osg::Uniform::FLOAT_VEC3, _node0._vertex * _world2local );
    SET_UNIFORM( "v1", osg::Uniform::FLOAT_VEC3, _node1._vertex * _world2local );
    SET_UNIFORM( "v2", osg::Uniform::FLOAT_VEC3, _node2._vertex * _world2local );
#else
    SET_UNIFORM( "v0", osg::Uniform::FLOAT_VEC3, _node0._vertex );
    SET_UNIFORM( "v1", osg::Uniform::FLOAT_VEC3, _node1._vertex );
    SET_UNIFORM( "v2", osg::Uniform::FLOAT_VEC3, _node2._vertex );
#endif // LOCALIZE_VERTS

    SET_UNIFORM( "t0", osg::Uniform::FLOAT_VEC2, t0 );
    SET_UNIFORM( "t1", osg::Uniform::FLOAT_VEC2, t1 );
    SET_UNIFORM( "t2", osg::Uniform::FLOAT_VEC2, t2 );

    SET_UNIFORM( "n0", osg::Uniform::FLOAT_VEC3, _node0._normal );
    SET_UNIFORM( "n1", osg::Uniform::FLOAT_VEC3, _node1._normal );
    SET_UNIFORM( "n2", osg::Uniform::FLOAT_VEC3, _node2._normal );

    //SET_UNIFORM( "n0", osg::Uniform::FLOAT_VEC3, _node0._normal * _world2local );
    //SET_UNIFORM( "n1", osg::Uniform::FLOAT_VEC3, _node1._normal * _world2local );
    //SET_UNIFORM( "n2", osg::Uniform::FLOAT_VEC3, _node2._normal * _world2local );

    //SET_UNIFORM( "r0", osg::Uniform::FLOAT_VEC4, _node0._geodeticRot.asVec4() );
    //SET_UNIFORM( "r1", osg::Uniform::FLOAT_VEC4, _node1._geodeticRot.asVec4() );
    //SET_UNIFORM( "r2", osg::Uniform::FLOAT_VEC4, _node2._geodeticRot.asVec4() );
}

void
AMRTriangle::expand( osg::BoundingBox& box )
{
    box.expandBy( _node0._vertex );
    box.expandBy( _node1._vertex );
    box.expandBy( _node2._vertex );
}

// --------------------------------------------------------------------------

AMRDrawable::AMRDrawable()
{
    _stateSet = new osg::StateSet();
}

// --------------------------------------------------------------------------

AMRGeometry::AMRGeometry()
{
    initShaders();
    initPatterns();

    _seaLevelUniform = new osg::Uniform( osg::Uniform::FLOAT, "seaLevel" );
    _seaLevelUniform->set( 0.0f );
    this->getOrCreateStateSet()->addUniform( _seaLevelUniform.get() );

    this->_supportsVertexBufferObjects = true;
}

AMRGeometry::AMRGeometry( const AMRGeometry& rhs, const osg::CopyOp& op ) :
osg::Drawable( rhs, op )
{
    //todo
    setInitialBound( osg::BoundingBox(-1e10, -1e10, -1e10, 1e10, 1e10, 1e10) );
}

osg::BoundingBox
AMRGeometry::computeBound() const
{
    osg::BoundingBox box;
    for( AMRDrawableList::const_iterator i = _drawList.begin(); i != _drawList.end(); ++i )
    {
        const AMRTriangleList& prims = i->get()->_triangles;
        for( AMRTriangleList::const_iterator j = prims.begin(); j != prims.end(); ++j )
        {
            j->get()->expand( box );
        }
    } 
    return box;
}

void
AMRGeometry::clearDrawList()
{
    if ( _drawList.size() > 0 )
    {
        _drawList.clear();
        dirtyBound();
    }
}

void
AMRGeometry::setDrawList( const AMRDrawableList& drawList )
{
    _drawList = drawList;
    dirtyBound();
}

void
AMRGeometry::initShaders()
{
    // initialize the shader program.
    _program = new osg::Program();
    _program->setName( "AMRGeometry" );

    osg::Shader* vertexShader = new osg::Shader( osg::Shader::VERTEX,
        std::string( source_vertShaderMain_geocentricMethod ) );

    vertexShader->setName( "AMR Vert Shader" );
    _program->addShader( vertexShader );

    osg::Shader* fragmentShader = new osg::Shader( osg::Shader::FRAGMENT,
        std::string( source_fragShaderMain ) );

    fragmentShader->setName( "AMR Frag Shader" );
    _program->addShader( fragmentShader );

    // the shader program:
    this->getOrCreateStateSet()->setAttribute( _program.get(), osg::StateAttribute::ON );
}

static void
toBarycentric(const osg::Vec3& p1, const osg::Vec3& p2, const osg::Vec3& p3, 
              const osg::Vec3& in,
              osg::Vec3& outVert, osg::Vec2& outTex )
{
    //from: http://forums.cgsociety.org/archive/index.php/t-275372.html
    osg::Vec3 
        v1 = in - p1,
        v2 = in - p2,
        v3 = in - p3;

    double 
        area1 = 0.5 * (v2 ^ v3).length(),
        area2 = 0.5 * (v1 ^ v3).length(),
        area3 = 0.5 * (v1 ^ v2).length();

    double fullArea = area1 + area2 + area3;

    double u = area1/fullArea;
    double v = area2/fullArea;
    double w = area3/fullArea; 

    outVert.set( u, v, w );

    // tex coords
    osg::Vec2 t1( p1.x(), p1.y() );
    osg::Vec2 t2( p2.x(), p2.y() );
    osg::Vec2 t3( p3.x(), p3.y() );
    outTex = t1*w + t2*v + t3*u;
}


void
AMRGeometry::initPatterns()
{
    this->setUseVertexBufferObjects( true );
    this->setUseDisplayList( false );

    _verts = new osg::Vec3Array();
    _verts->setVertexBufferObject( new osg::VertexBufferObject() );

    _texCoords = new osg::Vec2Array();
    _texCoords->setVertexBufferObject( _verts->getVertexBufferObject() );
 
    // build a right-triangle pattern. (0,0) is the lower-left (90d),
    // (0,1) is the lower right (45d) and (1,0) is the upper-left (45d)
    osg::Vec3f p1(0,0,0), p2(0,1,0), p3(1,0,0);

    for( int r=AMR_PATCH_ROWS-1; r >=0; --r )
    {
        int cols = AMR_PATCH_ROWS-r;
        for( int c=0; c<cols; ++c )
        {
            osg::Vec3 point( (float)c/(float)(AMR_PATCH_ROWS-1), (float)r/(float)(AMR_PATCH_ROWS-1), 0 );
            osg::Vec3 baryVert;
            osg::Vec2 baryTex;
            toBarycentric( p1, p2, p3, point, baryVert, baryTex );
            _verts->push_back( baryVert );
            _texCoords->push_back( baryTex );
        }
    }

    _pattern = new osg::DrawElementsUShort( GL_TRIANGLES );
    _pattern->setElementBufferObject( new osg::ElementBufferObject() );

    unsigned short rowptr = 0;
    for( unsigned short r=1; r<AMR_PATCH_ROWS; ++r )
    {
        unsigned short prev_rowptr = rowptr;
        rowptr += r;
        for( unsigned short c=0; c<r; ++c )
        {
            _pattern->push_back( rowptr + c );
            _pattern->push_back( prev_rowptr + c );
            _pattern->push_back( rowptr + c + 1 );

            if ( c+1 < r )
            {
                _pattern->push_back( prev_rowptr + c + 1 );
                _pattern->push_back( rowptr + c + 1 );
                _pattern->push_back( prev_rowptr + c );
            }
        }
    }  
}

void
AMRGeometry::compileGLObjects( osg::RenderInfo& renderInfo ) const
{
    osg::State& state = *renderInfo.getState();
    unsigned int contextID = state.getContextID();
    osg::GLBufferObject::Extensions* extensions = osg::GLBufferObject::getExtensions(contextID, true);
    if (!extensions) 
        return;

    typedef std::set<osg::BufferObject*> BufferObjects;
    BufferObjects bufferObjects;

    bufferObjects.insert( _verts->getBufferObject() );
    bufferObjects.insert( _pattern->getBufferObject() );

    for(BufferObjects::iterator itr = bufferObjects.begin(); itr != bufferObjects.end(); ++itr)
    {
        osg::GLBufferObject* glBufferObject = (*itr)->getOrCreateGLBufferObject(contextID);
        if (glBufferObject && glBufferObject->isDirty())
        {
            // OSG_NOTICE<<"Compile buffer "<<glBufferObject<<std::endl;
            glBufferObject->compileBuffer();
        }
    }

    // unbind the BufferObjects
    extensions->glBindBuffer(GL_ARRAY_BUFFER_ARB,0);
    extensions->glBindBuffer(GL_ELEMENT_ARRAY_BUFFER_ARB,0);
}

static int s_numTemplates = 0;

void
AMRGeometry::drawImplementation( osg::RenderInfo& renderInfo ) const
{   
    osg::State& state = *renderInfo.getState();
    
    // bind the VBO:
    state.setVertexPointer( _verts.get() );

    // bind the texture coordinate arrrays:
    state.setTexCoordPointer( 0, _texCoords.get() );

    // this will enable the amr geometry's stateset (and activate the Program)
    state.pushStateSet( this->getStateSet() );

#ifdef LOCALIZE_VERTS
    osg::Matrixd modelView = state.getModelViewMatrix();
#endif

    int numTemplates = 0;

    for( AMRDrawableList::const_iterator i = _drawList.begin(); i != _drawList.end(); ++i )
    {
        const AMRDrawable* drawable = i->get();

        // apply the drawable's state changes:
        state.pushStateSet( drawable->_stateSet.get() );

        for( AMRTriangleList::const_iterator j = drawable->_triangles.begin(); j != drawable->_triangles.end(); ++j )
        {
            const AMRTriangle* dtemplate = j->get();

            // apply the primitive's state changes:
            state.apply( dtemplate->_stateSet.get() );

#ifdef LOCALIZE_VERTS
            osg::ref_ptr<osg::RefMatrix> rm = new osg::RefMatrix( dtemplate->_local2world * modelView );
            state.applyModelViewMatrix( rm.get() );
            state.applyModelViewAndProjectionUniformsIfRequired();
#endif // LOCALIZE_VERTS

            _pattern->draw( state, true );

            numTemplates++;
        }

        state.popStateSet();
    }

#if 0
    if ( s_numTemplates != numTemplates )
    {
        s_numTemplates = numTemplates;
        OE_DEBUG << LC << std::dec 
            << "templates="  << numTemplates
            << ", verts="    << numTemplates*_numPatternVerts
            << ", strips="   << numTemplates*_numPatternStrips
            << ", tris="     << numTemplates*_numPatternTriangles
            << ", elements=" << numTemplates*_numPatternElements
            << std::endl;
    }
#endif

    // unbind the buffer objects.
    state.unbindVertexBufferObject();
    state.unbindElementBufferObject();

    // undo the program.
    state.popStateSet();
}
