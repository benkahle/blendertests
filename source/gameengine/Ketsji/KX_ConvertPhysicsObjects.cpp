/*
 * ***** BEGIN GPL LICENSE BLOCK *****
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * The Original Code is Copyright (C) 2001-2002 by NaN Holding BV.
 * All rights reserved.
 *
 * The Original Code is: all of this file.
 *
 * Contributor(s): none yet.
 *
 * ***** END GPL LICENSE BLOCK *****
 */

/** \file gameengine/Ketsji/KX_ConvertPhysicsObjects.cpp
 *  \ingroup ketsji
 */

#ifdef _MSC_VER
#  pragma warning (disable:4786)
#endif

#include "MT_assert.h"

#include "KX_SoftBodyDeformer.h"
#include "KX_ConvertPhysicsObject.h"
#include "BL_DeformableGameObject.h"
#include "RAS_MeshObject.h"
#include "KX_Scene.h"
#include "BL_System.h"

#include "PHY_Pro.h" //todo cleanup
#include "KX_ClientObjectInfo.h"

#include "CTR_Map.h"
#include "CTR_HashedPtr.h"

#include "KX_PhysicsEngineEnums.h"
#include "PHY_Pro.h"

#include "KX_MotionState.h" // bridge between motionstate and scenegraph node

extern "C"{
	#include "BLI_utildefines.h"
	#include "BKE_DerivedMesh.h"
}

#ifdef WITH_BULLET
#include "BulletSoftBody/btSoftBody.h"

#include "CcdPhysicsEnvironment.h"
#include "CcdPhysicsController.h"
#include "BulletCollision/BroadphaseCollision/btBroadphaseInterface.h"

#include "btBulletDynamicsCommon.h"

#ifdef WIN32
#if defined(_MSC_VER) && (_MSC_VER >= 1310)
//only use SIMD Hull code under Win32
//#define TEST_HULL 1
#ifdef TEST_HULL
#define USE_HULL 1
//#define TEST_SIMD_HULL 1

#include "NarrowPhaseCollision/Hull.h"
#endif //#ifdef TEST_HULL

#endif //_MSC_VER 
#endif //WIN32



// forward declarations

void	KX_ConvertBulletObject(	class	KX_GameObject* gameobj,
	class	RAS_MeshObject* meshobj,
	struct  DerivedMesh* dm,
	class	KX_Scene* kxscene,
	struct	PHY_ShapeProps* shapeprops,
	struct	PHY_MaterialProps*	smmaterial,
	struct	KX_ObjectProperties*	objprop)
{

	CcdPhysicsEnvironment* env = (CcdPhysicsEnvironment*)kxscene->GetPhysicsEnvironment();
	assert(env);
	

	bool isbulletdyna = false;
	bool isbulletsensor = false;
	bool isbulletchar = false;
	bool useGimpact = false;
	CcdConstructionInfo ci;
	class PHY_IMotionState* motionstate = new KX_MotionState(gameobj->GetSGNode());
	class CcdShapeConstructionInfo *shapeInfo = new CcdShapeConstructionInfo();

	
	if (!objprop->m_dyna)
	{
		ci.m_collisionFlags |= btCollisionObject::CF_STATIC_OBJECT;
	}
	if (objprop->m_ghost)
	{
		ci.m_collisionFlags |= btCollisionObject::CF_NO_CONTACT_RESPONSE;
	}

	ci.m_MotionState = motionstate;
	ci.m_gravity = btVector3(0,0,0);
	ci.m_linearFactor = btVector3(objprop->m_lockXaxis? 0 : 1,
									objprop->m_lockYaxis? 0 : 1,
									objprop->m_lockZaxis? 0 : 1);
	ci.m_angularFactor = btVector3(objprop->m_lockXRotaxis? 0 : 1,
									objprop->m_lockYRotaxis? 0 : 1,
									objprop->m_lockZRotaxis? 0 : 1);
	ci.m_localInertiaTensor =btVector3(0,0,0);
	ci.m_mass = objprop->m_dyna ? shapeprops->m_mass : 0.f;
	ci.m_clamp_vel_min = shapeprops->m_clamp_vel_min;
	ci.m_clamp_vel_max = shapeprops->m_clamp_vel_max;
	ci.m_margin = objprop->m_margin;
	ci.m_stepHeight = objprop->m_character ? shapeprops->m_step_height : 0.f;
	ci.m_jumpSpeed = objprop->m_character ? shapeprops->m_jump_speed : 0.f;
	ci.m_fallSpeed = objprop->m_character ? shapeprops->m_fall_speed : 0.f;
	shapeInfo->m_radius = objprop->m_radius;
	isbulletdyna = objprop->m_dyna;
	isbulletsensor = objprop->m_sensor;
	isbulletchar = objprop->m_character;
	useGimpact = ((isbulletdyna || isbulletsensor) && !objprop->m_softbody);

