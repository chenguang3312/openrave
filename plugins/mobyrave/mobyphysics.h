// -*- coding: utf-8 -*-
// Copyright (c) 2015 James Taylor, Rosen Diankov
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.


#include <openrave/plugin.h>
//#include <boost/bind.hpp>

#include "mobyspace.h"

#include <Moby/TimeSteppingSimulator.h>
#include <Moby/EulerIntegrator.h>
#include <Moby/GravityForce.h>

class MobyPhysicsEngine : public PhysicsEngineBase
{

    inline boost::shared_ptr<MobyPhysicsEngine> shared_physics() {
        return boost::dynamic_pointer_cast<MobyPhysicsEngine>(shared_from_this());
    }

    inline boost::shared_ptr<MobyPhysicsEngine const> shared_physics_const() const {
        return boost::dynamic_pointer_cast<MobyPhysicsEngine const>(shared_from_this());
    }

    class PhysicsPropertiesXMLReader : public BaseXMLReader
    {
    public:
        PhysicsPropertiesXMLReader(boost::shared_ptr<MobyPhysicsEngine> physics, const AttributesList& atts) : _physics(physics) {
        }

        virtual ProcessElement startElement(const string& name, const AttributesList& atts) {
            if( !!_pcurreader ) {
                if( _pcurreader->startElement(name,atts) == PE_Support ) {
                    return PE_Support;
                }
                return PE_Ignore;
            }

            if( find(GetTags().begin(),GetTags().end(),name) == GetTags().end() ) {
                return PE_Pass;
            }
            _ss.str("");
            return PE_Support;
        }

        virtual bool endElement(const string& name)
        {
            if( name == "mobyproperties" )
                return true;
	    else if( name == "gains" ) {
                string jointid;
                dReal gains[3];

                _ss >> jointid;
                _ss >> gains[0] >> gains[1] >> gains[2];
        
                _physics->_mapJointGains.insert(pair<string, vector<dReal> >(jointid, vector<dReal>(gains, gains+3) ));

                //RAVELOG_INFO(str(boost::format("read joint[%s] gains[%f,%f,%f]\n") % jointid % gains[0] % gains[1] % gains[2]));
            }
            else {
                RAVELOG_ERROR("unknown field %s\n", name.c_str());
            }

            if( !_ss ) {
                RAVELOG_WARN(str(boost::format("error parsing %s\n")%name));
            }

            return false;
        }

        virtual void characters(const string& ch)
        {
            if( !!_pcurreader ) {
                _pcurreader->characters(ch);
            }
            else {
                _ss.clear();
                _ss << ch;
            }
        }

        static const boost::array<string, 1>& GetTags() {
        static const boost::array<string, 1> tags = {{"gains"}};
            return tags;
        }

protected:
        BaseXMLReaderPtr _pcurreader;
        boost::shared_ptr<MobyPhysicsEngine> _physics;
        stringstream _ss;
    };

public:

    static BaseXMLReaderPtr CreateXMLReader(InterfaceBasePtr ptr, const AttributesList& atts)
    {
    	return BaseXMLReaderPtr(new PhysicsPropertiesXMLReader(boost::dynamic_pointer_cast<MobyPhysicsEngine>(ptr),atts));
    }

    MobyPhysicsEngine(EnvironmentBasePtr penv, istream& sinput) : PhysicsEngineBase(penv), _StepSize(0.001), _space(new MobySpace(penv, GetPhysicsInfo, true)) 
    {
	stringstream ss;
	__description = ":Interface Authors: James Taylor and Rosen Diankov\n\nInterface to `Moby Physics Engine <https://github.com/PositronicsLab/Moby/>`_\n";

        dReal gains[3] = {1.0,1.0,1.0};  // default gain values
        _mapJointGains.insert(pair<string, vector<dReal> >("default", vector<dReal>(gains, gains+3) ));

        FOREACHC(it, PhysicsPropertiesXMLReader::GetTags()) {
            ss << "**" << *it << "**, ";
        }
        ss << "\n\n";
        RAVELOG_INFO( "processed xml\n" );
    }

    virtual ~MobyPhysicsEngine() 
    {

    }

