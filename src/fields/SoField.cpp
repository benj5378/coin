/**************************************************************************\
 *
 *  This file is part of the Coin 3D visualization library.
 *  Copyright (C) 1998-2001 by Systems in Motion.  All rights reserved.
 *  
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  version 2 as published by the Free Software Foundation.  See the
 *  file LICENSE.GPL at the root directory of this source distribution
 *  for more details.
 *
 *  If you desire to use Coin with software that is incompatible
 *  licensewise with the GPL, and / or you would like to take
 *  advantage of the additional benefits with regard to our support
 *  services, please contact Systems in Motion about acquiring a Coin
 *  Professional Edition License.  See <URL:http://www.coin3d.org> for
 *  more information.
 *
 *  Systems in Motion, Prof Brochs gate 6, 7030 Trondheim, NORWAY
 *  <URL:http://www.sim.no>, <mailto:support@sim.no>
 *
\**************************************************************************/

/*!
  \class SoField SoField.h Inventor/fields/SoField.h
  \brief The SoField class is the top-level abstract base class for fields.
  \ingroup fields

  Fields is the mechanism used throughout Coin for encapsulating basic
  data types to detect changes made to them, and to provide
  conversion, import and export facilities.

  Almost all public properties in nodes are stored in fields, and so
  are the inputs and outputs of engines. So fields can be viewed as
  the major mechanism for scenegraph nodes and engines to expose their
  public API.

  Forcing data modification to go through a public function interface
  while hiding the data members makes it possible to automatically
  detect and react upon changes in the data structures set up by the
  application programmer.

  E.g. the default behavior when changing the value of a field in a
  scenegraph node is that there'll automatically be a chain of
  notifications -- from the field to the owner node, from that node to
  it's parent node, etc all the way through to the top-most root node,
  where the need for a rendering update will be signalled to the
  application.

  (This notification mechanism is the underlying feature that makes the
  Coin library classify as a so-called \e data-driven scenegraph API.

  The practical consequences of this is that rendering and many other
  processing actions is default scheduled to \e only happen when
  something has changed in the retained data structures, making the
  Coin library under normal circumstances \e much less CPU intensive
  than so-called "application-driven" scenegraph API, like for
  instance SGI IRIS Performer, which are continuously re-rendering
  even when nothing has changed in the data structures or with the
  camera viewport.)

  Note: there are some field classes which has been obsoleted from the
  Open Inventor API. They are: SoSFLong, SoSFULong, SoMFLong and
  SoMFULong. You should use these classes instead (respectively):
  SoSFInt32, SoSFUInt32, SoMFInt32 and SoMFUInt32.


  \sa SoFieldContainer, SoFieldData
*/


#include <assert.h>
#include <string.h>
#include <coindefs.h> // COIN_STUB()
#include <../tidbits.h> // coin_atexit()

#include <Inventor/SoDB.h>
#include <Inventor/SoInput.h>
#include <Inventor/SoOutput.h>
#include <Inventor/actions/SoWriteAction.h>
#include <Inventor/engines/SoConvertAll.h>
#include <Inventor/engines/SoNodeEngine.h>
#include <Inventor/errors/SoDebugError.h>
#include <Inventor/errors/SoReadError.h>
#include <Inventor/fields/SoFields.h>
#include <Inventor/lists/SoEngineList.h>
#include <Inventor/lists/SoEngineOutputList.h>
#include <Inventor/sensors/SoDataSensor.h>

// flags for this->statusbits
#define FLAG_TYPEMASK       0x0007  // need 3 bits for values [0-5]
#define FLAG_ISDEFAULT      0x0008
#define FLAG_IGNORE         0x0010
#define FLAG_EXTSTORAGE     0x0020
#define FLAG_ENABLECONNECTS 0x0040
#define FLAG_NEEDEVALUATION 0x0080
#define FLAG_READONLY       0x0100
#define FLAG_DONOTIFY       0x0200
#define FLAG_ISDESTRUCTING  0x0400
#define FLAG_ISEVALUATING   0x0800
#define FLAG_ISNOTIFIED     0x1000

static const char IGNOREDCHAR = '~';
static const char CONNECTIONCHAR = '=';
/*
  This class is used to aid in "multiplexing" the pointer member of
  SoField. This is a way to achieve the goal of using minimum storage
  space for SoField classes in the default case (which is important,
  as fields are ubiquitous in Coin). The default case means no
  connections and only a field container given. If any connections are
  made (either "to" or "from"), we allocate an SoConnectStorage and
  move the field container pointer into it, while swapping in the
  SoConnectStorage pointer where the field container pointer used to
  be.
*/
class SoConnectStorage {
public:
  SoConnectStorage(SoFieldContainer * c, SoType t)
    : container(c), fieldtype(t),
      maptoconverter(13) // save about ~1kB vs default SbDict nr of buckets
    { }

#if COIN_DEBUG
  // Check that everything has been emptied.
  ~SoConnectStorage()
  {
    SbPList keys, values;
    maptoconverter.makePList(keys, values);
    assert(keys.getLength() == 0);
    assert(values.getLength() == 0);

    assert(masterfields.getLength() == 0);
    assert(masterengineouts.getLength() == 0);

    assert(slaves.getLength() == 0);
    assert(auditors.getLength() == 0);
  }
#endif // COIN_DEBUG

  // The container this field is part of.
  SoFieldContainer * container;

  // List of masters we're connected to as a slave. Use maptoconverter
  // dict to find SoFieldConverter engine in the connection (if any).
  SoFieldList masterfields;
  SoEngineOutputList masterengineouts;
  // Fields which are slaves to us. Use maptoconverter dict to find
  // SoFieldConverter engine in the connection (if any).
  SoFieldList slaves;
  // Direct auditors of us.
  SoAuditorList auditors;


  // Convenience functions for adding, removing and finding SbDict
  // mappings.

  void addConverter(const void * item, SoFieldConverter * converter)
  {
    // "item" can be SoField* or SoEngineOutput*.

    // FIXME: this probably hashes horribly bad, as the item value is
    // a pointer and is therefore address-aligned (lower 32 (?) bits
    // are all 0).  20010911 mortene.
    this->maptoconverter.enter((unsigned long)item, converter);
  }

  void removeConverter(const void * item)
  {
    SbBool ok = this->maptoconverter.remove((unsigned long)item);
    assert(ok);
  }

  SoFieldConverter * findConverter(const void * item)
  {
    void * val;
    if (!this->maptoconverter.find((unsigned long)item, val))
      return NULL;
    return (SoFieldConverter *)val;
  }

  // Provides us with a hack to get at a master field's type in code
  // called from its constructor (SoField::getTypeId() is virtual and
  // can't be used).
  //
  // (Used in the master::~SoField() -> slave::disconnect(master)
  // chain.)
  SoType fieldtype;


private:
  // Dictionary of void* -> SoFieldConverter* mappings.
  SbDict maptoconverter;
};


// *************************************************************************

// Documentation for abstract methods.

// FIXME: grab better version of getTypeId() doc from SoBase, SoAction
// and / or SoDetail. 20010913 mortene.
/*!
  \fn SoType SoField::getTypeId(void) const

  Returns the type identification instance which uniquely identifies
  the Coin field class the object belongs to.

  \sa getClassTypeId(), SoType
*/

/*!
  \fn SbBool SoField::isSame(const SoField & f) const
  Check for equal type and value(s).
*/