	ci.m_localInertiaTensor = btVector3(ci.m_mass/3.f,ci.m_mass/3.f,ci.m_mass/3.f);
	
	btCollisionShape* bm = 0;

	switch (objprop->m_boundclass)
	{
	case KX_BOUNDSPHERE:
		{
			//float radius = objprop->m_radius;
			//btVector3 inertiaHalfExtents (
			//	radius,
			//	radius,
			//	radius);
			
			//blender doesn't support multisphere, but for testing:

			//bm = new MultiSphereShape(inertiaHalfExtents,,&trans.getOrigin(),&radius,1);
			shapeInfo->m_shapeType = PHY_SHAPE_SPHERE;
			bm = shapeInfo->CreateBulletShape(ci.m_margin);
			break;
		};
	case KX_BOUNDBOX:
		{
			shapeInfo->m_halfExtend.setValue(
				objprop->m_boundobject.box.m_extends[0],
				objprop->m_boundobject.box.m_extends[1],
				objprop->m_boundobject.box.m_extends[2]);

			shapeInfo->m_halfExtend /= 2.0;
			shapeInfo->m_halfExtend = shapeInfo->m_halfExtend.absolute();
			shapeInfo->m_shapeType = PHY_SHAPE_BOX;
			bm = shapeInfo->CreateBulletShape(ci.m_margin);
			break;
		};
	case KX_BOUNDCYLINDER:
		{
			shapeInfo->m_halfExtend.setValue(
				objprop->m_boundobject.c.m_radius,
				objprop->m_boundobject.c.m_radius,
				objprop->m_boundobject.c.m_height * 0.5f
			);
			shapeInfo->m_shapeType = PHY_SHAPE_CYLINDER;
			bm = shapeInfo->CreateBulletShape(ci.m_margin);
			break;
		}

	case KX_BOUNDCONE:
		{
			shapeInfo->m_radius = objprop->m_boundobject.c.m_radius;
			shapeInfo->m_height = objprop->m_boundobject.c.m_height;
			shapeInfo->m_shapeType = PHY_SHAPE_CONE;
			bm = shapeInfo->CreateBulletShape(ci.m_margin);
			break;
		}
	case KX_BOUNDPOLYTOPE:
		{
			shapeInfo->SetMesh(meshobj, dm,true);
			bm = shapeInfo->CreateBulletShape(ci.m_margin);
			break;
		}
	case KX_BOUNDCAPSULE:
		{
			shapeInfo->m_radius = objprop->m_boundobject.c.m_radius;
			shapeInfo->m_height = objprop->m_boundobject.c.m_height;
			shapeInfo->m_shapeType = PHY_SHAPE_CAPSULE;
			bm = shapeInfo->CreateBulletShape(ci.m_margin);
			break;
		}
	case KX_BOUNDMESH:
		{
			// mesh shapes can be shared, check first if we already have a shape on that mesh
			class CcdShapeConstructionInfo *sharedShapeInfo = CcdShapeConstructionInfo::FindMesh(meshobj, dm, false);
			if (sharedShapeInfo != NULL) 
			{
				shapeInfo->Release();
				shapeInfo = sharedShapeInfo;
				shapeInfo->AddRef();
			} else
			{
				shapeInfo->SetMesh(meshobj, dm, false);
			}

			// Soft bodies can benefit from welding, don't do it on non-soft bodies
			if (objprop->m_softbody)
			{
				shapeInfo->setVertexWeldingThreshold1(objprop->m_soft_welding); //todo: expose this to the UI
			}

			bm = shapeInfo->CreateBulletShape(ci.m_margin, useGimpact, !objprop->m_softbody);
			//should we compute inertia for dynamic shape?
			//bm->calculateLocalInertia(ci.m_mass,ci.m_localInertiaTensor);

			break;
		}
	case KX_BOUND_DYN_MESH:
		/* do nothing */
		break;
	}


//	ci.m_localInertiaTensor.setValue(0.1f,0.1f,0.1f);