    virtual bool InitEnvironment()
    {
        RAVELOG_INFO( "init Moby physics environment\n" );
        _space->SetSynchronizationCallback(boost::bind(&MobyPhysicsEngine::_SyncCallback, shared_physics(),_1));

        // +basic simulator
        //_sim.reset(new Moby::Simulator());
        //_sim->integrator = boost::shared_ptr<Moby::Integrator>(new Moby::EulerIntegrator());
        // -basic simulator

        // +simulator with constraints (limits and contact)
        _sim.reset(new Moby::TimeSteppingSimulator());
        // -simulator with constraints (limits and contact)

        if(!_space->InitEnvironment(_sim)) {
            return false;
        }

        // if the gravity force is uninitialized create the reference        
        if( !_space->_gravity ) {
            _space->_gravity.reset( new Moby::GravityForce());
        }     
   
        vector<KinBodyPtr> vbodies;
        GetEnv()->GetBodies(vbodies); 
        FOREACHC(itbody, vbodies) { 
            InitKinBody(*itbody);
        }

        SetGravity(_gravity);

        RAVELOG_INFO( "Moby physics environment created\n" );
        return true;
    }

    virtual void DestroyEnvironment()
    {
        vector<KinBodyPtr> vbodies;
        GetEnv()->GetBodies(vbodies);
        FOREACHC(itbody, vbodies) {
            (*itbody)->RemoveUserData("mobyphysics");
        }
        RAVELOG_INFO( "destroy Moby physics environment\n" );
        _space->DestroyEnvironment();

       // clean up any other resources here
       //_sim->reset();
    }

    virtual bool InitKinBody(KinBodyPtr pbody)
    {
        MobySpace::KinBodyInfoPtr pinfo = _space->InitKinBody(pbody);
        pbody->SetUserData("mobyphysics", pinfo);

        // set any body specific parameters here
        _space->MapGains(pbody, _mapJointGains);

        return !!pinfo;
    }


    virtual void RemoveKinBody(KinBodyPtr pbody)
    {
        if( !!pbody ) {
            pbody->RemoveUserData("mobyphysics");
        }
    }

    virtual bool SetPhysicsOptions(int physicsoptions)
                    //RAVELOG_INFO(str(boost::format("applying forces\n")));
    {
        _options = physicsoptions;
        return true;
    }

    virtual int GetPhysicsOptions() const
    {
        return _options;
    }

    virtual bool SetPhysicsOptions(ostream& sout, istream& sinput) {
        return false;
    }

    // Note: this implementation may not reflect a synchronized environment
    // Note: this implementation is only additive
    virtual bool SetBodyForce(KinBody::LinkPtr plink, const Vector& force, const Vector& position, bool bAdd)
    {
        //_space->Synchronize(KinBodyConstPtr(plink->GetParent()));
        Moby::RigidBodyPtr body = _space->GetLinkBody(plink);
        boost::shared_ptr<Ravelin::Pose3d> pose(new Ravelin::Pose3d(Ravelin::Quatd(0,0,0,1), _space->GetRavelinOrigin(position), Moby::GLOBAL));
                    //RAVELOG_INFO(str(boost::format("applying forces\n")));
        _space->AddImpulse(body, _space->GetRavelinSForce(force, Vector(0,0,0), pose));

        return true;
    }

    // Note: this implementation may not reflect a synchronized environment
    // Note: w.r.t to what reference frame?
    virtual bool SetLinkVelocity(KinBody::LinkPtr plink, const Vector& linearvel, const Vector& angularvel)
                    //RAVELOG_INFO(str(boost::format("applying forces\n")));
    {
        //_space->Synchronize(KinBodyConstPtr(plink->GetParent()));
        Moby::RigidBodyPtr body = _space->GetLinkBody(plink);
        if( !body )
        {
            return false;
        }

        boost::shared_ptr<Ravelin::Pose3d> pose(new Ravelin::Pose3d(Moby::GLOBAL));
        
        Ravelin::SVelocityd v(angularvel[0],angularvel[1],angularvel[2],linearvel[0],linearvel[1],linearvel[2], pose);

        _space->SetVelocity(body, v);

        return true;
    }