/*!
  \fn void SoField::copyFrom(const SoField & f)
  Copy value(s) from \a f into this field.
*/

/*!
  \fn SbBool SoField::readValue(SoInput * in)
  Read field value(s).
*/

/*!
  \fn void SoField::writeValue(SoOutput * out) const
  Write field value(s).
*/


// *************************************************************************

// Don't set value explicitly to SoType::badType(), to avoid a bug in
// Sun CC v4.0. (Bitpattern 0x0000 equals SoType::badType()).
SoType SoField::classTypeId;

// *************************************************************************


// private methods. Inlined inside this file only.

// clear bits in statusbits
inline void
SoField::clearStatusBits(const unsigned int bits)
{
  this->statusbits &= ~bits;
}

// sets bits in statusbits
inline void
SoField::setStatusBits(const unsigned int bits)
{
  this->statusbits |= bits;
}

// return TRUE if any of bits is set
inline SbBool
SoField::getStatus(const unsigned int bits) const
{
  return (this->statusbits & bits) != 0;
}

// convenience method for clearing or setting based on boolean value
// returns TRUE if any bitflag changed value
inline SbBool
SoField::changeStatusBits(const unsigned int bits, const SbBool onoff)
{
  unsigned int oldval = this->statusbits;
  unsigned int newval = oldval;
  if (onoff) newval |= bits;
  else newval &= ~bits;
  if (oldval != newval) {
    this->statusbits = newval;
    return TRUE;
  }
  return FALSE;
}

// returns TRUE if this field has ext storage
inline SbBool
SoField::hasExtendedStorage(void) const
{
  return this->getStatus(FLAG_EXTSTORAGE);
}


/*!
  This is the base constructor for field classes. It takes care of
  doing the common parts of data initialization in fields.
*/
SoField::SoField(void)
  : container(NULL)
{
  this->statusbits = 0;
  this->setStatusBits(FLAG_DONOTIFY |
                      FLAG_ISDEFAULT |
                      FLAG_ENABLECONNECTS);
}

/*!
  Destructor. Disconnects ourself from any connected field or engine
  before we disconnect all auditors on the field.
*/
SoField::~SoField()
{
  // set status bit to avoid evaluating this field while
  // disconnecting connections.
  this->setStatusBits(FLAG_ISDESTRUCTING);

#if COIN_DEBUG && 0 // debug
  SoDebugError::postInfo("SoField::~SoField", "destructing %p", this);
#endif // debug

  // Disconnect ourself from all connections where this field is the
  // slave.
  this->disconnect();

  if (this->hasExtendedStorage()) {

    // Disconnect slave fields using us as a master.
    while (this->storage->slaves.getLength()) {
      this->storage->slaves[0]->disconnect(this);
    }

    // Disconnect other auditors.
    while (this->storage->auditors.getLength()) {
      SoNotRec::Type type = this->storage->auditors.getType(0);
      void * obj = this->storage->auditors.getObject(0);

      switch (type) {
      case SoNotRec::ENGINE:
        ((SoEngineOutput *)obj)->removeConnection(this);
        break;

      case SoNotRec::CONTAINER:
        assert(FALSE && "Container should not be in auditorlist");
        break;

      case SoNotRec::SENSOR:
        ((SoDataSensor *)obj)->dyingReference();
        break;

      case SoNotRec::FIELD:
        assert(FALSE); // should not happen, as slave fields are removed first.
        break;

      default:
        assert(FALSE); // no other allowed types.
        break;
      }
    }

    delete this->storage;
  }

#if COIN_DEBUG && 0 // debug
  SoDebugError::postInfo("SoField::~SoField", "%p done", this);
#endif // debug
}

/*!
  Internal method called upon initialization of the library (from
  SoDB::init()) to set up the type system.
*/
void
SoField::initClass(void)
{
  // Make sure we only initialize once.
  assert(SoField::classTypeId == SoType::badType());

  SoField::classTypeId = SoType::createType(SoType::badType(), "Field");
  SoField::initClasses();
}

/*!
  Sets the flag which indicates whether or not the field should be
  ignored during certain operations.

  The effect of this flag depends on what type of field it is used on,
  and the type of the node which includes the field.

  \sa isIgnored()
*/
void
SoField::setIgnored(SbBool ignore)
{
  if (this->changeStatusBits(FLAG_IGNORE, ignore)) {
    this->valueChanged(FALSE);
  }
}

/*!
  Returns the ignore flag.

  \sa setIgnored()
*/
SbBool
SoField::isIgnored(void) const
{
  return this->getStatus(FLAG_IGNORE);
}

/*!
  Set whether or not this field should be marked as containing a
  default value.

  \sa isDefault()
*/
void
SoField::setDefault(SbBool def)
{
  (void) this->changeStatusBits(FLAG_ISDEFAULT, def);
}

/*!
  Check if the field contains its default value. Fields which has
  their default value intact will normally not be included in the
  output when writing scene graphs out to a file, for instance.

  \sa setDefault()
*/
SbBool
SoField::isDefault(void) const
{
  return this->getStatus(FLAG_ISDEFAULT);
}

/*!
  Returns a unique type identifier for this field class.

  \sa getTypeId(), SoType
*/
SoType
SoField::getClassTypeId(void)
{
  return SoField::classTypeId;
}

/*!
  Check if this instance is of a derived type or is the same type as
  the one given with the \a type parameter.
*/
SbBool
SoField::isOfType(const SoType type) const
{
  return this->getTypeId().isDerivedFrom(type);
}

/*!
  This sets a \a flag value which indicates whether or not the set up
  connection should be considered active. For as long as the "enable
  connection" flag is \c FALSE, no value propagation will be done from
  any connected source field, engine or interpolator into this field.

  If the connection is first disabled and then enabled again, the
  field will automatically be synchronized with any master field,
  engine or interpolator.

  \sa isConnectionEnabled()
*/
void
SoField::enableConnection(SbBool flag)
{
  SbBool oldval = this->getStatus(FLAG_ENABLECONNECTS);
  (void) this->changeStatusBits(FLAG_ENABLECONNECTS, flag);
  if (!oldval && flag) this->setDirty(TRUE);
}

/*!
  Return the current status of the connection enabled flag.

  \sa enableConnection()
*/
SbBool
SoField::isConnectionEnabled(void) const
{
  return this->getStatus(FLAG_ENABLECONNECTS);
}