	if (!bm)
	{
		delete motionstate;
		shapeInfo->Release();
		return;
	}

	//bm->setMargin(ci.m_margin);


		if (objprop->m_isCompoundChild)
		{
			//find parent, compound shape and add to it
			//take relative transform into account!
			CcdPhysicsController* parentCtrl = (CcdPhysicsController*)objprop->m_dynamic_parent->GetPhysicsController();
			assert(parentCtrl);
			CcdShapeConstructionInfo* parentShapeInfo = parentCtrl->GetShapeInfo();
			btRigidBody* rigidbody = parentCtrl->GetRigidBody();
			btCollisionShape* colShape = rigidbody->getCollisionShape();
			assert(colShape->isCompound());
			btCompoundShape* compoundShape = (btCompoundShape*)colShape;

			// compute the local transform from parent, this may include several node in the chain
			SG_Node* gameNode = gameobj->GetSGNode();
			SG_Node* parentNode = objprop->m_dynamic_parent->GetSGNode();
			// relative transform
			MT_Vector3 parentScale = parentNode->GetWorldScaling();
			parentScale[0] = MT_Scalar(1.0)/parentScale[0];
			parentScale[1] = MT_Scalar(1.0)/parentScale[1];
			parentScale[2] = MT_Scalar(1.0)/parentScale[2];
			MT_Vector3 relativeScale = gameNode->GetWorldScaling() * parentScale;
			MT_Matrix3x3 parentInvRot = parentNode->GetWorldOrientation().transposed();
			MT_Vector3 relativePos = parentInvRot*((gameNode->GetWorldPosition()-parentNode->GetWorldPosition())*parentScale);
			MT_Matrix3x3 relativeRot = parentInvRot*gameNode->GetWorldOrientation();

			shapeInfo->m_childScale.setValue(relativeScale[0],relativeScale[1],relativeScale[2]);
			bm->setLocalScaling(shapeInfo->m_childScale);
			shapeInfo->m_childTrans.getOrigin().setValue(relativePos[0],relativePos[1],relativePos[2]);
			float rot[12];
			relativeRot.getValue(rot);
			shapeInfo->m_childTrans.getBasis().setFromOpenGLSubMatrix(rot);

			parentShapeInfo->AddShape(shapeInfo);
			compoundShape->addChildShape(shapeInfo->m_childTrans,bm);
			//do some recalc?
			//recalc inertia for rigidbody
			if (!rigidbody->isStaticOrKinematicObject())
			{
				btVector3 localInertia;
				float mass = 1.f/rigidbody->getInvMass();
				compoundShape->calculateLocalInertia(mass,localInertia);
				rigidbody->setMassProps(mass,localInertia);
			}
			shapeInfo->Release();
			// delete motionstate as it's not used
			delete motionstate;
			return;
		}

		if (objprop->m_hasCompoundChildren)
		{
			// create a compound shape info
			CcdShapeConstructionInfo *compoundShapeInfo = new CcdShapeConstructionInfo();
			compoundShapeInfo->m_shapeType = PHY_SHAPE_COMPOUND;
			compoundShapeInfo->AddShape(shapeInfo);
			// create the compound shape manually as we already have the child shape
			btCompoundShape* compoundShape = new btCompoundShape();
			compoundShape->addChildShape(shapeInfo->m_childTrans,bm);
			// now replace the shape
			bm = compoundShape;
			shapeInfo->Release();
			shapeInfo = compoundShapeInfo;
		}






#ifdef TEST_SIMD_HULL
	if (bm->IsPolyhedral())
	{
		PolyhedralConvexShape* polyhedron = static_cast<PolyhedralConvexShape*>(bm);
		if (!polyhedron->m_optionalHull)
		{
			//first convert vertices in 'Point3' format
			int numPoints = polyhedron->GetNumVertices();
			Point3* points = new Point3[numPoints+1];
			//first 4 points should not be co-planar, so add central point to satisfy MakeHull
			points[0] = Point3(0.f,0.f,0.f);
			
			btVector3 vertex;
			for (int p=0;p<numPoints;p++)
			{
				polyhedron->GetVertex(p,vertex);
				points[p+1] = Point3(vertex.getX(),vertex.getY(),vertex.getZ());
			}

			Hull* hull = Hull::MakeHull(numPoints+1,points);
			polyhedron->m_optionalHull = hull;
		}

	}
#endif //TEST_SIMD_HULL