    // Note: this implementation may not reflect a synchronized environment
    // Note: w.r.t to what reference frame?
    virtual bool SetLinkVelocities(KinBodyPtr pbody, const vector<pair<Vector,Vector> >& velocities)
    {
        //_space->Synchronize(KinBodyConstPtr(plink->GetParent()));
        FOREACHC(itlink, pbody->GetLinks()) 
        {
            int idx = (*itlink)->GetIndex();
            Moby::RigidBodyPtr body = _space->GetLinkBody(*itlink);
            if(!!body) 
            {
                boost::shared_ptr<Ravelin::Pose3d> pose(new Ravelin::Pose3d(Moby::GLOBAL));
                Vector omega = velocities.at(idx).first;
                Vector dx = velocities.at(idx).second;

                Ravelin::SVelocityd v(omega[0],omega[1],omega[2],dx[0],dx[1],dx[2], pose);

                _space->SetVelocity(body, v);
                
            }
        }

        return true;
    }

    // Note: this implementation may not reflect a synchronized environment
    // Note: w.r.t to what reference frame?
    virtual bool GetLinkVelocity(KinBody::LinkConstPtr plink, Vector& linearvel, Vector& angularvel)
    {
        //_space->Synchronize(KinBodyConstPtr(plink->GetParent()));
        Moby::RigidBodyPtr body = _space->GetLinkBody(plink);
        if( !body )
        {
            return false;
        }

        Ravelin::SVelocityd svel = body->get_velocity();
        Ravelin::Vector3d dx = svel.get_linear();
        Ravelin::Vector3d omega = svel.get_angular();

        // what frame is the velocity w.r.t.
        //dx.update_relative_pose(Moby::GLOBAL);
        //omega.update_relative_pose(Moby::GLOBAL);

        linearvel = Vector(dx[0],dx[1],dx[2]);
        angularvel = Vector(omega[0],omega[1],omega[2]);
/*
        _space->Synchronize(KinBodyConstPtr(plink->GetParent()));
        boost::shared_ptr<btRigidBody> rigidbody = boost::dynamic_pointer_cast<btRigidBody>(_space->GetLinkBody(plink));
        if (!!rigidbody) {
            btVector3 pf = rigidbody->getLinearVelocity();
            linearvel = Vector(pf[0],pf[1],pf[2]);
            pf = rigidbody->getAngularVelocity();
            angularvel = Vector(pf[0],pf[1],pf[2]);
        }
        else {
            linearvel = angularvel = Vector(0,0,0);
        }
*/
        return true;
    }

    // Note: this implementation may not reflect a synchronized environment
    // Note: w.r.t to what reference frame?
    virtual bool GetLinkVelocities(KinBodyConstPtr pbody, vector<pair<Vector,Vector> >& velocities)
    {
        velocities.resize(0);
        velocities.resize(pbody->GetLinks().size());

        FOREACHC(itlink, pbody->GetLinks()) 
        {
            Moby::RigidBodyPtr body = _space->GetLinkBody(*itlink);
            if(!!body) 
            {
                Ravelin::SVelocityd svel = body->get_velocity();
                Ravelin::Vector3d dx = svel.get_linear();
                Ravelin::Vector3d omega = svel.get_angular();

                // what frame is the velocity w.r.t.
                //dx.update_relative_pose(Moby::GLOBAL);
                //omega.update_relative_pose(Moby::GLOBAL);

                velocities.at((*itlink)->GetIndex()).first = Vector(dx[0],dx[1],dx[2]);
                velocities.at((*itlink)->GetIndex()).second = Vector(omega[0],omega[1],omega[2]);
            }
        }

        return true;
    }

    // Note: neither in current physicsengine interface nor a python binding, came from bulletphysics
    virtual bool SetJointVelocity(KinBody::JointPtr pjoint, const vector<dReal>& pJointVelocity)
    {
        
        return false;
    }

    // Note: this implementation may not reflect a synchronized environment
    // Note: neither in current physicsengine interface nor a python binding, came from bulletphysics
    virtual bool GetJointVelocity(KinBody::JointConstPtr pjoint, vector<dReal>& pJointVelocity)
    {
        //_space->Synchronize(KinBodyConstPtr(plink->GetParent()));
        Moby::JointPtr joint = _space->GetJoint(pjoint);
        if( !joint )
        {
            return false;
        }

        Ravelin::VectorNd dq = (joint->qd);

        // what frame is the velocity w.r.t.

        pJointVelocity = vector<dReal>( dq.size() );
        for( unsigned i = 0; i < dq.size(); i++ )
        {
            pJointVelocity[i] = dq[i];
        }

        return true;
    }

