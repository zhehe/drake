#include <string>
#include <fstream>
#include <sstream>

#include "spruce.hh"

#include "drake/thirdParty/tinyxml2/tinyxml2.h"
#include "drake/systems/plants/RigidBodyTree.h"
#include "joints/DrakeJoints.h"

#include "drake/Path.h"
#include "xmlUtil.h"

// from http://stackoverflow.com/questions/478898/how-to-execute-a-command-and-get-output-of-command-within-c
#if defined(WIN32) || defined(WIN64)
  #define POPEN _popen
  #define PCLOSE _pclose
#else
  #define POPEN popen
  #define PCLOSE pclose
#endif

using namespace std;
using namespace Eigen;
using namespace tinyxml2;

void parseSDFInertial(shared_ptr<RigidBody> body, XMLElement* node, RigidBodyTree * model, Isometry3d& T_element_to_link)
{
  Isometry3d T = Isometry3d::Identity();
  XMLElement* pose = node->FirstChildElement("pose");
  if (pose) poseValueToTransform(pose, T);
  T = T_element_to_link*T;

  parseScalarValue(node,"mass",body->mass);

  body->com << T(0, 3), T(1, 3), T(2, 3);

  Matrix<double, TWIST_SIZE, TWIST_SIZE> I = Matrix<double, TWIST_SIZE, TWIST_SIZE>::Zero();
  I.block(3, 3, 3, 3) << body->mass * Matrix3d::Identity();

  XMLElement* inertia = node->FirstChildElement("inertia");
  if (inertia) {
    parseScalarValue(inertia, "ixx", I(0, 0));
    parseScalarValue(inertia, "ixy", I(0, 1));
    I(1, 0) = I(0, 1);
    parseScalarValue(inertia, "ixz", I(0, 2));
    I(2, 0) = I(0, 2);
    parseScalarValue(inertia, "iyy", I(1, 1));
    parseScalarValue(inertia, "iyz", I(1, 2));
    I(2, 1) = I(1, 2);
    parseScalarValue(inertia, "izz", I(2, 2));
  }

  auto bodyI = transformSpatialInertia(T, static_cast<Gradient<Isometry3d::MatrixType, Eigen::Dynamic>::type*>(NULL), I);
  body->I = bodyI.value();
}

void parseSDFLink(RigidBodyTree * model, XMLElement* node, const map<string, Vector4d, less<string>, aligned_allocator<pair<string, Vector4d> > >& materials, const map<string,string>& package_map, const string& root_dir)
{
  const char* attr = node->Attribute("drake_ignore");
  if (attr && strcmp(attr, "true") == 0) return;

  shared_ptr<RigidBody> body(new RigidBody());

  attr = node->Attribute("name");
  if (!attr) throw runtime_error("ERROR: link tag is missing name attribute");
  body->linkname = attr;

  if (body->linkname == "world") throw runtime_error("ERROR: do not name a link 'world', it is a reserved name");

  Isometry3d T_element_to_link = Isometry3d::Identity();
  XMLElement* pose = node->FirstChildElement("pose");
  if (pose) poseValueToTransform(pose, T_element_to_link);

  XMLElement* inertial_node = node->FirstChildElement("inertial");
  if (inertial_node) parseSDFInertial(body, inertial_node, model, T_element_to_link);

/*
  for (XMLElement* visual_node = node->FirstChildElement("visual"); visual_node; visual_node = visual_node->NextSiblingElement("visual")) {
    parseVisual(body, visual_node, model, materials, package_map, root_dir);
  }

  for (XMLElement* collision_node = node->FirstChildElement("collision"); collision_node; collision_node = collision_node->NextSiblingElement("collision")) {
    parseCollision(body, collision_node, model, package_map, root_dir);
  }
*/

  model->bodies.push_back(body);
  body->body_index = static_cast<int>(model->bodies.size()) - 1;
}

template <typename JointType>
void setSDFLimits(XMLElement *node, FixedAxisOneDoFJoint<JointType> *fjoint) {
  XMLElement* limit_node = node->FirstChildElement("limit");
  if (fjoint != nullptr && limit_node) {
    double lower = -numeric_limits<double>::infinity(), upper = numeric_limits<double>::infinity();
    parseScalarAttribute(limit_node,"lower",lower);
    parseScalarAttribute(limit_node,"upper",upper);
    fjoint->setJointLimits(lower,upper);
  }
}

template <typename JointType>
void setSDFDynamics(RigidBodyTree *model, XMLElement *node, FixedAxisOneDoFJoint<JointType> *fjoint) {
  XMLElement* dynamics_node = node->FirstChildElement("dynamics");
  if (fjoint != nullptr && dynamics_node) {
    double damping=0.0, coulomb_friction=0.0, coulomb_window=0.0;
    parseScalarValue(dynamics_node,"damping",damping);
    parseScalarValue(dynamics_node,"friction",coulomb_friction);
    parseScalarValue(dynamics_node,"coulomb_window",coulomb_window);  // note: not a part of the sdf spec
    fjoint->setDynamics(damping,coulomb_friction,coulomb_window);
  }
}