	ci.m_collisionShape = bm;
	ci.m_shapeInfo = shapeInfo;
	ci.m_friction = smmaterial->m_friction;//tweak the friction a bit, so the default 0.5 works nice
	ci.m_restitution = smmaterial->m_restitution;
	ci.m_physicsEnv = env;
	// drag / damping is inverted
	ci.m_linearDamping = 1.f - shapeprops->m_lin_drag;
	ci.m_angularDamping = 1.f - shapeprops->m_ang_drag;
	//need a bit of damping, else system doesn't behave well
	ci.m_inertiaFactor = shapeprops->m_inertia/0.4f;//defaults to 0.4, don't want to change behavior
	
	ci.m_do_anisotropic = shapeprops->m_do_anisotropic;
	ci.m_anisotropicFriction.setValue(shapeprops->m_friction_scaling[0],shapeprops->m_friction_scaling[1],shapeprops->m_friction_scaling[2]);


//////////
	//do Fh, do Rot Fh
	ci.m_do_fh = shapeprops->m_do_fh;
	ci.m_do_rot_fh = shapeprops->m_do_rot_fh;
	ci.m_fh_damping = smmaterial->m_fh_damping;
	ci.m_fh_distance = smmaterial->m_fh_distance;
	ci.m_fh_normal = smmaterial->m_fh_normal;
	ci.m_fh_spring = smmaterial->m_fh_spring;
	ci.m_radius = objprop->m_radius;
	
	
	///////////////////
	ci.m_gamesoftFlag = objprop->m_gamesoftFlag;
	ci.m_soft_linStiff = objprop->m_soft_linStiff;
	ci.m_soft_angStiff = objprop->m_soft_angStiff;		/* angular stiffness 0..1 */
	ci.m_soft_volume= objprop->m_soft_volume;			/* volume preservation 0..1 */

	ci.m_soft_viterations= objprop->m_soft_viterations;		/* Velocities solver iterations */
	ci.m_soft_piterations= objprop->m_soft_piterations;		/* Positions solver iterations */
	ci.m_soft_diterations= objprop->m_soft_diterations;		/* Drift solver iterations */
	ci.m_soft_citerations= objprop->m_soft_citerations;		/* Cluster solver iterations */

	ci.m_soft_kSRHR_CL= objprop->m_soft_kSRHR_CL;		/* Soft vs rigid hardness [0,1] (cluster only) */
	ci.m_soft_kSKHR_CL= objprop->m_soft_kSKHR_CL;		/* Soft vs kinetic hardness [0,1] (cluster only) */
	ci.m_soft_kSSHR_CL= objprop->m_soft_kSSHR_CL;		/* Soft vs soft hardness [0,1] (cluster only) */
	ci.m_soft_kSR_SPLT_CL= objprop->m_soft_kSR_SPLT_CL;	/* Soft vs rigid impulse split [0,1] (cluster only) */

	ci.m_soft_kSK_SPLT_CL= objprop->m_soft_kSK_SPLT_CL;	/* Soft vs rigid impulse split [0,1] (cluster only) */
	ci.m_soft_kSS_SPLT_CL= objprop->m_soft_kSS_SPLT_CL;	/* Soft vs rigid impulse split [0,1] (cluster only) */
	ci.m_soft_kVCF= objprop->m_soft_kVCF;			/* Velocities correction factor (Baumgarte) */
	ci.m_soft_kDP= objprop->m_soft_kDP;			/* Damping coefficient [0,1] */

	ci.m_soft_kDG= objprop->m_soft_kDG;			/* Drag coefficient [0,+inf] */
	ci.m_soft_kLF= objprop->m_soft_kLF;			/* Lift coefficient [0,+inf] */
	ci.m_soft_kPR= objprop->m_soft_kPR;			/* Pressure coefficient [-inf,+inf] */
	ci.m_soft_kVC= objprop->m_soft_kVC;			/* Volume conversation coefficient [0,+inf] */