    // Note: this implementation may not reflect a synchronized environment
    // Note: this implementation is only additive
    virtual bool AddJointTorque(KinBody::JointPtr pjoint, const vector<dReal>& pTorques)
    {
        //_space->Synchronize(KinBodyConstPtr(plink->GetParent()));
        Moby::JointPtr joint = _space->GetJoint(pjoint);
        _space->AddControl(joint, _space->GetRavelinVectorN(pTorques));

        return true;
    
    }

    // Note: this implementation may not reflect a synchronized environment
    // Note: this implementation is only additive
    virtual bool SetBodyTorque(KinBody::LinkPtr plink, const Vector& torque, bool bAdd)
    {
        //_space->Synchronize(KinBodyConstPtr(plink->GetParent()));
        Moby::RigidBodyPtr body = _space->GetLinkBody(plink);
        boost::shared_ptr<Ravelin::Pose3d> pose( new Ravelin::Pose3d( body->get_inertial_pose() ) );
        //boost::shared_ptr<Ravelin::Pose3d> pose( new Ravelin::Pose3d( body->get_pose() ) );
        _space->AddImpulse(body, _space->GetRavelinSForce(Vector(0,0,0), torque, pose));

        return true;
    }

    virtual bool GetLinkForceTorque(KinBody::LinkConstPtr plink, Vector& force, Vector& torque)
    {
        return false;
    }

    virtual bool GetJointForceTorque(KinBody::JointConstPtr pjoint, Vector& force, Vector& torque)
    {
        return false;
    }

    virtual void SetGravity(const Vector& gravity)
    {     
        // if gravity has not been initialized create the reference
        if(!_space->_gravity) 
        {
            _space->_gravity.reset( new Moby::GravityForce());
        }

        // update the Moby gravity force object
        _space->_gravity->gravity = Ravelin::Vector3d(gravity.x, gravity.y, gravity.z);
       
        // update the local OpenRave gravity variable  
        _gravity = gravity;
    }

    virtual Vector GetGravity()
    {
        return _gravity;
    }

    virtual void SimulateStep(dReal fTimeElapsed)
    {
        //+dbg
/*
        static bool first = true;
        if(first) {
            for(map<string,vector<dReal> >::iterator it = _space->_mapJointGains.begin(); it != _space->_mapJointGains.end(); it++) {
                RAVELOG_INFO(str(boost::format("gain[%s]: [%f,%f,%f]\n") % it->first % it->second[0] % it->second[1] % it->second[2]));
            }
            first = false;
        }
*/
        //-dbg

        // The requested fTimeElapsed may be large in comparison to a 
        // an integration step size that is accurate.  Current 
        // configuration dictates an fTimeElapsed of 1ms which is at 
        // the upper bound of accuracy for integration steps.  Some
        // logic should be emplaced to select for an accurate 
        // integration step if fTimeElapsed is set larger than 1ms
        // For now, assume fTimeElapsed is a resonable value for 
        // accurate integration

/*
        dReal endOfStepTime = ?;
        dReal t = ?;
        do {
            // compute the least sized step requested
            //dReal actualStep = fTimeElapsed < _StepSize ? fTimeElapsed : _StepSize;

            // if actualStep is equal to _StepSize, there may be some residual time             // after a number of steps so need to compute the last fragment of time 
            // after a number of steps so need to compute the last fragment of time 
            // as accurately as possible and therefore the above actualStep 
            // computation is too simplistic

            _sim->step( actualStep );
            t += actualStep;        // naive fp adding will have error here
        } while(t<endOfStepTime);
*/
        
        //RAVELOG_INFO( "attempting to step\n" );
        _sim->step(fTimeElapsed);

        // +dbg
        vector<Moby::DynamicBodyPtr> dbs = _sim->get_dynamic_bodies();
        //RAVELOG_INFO(str(boost::format("dbs.size[%u]\n") % dbs.size()));
        for(vector<Moby::DynamicBodyPtr>::iterator it=dbs.begin(); it!=dbs.end();it++) 
        {
            // attempt to cast
            Moby::RigidBodyPtr rb = boost::dynamic_pointer_cast<Moby::RigidBody>(*it);
            if(rb) {
                boost::shared_ptr<const Ravelin::Pose3d> pose = rb->get_mixed_pose();
                //RAVELOG_INFO(str(boost::format("x[%f,%f,%f]\n") % pose->x.x() % pose->x.y() % pose->x.z())); 
            }
        } 
        // -dbg

        vector<KinBodyPtr> vbodies;
        GetEnv()->GetBodies(vbodies);
        FOREACHC(itbody, vbodies) {
            MobySpace::KinBodyInfoPtr pinfo = GetPhysicsInfo(*itbody);
            //RAVELOG_INFO(str(boost::format("bodies.size[%u], links.size[%u]\n") % vbodies.size() % pinfo->vlinks.size())); 
            FOREACH(itlink, pinfo->vlinks) {
                Transform t = MobySpace::GetTransform(*(*itlink)->get_pose().get());
                (*itlink)->plink->SetTransform(t*(*itlink)->tlocal.inverse());

                 // +dbg
                 //double vt = _sim->current_time;
                 boost::shared_ptr<const Ravelin::Pose3d> pose = (*itlink)->get_pose();
                 //RAVELOG_INFO(str(boost::format("vt[%f], x[%f,%f,%f]\n") % vt % pose->x.x() % pose->x.y() % pose->x.z())); 
                 // -dbg
            }
            pinfo->nLastStamp = (*itbody)->GetUpdateStamp();
        }
        //RAVELOG_INFO( "completed step\n" );
    }