/*!
  Connects this field as a slave to \a master. This means that the
  value of this field will be automatically updated when \a master is
  changed (as long as the connection also is enabled).

  If the field connected \e from has a different type from the field
  connected \e to, a field converter is inserted. For some
  combinations of fields no such conversion is possible, and we'll
  return \c FALSE.

  If this field had any connections to master fields beforehand, these
  are all broken up if \a append is \c FALSE.

  Call with \a notnotify if you want to avoid the initial notification
  of connected auditors (a.k.a. \e slaves).

  \sa enableConnection(), isConnectionEnabled(), isConnectedFromField()
  \sa getConnectedField(), appendConnection(SoField *)
*/
SbBool
SoField::connectFrom(SoField * master, SbBool notnotify, SbBool append)
{
  // Initialize.  /////////////////////////////////////////////////

  this->extendStorageIfNecessary();
  master->extendStorageIfNecessary();

  SoType mastertype = master->getTypeId();
  SoType thistype = this->getTypeId();
  SoType containertype = this->getContainer()->getTypeId();
  SbBool containerisconverter =
    containertype.isDerivedFrom(SoFieldConverter::getClassTypeId());


  // Set up all links.  ///////////////////////////////////////////

  if (mastertype == thistype) { // Can do direct field-to-field link.
    if (!append) this->disconnect();
    // Set up the auditor link from the master to the slave field.
    // (Note that the ``this'' slave field can also be an input field
    // of an SoFieldConverter instance.)
    master->addAuditor(this, SoNotRec::FIELD);
  }
  else { // Needs an SoFieldConverter between the fields.
    SoFieldConverter * conv = this->createConverter(mastertype);
    if (!conv) return FALSE;

    if (!append) this->disconnect();

    // Link up the input SoField of the SoFieldConverter to the master
    // field by recursively calling connectFrom().
    SoField * converterinput = conv->getInput(mastertype);
    converterinput->connectFrom(master, notnotify);

    // Connect from the SoFieldConverter output to the slave field.
    SoEngineOutput * converteroutput =
      conv->getOutput(SoType::badType()); // dummy type
    converteroutput->addConnection(this);

    // Remember the connection from the slave field to the
    // SoFieldConverter by setting up a dict entry.
    this->storage->addConverter(master, conv);
  }

  // Common bookkeeping.
  this->storage->masterfields.append(master); // slave -> master link
  if (!containerisconverter)
    master->storage->slaves.append(this); // master -> slave link


  // Notification.  ///////////////////////////////////////////////

  if ((notnotify == FALSE) && this->isConnectionEnabled()) {
    this->setDirty(TRUE);
    this->setDefault(FALSE);
    this->startNotify();
  }

  return TRUE;
}

/*!
  Connects this field as a slave to \a master. This means that the value
  of this field will be automatically updated when \a master is changed (as
  long as the connection also is enabled).

  If the field output connected \e from is of a different type from
  the field connected \e to, a field converter is inserted. For some
  combinations of fields no such conversion is possible, and we'll
  return \c FALSE.

  If this field had any master-relationships beforehand, these are all
  broken up if \a append is \c FALSE.

  Call with \a notnotify if you want to avoid the initial notification
  of connected auditors (a.k.a. \e slaves).

  \sa enableConnection(), isConnectionEnabled(), isConnectedFromField()
  \sa getConnectedField(), appendConnection(SoEngineOutput *)
*/
SbBool
SoField::connectFrom(SoEngineOutput * master, SbBool notnotify, SbBool append)
{
  // Initialize.  /////////////////////////////////////////////////

  this->extendStorageIfNecessary();

  SoType mastertype = master->getConnectionType();
  SoType thistype = this->getTypeId();

  // If we connectFrom() on the same engine as the field is already
  // connected to, we want to avoid the master container engine being
  // unref()'ed down to ref-count 0 upon the disconnect().
  SoFieldContainer * masterengine = master->getFieldContainer();

  if (masterengine) masterengine->ref();


  // Set up all links.  ///////////////////////////////////////////

  if (mastertype == thistype) { // Can do direct field-to-engineout link.
    if (!append) this->disconnect();

    // Set up the auditor link from the master engineout to the slave
    // field.  (Note that the ``this'' slave field can also be an
    // input field of an SoFieldConverter instance.)

    // This is enough, the container SoEngine will automatically pick
    // up on it.
    master->addConnection(this);
  }
  else { // Needs an SoFieldConverter between this field and the SoEngineOutput
    SoFieldConverter * conv = this->createConverter(mastertype);
    if (!conv) { // Handle this exception.
      // Clean up the ref().
      if (masterengine) masterengine->unref();
      // Sorry, can't connect.
      return FALSE;
    }

    if (!append) this->disconnect();

    // Link up the input SoField of the SoFieldConverter to the master
    // SoEngineOutput by recursively calling connectFrom().
    SoField * converterinput = conv->getInput(mastertype);
    converterinput->connectFrom(master, notnotify);

    // Connect from the SoFieldConverter output to the slave field.
    SoEngineOutput * converteroutput =
      conv->getOutput(SoType::badType()); // dummy type
    converteroutput->addConnection(this);

    // Remember the connection from the slave field to the
    // SoFieldConverter by setting up a dict entry.
    this->storage->addConverter(master, conv);
  }

  // Match the ref() invocation.
  if (masterengine) masterengine->unref();

  // Common bookkeeping.
  this->storage->masterengineouts.append(master); // slave -> master link


  // Notification.  ///////////////////////////////////////////////

  if ((notnotify == FALSE) && this->isConnectionEnabled()) {
    this->setDirty(TRUE);
    this->setDefault(FALSE);
    this->startNotify();
  }

  return TRUE;
}


/*!
  Disconnect this field as a slave from \a master.
*/
void
SoField::disconnect(SoField * master)
{
#if COIN_DEBUG && 0 // debug
  SoDebugError::postInfo("SoField::disconnect",
                         "removing slave field %p from master field %p",
                         this, master);
#endif // debug

  this->evaluate();

  SoType containertype = this->getContainer()->getTypeId();
  SbBool containerisconverter =
    containertype.isDerivedFrom(SoFieldConverter::getClassTypeId());


  // Decouple links. ///////////////////////////////////////////////////

  // Remove bookkeeping material.
  if (!containerisconverter) master->storage->slaves.removeItem(this);
  this->storage->masterfields.removeItem(master);

  SoFieldConverter * converter = this->storage->findConverter(master);
  if (converter) { // There's a converter engine between the fields.

    SoField * converterinput =
      converter->getInput(SoType::badType()); // dummy type
    converterinput->disconnect(master);

    SoEngineOutput * converteroutput =
      converter->getOutput(SoType::badType()); // dummy type
    converteroutput->removeConnection(this);

    this->storage->removeConverter(master);
    converter->unref();
  }
  else { // No converter, just a direct link.
    master->removeAuditor(this, SoNotRec::FIELD);
  }
}

/*!
  Disconnect this field as a slave from \a master.
*/
void
SoField::disconnect(SoEngineOutput * master)
{
  // First check to see we're the input field of an
  // SoFieldConverter. If so, recursively call disconnect() with the
  // field on "the other side" of the converter.

  SoType containertype = this->getContainer()->getTypeId();
  SbBool containerisconverter =
    containertype.isDerivedFrom(SoFieldConverter::getClassTypeId());
  if (containerisconverter) {
    SoFieldConverter * converter = (SoFieldConverter *)this->getContainer();
    SoEngineOutput * converterout =
      converter->getOutput(SoType::badType()); // dummy type
    SoFieldList fl;
    converterout->getForwardConnections(fl);
    fl[0]->disconnect(master);
    return;
  }


#if COIN_DEBUG && 0 // debug
  SoDebugError::postInfo("SoField::disconnect",
                         "removing slave field %p (%s.%s) from master "
                         "engineout %p",
                         this,
                         this->storage->container->getTypeId().getName().getString(),
                         this->storage->fieldtype.getName().getString(),
                         master);
#endif // debug


  // Check the enabled flag to avoid evaluating from engines which are
  // being destructed. This is a bit of a hack, but I don't think it
  // matters.   -- mortene.
  if (master->isEnabled()) this->evaluate();

  // Decouple links. ///////////////////////////////////////////////////

  // Remove bookkeeping material.
  this->storage->masterengineouts.removeItem(master);

  SoFieldConverter * converter = this->storage->findConverter(master);
  if (converter) { // There's a converter engine between the fields.
    SoField * converterinput =
      converter->getInput(SoType::badType()); // dummy type
    converterinput->storage->masterengineouts.removeItem(master);
    master->removeConnection(converterinput);

    SoEngineOutput * converteroutput =
      converter->getOutput(SoType::badType()); // dummy type
    converteroutput->removeConnection(this);

    this->storage->removeConverter(master);
    converter->unref();
  }
  else { // No converter, just a direct link.
    master->removeConnection(this);
  }
}