	ci.m_soft_kDF= objprop->m_soft_kDF;			/* Dynamic friction coefficient [0,1] */
	ci.m_soft_kMT= objprop->m_soft_kMT;			/* Pose matching coefficient [0,1] */
	ci.m_soft_kCHR= objprop->m_soft_kCHR;			/* Rigid contacts hardness [0,1] */
	ci.m_soft_kKHR= objprop->m_soft_kKHR;			/* Kinetic contacts hardness [0,1] */

	ci.m_soft_kSHR= objprop->m_soft_kSHR;			/* Soft contacts hardness [0,1] */
	ci.m_soft_kAHR= objprop->m_soft_kAHR;			/* Anchors hardness [0,1] */
	ci.m_soft_collisionflags= objprop->m_soft_collisionflags;	/* Vertex/Face or Signed Distance Field(SDF) or Clusters, Soft versus Soft or Rigid */
	ci.m_soft_numclusteriterations= objprop->m_soft_numclusteriterations;	/* number of iterations to refine collision clusters*/

	////////////////////
	ci.m_collisionFilterGroup = 
		(isbulletsensor) ? short(CcdConstructionInfo::SensorFilter) :
		(isbulletdyna) ? short(CcdConstructionInfo::DefaultFilter) :
		(isbulletchar) ? short(CcdConstructionInfo::CharacterFilter) : 
		short(CcdConstructionInfo::StaticFilter);
	ci.m_collisionFilterMask = 
		(isbulletsensor) ? short(CcdConstructionInfo::AllFilter ^ CcdConstructionInfo::SensorFilter) :
		(isbulletdyna) ? short(CcdConstructionInfo::AllFilter) : 
		(isbulletchar) ? short(CcdConstructionInfo::AllFilter) :
		short(CcdConstructionInfo::AllFilter ^ CcdConstructionInfo::StaticFilter);
	ci.m_bRigid = objprop->m_dyna && objprop->m_angular_rigidbody;
	
	ci.m_contactProcessingThreshold = objprop->m_contactProcessingThreshold;//todo: expose this in advanced settings, just like margin, default to 10000 or so
	ci.m_bSoft = objprop->m_softbody;
	ci.m_bDyna = isbulletdyna;
	ci.m_bSensor = isbulletsensor;
	ci.m_bCharacter = isbulletchar;
	ci.m_bGimpact = useGimpact;
	MT_Vector3 scaling = gameobj->NodeGetWorldScaling();
	ci.m_scaling.setValue(scaling[0], scaling[1], scaling[2]);
	CcdPhysicsController* physicscontroller = new CcdPhysicsController(ci);
	// shapeInfo is reference counted, decrement now as we don't use it anymore
	if (shapeInfo)
		shapeInfo->Release();

	gameobj->SetPhysicsController(physicscontroller,isbulletdyna);

	// record animation for dynamic objects
	if (isbulletdyna)
		gameobj->SetRecordAnimation(true);

	// don't add automatically sensor object, they are added when a collision sensor is registered
	if (!isbulletsensor && objprop->m_in_active_layer)
	{
		env->AddCcdPhysicsController( physicscontroller);
	}
	physicscontroller->SetNewClientInfo(gameobj->getClientInfo());
	{
		btRigidBody* rbody = physicscontroller->GetRigidBody();

		if (rbody)
		{
			if (objprop->m_angular_rigidbody)
			{
				rbody->setLinearFactor(ci.m_linearFactor);
				rbody->setAngularFactor(ci.m_angularFactor);
			}

			if (rbody && objprop->m_disableSleeping)
			{
				rbody->setActivationState(DISABLE_DEACTIVATION);
			}
		}
	}

	CcdPhysicsController* parentCtrl = objprop->m_dynamic_parent ? (CcdPhysicsController*)objprop->m_dynamic_parent->GetPhysicsController() : 0;
	physicscontroller->SetParentCtrl(parentCtrl);

	
	//Now done directly in ci.m_collisionFlags so that it propagates to replica
	//if (objprop->m_ghost)
	//{
	//	rbody->setCollisionFlags(rbody->getCollisionFlags() | btCollisionObject::CF_NO_CONTACT_RESPONSE);
	//}
	