    dReal GetTime()
    {
        return _sim->current_time;
    }

    bool SendCommand(ostream& os, istream& is)
    {
        string cmd;
        is >> cmd;
        std::transform(cmd.begin(), cmd.end(), cmd.begin(), ::tolower);

        if( cmd == "setposition" ) {
            string jointname;
            unsigned axis;
            dReal value;

            for(unsigned i = 0; i < 3; i++)
            {
                if(i==0) {
                    is >> jointname;
                } else if(i==1) {
                    is >> axis;
                } else { // i==2
                    is >> value;
                }

                if( !is ) {
                    RAVELOG_WARN("setposition bad command\n");
                    return false;
                }
            }

            Moby::JointPtr joint = _space->GetJoint(jointname);
            if( !!joint )
            {
                _space->SetPosition(joint, axis, value);
                return true;
            }
            RAVELOG_WARN("setposition invalid joint\n");
            return false;
        }

        throw openrave_exception(str(boost::format(("command %s supported"))%cmd),OpenRAVE::ORE_CommandNotSupported);
        return false;
    }

    bool GetGains(RobotBasePtr probot, int dofIndex, vector<dReal>& gains) {
        map<KinBodyPtr, map<int, vector<dReal> > >::iterator bit;
        bit = _space->_mapGains.find(probot);
        if(bit == _space->_mapGains.end() )
        {
            //RAVELOG_INFO(str(boost::format("Could not locate gains for robot %s.\n") % probot->GetName() ));
            return false;
        }
        
        map<int, vector<dReal> >::iterator dit;    // dof iterator
        dit = bit->second.find(dofIndex);
        if(dit == bit->second.end() )
        {
            //RAVELOG_INFO(str(boost::format("Could not locate gains for dofIndex %d.\n") % dofIndex ));
            return false;
        }
     
        gains = dit->second;
        //RAVELOG_INFO(str(boost::format("Found gains [%f,%f,%f] for dofIndex %d.\n") % gains[0] % gains[1] % gains[2] % dofIndex ));
 
        return true;
    }

    dReal _StepSize;
    Vector _gravity;
    boost::shared_ptr<MobySpace> _space;

    map<string, vector<dReal> > _mapJointGains;

private:
    static MobySpace::KinBodyInfoPtr GetPhysicsInfo(KinBodyConstPtr pbody)
    {
        return boost::dynamic_pointer_cast<MobySpace::KinBodyInfo>(pbody->GetUserData("mobyphysics"));
    }

    void _SyncCallback(MobySpace::KinBodyInfoConstPtr pinfo)
    {
        Ravelin::SVelocityd zerov = Ravelin::SVelocityd::zero(Moby::GLOBAL);

        // reset dynamics
        FOREACH(itlink, pinfo->vlinks) {
            (*itlink)->set_velocity(zerov);
        }
    }

    int _options;
    boost::shared_ptr<Moby::Simulator> _sim; 
};