void parseSDFJoint(RigidBodyTree * model, XMLElement* node)
{
  const char* attr = node->Attribute("drake_ignore");
  if (attr && strcmp(attr, "true") == 0)
    return;

  attr = node->Attribute("name");
  if (!attr) throw runtime_error("ERROR: joint tag is missing name attribute");
  string name(attr);

  attr = node->Attribute("type");
  if (!attr) throw runtime_error("ERROR: joint " + name + " is missing the type attribute");
  string type(attr);

  // parse parent
  string parent_name;
  if (!parseStringValue(node,"parent",parent_name))
    throw runtime_error("ERROR: joint " + name + " doesn't have a parent node");

  auto parent = model->findLink(parent_name);
  if (!parent) throw runtime_error("ERROR: could not find parent link named " + parent_name);

  // parse child
  string child_name;
  if (!parseStringValue(node,"child",child_name))
    throw runtime_error("ERROR: joint " + name + " doesn't have a child node");

  auto child = model->findLink(child_name);
  if (!child) throw runtime_error("ERROR: could not find child link named " + child_name);

  Isometry3d Ttree = Isometry3d::Identity();
  XMLElement* pose = node->FirstChildElement("pose");
  if (pose) poseValueToTransform(pose, Ttree);

  Vector3d axis;
  axis << 1, 0, 0;
  XMLElement* axis_node = node->FirstChildElement("axis");
  if (axis_node && type.compare("fixed")!=0 && type.compare("floating")!=0) {
    parseVectorValue(axis_node, "xyz", axis);
    if (axis.norm()<1e-8) throw runtime_error("ERROR: axis is zero.  don't do that");
    axis.normalize();
    double in_parent_frame;
    if (parseScalarValue(axis_node,"in_parent_frame",in_parent_frame) && in_parent_frame>0.0)
      axis = Ttree*axis;
  }


  // now construct the actual joint (based on it's type)
  DrakeJoint* joint = nullptr;

  if (type.compare("revolute") == 0 || type.compare("continuous") == 0) {
    FixedAxisOneDoFJoint<RevoluteJoint>* fjoint = new RevoluteJoint(name, Ttree, axis);
    if (axis_node) {
      setSDFDynamics(model, axis_node, fjoint);
      setSDFLimits(axis_node, fjoint);
    }
    joint = fjoint;
  } else if (type.compare("fixed") == 0) {
    joint = new FixedJoint(name, Ttree);
  } else if (type.compare("prismatic") == 0) {
    FixedAxisOneDoFJoint<PrismaticJoint>* fjoint = new PrismaticJoint(name, Ttree, axis);
    if (axis_node) {
      setSDFDynamics(model, axis_node, fjoint);
      setSDFLimits(axis_node, fjoint);
    }
    joint = fjoint;
  } else if (type.compare("floating") == 0) {
    joint = new RollPitchYawFloatingJoint(name, Ttree);
  } else {
    throw runtime_error("ERROR: Unrecognized joint type: " + type);
  }

  unique_ptr<DrakeJoint> joint_unique_ptr(joint);
  child->setJoint(move(joint_unique_ptr));
  child->parent = parent;
}


void parseModel(RigidBodyTree * model, XMLElement* node, const map<string,string> package_map, const string &root_dir, const DrakeJoint::FloatingBaseType floating_base_type) {
/*
  if (!node->Attribute("name"))
    throw runtime_error("Error: your model must have a name attribute");
  string model_name = node->Attribute("name");
*/

  // parse material elements
  map<string, Vector4d, less<string>, aligned_allocator<pair<string, Vector4d> > > materials;

  // parse link elements
  for (XMLElement *link_node = node->FirstChildElement("link"); link_node; link_node = link_node->NextSiblingElement("link"))
    parseSDFLink(model, link_node, materials, package_map, root_dir);

  // parse joints
  for (XMLElement* joint_node = node->FirstChildElement("joint"); joint_node; joint_node = joint_node->NextSiblingElement("joint"))
    parseSDFJoint(model, joint_node);



  for (unsigned int i = 1; i < model->bodies.size(); i++) {
    if (model->bodies[i]->parent == nullptr) {  // attach the root nodes to the world with a floating base joint
      model->bodies[i]->parent = model->bodies[0];
      switch (floating_base_type) {
        case DrakeJoint::FIXED:
        {
          unique_ptr<DrakeJoint> joint(new FixedJoint("base", Isometry3d::Identity()));
          model->bodies[i]->setJoint(move(joint));
        }
          break;
        case DrakeJoint::ROLLPITCHYAW:
        {
          unique_ptr<DrakeJoint> joint(new RollPitchYawFloatingJoint("base", Isometry3d::Identity()));
          model->bodies[i]->setJoint(move(joint));
        }
          break;
        case DrakeJoint::QUATERNION:
        {
          unique_ptr<DrakeJoint> joint(new QuaternionFloatingJoint("base", Isometry3d::Identity()));
          model->bodies[i]->setJoint(move(joint));
        }
          break;
        default:
          throw std::runtime_error("unknown floating base type");
      }
    }
  }
}

void parseSDF(RigidBodyTree * model, XMLDocument * xml_doc, map<string,string>& package_map, const string &root_dir, const DrakeJoint::FloatingBaseType floating_base_type)
{
  populatePackageMap(package_map);

  XMLElement * node = xml_doc->FirstChildElement("sdf");
  if (!node) throw std::runtime_error("ERROR: This xml file does not contain an sdf tag");

  for (XMLElement *model_node = node->FirstChildElement("model"); model_node; model_node = model_node->NextSiblingElement("model"))
    parseModel(model, model_node, package_map, root_dir, floating_base_type);

  model->compile();
}


void RigidBodyTree::addRobotFromSDF(const string &urdf_filename, const DrakeJoint::FloatingBaseType floating_base_type)
{
  map<string,string> package_map;

  XMLDocument xml_doc;
  xml_doc.LoadFile(urdf_filename.data());
  if (xml_doc.ErrorID()) {
    throw std::runtime_error("failed to parse xml in file " + urdf_filename + "\n" + xml_doc.ErrorName());
  }

  string root_dir=".";
  size_t found = urdf_filename.find_last_of("/\\");
  if (found != string::npos) {
    root_dir = urdf_filename.substr(0, found);
  }

  parseSDF(this,&xml_doc,package_map,root_dir,floating_base_type);
}