/*!
  Returns number of fields this field is a slave of.

  \sa getConnections()
*/
int
SoField::getNumConnections(void) const
{
  return this->hasExtendedStorage() ?
    this->storage->masterfields.getLength() : 0;
}

/*!
  Returns number of masters this field is connected to, and places
  pointers to all of them into \a masterlist.

  Note that we replace the contents of \a masterlist, i.e. we're \e
  not appending new data.

  \sa getNumConnections()
*/
int
SoField::getConnections(SoFieldList & masterlist) const
{
  if (!this->hasExtendedStorage()) return 0;

  masterlist = this->storage->masterfields;
  return masterlist.getLength();
}

/*!
  Disconnect all connections from this field as a slave to master
  fields or engine outputs.
*/
void
SoField::disconnect(void)
{
  // Disconnect us from all master fields.
  while (this->isConnectedFromField())
    this->disconnect(this->storage->masterfields[0]);

  // Disconnect us from all master engine outputs.
  while (this->isConnectedFromEngine())
    this->disconnect(this->storage->masterengineouts[0]);

  assert(this->isConnected() == FALSE);
}

/*!
  Returns \c TRUE if we're connected from another field, engine or
  interpolator.

  \sa isConnectedFromField(), isConnectedFromEngine()
  \sa connectFrom()
*/
SbBool
SoField::isConnected(void) const
{
  return (this->isConnectedFromField() ||
          this->isConnectedFromEngine());
}

/*!
  Returns \c TRUE if we're a slave of at least one field.

  \sa isConnected(), isConnectedFromEngine()
  \sa connectFrom(SoField *)
*/
SbBool
SoField::isConnectedFromField(void) const
{
  return (this->hasExtendedStorage() &&
          this->storage->masterfields.getLength() > 0);
}

/*!
  Returns \c TRUE if we're connected from an engine.

  \sa isConnected(), isConnectedFromField()
  \sa connectFrom(SoEngineOutput *)
*/
SbBool
SoField::isConnectedFromEngine(void) const
{
  return (this->hasExtendedStorage() &&
          this->storage->masterengineouts.getLength() > 0);
}

// Simplify by collecting common code for SoField::getConnected*() methods.
#define SOFIELD_GETCONNECTED(_masterlist_) \
  if (!this->hasExtendedStorage()) return FALSE; \
  int nrmasters = this->storage->_masterlist_.getLength(); \
  if (nrmasters < 1) return FALSE; \
  master = this->storage->_masterlist_[nrmasters - 1]; \
  return TRUE

/*!
  Returns \c TRUE if we are connected as a slave to at least one other
  field.  \a master will be set to the source field in the last field
  connection made.

  \sa isConnectedFromField(), connectFrom(SoField *),
  \sa appendConnection(SoField *)
*/
SbBool
SoField::getConnectedField(SoField *& master) const
{
  SOFIELD_GETCONNECTED(masterfields);
}

/*!
  Returns \c TRUE if we are connected as a slave to at least one
  engine. \a master will be set to the source of the last engine
  connection made.

  \sa isConnectedFromEngine(), connectFrom(SoEngineOutput *)
  \sa appendConnection(SoEngineOutput *)
*/
SbBool
SoField::getConnectedEngine(SoEngineOutput *& master) const
{
  SOFIELD_GETCONNECTED(masterengineouts);
}

#undef SOFIELD_GETCONNECTED

/*!
  Appends all the fields which are auditing this field in \a
  slavelist, and returns the number of fields which are our slaves.
*/
int
SoField::getForwardConnections(SoFieldList & slavelist) const
{
  if (!this->hasExtendedStorage()) return 0;

  int nr = 0;

  for (int i=0; i < this->storage->slaves.getLength(); i++) {
    slavelist.append(this->storage->slaves[i]);
    nr++;
  }

  return nr;
}

/*!
  Let the field know to which container it belongs.

  \sa getContainer(), SoFieldContainer
*/
void
SoField::setContainer(SoFieldContainer * cont)
{
  if (!this->hasExtendedStorage()) this->container = cont;
  else this->storage->container = cont;

  // The field should have been set to its default value before it is
  // added to the container.
  //
  // This might seem strange, but it looks like it is necessary to do
  // it this way to be compatible with Open Inventor.
  this->setDefault(TRUE);
}

/*!
  Returns the SoFieldContainer object "owning" this field.

  \sa SoFieldContainer, setContainer()
*/
SoFieldContainer *
SoField::getContainer(void) const
{
  if (!this->hasExtendedStorage()) return this->container;
  else return this->storage->container;
}

/*!
  Set the field's value through the given \a valuestring. The format
  of the string must adhere to the ASCII format used in Coin data
  format files.

  Only the value should be specified - \e not the name of the field.

  \c FALSE is returned if the field value is invalid for the field
  type and can't be parsed in any sensible way.

  \sa get()
*/
SbBool
SoField::set(const char * valuestring)
{
  // Note that it is not necessary to set a header identification line
  // for this to work.
  SoInput in;
  in.setBuffer((void *)valuestring, strlen(valuestring));
  if (!this->readValue(&in)) return FALSE;

  this->valueChanged();
  return TRUE;
}

static void * field_buffer = NULL;
static size_t field_buffer_size = 0;

static void
field_buffer_cleanup(void)
{
  if (field_buffer) {
    free(field_buffer);
    field_buffer = NULL;
    field_buffer_size = 0;
  }
}

static void *
field_buffer_realloc(void * bufptr, size_t size)
{
  void * newbuf = realloc(bufptr, size);
  field_buffer = newbuf;
  field_buffer_size = size;
  return newbuf;
}

/*!
  Returns the field's value as an ASCII string in the export data
  format for Inventor files.

  \sa set()
*/
void
SoField::get(SbString & valuestring)
{
  // Note: this code has an almost verbatim copy in SoMField::get1(),
  // so remember to update both places if any fixes are done.

  // Initial buffer setup.
  SoOutput out;
  const size_t STARTSIZE = 32;
  // if buffer grow bigger than 1024 bytes, free memory
  // at end of method. Otherwise, just keep using the allocated
  // memory the next time this method is called.
  const size_t MAXSIZE = 1024;

  if (field_buffer_size < STARTSIZE) {
    field_buffer = malloc(STARTSIZE);
    field_buffer_size = STARTSIZE;
    coin_atexit(field_buffer_cleanup);
  }

  out.setBuffer(field_buffer, field_buffer_size,
                field_buffer_realloc);

  // Record offset to skip header.
  out.write("");
  size_t offset;
  void * buffer;
  out.getBuffer(buffer, offset);

  // Write field..
  this->writeValue(&out);
  // ..then read it back into the SbString.
  size_t size;
  out.getBuffer(buffer, size);
  valuestring = ((char *)buffer) + offset;

  // check if buffer grew too big
  if (field_buffer_size >= MAXSIZE) {
    (void) field_buffer_realloc(field_buffer, STARTSIZE);
  }
}

