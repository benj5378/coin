/**************************************************************************\
 * 
 *  Copyright (C) 1998-1999 by Systems in Motion.  All rights reserved.
 *
 *  This file is part of the Coin library.
 *
 *  This file may be distributed under the terms of the Q Public License
 *  as defined by Troll Tech AS of Norway and appearing in the file
 *  LICENSE.QPL included in the packaging of this file.
 *
 *  If you want to use Coin in applications not covered by licenses
 *  compatible with the QPL, you can contact SIM to aquire a
 *  Professional Edition license for Coin.
 *
 *  Systems in Motion AS, Prof. Brochs gate 6, N-7030 Trondheim, NORWAY
 *  http://www.sim.no/ sales@sim.no Voice: +47 22114160 Fax: +47 67172912
 *
\**************************************************************************/

/*!
  \class SoProfileCoordinateElement Innventor/elements/SoProfileCoordinateElement.h
  \brief The SoProfileCoordinateElement class is yet to be documented.

  FIXME: write doc.
*/

#include <Inventor/elements/SoProfileCoordinateElement.h>

#include <Inventor/SbName.h>
#include <Inventor/nodes/SoNode.h>

#include <assert.h>

SbVec2f SoProfileCoordinateElement::initdefaultcoords(0.0f, 0.0f);

/*!
  \var SoProfileCoordinateElement::initdefaultcoords

  FIXME: write doc.
*/

/*!
  \var SoProfileCoordinateElement::numCoords

  FIXME: write doc.
*/

/*!
  \var SoProfileCoordinateElement::coords2

  FIXME: write doc.
*/

/*!
  \var SoProfileCoordinateElement::coords3

  FIXME: write doc.
*/

/*!
  \var SoProfileCoordinateElement::coordsAre2D

  FIXME: write doc.
*/

//$ BEGIN TEMPLATE ElementSource( SoProfileCoordinateElement )

/*!
  \var SoProfileCoordinateElement::classTypeId

  This is the static class type identifier for the
  SoProfileCoordinateElement class.
*/

SoType SoProfileCoordinateElement::classTypeId = SoType::badType();

/*!
  This method returns the SoType object for the element class of
  the instance.
*/
SoType
SoProfileCoordinateElement::getClassTypeId(void)
{
  return SoProfileCoordinateElement::classTypeId;
}

/*!
  This static method creates an object instance of the SoProfileCoordinateElement class.
*/
void *
SoProfileCoordinateElement::createInstance(void)
{
  return (void *) new SoProfileCoordinateElement;
}

/*!
  \var SoProfileCoordinateElement::classStackIndex

  This is the static state stack index for the
  SoProfileCoordinateElement class.
*/
int SoProfileCoordinateElement::classStackIndex;

/*!
  This static method returns the state stack index for the SoProfileCoordinateElement class.
*/
int
SoProfileCoordinateElement::getClassStackIndex(void)
{
  return SoProfileCoordinateElement::classStackIndex;
}
//$ END TEMPLATE ElementSource

/*!
  This static method initializes static data for the
  SoProfileCoordinateElement class.
*/

void
SoProfileCoordinateElement::initClass( // static
    void )
{
//$ BEGIN TEMPLATE InitElementSource( SoProfileCoordinateElement )
  assert(SoProfileCoordinateElement::classTypeId == SoType::badType());
  assert(inherited::getClassTypeId() != SoType::badType());

  SoProfileCoordinateElement::classTypeId = SoType::createType(
    inherited::getClassTypeId(),
    "SoProfileCoordinateElement",
    &SoProfileCoordinateElement::createInstance);

  if (inherited::classStackIndex < 0) {
    SoProfileCoordinateElement::classStackIndex =
      createStackIndex( SoProfileCoordinateElement::classTypeId );
  } else {
    SoProfileCoordinateElement::classStackIndex =
      inherited::classStackIndex;
  }
//$ END TEMPLATE InitElementSource
}

/*!
  This static method cleans up static data for the SoProfileCoordinateElement
  class.
*/

void
SoProfileCoordinateElement::cleanClass( // static
    void )
{
//$ BEGIN TEMPLATE CleanElementSource( SoProfileCoordinateElement )
//$ END TEMPLATE CleanElementSource
}

