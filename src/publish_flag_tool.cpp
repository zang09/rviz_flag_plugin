/*
 * Copyright (c) 2011, Willow Garage, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the Willow Garage, Inc. nor the names of its
 *       contributors may be used to endorse or promote products derived from
 *       this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <OGRE/OgreSceneNode.h>
#include <OGRE/OgreSceneManager.h>
#include <OGRE/OgreEntity.h>

#include <ros/console.h>

#include <rviz/viewport_mouse_event.h>
#include <rviz/visualization_manager.h>
#include <rviz/mesh_loader.h>
#include <rviz/geometry.h>
#include <rviz/properties/vector_property.h>

#include "publish_flag_tool.h"

namespace rviz_flag_plugin
{

// BEGIN_TUTORIAL
// Construction and destruction
// ^^^^^^^^^^^^^^^^^^^^^^^^^^^^
//
// The constructor must have no arguments, so we can't give the
// constructor the parameters it needs to fully initialize.
//
// Here we set the "shortcut_key_" member variable defined in the
// superclass to declare which key will activate the tool.
PublishFlagTool::PublishFlagTool()
  : moving_flag_node_( NULL )
  , current_flag_property_( NULL )
{
  shortcut_key_ = 'l';
}

// The destructor destroys the Ogre scene nodes for the flags so they
// disappear from the 3D scene.  The destructor for a Tool subclass is
// only called when the tool is removed from the toolbar with the "-"
// button.
PublishFlagTool::~PublishFlagTool()
{
  for( unsigned i = 0; i < flag_nodes_.size(); i++ )
  {
    scene_manager_->destroySceneNode( flag_nodes_[ i ]);
  }
}

// onInitialize() is called by the superclass after scene_manager_ and
// context_ are set.  It should be called only once per instantiation.
// This is where most one-time initialization work should be done.
// onInitialize() is called during initial instantiation of the tool
// object.  At this point the tool has not been activated yet, so any
// scene objects created should be invisible or disconnected from the
// scene at this point.
//
// In this case we load a mesh object with the shape and appearance of
// the flag, create an Ogre::SceneNode for the moving flag, and then
// set it invisible.
void PublishFlagTool::onInitialize()
{
  flag_resource_ = "package://rviz_flag_plugin/model/flag.dae";

  if( rviz::loadMeshFromResource( flag_resource_ ).isNull() )
  {
    ROS_ERROR( "PublishFlagTool: failed to load model resource '%s'.", flag_resource_.c_str() );
    return;
  }

  moving_flag_node_ = scene_manager_->getRootSceneNode()->createChildSceneNode();
  Ogre::Entity* entity = scene_manager_->createEntity( flag_resource_ );
  moving_flag_node_->attachObject( entity );
  moving_flag_node_->setVisible( false );

  flag_publisher_     = nh_.advertise<rviz_flag_plugin::PointArray>("rviz/flag_points", 1, true);
  flag_subscriber_    = nh_.subscribe<rviz_flag_plugin::PointArray>("rviz/make_flag", 1, &PublishFlagTool::flagCallback, this);
  visible_subscriber_ = nh_.subscribe<std_msgs::Bool>("rviz/visible_flag", 1, &PublishFlagTool::visibleCallback, this);
  clear_subscriber_   = nh_.subscribe<std_msgs::Bool>("rviz/clear_flag", 1, &PublishFlagTool::clearCallback, this);
}

// Activation and deactivation
// ^^^^^^^^^^^^^^^^^^^^^^^^^^^
//
// activate() is called when the tool is started by the user, either
// by clicking on its button in the toolbar or by pressing its hotkey.
//
// First we set the moving flag node to be visible, then we create an
// rviz::VectorProperty to show the user the position of the flag.
// Unlike rviz::Display, rviz::Tool is not a subclass of
// rviz::Property, so when we want to add a tool property we need to
// get the parent container with getPropertyContainer() and add it to
// that.
//
// We wouldn't have to set current_flag_property_ to be read-only, but
// if it were writable the flag should really change position when the
// user edits the property.  This is a fine idea, and is possible, but
// is left as an exercise for the reader.
void PublishFlagTool::activate()
{
  if( moving_flag_node_ )
  {
    moving_flag_node_->setVisible( true );

    current_flag_property_ = new rviz::VectorProperty( "Flag " + QString::number( flag_nodes_.size() ));
    current_flag_property_->setReadOnly( true );
    getPropertyContainer()->addChild( current_flag_property_ );
  }
}

// deactivate() is called when the tool is being turned off because
// another tool has been chosen.
//
// We make the moving flag invisible, then delete the current flag
// property.  Deleting a property also removes it from its parent
// property, so that doesn't need to be done in a separate step.  If
// we didn't delete it here, it would stay in the list of flags when
// we switch to another tool.
void PublishFlagTool::deactivate()
{
  if( moving_flag_node_ )
  {
    moving_flag_node_->setVisible( false );
    moving_flag_node_->showBoundingBox( false );
    delete current_flag_property_;
    current_flag_property_ = NULL;
  }
}

// Handling mouse events
// ^^^^^^^^^^^^^^^^^^^^^
//
// processMouseEvent() is sort of the main function of a Tool, because
// mouse interactions are the point of Tools.
//
// We use the utility function rviz::getPointOnPlaneFromWindowXY() to
// see where on the ground plane the user's mouse is pointing, then
// move the moving flag to that point and update the VectorProperty.
//
// If this mouse event was a left button press, we want to save the
// current flag location.  Therefore we make a new flag at the same
// place and drop the pointer to the VectorProperty.  Dropping the
// pointer means when the tool is deactivated the VectorProperty won't
// be deleted, which is what we want.
int PublishFlagTool::processMouseEvent( rviz::ViewportMouseEvent& event )
{
  if( !moving_flag_node_ )
  {
    return Render;
  }
  Ogre::Vector3 intersection;
  Ogre::Plane ground_plane( Ogre::Vector3::UNIT_Z, 0.0f );
  if( rviz::getPointOnPlaneFromWindowXY( event.viewport,
                                         ground_plane,
                                         event.x, event.y, intersection ))
  {
    moving_flag_node_->setVisible( true );
    moving_flag_node_->setPosition( intersection );
    moving_flag_node_->showBoundingBox(true);
    current_flag_property_->setVector( intersection );

    if( event.leftDown() ) // add
    {
      makeFlag( intersection );
      current_flag_property_ = nullptr; // Drop the reference so that deactivate() won't remove it.
      moving_flag_node_->showBoundingBox(false);
      return Render | Finished;
    }

    if( event.rightDown() ) // delete
    {
      int size = flag_nodes_.size();

      if(size != 0)
      {
        scene_manager_->destroySceneNode(flag_nodes_[size-1]);

        rviz::VectorProperty* this_flag_property_ = new rviz::VectorProperty( "Flag " + QString::number(size-1));
        getPropertyContainer()->takeChild(this_flag_property_);
        getPropertyContainer()->removeChildren(size-1, 1);
        current_flag_property_->setName("Flag " + QString::number(size-1));

        if(ros::ok())
        {
          flag_.points.erase(flag_.points.end()-1);
          flag_publisher_.publish(flag_);
        }
        flag_nodes_.erase(flag_nodes_.end()-1);
      }
    }

    if( event.middleDown() ) // delete all
    {
      int size = flag_nodes_.size();

      if(size != 0)
      {
        for( int i=0; i<size; i++ )
        {
          scene_manager_->destroySceneNode(flag_nodes_[i]);
          rviz::VectorProperty* this_flag_property_ = new rviz::VectorProperty( "Flag " + QString::number(i));
          getPropertyContainer()->takeChild(this_flag_property_);
        }

        getPropertyContainer()->removeChildren(0, size);
        current_flag_property_->setName("Flag " + QString::number(0));

        if(ros::ok())
        {
          flag_.points.clear();
          flag_publisher_.publish(flag_);
        }
        flag_nodes_.clear();
      }
    }
  }
  else
  {
    moving_flag_node_->setVisible( false ); // If the mouse is not pointing at the ground plane, don't show the flag.
  }
  return Render;
}

// This is a helper function to create a new flag in the Ogre scene and save its scene node in a list.
void PublishFlagTool::makeFlag( const Ogre::Vector3& position )
{
  Ogre::SceneNode* node = scene_manager_->getRootSceneNode()->createChildSceneNode();
  Ogre::Entity* entity = scene_manager_->createEntity( flag_resource_ );
  node->attachObject( entity );
  node->setVisible( true );
  node->setPosition( position );

  if(ros::ok())
  {
    geometry_msgs::Point p;
    p.x = position.x;
    p.y = position.y;
    p.z = position.z;
    flag_.points.push_back(p);

    flag_publisher_.publish(flag_);
  }
  flag_nodes_.push_back( node );
}

void PublishFlagTool::flagCallback(const rviz_flag_plugin::PointArrayConstPtr &msg)
{
  for(int i=0; i<msg->points.size(); i++)
  {
    Ogre::Vector3 intersection;
    intersection.x = msg->points[i].x;
    intersection.y = msg->points[i].y;
    intersection.z = msg->points[i].z;

    current_flag_property_ = new rviz::VectorProperty( "Flag " + QString::number( flag_nodes_.size() ));
    current_flag_property_->setReadOnly( true );
    getPropertyContainer()->addChild( current_flag_property_ );

    current_flag_property_->setVector( intersection );
    current_flag_property_ = nullptr;

    makeFlag( intersection );
  }
}

void PublishFlagTool::visibleCallback(const std_msgs::BoolConstPtr &msg)
{
  for(int i=0; i<flag_nodes_.size(); i++)
  {
    flag_nodes_.at(i)->setVisible(msg->data);
  }
}

void PublishFlagTool::clearCallback(const std_msgs::BoolConstPtr &msg)
{
  if(msg->data == true)
  {
    int size = flag_nodes_.size();

    if(size != 0)
    {
      for( int i=0; i<size; i++ )
      {
        scene_manager_->destroySceneNode(flag_nodes_[i]);
        rviz::VectorProperty* this_flag_property_ = new rviz::VectorProperty( "Flag " + QString::number(i));
        getPropertyContainer()->takeChild(this_flag_property_);
      }

      getPropertyContainer()->removeChildren(0, size);

      if(ros::ok())
      {
        flag_.points.clear();
        flag_publisher_.publish(flag_);
      }
      flag_nodes_.clear();
    }
  }
}

// Loading and saving the flags
// ^^^^^^^^^^^^^^^^^^^^^^^^^^^^
//
// Tools with a fixed set of Property objects representing adjustable
// parameters are typically just created in the tool's constructor and
// added to the Property container (getPropertyContainer()).  In that
// case, the Tool subclass does not need to override load() and save()
// because the default behavior is to read all the Properties in the
// container from the Config object.
//
// Here however, we have a list of named flag positions of unknown
// length, so we need to implement save() and load() ourselves.
//
// We first save the class ID to the config object so the
// rviz::ToolManager will know what to instantiate when the config
// file is read back in.
void PublishFlagTool::save( rviz::Config config ) const
{
  config.mapSetValue( "Class", getClassId() );

  //  // The top level of this tool's Config is a map, but our flags
  //  // should go in a list, since they may or may not have unique keys.
  //  // Therefore we make a child of the map (``flags_config``) to store
  //  // the list.
  //  rviz::Config flags_config = config.mapMakeChild( "Flags" );

  //  // To read the positions and names of the flags, we loop over the
  //  // the children of our Property container:
  //  rviz::Property* container = getPropertyContainer();
  //  int num_children = container->numChildren();
  //  for( int i = 0; i < num_children; i++ )
  //  {
  //    rviz::Property* position_prop = container->childAt( i );
  //    // For each Property, we create a new Config object representing a
  //    // single flag and append it to the Config list.
  //    rviz::Config flag_config = flags_config.listAppendNew();
  //    // Into the flag's config we store its name:
  //    flag_config.mapSetValue( "Name", position_prop->getName() );
  //    // ... and its position.
  //    position_prop->save( flag_config );
  //  }
}

// In a tool's load() function, we don't need to read its class
// because that has already been read and used to instantiate the
// object before this can have been called.
void PublishFlagTool::load( const rviz::Config& config )
{
  // Here we get the "Flags" sub-config from the tool config and loop over its entries:
  rviz::Config flags_config = config.mapGetChild( "Flags" );
  int num_flags = flags_config.listLength();
  for( int i = 0; i < num_flags; i++ )
  {
    rviz::Config flag_config = flags_config.listChildAt( i );
    // At this point each ``flag_config`` represents a single flag.
    //
    // Here we provide a default name in case the name is not in the config file for some reason:
    QString name = "Flag " + QString::number( i + 1 );
    // Then we use the convenience function mapGetString() to read the
    // name from ``flag_config`` if it is there.  (If no "Name" entry
    // were present it would return false, but we don't care about
    // that because we have already set a default.)
    flag_config.mapGetString( "Name", &name );
    // Given the name we can create an rviz::VectorProperty to display the position:
    rviz::VectorProperty* prop = new rviz::VectorProperty( name );
    // Then we just tell the property to read its contents from the config, and we've read all the data.
    prop->load( flag_config );
    // We finish each flag by marking it read-only (as discussed
    // above), adding it to the property container, and finally making
    // an actual visible flag object in the 3D scene at the correct
    // position.
    prop->setReadOnly( true );
    getPropertyContainer()->addChild( prop );
    makeFlag( prop->getVector() );
  }
}

// End of .cpp file
// ^^^^^^^^^^^^^^^^
//
// At the end of every plugin class implementation, we end the
// namespace and then tell pluginlib about the class.  It is important
// to do this in global scope, outside our package's namespace.

} // end namespace rviz_plugin_tutorials

#include <pluginlib/class_list_macros.h>
PLUGINLIB_EXPORT_CLASS(rviz_flag_plugin::PublishFlagTool,rviz::Tool )
// END_TUTORIAL