/*!
  Notify the field as well as the field's owner / container that it
  has been changed.

  Touching a field which is part of any component (engine or node) in
  a scene graph will lead to a forced redraw. This is useful if you
  have been doing several updates to the field wrapped in a pair of
  enableNotify() calls to notify the field's auditors that its value
  has changed.

  \sa setValue(), enableNotify()
*/
void
SoField::touch(void)
{
  if (this->container) this->startNotify();
}

/*!
  Trigger a notification sequence.

  At the end of a notification sequence, all "immediate" sensors
  (i.e. sensors set up with a zero priority) are triggered.
*/
void
SoField::startNotify(void)
{
  SoNotList l;
#if COIN_DEBUG && 0 // debug
  SoDebugError::postInfo("SoField::startNotify", "field %p (%s), list %p",
                         this, this->getTypeId().getName().getString(), &l);
#endif // debug

  SoDB::startNotify();
  this->notify(&l);
  SoDB::endNotify();

#if COIN_DEBUG && 0 // debug
  SoDebugError::postInfo("SoField::startNotify", "DONE\n\n");
#endif // debug
}

/*!
  Notify auditors that this field has changed.
*/
void
SoField::notify(SoNotList * nlist)
{
  // In Inventor it is legal to have circular field connections. This
  // test stops the notification from entering into an infinite
  // recursion because of such connections. The flag is set/cleared
  // before/after progagating the notification.
  if (this->getStatus(FLAG_ISNOTIFIED)) return;

#if COIN_DEBUG && 0 // debug
  if (this != SoDB::getGlobalField("realTime")) {
    SoDebugError::postInfo("SoField::notify", "%p (%s (%s '%s')) -- start",
                           this,
                           this->getTypeId().getName().getString(),
                           this->getContainer() ? this->getContainer()->getTypeId().getName().getString() : "*none*",
                           this->getContainer() ? this->getContainer()->getName().getString() : "*none*");
  }
#endif // debug

  // If we're not the originator of the notification process, we need
  // to be marked dirty, as it means something we're connected to as a
  // slave has changed and our value needs to be updated.
  //
  // Note: don't try to "optimize" code here by moving the setDirty()
  // call down into the isNotifyEnabled() check, as setDirty()
  // _should_ happen if we're not the originator -- no matter what the
  // status of the notification enable flag is.
  if (nlist->getFirstRec()) this->setDirty(TRUE);

  if (this->isNotifyEnabled()) {
    this->setStatusBits(FLAG_ISNOTIFIED);
    SoNotRec rec(this->getContainer());
    nlist->append(&rec, this);
    nlist->setLastType(SoNotRec::CONTAINER); // FIXME: Not sure about this. 20000304 mortene.

#if COIN_DEBUG && 0 // debug
    SoDebugError::postInfo("SoField::notify",
                           "field %p, list %p", this, nlist);
#endif // debug
    if (this->getContainer()) this->getContainer()->notify(nlist);
    this->notifyAuditors(nlist);
    this->clearStatusBits(FLAG_ISNOTIFIED);
  }

#if COIN_DEBUG && 0 // debug
  if (this != SoDB::getGlobalField("realTime")) {
    SoDebugError::postInfo("SoField::notify", "%p (%s (%s '%s')) -- done",
                           this,
                           this->getTypeId().getName().getString(),
                           this->getContainer() ? this->getContainer()->getTypeId().getName().getString() : "*none*",
                           this->getContainer() ? this->getContainer()->getName().getString() : "*none*");
  }
#endif // debug
}

/*!
  This method sets whether notification will be propagated on changing
  the value of the field.  The old value of the setting is returned.

  \sa isNotifyEnabled()
*/
SbBool
SoField::enableNotify(SbBool on)
{
  const SbBool old = this->getStatus(FLAG_DONOTIFY);
  (void) this->changeStatusBits(FLAG_DONOTIFY, on);
  return old;
}

/*!
  This method returns whether notification of changes to the field
  value are propagated to the auditors.

  \sa enableNotify()
*/
SbBool
SoField::isNotifyEnabled(void) const
{
  return this->getStatus(FLAG_DONOTIFY);
}

// Makes an extended storage block on first connection.
void
SoField::extendStorageIfNecessary(void)
{
  if (!this->hasExtendedStorage()) {
    this->storage = new SoConnectStorage(this->container, this->getTypeId());
    this->setStatusBits(FLAG_EXTSTORAGE);
  }
}

/*!
  Add an auditor to the list. All auditors will be notified whenever
  this field changes its value(s).
*/
void
SoField::addAuditor(void * f, SoNotRec::Type type)
{
  this->extendStorageIfNecessary();
  this->storage->auditors.append(f, type);
  this->connectionStatusChanged(+1);
}

/*!
  Remove an auditor from the list.
*/
void
SoField::removeAuditor(void * f, SoNotRec::Type type)
{
#if COIN_DEBUG && 0 // debug
  SoDebugError::postInfo("SoField::removeAuditor",
                         "%p removing %p", this, f);
#endif // debug

  assert(this->hasExtendedStorage());
  this->storage->auditors.remove(f, type);
  this->connectionStatusChanged(-1);
}

/*!
  Checks for equality. Returns \c 0 if the fields are of different
  type or the field's value(s) are not equal.
*/
int
SoField::operator ==(const SoField & f) const
{
  return this->isSame(f);
}

/*!
  Returns \c TRUE if the fields are of different type or has different
  value.
*/
int
SoField::operator !=(const SoField & f) const
{
  return !this->isSame(f);
}

/*!
  Returns \c TRUE if it is necessary to write the field when dumping a
  scene graph. This needs to be done if the field is not default (it
  has been changed from its default value), if it's ignored, or if
  it's connected from another field or engine.
*/
SbBool
SoField::shouldWrite(void) const
{
  if (!this->isDefault()) return TRUE;
  if (this->isIgnored()) return TRUE;
  if (this->isConnected()) return TRUE;

  // FIXME: SGI Inventor seems to test forward connections here
  // also. We consider this is bug, since this field should not write
  // just because some field is connected from this field.  
  // pederb, 2002-02-07
  return FALSE;
}

/*!
  Called whenever another slave attaches or detaches itself to us.  \a
  numconnections is the difference in number of connections made
  (i.e. if stuff is \e disconnected, \a numconnections will be a
  negative number).

  The default method is empty. Overload in subclasses if you want do
  something special on connections/deconnections.
*/
void
SoField::connectionStatusChanged(int numconnections)
{
}

/*!
  Returns \c TRUE if this field should not be written into at the
  moment the method is called.

  This method is used internally in Coin during notification and
  evaluation processes, and should normally not be of interest to the
  application programmer.
*/
SbBool
SoField::isReadOnly(void) const
{
  return this->getStatus(FLAG_READONLY);
}

/*!
  This method is internally called after copyFrom() during scene graph
  copies, and should do the operations necessary for fixing up the
  field instance after it has gotten a new value.

  The default method in the SoField superclass does nothing.

  The application programmer should normally not need to consider this
  method, unless he constructs a complex field type which contains new
  references to container instances (i.e. nodes or
  engines). Overloading this method is then necessary to update the
  reference pointers, as they could have been duplicated during the
  copy operation.
*/
void
SoField::fixCopy(SbBool copyconnections)
{
}