	if (objprop->m_dyna && !objprop->m_angular_rigidbody)
	{
#if 0
		//setting the inertia could achieve similar results to constraint the up
		//but it is prone to instability, so use special 'Angular' constraint
		btVector3 inertia = physicscontroller->GetRigidBody()->getInvInertiaDiagLocal();
		inertia.setX(0.f);
		inertia.setZ(0.f);

		physicscontroller->GetRigidBody()->setInvInertiaDiagLocal(inertia);
		physicscontroller->GetRigidBody()->updateInertiaTensor();
#endif

		//env->createConstraint(physicscontroller,0,PHY_ANGULAR_CONSTRAINT,0,0,0,0,0,1);
	
		//Now done directly in ci.m_bRigid so that it propagates to replica
		//physicscontroller->GetRigidBody()->setAngularFactor(0.f);
		;
	}

	bool isActor = objprop->m_isactor;
	gameobj->getClientInfo()->m_type = 
		(isbulletsensor) ? ((isActor) ? KX_ClientObjectInfo::OBACTORSENSOR : KX_ClientObjectInfo::OBSENSOR) :
		(isActor) ? KX_ClientObjectInfo::ACTOR : KX_ClientObjectInfo::STATIC;

	// should we record animation for this object?
	if (objprop->m_record_animation)
		gameobj->SetRecordAnimation(true);

	// store materialname in auxinfo, needed for touchsensors
	if (meshobj)
	{
		const STR_String& matname=meshobj->GetMaterialName(0);
		gameobj->getClientInfo()->m_auxilary_info = (matname.Length() ? (void*)(matname.ReadPtr()+2) : NULL);
	} else
	{
		gameobj->getClientInfo()->m_auxilary_info = 0;
	}



	STR_String materialname;
	if (meshobj)
		materialname = meshobj->GetMaterialName(0);


#if 0
	///test for soft bodies
	if (objprop->m_softbody && physicscontroller)
	{
		btSoftBody* softBody = physicscontroller->GetSoftBody();
		if (softBody && gameobj->GetMesh(0))//only the first mesh, if any
		{
			//should be a mesh then, so add a soft body deformer
			KX_SoftBodyDeformer* softbodyDeformer = new KX_SoftBodyDeformer( gameobj->GetMesh(0),(BL_DeformableGameObject*)gameobj);
			gameobj->SetDeformer(softbodyDeformer);
		}
	}
#endif

}


void	KX_ClearBulletSharedShapes()
{
}

/* Refresh the physics object from either an object or a mesh.
 * gameobj must be valid
 * from_gameobj and from_meshobj can be NULL
 * 
 * when setting the mesh, the following vars get priority
 * 1) from_meshobj - creates the phys mesh from RAS_MeshObject
 * 2) from_gameobj - creates the phys mesh from the DerivedMesh where possible, else the RAS_MeshObject
 * 3) gameobj - update the phys mesh from DerivedMesh or RAS_MeshObject
 * 
 * Most of the logic behind this is in shapeInfo->UpdateMesh(...)
 */
bool KX_ReInstanceBulletShapeFromMesh(KX_GameObject *gameobj, KX_GameObject *from_gameobj, RAS_MeshObject* from_meshobj)
{
	CcdPhysicsController	*spc= static_cast<CcdPhysicsController*>(gameobj->GetPhysicsController());
	CcdShapeConstructionInfo	*shapeInfo;

	/* if this is the child of a compound shape this can happen
	 * don't support compound shapes for now */
	if (spc==NULL)
		return false;
	
	shapeInfo = spc->GetShapeInfo();
	
	if (shapeInfo->m_shapeType != PHY_SHAPE_MESH/* || spc->GetSoftBody()*/)
		return false;
	
	spc->DeleteControllerShape();
	
	if (from_gameobj==NULL && from_meshobj==NULL)
		from_gameobj= gameobj;
	
	/* updates the arrays used for making the new bullet mesh */
	shapeInfo->UpdateMesh(from_gameobj, from_meshobj);

	/* create the new bullet mesh */
	CcdConstructionInfo& cci = spc->GetConstructionInfo();
	btCollisionShape* bm= shapeInfo->CreateBulletShape(cci.m_margin, cci.m_bGimpact, !cci.m_bSoft);

	spc->ReplaceControllerShape(bm);
	return true;
}
#endif // WITH_BULLET