/*!
  A constructor.  Can't be used directly.

  \sa void * SoProfileCoordinateElement::createInstance( void )
*/

SoProfileCoordinateElement::SoProfileCoordinateElement(
    void )
  : numCoords( 1 ),
    coords2( & SoProfileCoordinateElement::initdefaultcoords ),
    coords3( NULL ),
    coordsAre2D( TRUE )
{
    setTypeId( SoProfileCoordinateElement::classTypeId );
    setStackIndex( SoProfileCoordinateElement::classStackIndex );
}

/*!
  The destructor.
*/

SoProfileCoordinateElement::~SoProfileCoordinateElement(
    void )
{
}

//! FIXME: write doc.

void
SoProfileCoordinateElement::init( // virtual
    SoState * state )
{
    inherited::init( state );

    this->numCoords = 1;
    this->coords2 = & SoProfileCoordinateElement::initdefaultcoords;
    this->coords3 = NULL;
    this->coordsAre2D = TRUE;
}

//! FIXME: write doc.

void
SoProfileCoordinateElement::set2( // static
    SoState * const state,
    SoNode * const node,
    const int32_t numCoords,
    const SbVec2f * const coords )
{
  assert( numCoords >= 0 );
  SoProfileCoordinateElement * element =
    (SoProfileCoordinateElement *)
    (getElement( state, classStackIndex, NULL ));
  element->numCoords = numCoords;
  element->coords2 = coords;
  element->coords3 = NULL;
  element->coordsAre2D = TRUE;

  element->nodeId = node->getNodeId();
}

//! FIXME: write doc.

void
SoProfileCoordinateElement::set3( // static
    SoState * const state,
    SoNode * const node,
    const int32_t numCoords,
    const SbVec3f * const coords )
{
  assert( numCoords >= 0 );
  SoProfileCoordinateElement * element =
    (SoProfileCoordinateElement *)
    (getElement( state, classStackIndex, NULL ));
  element->numCoords = numCoords;
  element->coords2 = NULL;
  element->coords3 = coords;
  element->coordsAre2D = FALSE;

  element->nodeId = node->getNodeId();
}

//! FIXME: write doc.

void
SoProfileCoordinateElement::print( // virtual
    FILE * file ) const
{
    fprintf( file, "SoProfileCoordinateElement[%p]: %d coords at %p.\n", this,
        this->numCoords,
        this->coordsAre2D ? (void *) this->coords2 : (void *) this->coords3 );
}

//! FIXME: write doc.

void
SoProfileCoordinateElement::push( // virtual
    SoState * state )
{
    inherited::push( state );
}

//! FIXME: write doc.

void
SoProfileCoordinateElement::pop( // virtual
    SoState * state,
    const SoElement * prevTopElement )
{
    inherited::pop( state, prevTopElement );
}

/*!
  FIXME: write doc.
*/

const SoProfileCoordinateElement *
SoProfileCoordinateElement::getInstance( SoState * const state )
{
  return (const SoProfileCoordinateElement *)
        (getConstElement( state, classStackIndex ));
}

/*!
  FIXME: write doc.
*/

int32_t
SoProfileCoordinateElement::getNum( void ) const
{
  return this->numCoords;
}

/*!
  FIXME: write doc.
*/

const SbVec2f &
SoProfileCoordinateElement::get2( const int index ) const
{
  assert( index >= 0 && index < this->numCoords );
  assert( this->coordsAre2D );
  return this->coords2[ index ];
}

/*!
  FIXME: write doc.
*/

const SbVec3f &
SoProfileCoordinateElement::get3( const int index ) const
{
  assert( index >= 0 && index < this->numCoords );
  assert( ! this->coordsAre2D );
  return this->coords3[ index ];
}

/*!
  FIXME: write doc.
*/

SbBool
SoProfileCoordinateElement::is2D( void ) const
{
  return this->coordsAre2D;
}

/*!
  FIXME: write doc.
*/

SbVec2f
SoProfileCoordinateElement::getDefault2( void )
{
  return SbVec2f( 0.0f, 0.0f );
}

/*!
  FIXME: write doc.
*/

SbVec3f
SoProfileCoordinateElement::getDefault3( void )
{
  return SbVec3f( 0.0f, 0.0f, 1.0f );
}