/*!
  Returns \c TRUE if this field has references to any containers in
  the scene graph which are also duplicated during the copy operation.

  Note that this method \e only is valid to call during copy
  operations.

  See also the note about the relevance of the fixCopy() method for
  application programmers, as it is applicable on this method aswell.
*/
SbBool
SoField::referencesCopy(void) const
{
  SoFieldList masters;
  int nr = this->getConnections(masters);

  for (int i=0; i < nr; i++) {
    SoFieldContainer * fc = masters[i]->getContainer();
    if (SoFieldContainer::checkCopy(fc)) return TRUE;
  }

  return FALSE;
}

/*!
  If \a fromfield contains a connection to another field, make this
  field also use the same connection.
*/
void
SoField::copyConnection(const SoField * fromfield)
{
  // Consider most common case first.
  if (!fromfield->isConnected()) return;

  // FIXME: copy _all_ connections (in preparation for VRML2 support)?
  // 20000116 mortene.

#define COPYCONNECT(_fieldtype_, _getfunc_) \
  _fieldtype_ * master; \
  (void)fromfield->_getfunc_(master); \
  SoFieldContainer * masterfc = master->getContainer(); \
  int ptroffset = (char *)master - (char *)masterfc; \
  SoFieldContainer * copyfc = masterfc->copyThroughConnection(); \
  _fieldtype_ * copyfield = (_fieldtype_ *)((char *)copyfc + ptroffset); \
  (void)this->connectFrom(copyfield)


  // Connections already in place will be automatically removed, as
  // the append argument to connectFrom() is default FALSE.

  if (fromfield->isConnectedFromField()) {
    COPYCONNECT(SoField, getConnectedField);
  }
  else if (fromfield->isConnectedFromEngine()) {
    COPYCONNECT(SoEngineOutput, getConnectedEngine);
  }
#undef COPYCONNECT
}

/*!
  Reads and sets the value of this field from the given SoInput
  instance.  Returns \c FALSE if the field value can not be parsed
  from the input.

  This field has the \a name given as the second argument.

  \sa set(), write()
*/
SbBool
SoField::read(SoInput * in, const SbName & name)
{
  SbBool readok;
  if (in->checkISReference(this->getContainer(), name, readok) || readok == FALSE) {
    if (!readok) {
      SoReadError::post(in, "Couldn't read value for field \"%s\"",
                        name.getString());
    }
    return readok;
  }

  // This macro is convenient for reading with error detection.
#define READ_VAL(val) \
  if (!in->read(val)) { \
    SoReadError::post(in, "Premature end of file"); \
    return FALSE; \
  }


  this->setDefault(FALSE);
  this->setDirty(FALSE);

  if (!in->isBinary()) { // ASCII file format.
    char c;
    // Check for the ignored flag first, as it is valid to let the
    // field data be just the ignored flag and nothing else.
    READ_VAL(c);
    if (c == IGNOREDCHAR) this->setIgnored(TRUE);
    else {
      in->putBack(c);

      // Read field value(s).
      if (!this->readValue(in)) {
        SoReadError::post(in, "Couldn't read value for field \"%s\"",
                          name.getString());
        return FALSE;
      }

      if (!in->eof()) {  // Can happen for memory buffers with SoField::set().
        // Check again for ignored flag.
        READ_VAL(c);
        if (c == IGNOREDCHAR) this->setIgnored(TRUE);
        else in->putBack(c);
      }
    }

    if (!in->eof()) {  // Can happen for memory buffers with SoField::set().
      // Check if there's a field-to-field connection here.
      READ_VAL(c);
      if (c == CONNECTIONCHAR) { if (!this->readConnection(in)) return FALSE; }
      else in->putBack(c);
    }
  }
  else { // Binary file format.
    // Read field value(s).
    if (!this->readValue(in)) {
      SoReadError::post(in, "Couldn't read value for field \"%s\"",
                        name.getString());
      return FALSE;
    }

    // Check for the "ignored", "connection" and "default" flags.
    unsigned int flags;
    READ_VAL(flags);

    if (flags & SoField::IGNORED) this->setIgnored(TRUE);
    if (flags & SoField::CONNECTED) { if (!this->readConnection(in)) return FALSE; }
    if (flags & SoField::DEFAULT) this->setDefault(TRUE);
#if COIN_DEBUG
    if (flags & ~SoField::ALLFILEFLAGS) {
      SoDebugError::postWarning("SoField::read",
                                "unknown field flags (0x%x) -- ",
                                "please report to coin-bugs@sim.no", flags);
    }
#endif // COIN_DEBUG
  }

#undef READ_VAL

  return TRUE;
}

/*!
  Write the value of the field to the given SoOutput instance (which
  can be either a memory buffer or a file, in ASCII or in binary
  format).

  \sa get(), read(), SoOutput
*/
void
SoField::write(SoOutput * out, const SbName & name) const
{
  if (out->getStage() == SoOutput::COUNT_REFS) {
    // Handle first stage of write operations.
    this->countWriteRefs(out);
    return;
  }

  // Ok, we've passed the first write stage and is _really_ writing.

  // Check connection (this is common code for ASCII and binary
  // write).
  SbBool writeconnection = FALSE;
  SbName dummy;
  SoFieldContainer * fc = this->resolveWriteConnection(dummy);
  if (fc && (fc->shouldWrite() || fc->isOfType(SoEngine::getClassTypeId())))
    writeconnection = TRUE;


  // ASCII write.
  if (!out->isBinary()) {
    out->indent();
    // Cast to avoid "'s.
    out->write((const char *)name);
    if (!this->isDefault()) {
      out->write(' ');
      this->writeValue(out);
    }    
    if (this->isIgnored()) {
      out->write(' ');
      out->write(IGNOREDCHAR);
    }

    if (writeconnection) this->writeConnection(out);

    out->write('\n');
  }
  // Binary write.
  else {
    // Cast to avoid "'s.
    out->write((const char *)name);
    this->writeValue(out);

    unsigned int flags = 0;
    if (this->isIgnored()) flags |= SoField::IGNORED;
    if (writeconnection) flags |= SoField::CONNECTED;
    if (this->isDefault()) flags |= SoField::DEFAULT;

    out->write(flags);

    if (writeconnection) this->writeConnection(out);
  }
}

/*!
  This method is called during the first pass of write operations, to
  count the number of write references to this field and to "forward"
  the reference counting operation to the field containers we're
  connected to.
*/
void
SoField::countWriteRefs(SoOutput * out) const
{
  SbName dummy;
  SoFieldContainer * fc = this->resolveWriteConnection(dummy);
  if (fc) fc->addWriteReference(out, TRUE);
}

/*!
  Re-evaluates the value of this field any time a getValue() call is
  made and the field is marked dirty. This is done in this way to gain
  the advantages of having lazy evaluation.
*/
void
SoField::evaluate(void) const
{
  // if we're destructing, don't continue as this would cause
  // a call to the virtual evaluateConnection()
  if (this->getStatus(FLAG_ISDESTRUCTING)) {
#if COIN_DEBUG && 0 // debug
    SoDebugError::postInfo("SoField::evaluate",
                           "Stopped evaluate while destructing.");
#endif // debug
    return;
  }
  // do some simple tests to optimize evaluations
  if (this->getDirty() == FALSE) return;
  if (this->isConnected() == FALSE) return;

  // Recursive calls to SoField::evalute() shouldn't happen, as the
  // state of the field variables might not be consistent while
  // evaluating.
  assert(!this->getStatus(FLAG_ISEVALUATING));

  // Cast away the const. (evaluate() must be const, since we're using
  // evaluate() from getValue().)
  SoField * that = (SoField *)this;

  that->setStatusBits(FLAG_ISEVALUATING);
  this->evaluateConnection();
  that->clearStatusBits(FLAG_ISEVALUATING);
  that->setDirty(FALSE);
}

/*!
  Do we need re-evaluation?
*/
SbBool
SoField::getDirty(void) const
{
  return this->getStatus(FLAG_NEEDEVALUATION);
}

/*!
  Mark field for re-evaluation, but do not trigger it.
*/
void
SoField::setDirty(SbBool dirty)
{
  (void) this->changeStatusBits(FLAG_NEEDEVALUATION, dirty);
}

/*!
  Connect ourself as slave to another object, while still keeping the
  other connections currently in place.

  \sa connectFrom()
*/
SbBool
SoField::appendConnection(SoEngineOutput * master, SbBool notnotify)
{
  return this->connectFrom(master, notnotify, TRUE);
}

/*!
  Connect ourself as slave to another object, while still keeping the
  other connections currently in place.

  \sa connectFrom()
*/
SbBool
SoField::appendConnection(SoField * master, SbBool notnotify)
{
  return this->connectFrom(master, notnotify, TRUE);
}

// Make a converter from value(s) of the given field type and the
// value(s) of this type. Returns NULL if no value conversion between
// types is possible.
SoFieldConverter *
SoField::createConverter(SoType from) const
{
  SoType thistype = this->getTypeId();
  assert(from != thistype);
  SoType convtype = SoDB::getConverter(from, thistype);
  if (convtype == SoType::badType()) {
#if COIN_DEBUG // COIN_DEBUG
    SoDebugError::postWarning("SoField::createConverter",
                              "no converter for %s to %s",
                              from.getName().getString(),
                              thistype.getName().getString());
#endif // COIN_DEBUG
    return NULL;
  }

  SoFieldConverter * fc;

  if (convtype == SoConvertAll::getClassTypeId())
    fc = new SoConvertAll(from, this->getTypeId());
  else
    fc = (SoFieldConverter *)convtype.createInstance();

  fc->ref();
  return fc;
}


/*!
  Read the fieldcontainer and master field id of a field-to-field
  connection.
*/
SbBool
SoField::readConnection(SoInput * in)
{
  // Read the fieldcontainer instance containing the master field
  // we're connected to.
  SoBase * bp;
  if (!SoBase::read(in, bp, SoFieldContainer::getClassTypeId())) return FALSE;
  if (!bp) {
    SoReadError::post(in, "couldn't read field-to-field connection");
    return FALSE;
  }

  SoFieldContainer * fc = (SoFieldContainer *)bp;

  // Scan past the '.' character for ASCII format.
  if (!in->isBinary()) {
    char c;
    if (!in->read(c)) {
      SoReadError::post(in, "premature EOF");
      return FALSE;
    }
    if (c != '.') {
      SoReadError::post(in, "expected '.', got '%c'", c);
      return FALSE;
    }
  }

  // Read name of master field.
  SbName mastername;
  if (!in->read(mastername)) {
    SoReadError::post(in, "premature EOF");
    return FALSE;
  }


  // Get pointer to master field or engine output and connect.

  SoField * masterfield = fc->getField(mastername);
  if (!masterfield) {
    if (fc->isOfType(SoEngine::getClassTypeId()) || fc->isOfType(SoNodeEngine::getClassTypeId())) {
      SoEngineOutput * masteroutput =
        fc->isOfType(SoEngine::getClassTypeId()) ?
        ((SoEngine*)fc)->getOutput(mastername) :
        ((SoNodeEngine*)fc)->getOutput(mastername);

      if (!masteroutput) {
        SoReadError::post(in, "no field or output ``%s'' in ``%s''",
                          mastername.getString(),
                          fc->getTypeId().getName().getString());
        return FALSE;
      }
      else {
        // Make connection.
        if (!this->connectFrom(masteroutput)) {
          SoReadError::post(in, "couldn't connect to ``%s''",
                            mastername.getString());
        }
      }
    }
    else {
      SoReadError::post(in, "no field ``%s'' in ``%s''",
                        mastername.getString(),
                        fc->getTypeId().getName().getString());
      return FALSE;
    }
  }
  else {
    // Make connection.
    if (!this->connectFrom(masterfield)) {
      SoReadError::post(in, "couldn't connect to ``%s''",
                        mastername.getString());
    }
  }

  return TRUE;
}

/*!
  Write out information about this field's connection.
*/
void
SoField::writeConnection(SoOutput * out) const
{
  SbName mastername;
  SoFieldContainer * fc = this->resolveWriteConnection(mastername);
  assert(fc);

  if (!out->isBinary()) {
    out->write(' ');
    out->write(CONNECTIONCHAR);
  }

  if (fc->isOfType(SoNode::getClassTypeId())) {
    SoWriteAction wa(out);
    wa.continueToApply((SoNode *)fc);
  }
  else {
    // Note: for this to work, classes inheriting SoFieldContainer
    // which are _not_ also inheriting from SoNode must call
    // SoBase::writeHeader() and SoBase::writeFooter().
    fc->writeInstance(out);
    // FIXME: does this work for engines? 20000131 mortene.
  }

  if (!out->isBinary()) {
    out->indent();
    out->write(". ");
  }

  out->write(mastername.getString());
}

// Check if this field should write a connection upon export. Returns
// a pointer to the fieldcontainer with the master field we're
// connected to (or NULL if none, or if the master field's container
// is not within the scenegraph). If the return value is non-NULL, the
// name of the master field is copied to the mastername argument.
SoFieldContainer *
SoField::resolveWriteConnection(SbName & mastername) const
{
  if (!this->isConnected()) return NULL;

  SoFieldContainer * fc = NULL;
  SoField * fieldmaster;
  SoEngineOutput * enginemaster;

  if (this->getConnectedField(fieldmaster)) {
    fc = fieldmaster->getContainer();
    assert(fc);
    SbBool ok = fc->getFieldName(fieldmaster, mastername);
    assert(ok);
  }
  else if (this->getConnectedEngine(enginemaster)) {
    fc = enginemaster->getFieldContainer();
    assert(fc);
    // FIXME: couldn't we use getFieldName()? 20000129 mortene.
    SbBool ok =
      enginemaster->isNodeEngineOutput() ?
      ((SoNodeEngine *)fc)->getOutputName(enginemaster, mastername) :
      ((SoEngine *)fc)->getOutputName(enginemaster, mastername);
    assert(ok);
  }
  else assert(FALSE);

  return fc;
}


/*!
  If we're connected to a field/engine/interpolator, copy the value
  from the master source.
*/
void
SoField::evaluateConnection(void) const
{
  // FIXME: should we evaluate from all masters in turn? 19990623 mortene.
  if (this->isConnectedFromField()) {
    int idx = this->storage->masterfields.getLength() - 1;
    SoField * master = this->storage->masterfields[idx];
    // don't copy if master is destructing, or if master is currently
    // evaluating. The master might be evaluating if we have circular
    // field connections. If this is the case, the field will already
    // contain the correct value, and we should not copy again.
    if (!master->isDestructing() && !master->getStatus(FLAG_ISEVALUATING)) {
      SoFieldConverter * converter = this->storage->findConverter(master);
      if (converter) converter->evaluateWrapper();
      else {
        SoField * that = (SoField *)this; // cast away const
        // Copy data. Disable notification first since notification
        // has already been sent from the master.
        SbBool oldnotify = that->enableNotify(FALSE);
        that->copyFrom(*master);
        (void) that->enableNotify(oldnotify);
      }
    }
  }
  else if (this->isConnectedFromEngine()) {
    int idx = this->storage->masterengineouts.getLength() - 1;
    SoEngineOutput * master = this->storage->masterengineouts[idx];
    SoFieldConverter * converter = this->storage->findConverter(master);
    if (converter) converter->evaluateWrapper();
    else if (master->isNodeEngineOutput()) {
      master->getNodeContainer()->evaluateWrapper();
    }
    else {
      master->getContainer()->evaluateWrapper();
    }
  }
  else {
    // Should never happen.
    assert(0);
  }
}

/*!
  This method is always called whenever the field's value has been
  changed by direct invocation of setValue() or some such. You should
  \e never call this method from anywhere in the code where the field
  value is being set through an evaluation of its connections.

  If \a resetdefault is \c TRUE, the flag marking whether or not the
  field has its default value will be set to \c FALSE.

  The method will also notify any auditors that the field's value has
  changed.
*/
void
SoField::valueChanged(SbBool resetdefault)
{
  if (this->changeStatusBits(FLAG_READONLY, TRUE)) {
    this->setDirty(FALSE);
    if (resetdefault) this->setDefault(FALSE);
    if (this->container) this->startNotify();
    this->clearStatusBits(FLAG_READONLY);
  }
}

// Notify any auditors by marking them dirty - i.e. ready for
// re-evaluation.  Auditors include connected fields, sensors,
// containers (nodes/engines), ...
void
SoField::notifyAuditors(SoNotList * l)
{
#if COIN_DEBUG && 0 // debug
  SoDebugError::postInfo("SoField::notifyAuditors",
                         "field %p, list %p", this, l);
#endif // debug
  if (this->hasExtendedStorage() && this->storage->auditors.getLength())
    this->storage->auditors.notify(l);
}

/*!
  Set type of this field.

  The possible values for \a type is: 0 for ordinary fields, 1 for
  eventIn fields, 2 for eventOut fields, 3 for internal fields, 4 for
  VRML2 exposedField fields. There are also enum values in SoField.h.
*/
void
SoField::setFieldType(int type)
{
  this->clearStatusBits(FLAG_TYPEMASK);
  assert(type >=0 && type <= FLAG_TYPEMASK);
  this->setStatusBits((unsigned int)type);
}

/*!
  Return the type of this field.

  \sa setFieldType()
*/
int
SoField::getFieldType(void) const
{
  return this->statusbits & FLAG_TYPEMASK;
}

/*!
  Can be used to check if a field is being destructed.
*/
SbBool
SoField::isDestructing(void) const
{
  return this->getStatus(FLAG_ISDESTRUCTING);
}

/*!
  Initialize all the field classes.
*/
void
SoField::initClasses(void)
{
  SoSField::initClass();
  SoSFBool::initClass();
  SoSFColor::initClass();
  SoSFEngine::initClass();
  SoSFFloat::initClass();
  SoSFShort::initClass();
  SoSFUShort::initClass();
  SoSFInt32::initClass();
  SoSFUInt32::initClass();
  SoSFVec2f::initClass();
  SoSFVec3f::initClass();
  SoSFVec4f::initClass();
  SoSFMatrix::initClass();
  SoSFEnum::initClass();
  SoSFBitMask::initClass();
  SoSFImage::initClass();
  SoSFImage3::initClass();
  SoSFName::initClass();
  SoSFNode::initClass();
  SoSFPath::initClass();
  SoSFPlane::initClass();
  SoSFRotation::initClass();
  SoSFString::initClass();
  SoSFTime::initClass();
  SoSFTrigger::initClass();
  SoMField::initClass();
  SoMFBool::initClass();
  SoMFColor::initClass();
  SoMFEngine::initClass();
  SoMFEnum::initClass();
  SoMFBitMask::initClass();
  SoMFFloat::initClass();
  SoMFInt32::initClass();
  SoMFMatrix::initClass();
  SoMFName::initClass();
  SoMFNode::initClass();
  SoMFPath::initClass();
  SoMFPlane::initClass();
  SoMFRotation::initClass();
  SoMFShort::initClass();
  SoMFString::initClass();
  SoMFTime::initClass();
  SoMFUInt32::initClass();
  SoMFUShort::initClass();
  SoMFVec2f::initClass();
  SoMFVec3f::initClass();
  SoMFVec4f::initClass();

  // double precision
  SoSFVec3d::initClass();
  SoMFVec3d::initClass();

  // Create these obsoleted types for backwards compatibility. They
  // are typedef'ed to the types which obsoleted them, but this is
  // needed so it will also be possible to use SoType::fromName() with
  // the old names and create instances in that manner.
  //
  // FIXME: SoType::fromName("oldname") == SoType::fromName("newname")
  // will fail, but this can be solved with a hack in
  // SoType::operator==(). Do we _want_ to implement this hack,
  // though? It'd be ugly as hell.  19991109 mortene.

  SoType::createType(SoSField::getClassTypeId(), "SFLong",
                     &SoSFInt32::createInstance);
  SoType::createType(SoSField::getClassTypeId(), "SFULong",
                     &SoSFUInt32::createInstance);
  SoType::createType(SoMField::getClassTypeId(), "MFLong",
                     &SoMFInt32::createInstance);
  SoType::createType(SoMField::getClassTypeId(), "MFULong",
                     &SoMFUInt32::createInstance);
}

/*!
  Obsoleted 2001-10-18
*/
SbBool
SoField::connectFrom(SoVRMLInterpOutput * master,
                     SbBool notnotify, SbBool append)
{
  COIN_OBSOLETED();
  return FALSE;
}

/*!
  Obsoleted 2001-10-18
*/
SbBool
SoField::appendConnection(SoVRMLInterpOutput * master,
                          SbBool notnotify)
{
  COIN_OBSOLETED();
  return FALSE;
}

/*!
  Obsoleted 2001-10-18
*/
void
SoField::disconnect(SoVRMLInterpOutput * interpoutput)
{
  COIN_OBSOLETED();
}

/*!
  Obsoleted 2001-10-18
*/
SbBool
SoField::isConnectedFromVRMLInterp(void) const
{
  COIN_OBSOLETED();
  return FALSE;
}

/*!
  Obsoleted 2001-10-18
*/
SbBool
SoField::getConnectedVRMLInterp(SoVRMLInterpOutput *& master) const
{
  COIN_OBSOLETED();
  return FALSE;
}

#undef FLAG_TYPEMASK
#undef FLAG_ISDEFAULT
#undef FLAG_IGNORE
#undef FLAG_EXTSTORAGE
#undef FLAG_ENABLECONNECTS
#undef FLAG_NEEDEVALUATION
#undef FLAG_READONLY
#undef FLAG_DONOTIFY
#undef FLAG_ISDESTRUCTING
#undef FLAG_ISEVALUATING
#undef FLAG_ISNOTIFIED
