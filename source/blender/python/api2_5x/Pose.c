/* 
 * $Id:
 *
 * ***** BEGIN GPL/BL DUAL LICENSE BLOCK *****
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version. The Blender
 * Foundation also sells licenses for use in proprietary software under
 * the Blender License.  See http://www.blender.org/BL/ for information
 * about this.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston, MA	02111-1307, USA.
 *
 * The Original Code is Copyright (C) 2001-2002 by NaN Holding BV.
 * All rights reserved.
 *
 * Contributor(s): Joseph Gilbert
 *
 * ***** END GPL/BL DUAL LICENSE BLOCK *****
*/

#include "Pose.h"


#include "mydevice.h"
#include "BKE_armature.h"
#include "BKE_main.h"
#include "BKE_global.h"
#include "BKE_action.h"
#include "BKE_utildefines.h"
#include "BIF_editaction.h"
#include "BIF_space.h"
#include "BIF_poseobject.h"
#include "BKE_depsgraph.h"
#include "DNA_object_types.h"
#include "DNA_ipo_types.h"
#include "DNA_scene_types.h"
#include "DNA_space_types.h"	//1 - this order
#include "BSE_editipo.h"			//2
#include "BLI_blenlib.h"
#include "BLI_arithb.h"
#include "Mathutils.h"
#include "Object.h"
#include "Constraint.h"
#include "NLA.h"
#include "gen_utils.h"
#include "gen_library.h"
#include "DNA_armature_types.h" /*used for pose bone select*/

extern void chan_calc_mat(bPoseChannel *chan);

//------------------------ERROR CODES---------------------------------
//This is here just to make me happy and to have more consistant error strings :)
static const char sPoseError[] = "Pose - Error: ";
//static const char sPoseBadArgs[] = "Pose - Bad Arguments: ";
static const char sPoseBoneError[] = "PoseBone - Error: ";
//static const char sPoseBoneBadArgs[] = "PoseBone - Bad Arguments: ";
static const char sPoseBonesDictError[] = "PoseBone - Error: ";
//static const char sPoseBonesDictBadArgs[] = "PoseBone - Bad Arguments: ";

//################## BPyPoseBoneDict_Type (internal) ########################
/*This is an internal psuedo-dictionary type that allows for manipulation
* of posechannels inside of a pose structure. It is a subobject of pose.
* i.e. Pose.bones['key']*/
//################################################################

//------------------METHOD IMPLEMENTATIONS-----------------------------
//------------------------Pose.bones.items()
//Returns a list of key:value pairs like dict.items()
static PyObject* PoseBonesDict_items(BPyPoseBoneDictObject *self)
{
	return PyDict_Items(self->bonesMap); 
}
//------------------------Pose.bones.keys()
//Returns a list of keys like dict.keys()
static PyObject* PoseBonesDict_keys(BPyPoseBoneDictObject *self)
{
	return PyDict_Keys(self->bonesMap);
}
//------------------------Armature.bones.values()
//Returns a list of values like dict.values()
static PyObject* PoseBonesDict_values(BPyPoseBoneDictObject *self)
{
	return PyDict_Values(self->bonesMap);
}
//------------------ATTRIBUTE IMPLEMENTATION---------------------------
//------------------TYPE_OBECT IMPLEMENTATION-----------------------
//------------------------tp_doc
//The __doc__ string for this object
static char BPyPoseBoneDict_doc[] = "This is an internal subobject of pose\
designed to act as a Py_PoseBone dictionary.";

//------------------------tp_methods
//This contains a list of all methods the object contains
static PyMethodDef BPyPoseBoneDict_methods[] = {
	{"items", (PyCFunction) PoseBonesDict_items, METH_NOARGS, 
		"() - Returns the key:value pairs from the dictionary"},
	{"keys", (PyCFunction) PoseBonesDict_keys, METH_NOARGS, 
		"() - Returns the keys the dictionary"},
	{"values", (PyCFunction) PoseBonesDict_values, METH_NOARGS, 
		"() - Returns the values from the dictionary"},
	{NULL, NULL, 0, NULL}
};
//-----------------(internal)
static int PoseBoneMapping_Init(PyObject *dictionary, ListBase *posechannels){
	bPoseChannel *pchan = NULL;
	PyObject *py_posechannel = NULL;

	for (pchan = posechannels->first; pchan; pchan = pchan->next){
		py_posechannel = PyPoseBone_FromPosechannel(pchan);
		if (!py_posechannel)
			return -1;

		if(PyDict_SetItemString(dictionary,
					pchan->name, py_posechannel) == -1){
			return -1;
		}
		Py_DECREF(py_posechannel);
	}
	return 0;
}

//----------------- BonesDict_InitBones
static int PoseBonesDict_InitBones(BPyPoseBoneDictObject *self)
{
	PyDict_Clear(self->bonesMap);
	if (PoseBoneMapping_Init(self->bonesMap, self->bones) == -1)
		return 0;
	return 1;
} 

//------------------------tp_repr
//This is the string representation of the object
static PyObject *PoseBonesDict_repr(BPyPoseBoneDictObject *self)
{
	char buffer[128], str[4096];
	PyObject *key, *value;
	int pos = 0;

	BLI_strncpy(str,"",4096);
	sprintf(buffer, "[Pose Bone Dict: {");
	strcat(str,buffer);
	while (PyDict_Next(self->bonesMap, &pos, &key, &value)) {
		sprintf(buffer, "%s : %s, ", PyString_AsString(key), 
			PyString_AsString(value->ob_type->tp_repr(value)));
		strcat(str,buffer);
	}
	sprintf(buffer, "}]\n");
	strcat(str,buffer);
	return PyString_FromString(str);
}

//------------------------tp_dealloc
//This tells how to 'tear-down' our object when ref count hits 0
static void PoseBonesDict_dealloc(BPyPoseBoneDictObject * self)
{
	Py_DECREF(self->bonesMap);
	BPyPoseBoneDict_Type.tp_free(self);
	return;
}
//------------------------mp_length
//This gets the size of the dictionary
static int PoseBonesDict_len(BPyPoseBoneDictObject *self)
{
	return BLI_countlist(self->bones);
}
//-----------------------mp_subscript
//This defines getting a bone from the dictionary - x = Bones['key']
static PyObject *PoseBonesDict_GetItem(BPyPoseBoneDictObject *self, PyObject* key)
{ 
	PyObject *value = NULL;

	value = PyDict_GetItem(self->bonesMap, key);
	if(value == NULL)
        Py_RETURN_NONE;
	
	return EXPP_incr_ret(value);
}
//------------------TYPE_OBECT DEFINITION--------------------------
//Mapping Protocol
static PyMappingMethods PoseBonesDict_MapMethods = {
	(inquiry) PoseBonesDict_len,					//mp_length
	(binaryfunc)PoseBonesDict_GetItem,		//mp_subscript
	0,														//mp_ass_subscript
};
//PoseBonesDict TypeObject
PyTypeObject BPyPoseBoneDict_Type = {
	PyObject_HEAD_INIT(NULL)			//tp_head
	0,												//tp_internal
	"PoseBonesDict",								//tp_name
	sizeof(BPyPoseBoneDictObject),					//tp_basicsize
	0,												//tp_itemsize
	(destructor)PoseBonesDict_dealloc,		//tp_dealloc
	0,												//tp_print
	0,												//tp_getattr
	0,												//tp_setattr
	0,												//tp_compare
	(reprfunc) PoseBonesDict_repr,				//tp_repr
	0,												//tp_as_number
	0,												//tp_as_sequence
	&PoseBonesDict_MapMethods,				//tp_as_mapping
	0,												//tp_hash
	0,												//tp_call
	0,												//tp_str
	0,												//tp_getattro
	0,												//tp_setattro
	0,												//tp_as_buffer
	Py_TPFLAGS_DEFAULT,					//tp_flags
	BPyPoseBoneDict_doc,						//tp_doc
	0,												//tp_traverse
	0,												//tp_clear
	0,												//tp_richcompare
	0,												//tp_weaklistoffset
	0,												//tp_iter
	0,												//tp_iternext
	BPyPoseBoneDict_methods,				//tp_methods
	0,												//tp_members
	0,												//tp_getset
	0,												//tp_base
	0,												//tp_dict
	0,												//tp_descr_get
	0,												//tp_descr_set
	0,												//tp_dictoffset
	0, 				                                //tp_init
	0,												//tp_alloc
	0,												//tp_new
	0,												//tp_free
	0,												//tp_is_gc
	0,												//tp_bases
	0,												//tp_mro
	0,												//tp_cache
	0,												//tp_subclasses
	0,												//tp_weaklist
	0												//tp_del
};
//-----------------------PyPoseBonesDict_FromPyPose
static PyObject *PyPoseBonesDict_FromPyPose(BPyPoseObject *py_pose)
{
	BPyPoseBoneDictObject *py_posebonesdict = NULL;

	//create py object
	py_posebonesdict = (BPyPoseBoneDictObject *)BPyPoseBoneDict_Type.tp_alloc(&BPyPoseBoneDict_Type, 0); 
	if (!py_posebonesdict)
		goto RuntimeError;

	//create internal dictionaries
	py_posebonesdict->bonesMap = PyDict_New();
	if (!py_posebonesdict->bonesMap)
		goto RuntimeError;

	//set listbase pointer
	py_posebonesdict->bones = &py_pose->bpyob->object->pose->chanbase;

	//now that everything is setup - init the mappings
	if (!PoseBonesDict_InitBones(py_posebonesdict))
		goto RuntimeError;

	return (PyObject*)py_posebonesdict;

RuntimeError:
	return EXPP_objError(PyExc_RuntimeError, "%s%s", 
		sPoseBonesDictError, "Failed to create class");
}

//################## BPyPose_Type ##########################
/*This type is a wrapper for a pose*/
//####################################################
//------------------METHOD IMPLEMENTATIONS------------------------------
static PyObject *Pose_update(BPyPoseObject *self)
{
	self->bpyob->object->pose->flag |= POSE_RECALC;
	where_is_pose(self->bpyob->object);

	Py_RETURN_NONE;
}
//------------------------tp_methods
//This contains a list of all methods the object contains
static PyMethodDef BPyPose_methods[] = {
	{"update", (PyCFunction) Pose_update, METH_NOARGS, 
		"() - Rebuilds the pose with new values"},
	{NULL, NULL, 0, NULL}
};
//------------------ATTRIBUTE IMPLEMENTATIONS---------------------------
//------------------------Pose.bones (getter)
//Gets the bones attribute
static PyObject *Pose_getBoneDict(BPyPoseObject *self, void *closure)
{
    return EXPP_incr_ret((PyObject*)self->Bones);
}
//------------------------Pose.bones (setter)
//Sets the bones attribute
static int Pose_setBoneDict(BPyPoseObject *self, PyObject *value, void *closure)
{
	goto AttributeError;

AttributeError:
	return EXPP_intError(PyExc_AttributeError, "%s%s", 
		sPoseError, "You are not allowed to change the .bones attribute");
}
//------------------TYPE_OBECT IMPLEMENTATION---------------------------
//------------------------tp_getset
//This contains methods for attributes that require checking
static PyGetSetDef BPyPose_getset[] = {
	{"bones", (getter)Pose_getBoneDict, (setter)Pose_setBoneDict, 
		"The pose's Bone dictionary", NULL},
	{NULL}
};
//------------------------tp_dealloc
//This tells how to 'tear-down' our object when ref count hits 0
static void Pose_dealloc(BPyPoseObject *self)
{
	Py_DECREF(self->bpyob);
	Py_DECREF(self->Bones);
	BPyPose_Type.tp_free(self);
	return;
}
//------------------------tp_cmp
//This compares 2 pose types
static int Pose_compare(BPyPoseObject *a, BPyPoseObject *b )
{
	return ( a->bpyob->object== b->bpyob->object ) ? 0 : -1;
}
//------------------------tp_repr
//This is the string representation of the object
static PyObject *Pose_repr(BPyPoseObject *self)
{
	return PyString_FromFormat( "[Pose \"%s\"]", self->bpyob->object->id.name); 
}
//------------------------tp_doc
//The __doc__ string for this object
static char BPyPose_doc[] = "This object wraps a Blender Pose object.";

//------------------TYPE_OBECT DEFINITION--------------------------
PyTypeObject BPyPose_Type = {
	PyObject_HEAD_INIT(NULL)   //tp_head
	0,										//tp_internal
	"Pose",									//tp_name
	sizeof(BPyPoseObject),						//tp_basicsize
	0,										//tp_itemsize
	(destructor)Pose_dealloc,				//tp_dealloc
	0,										//tp_print
	0,										//tp_getattr
	0,										//tp_setattr
	(cmpfunc)Pose_compare,					//tp_compare
	(reprfunc)Pose_repr,					//tp_repr
	0,										//tp_as_number
	0,										//tp_as_sequence
	0,										//tp_as_mapping
	0,										//tp_hash
	0,										//tp_call
	0,										//tp_str
	0,										//tp_getattro
	0,										//tp_setattro
	0,										//tp_as_buffer
	Py_TPFLAGS_DEFAULT,     			    //tp_flags
	BPyPose_doc,							//tp_doc
	0,										//tp_traverse
	0,										//tp_clear
	0,										//tp_richcompare
	0,										//tp_weaklistoffset
	0,										//tp_iter
	0,										//tp_iternext
	BPyPose_methods,						//tp_methods
	0,										//tp_members
	BPyPose_getset,							//tp_getset
	0,										//tp_base
	0,										//tp_dict
	0,										//tp_descr_get
	0,										//tp_descr_set
	0,										//tp_dictoffset
	0,										//tp_init
	0,										//tp_alloc
	0,										//tp_new
	0,										//tp_free
	0,										//tp_is_gc
	0,										//tp_bases
	0,										//tp_mro
	0,										//tp_cache
	0,										//tp_subclasses
	0,										//tp_weaklist
	0										//tp_del
};
//################## BPyPoseBone_Type #####################
/*This type is a wrapper for a posechannel*/
//####################################################
//------------------METHOD IMPLEMENTATIONS------------------------------
//------------------------------PoseBone.insertKey()
static PyObject *PoseBone_insertKey(BPyPoseBoneObject *self, PyObject *args)
{
	PyObject *parent_object = NULL;
	PyObject *constants = NULL, *item = NULL;
	int frame = 1, oldframe, length, x, numeric_value = 0, oldflag, no_ipo_update = 0;
	bPoseChannel *pchan = NULL;
	

	if (!PyArg_ParseTuple(args, "O!i|Oi", &BPyObject_Type, &parent_object, &frame, &constants, &no_ipo_update ))
		goto AttributeError;

	
	/* incase we ever have a value other then 1 for fast */
	if (no_ipo_update)
		no_ipo_update = 1;
	
	//verify that this pchannel is part of the object->pose
	for (pchan = ((BPyObject*)parent_object)->object->pose->chanbase.first; 
		pchan; pchan = pchan->next){
		if (pchan == self->posechannel)
			break;
	}
	if (!pchan)
		goto AttributeError2;

	//verify that there is an action bound to this object
	if (!((BPyObject*)parent_object)->object->action){
		goto AttributeError5;
	}

	oldflag = self->posechannel->flag;
	self->posechannel->flag = 0;
	//set the flags for this posechannel
	if (constants){
		if(PySequence_Check(constants)){
			length = PySequence_Length(constants);
			for (x = 0; x < length; x++){
				item = PySequence_GetItem(constants, x);
				if (item == EXPP_GetModuleConstant("Blender.Object.Pose", "ROT")){
					numeric_value |= POSE_ROT;
				}else if (item == EXPP_GetModuleConstant("Blender.Object.Pose", "LOC")){
					numeric_value |= POSE_LOC;
				}else if (item == EXPP_GetModuleConstant("Blender.Object.Pose", "SCALE")){
					numeric_value |= POSE_SIZE;
				}else{
					Py_DECREF(item);
					self->posechannel->flag = (short)oldflag;
					goto AttributeError4;
				}
				Py_DECREF(item);
			}
			self->posechannel->flag = (short)numeric_value;
		}else{
			goto AttributeError3;
		}
	}else{ //nothing passed so set them all
		self->posechannel->flag |= POSE_ROT;
		self->posechannel->flag |= POSE_LOC;
		self->posechannel->flag |= POSE_SIZE;
	}

	//set the frame we want insertion on
	oldframe = G.scene->r.cfra;
	G.scene->r.cfra = frame;

	//add the action channel if it's not there
	verify_action_channel(((BPyObject*)parent_object)->object->action, 
		self->posechannel->name);

	//insert the pose keys
	if (self->posechannel->flag & POSE_ROT){
		insertkey(&((BPyObject*)parent_object)->object->id, 
			ID_PO, self->posechannel->name, NULL, AC_QUAT_X, no_ipo_update);
		insertkey(&((BPyObject*)parent_object)->object->id, 
			ID_PO, self->posechannel->name, NULL, AC_QUAT_Y, no_ipo_update);
		insertkey(&((BPyObject*)parent_object)->object->id, 
			ID_PO, self->posechannel->name, NULL, AC_QUAT_Z, no_ipo_update);
		insertkey(&((BPyObject*)parent_object)->object->id, 
			ID_PO, self->posechannel->name, NULL, AC_QUAT_W, no_ipo_update);
	}
	if (self->posechannel->flag & POSE_LOC){
		insertkey(&((BPyObject*)parent_object)->object->id, 
			ID_PO, self->posechannel->name, NULL, AC_LOC_X, no_ipo_update);
		insertkey(&((BPyObject*)parent_object)->object->id, 
			ID_PO, self->posechannel->name, NULL, AC_LOC_Y, no_ipo_update);
		insertkey(&((BPyObject*)parent_object)->object->id, 
			ID_PO, self->posechannel->name, NULL, AC_LOC_Z, no_ipo_update);
	}
	if (self->posechannel->flag & POSE_SIZE){
		insertkey(&((BPyObject*)parent_object)->object->id, 
			ID_PO, self->posechannel->name, NULL, AC_SIZE_X, no_ipo_update);
		insertkey(&((BPyObject*)parent_object)->object->id, 
			ID_PO, self->posechannel->name, NULL, AC_SIZE_Y, no_ipo_update);
		insertkey(&((BPyObject*)parent_object)->object->id, 
			ID_PO, self->posechannel->name, NULL, AC_SIZE_Z, no_ipo_update);
	}

	//flip the frame back
	G.scene->r.cfra = oldframe;

	//update the IPOs
	if (no_ipo_update==0)
		remake_action_ipos (((BPyObject*)parent_object)->object->action);

	Py_RETURN_NONE;

AttributeError:
	return EXPP_objError(PyExc_AttributeError, "%s%s%s",
		sPoseBoneError, ".insertKey: ", "expects an Object, int, (optional) constants");
AttributeError2:
	return EXPP_objError(PyExc_AttributeError, "%s%s%s",
		sPoseBoneError, ".insertKey: ", "wrong object detected. \
		Use the object this pose came from");
AttributeError3:
	return EXPP_objError(PyExc_AttributeError, "%s%s%s",
		sPoseBoneError, ".insertKey: ", "Expects a constant or list of constants");
AttributeError4:
	return EXPP_objError(PyExc_AttributeError, "%s%s%s",
		sPoseBoneError, ".insertKey: ", "Please use a constant defined in the Pose module");
AttributeError5:
	return EXPP_objError(PyExc_AttributeError, "%s%s%s",
		sPoseBoneError, ".insertKey: ", "You must set up and link an Action to this object first");
}
//------------------------tp_methods
//This contains a list of all methods the object contains
static PyMethodDef BPyPoseBone_methods[] = {
	{"insertKey", (PyCFunction) PoseBone_insertKey, METH_VARARGS, 
		"() - insert a key for this pose into an action"},
	{NULL, NULL, 0, NULL}
};
//------------------ATTRIBUTE IMPLEMENTATIONS---------------------------
//------------------------PoseBone.name (getter)
//Gets the name attribute
static PyObject *PoseBone_getName(BPyPoseBoneObject *self, void *closure)
{
    return PyString_FromString(self->posechannel->name);
}
//------------------------PoseBone.name (setter)
//Sets the name attribute
static int PoseBone_setName(BPyPoseBoneObject *self, PyObject *value, void *closure)
{
	char *name = "";

	if (!PyArg_Parse(value, "s", &name))
		goto AttributeError;

	BLI_strncpy(self->posechannel->name, name, 32);
	return 0;

AttributeError:
	return EXPP_intError(PyExc_AttributeError, "%s%s%s",
		sPoseBoneError, ".name: ", "expects a string");
}
//------------------------PoseBone.loc (getter)
//Gets the loc attribute
static PyObject *PoseBone_getLoc(BPyPoseBoneObject *self, void *closure)
{
    return Vector_CreatePyObject(self->posechannel->loc, 3, Py_None);
}
//------------------------PoseBone.loc (setter)
//Sets the loc attribute
static int PoseBone_setLoc(BPyPoseBoneObject *self, PyObject *value, void *closure)
{
	return EXPP_setVec3(value, &self->posechannel->loc[0], &self->posechannel->loc[1], &self->posechannel->loc[2]);
}
//------------------------PoseBone.size (getter)
//Gets the size attribute
static PyObject *PoseBone_getSize(BPyPoseBoneObject *self, void *closure)
{
    return Vector_CreatePyObject(self->posechannel->size, 3, (PyObject *)NULL);
}
//------------------------PoseBone.size (setter)
//Sets the size attribute
static int PoseBone_setSize(BPyPoseBoneObject *self, PyObject *value, void *closure)
{
	return EXPP_setVec3(value, &self->posechannel->size[0], &self->posechannel->size[1], &self->posechannel->size[2]);
}
//------------------------PoseBone.quat (getter)
//Gets the quat attribute
static PyObject *PoseBone_getQuat(BPyPoseBoneObject *self, void *closure)
{
    return Quat_CreatePyObject(self->posechannel->quat, (PyObject *)Py_None);
}
//------------------------PoseBone.quat (setter)
//Sets the quat attribute
static int PoseBone_setQuat(BPyPoseBoneObject *self, PyObject *value, void *closure)
{
	return EXPP_setVec4(value,	&self->posechannel->quat[0], &self->posechannel->quat[1],
								&self->posechannel->quat[2], &self->posechannel->quat[3]);
}
//------------------------PoseBone.localMatrix (getter)
//Gets the chan_mat
static PyObject *PoseBone_getLocalMatrix(BPyPoseBoneObject *self, void *closure)
{
    return Matrix_CreatePyObject((float*)self->posechannel->chan_mat, 4, 4, (PyObject *)NULL);
}
//------------------------PoseBone.localMatrix (setter)
//Sets the chan_mat 
static int PoseBone_setLocalMatrix(BPyPoseBoneObject *self, PyObject *value, void *closure)
{
	BPyMatrixObject *matrix = NULL;
	float size[3], quat[4], loc[3];
	float mat3[3][3], mat4[4][4];
	int matsize = 0;

	if (!PyArg_Parse(value, "O!", &BPyMatrix_Type, &matrix))
		goto AttributeError;

	if (matrix->rowSize == 3 && matrix->colSize == 3){
		matsize = 3;
		Mat3CpyMat3(mat3, (float(*)[3])*matrix->matrix);
	}else if (matrix->rowSize == 4 && matrix->colSize == 4){
		matsize = 4;
		Mat4CpyMat4(mat4, (float(*)[4])*matrix->matrix);
	}

	if (matsize != 3 && matsize != 4){
		goto AttributeError;
	}

	//get size and rotation
	if (matsize == 3){
		Mat3ToSize(mat3, size);
		Mat3Ortho(mat3);
		Mat3ToQuat(mat3, quat);
	}else if (matsize == 4){
		Mat4ToSize(mat4, size);
		Mat4Ortho(mat4);
		Mat4ToQuat(mat4, quat);
	}

	//get loc
	if (matsize == 4) {
		VECCOPY(loc, matrix->matrix[3]);
	}
	else {
		loc[0]= loc[1]= loc[2]= 0.0f;
	}

	//copy new attributes
	VECCOPY(self->posechannel->size, size);
	QUATCOPY(self->posechannel->quat, quat);
	if (matsize == 4){
		VECCOPY(self->posechannel->loc, loc);
	}

	//rebuild matrix
	chan_calc_mat(self->posechannel);
	return 0;

AttributeError:
	return EXPP_intError(PyExc_AttributeError, "%s%s%s",
		sPoseBoneError, ".localMatrix: ", "expects a 3x3 or 4x4 matrix object");
}
//------------------------PoseBone.poseMatrix (getter)
//Gets the pose_mat
static PyObject *PoseBone_getPoseMatrix(BPyPoseBoneObject *self, void *closure)
{
    return Matrix_CreatePyObject((float*)self->posechannel->pose_mat, 4, 4, (PyObject *)NULL);
}
//------------------------PoseBone.poseMatrix (setter)
//Sets the pose_mat
static int PoseBone_setPoseMatrix(BPyPoseBoneObject *self, BPyMatrixObject *value, void *closure)
{
	float delta_mat[4][4], quat[4]; /* rotation */
	float size[4]; /* size only */
	
	if( !BPyMatrix_Check( value ) )
		return EXPP_ReturnIntError( PyExc_TypeError,
									"expected matrix object as argument" );
	
	if( value->colSize != 4 || value->rowSize != 4 )
		return EXPP_ReturnIntError( PyExc_AttributeError,
			"matrix must be a 4x4 transformation matrix\n"
			"for example as returned by object.matrixWorld" );

	/* get bone-space cursor matrix and extract location */
	armature_mat_pose_to_bone(self->posechannel, (float (*)[4]) *value->matrix, delta_mat);
	
	/* Visual Location */
	VECCOPY(self->posechannel->loc, delta_mat[3]);

	/* Visual Size */
	Mat4ToSize(delta_mat, size);
	VECCOPY(self->posechannel->size, size);
	
	/* Visual Rotation */
	Mat4ToQuat(delta_mat, quat);
	QUATCOPY(self->posechannel->quat, quat);
	
	return 0;
}
//------------------------PoseBone.constraints (getter)
//Gets the constraints sequence
static PyObject *PoseBone_getConstraints(BPyPoseBoneObject *self, void *closure)
{
	return PoseConstraintSeq_CreatePyObject( self->posechannel );
}
//------------------------PoseBone.limitmin (getter)
//Gets the pose bone limitmin value
static PyObject *PoseBone_getLimitMin(BPyPoseBoneObject *self, void *closure)
{
	float mylimitmin[3];
	Object *obj = NULL;

	obj = Object_FromPoseChannel(self->posechannel);
	if (obj==NULL){
		return EXPP_ReturnPyObjError(PyExc_AttributeError, "Bone data is not found");
	}
	mylimitmin[0]=0.0f;
	mylimitmin[1]=0.0f;
	mylimitmin[2]=0.0f;
	if(pose_channel_in_IK_chain(obj, self->posechannel)){
		if ((self->posechannel->ikflag & BONE_IK_NO_XDOF)==0) {
			if ((self->posechannel->ikflag & BONE_IK_XLIMIT)) {
				mylimitmin[0] = self->posechannel->limitmin[0];
			}
		}
		if ((self->posechannel->ikflag & BONE_IK_NO_YDOF)==0) {
			if ((self->posechannel->ikflag & BONE_IK_YLIMIT)) {
				mylimitmin[1] = self->posechannel->limitmin[1];
			}
		}
		if ((self->posechannel->ikflag & BONE_IK_NO_ZDOF)==0) {
			if ((self->posechannel->ikflag & BONE_IK_ZLIMIT)) {
				mylimitmin[2] = self->posechannel->limitmin[2];
			}
		}
	}
	return Vector_CreatePyObject(mylimitmin, 3, (PyObject *)NULL);
}
//------------------------PoseBone.limitmin (setter)
//Sets the pose bone limitmin value
static int PoseBone_setLimitMin(BPyPoseBoneObject *self, PyObject *value, void *closure)
{
	float newlimitmin[3];
	int x;
	Object *obj = NULL;
	if(!PySequence_Check(value)){
		return EXPP_ReturnIntError(PyExc_AttributeError, "Argument is not a sequence");
	}
	if (PySequence_Size(value) !=3){
		return EXPP_ReturnIntError(PyExc_AttributeError, "Argument size must be 3");
	}
	newlimitmin[0]=0.0f;
	newlimitmin[1]=0.0f;
	newlimitmin[2]=0.0f;
	for (x = 0; x<3;x++){
		PyObject *item;
		item = PySequence_GetItem(value, x); //new reference
		if (PyFloat_Check(item)){
			newlimitmin[x] = (float)PyFloat_AsDouble(item);
		}else if (PyInt_Check(item)){
			newlimitmin[x] = (float)PyInt_AsLong(item);
		}
		Py_DECREF(item);
	}
	obj = Object_FromPoseChannel(self->posechannel);
	if (obj==NULL){
		return EXPP_ReturnIntError(PyExc_AttributeError, "Bone data is not found");
	}
	if(!pose_channel_in_IK_chain(obj, self->posechannel)){
		return EXPP_ReturnIntError(PyExc_AttributeError, "Bone is not part of an IK chain");
	}
	if ((self->posechannel->ikflag & BONE_IK_NO_XDOF)==0) {
		if ((self->posechannel->ikflag & BONE_IK_XLIMIT)) {
			self->posechannel->limitmin[0] = EXPP_ClampFloat(newlimitmin[0], -180.0f, 0.0f);
		}
	}
	if ((self->posechannel->ikflag & BONE_IK_NO_YDOF)==0) {
		if ((self->posechannel->ikflag & BONE_IK_YLIMIT)) {
			self->posechannel->limitmin[1] = EXPP_ClampFloat(newlimitmin[1], -180.0f, 0.0f);
		}
	}
	if ((self->posechannel->ikflag & BONE_IK_NO_ZDOF)==0) {
		if ((self->posechannel->ikflag & BONE_IK_ZLIMIT)) {
			self->posechannel->limitmin[2] = EXPP_ClampFloat(newlimitmin[2], -180.0f, 0.0f);
		}
	}
	DAG_object_flush_update(G.scene, obj, OB_RECALC_DATA);
	return 0;
}

//------------------------PoseBone.limitmax (getter)
//Gets the pose bone limitmax value
static PyObject *PoseBone_getLimitMax(BPyPoseBoneObject *self, void *closure)
{
	float mylimitmax[3];
	Object *obj = NULL;

	obj = Object_FromPoseChannel(self->posechannel);
	if (obj==NULL){
		return EXPP_ReturnPyObjError(PyExc_AttributeError, "Bone data is not found");
	}
	mylimitmax[0]=0.0f;
	mylimitmax[1]=0.0f;
	mylimitmax[2]=0.0f;
	if(pose_channel_in_IK_chain(obj, self->posechannel)){
		if ((self->posechannel->ikflag & BONE_IK_NO_XDOF)==0) {
			if ((self->posechannel->ikflag & BONE_IK_XLIMIT)) {
				mylimitmax[0] = self->posechannel->limitmax[0];
			}
		}
		if ((self->posechannel->ikflag & BONE_IK_NO_YDOF)==0) {
			if ((self->posechannel->ikflag & BONE_IK_YLIMIT)) {
				mylimitmax[1] = self->posechannel->limitmax[1];
			}
		}
		if ((self->posechannel->ikflag & BONE_IK_NO_ZDOF)==0) {
			if ((self->posechannel->ikflag & BONE_IK_ZLIMIT)) {
				mylimitmax[2] = self->posechannel->limitmax[2];
			}
		}
	}
	return Vector_CreatePyObject(mylimitmax, 3, (PyObject *)NULL);
}
//------------------------PoseBone.limitmax (setter)
//Sets the pose bone limitmax value
static int PoseBone_setLimitMax(BPyPoseBoneObject *self, PyObject *value, void *closure)
{
	float newlimitmax[3];
	int x;
	Object *obj = NULL;
	if(!PySequence_Check(value)){
		return EXPP_ReturnIntError(PyExc_AttributeError, "Argument is not a sequence");
	}
	if (PySequence_Size(value) !=3){
		return EXPP_ReturnIntError(PyExc_AttributeError, "Argument size must be 3");
	}
	newlimitmax[0]=0.0f;
	newlimitmax[1]=0.0f;
	newlimitmax[2]=0.0f;
	for (x = 0; x<3;x++){
		PyObject *item;
		item = PySequence_GetItem(value, x); //new reference
		if (PyFloat_Check(item)){
			newlimitmax[x] = (float)PyFloat_AsDouble(item);
		}else if (PyInt_Check(item)){
			newlimitmax[x] = (float)PyInt_AsLong(item);
		}
		Py_DECREF(item);
	}
	obj = Object_FromPoseChannel(self->posechannel);
	if (obj==NULL){
		return EXPP_ReturnIntError(PyExc_AttributeError, "Bone data is not found");
	}
	if(!pose_channel_in_IK_chain(obj, self->posechannel)){
		return EXPP_ReturnIntError(PyExc_AttributeError, "Bone is not part of an IK chain");
	}
	if ((self->posechannel->ikflag & BONE_IK_NO_XDOF)==0) {
		if ((self->posechannel->ikflag & BONE_IK_XLIMIT)) {
			self->posechannel->limitmax[0] = EXPP_ClampFloat(newlimitmax[0], 0.0f, 180.0f);
		}
	}
	if ((self->posechannel->ikflag & BONE_IK_NO_YDOF)==0) {
		if ((self->posechannel->ikflag & BONE_IK_YLIMIT)) {
			self->posechannel->limitmax[1] = EXPP_ClampFloat(newlimitmax[1], 0.0f, 180.0f);
		}
	}
	if ((self->posechannel->ikflag & BONE_IK_NO_ZDOF)==0) {
		if ((self->posechannel->ikflag & BONE_IK_ZLIMIT)) {
			self->posechannel->limitmax[2] = EXPP_ClampFloat(newlimitmax[2], 0.0f, 180.0f);
		}
	}
	DAG_object_flush_update(G.scene, obj, OB_RECALC_DATA);
	return 0;
}
//------------------------PoseBone.head (getter)
//Gets the pose head position
static PyObject *PoseBone_getHead(BPyPoseBoneObject *self, void *closure)
{
    return Vector_CreatePyObject(self->posechannel->pose_head, 3, (PyObject *)NULL);
}
//------------------------PoseBone.head (setter)
//Sets the pose head position
static int PoseBone_setHead(BPyPoseBoneObject *self, PyObject *value, void *closure)
{
	return EXPP_intError(PyExc_AttributeError, "%s%s%s",
		sPoseBoneError, ".head: ", "not able to set this property");
}
//------------------------PoseBone.tail (getter)
//Gets the pose tail position
static PyObject *PoseBone_getTail(BPyPoseBoneObject *self, void *closure)
{
    return Vector_CreatePyObject(self->posechannel->pose_tail, 3, (PyObject *)NULL);
}
//------------------------PoseBone.tail (setter)
//Sets the pose tail position
static int PoseBone_setTail(BPyPoseBoneObject *self, PyObject *value, void *closure)
{
	return EXPP_intError(PyExc_AttributeError, "%s%s%s",
		sPoseBoneError, ".tail: ", "not able to set this property");
}
//------------------------PoseBone.sel (getter)
//Gets the pose bones selection
static PyObject *PoseBone_getSelect(BPyPoseBoneObject *self, void *closure)
{
	if (self->posechannel->bone->flag & BONE_SELECTED)
		Py_RETURN_TRUE;
	else
		Py_RETURN_FALSE;
}
//------------------------PoseBone.sel (setter)
//Sets the pose bones selection
static int PoseBone_setSelect(BPyPoseBoneObject *self, PyObject *value, void *closure)
{
	if (PyObject_IsTrue( value ))
		self->posechannel->bone->flag |= BONE_SELECTED;
	else
		self->posechannel->bone->flag &= ~(BONE_SELECTED | BONE_ACTIVE);
	return 0;
}


//------------------------PoseBone.parent (getter)
//Gets the bones parent if any
static PyObject *PoseBone_getParent(BPyPoseBoneObject *self, void *closure)
{
	if (self->posechannel->parent)
		return PyPoseBone_FromPosechannel(self->posechannel->parent);
	else
        Py_RETURN_NONE;
}

//------------------------PoseBone.displayObject (getter)
//Gets the pose bones object used for display
static PyObject *PoseBone_getDisplayObject(BPyPoseBoneObject *self, void *closure)
{	/* custom==NULL -> None is ok */
	return Object_CreatePyObject(self->posechannel->custom);
}

//------------------------PoseBone.displayObject (setter)
//Sets the pose bones object used for display
static int PoseBone_setDisplayObject(BPyPoseBoneObject *self, PyObject *value, void *closure)
{
	return GenericLib_assignData(value, (void **) &self->posechannel->custom, 0, 0, ID_OB, 0);
}

//------------------------PoseBone.hasIK (getter)
//Returns True/False if the bone has IK's
static PyObject *PoseBone_hasIK(BPyPoseBoneObject *self, void *closure)
{
	Object *obj = NULL;
	
	obj = Object_FromPoseChannel(self->posechannel);
	if (obj==NULL)
		Py_RETURN_FALSE;
	
	if( pose_channel_in_IK_chain(obj, self->posechannel) )
		Py_RETURN_TRUE;
	
	Py_RETURN_FALSE;
}

//------------------------PoseBone.stretch (getter)
//Gets the pose bones IK Stretch value
static PyObject *PoseBone_getStretch(BPyPoseBoneObject *self, void *closure)
{
	return PyFloat_FromDouble( self->posechannel->ikstretch );
}

//------------------------PoseBone.stretch (setter)
//Sets the pose bones IK Stretch value
static int PoseBone_setStretch(BPyPoseBoneObject *self, PyObject *value, void *closure)
{
	float ikstretch;
	
	if( !PyNumber_Check( value ) )
		return EXPP_ReturnIntError( PyExc_TypeError,
					"expected float argument" );
	
	ikstretch = (float)PyFloat_AsDouble(value);
	if (ikstretch<0) ikstretch = 0.0;
	if (ikstretch>1) ikstretch = 1.0;
	self->posechannel->ikstretch = ikstretch;
	return 0;
}

//------------------------PoseBone.stiffX/Y/Z (getter)
//Gets the pose bones IK stiffness
static PyObject *PoseBone_getStiff(BPyPoseBoneObject *self, void *axis)
{
	return PyFloat_FromDouble( self->posechannel->stiffness[(int)axis] );
}

//------------------------PoseBone.stiffX/Y/Z (setter)
//Sets the pose bones IK stiffness
static int PoseBone_setStiff(BPyPoseBoneObject *self, PyObject *value, void *axis)
{
	float stiff;
	
	if( !PyNumber_Check( value ) )
		return EXPP_ReturnIntError( PyExc_TypeError,
					"expected float argument" );
	
	stiff = (float)PyFloat_AsDouble(value);
	if (stiff<0) stiff = 0;
	if (stiff>0.990) stiff = 0.990f;
	self->posechannel->stiffness[(int)axis] = stiff;
	return 0;
}

//------------------------PoseBone.* (getter)
//Gets the pose bones flag
/*
static PyObject *PoseBone_getFlag(BPyPoseBoneObject *self, void *flag)
{
	if (self->posechannel->flag & (int)flag)
		Py_RETURN_TRUE;
	else
		Py_RETURN_FALSE;
		
}
*/

//------------------------PoseBone.* (setter)
//Gets the pose bones flag
/*
static int PoseBone_setFlag(BPyPoseBoneObject *self, PyObject *value, void *flag)
{
	if ( PyObject_IsTrue(value) )
		self->posechannel->flag |= (int)flag;
	else
		self->posechannel->flag &= ~(int)flag;
	return 0;
}
*/

//------------------------PoseBone.* (getter)
//Gets the pose bones ikflag
static PyObject *PoseBone_getIKFlag(BPyPoseBoneObject *self, void *flag)
{
	if (self->posechannel->ikflag & (int)flag)
		Py_RETURN_TRUE;
	else
		Py_RETURN_FALSE;
		
}

//------------------------PoseBone.* (setter)
//Sets the pose bones ikflag
static int PoseBone_setIKFlag(BPyPoseBoneObject *self, PyObject *value, void *flag)
{
	if ( PyObject_IsTrue(value) )
		self->posechannel->ikflag |= (int)flag;
	else
		self->posechannel->ikflag &= ~(int)flag;
	return 0;
}

//------------------------Bone.layerMask (get)
static PyObject *PoseBone_getLayerMask(BPyPoseBoneObject *self)
{
	/* do this extra stuff because the short's bits can be negative values */
	unsigned short laymask = 0;
	laymask |= self->posechannel->bone->layer;
	return PyInt_FromLong((int)laymask);
}
//------------------------Bone.layerMask (set)
static int PoseBone_setLayerMask(BPyPoseBoneObject *self, PyObject *value)
{
	int laymask;
	if (!PyInt_Check(value)) {
		return EXPP_ReturnIntError( PyExc_AttributeError,
									"expected an integer (bitmask) as argument" );
	}
	
	laymask = PyInt_AsLong(value);

	if (laymask <= 0 || laymask > (1<<16) - 1)
		return EXPP_ReturnIntError( PyExc_AttributeError,
									"bitmask must have from 1 up to 16 bits set");

	self->posechannel->bone->layer = 0;
	self->posechannel->bone->layer |= laymask;

	return 0;
}

//------------------TYPE_OBECT IMPLEMENTATION---------------------------
//------------------------tp_getset
//This contains methods for attributes that require checking
static PyGetSetDef BPyPoseBone_getset[] = {
	{"name", (getter)PoseBone_getName, (setter)PoseBone_setName, 
		"The pose bone's name", NULL},
	{"loc", (getter)PoseBone_getLoc, (setter)PoseBone_setLoc, 
		"The pose bone's change in location as a vector", NULL},
	{"scale", (getter)PoseBone_getSize, (setter)PoseBone_setSize, 
		"The pose bone's change in scale as a vector", NULL},
	{"quat", (getter)PoseBone_getQuat, (setter)PoseBone_setQuat, 
		"The pose bone's change in rotation as a quat", NULL},
	{"localMatrix", (getter)PoseBone_getLocalMatrix, (setter)PoseBone_setLocalMatrix, 
		"The pose bone's change matrix built from the quat, loc, and scale", NULL},
	{"poseMatrix", (getter)PoseBone_getPoseMatrix, (setter)PoseBone_setPoseMatrix, 
		"The pose bone's matrix", NULL},
	{"head", (getter)PoseBone_getHead, (setter)PoseBone_setHead, 
		"The pose bone's head positon", NULL},
	{"tail", (getter)PoseBone_getTail, (setter)PoseBone_setTail, 
		"The pose bone's tail positon", NULL},
	{"sel", (getter)PoseBone_getSelect, (setter)PoseBone_setSelect, 
		"The pose selection state", NULL},
    {"limitMin", (getter)PoseBone_getLimitMin, (setter)PoseBone_setLimitMin,
        "The pose bone dof min", NULL},
    {"limitMax", (getter)PoseBone_getLimitMax, (setter)PoseBone_setLimitMax,
        "The pose bone dof max", NULL},
	{"constraints", (getter)PoseBone_getConstraints, (setter)NULL, 
		"The list of contraints that pertain to this pose bone", NULL},
	{"parent", (getter)PoseBone_getParent, (setter)NULL, 
		"The bones parent (read only for posebones)", NULL},
	{"displayObject", (getter)PoseBone_getDisplayObject, (setter)PoseBone_setDisplayObject, 
		"The poseMode object to draw in place of this bone", NULL},
	
	{"hasIK", (getter)PoseBone_hasIK, (setter)NULL, 
		"True if the pose bone has IK (readonly)", NULL },
	
	{"stretch", (getter)PoseBone_getStretch, (setter)PoseBone_setStretch, 
		"Stretch the bone to the IK Target", NULL },
		
	{"stiffX", (getter)PoseBone_getStiff, (setter)PoseBone_setStiff, 
		"bones stiffness on the X axis", (void *)0 },
	{"stiffY", (getter)PoseBone_getStiff, (setter)PoseBone_setStiff, 
		"bones stiffness on the Y axis", (void *)1 },
	{"stiffZ", (getter)PoseBone_getStiff, (setter)PoseBone_setStiff, 
		"bones stiffness on the Z axis", (void *)2 },
		
	{"limitX", (getter)PoseBone_getIKFlag, (setter)PoseBone_setIKFlag, 
		"limit rotation over X axis when part of an IK", (void *)BONE_IK_XLIMIT },
	{"limitY", (getter)PoseBone_getIKFlag, (setter)PoseBone_setIKFlag, 
		"limit rotation over Y axis when part of an IK", (void *)BONE_IK_YLIMIT },
	{"limitZ", (getter)PoseBone_getIKFlag, (setter)PoseBone_setIKFlag, 
		"limit rotation over Z axis when part of an IK", (void *)BONE_IK_ZLIMIT },
	
	{"lockXRot", (getter)PoseBone_getIKFlag, (setter)PoseBone_setIKFlag,
		"disable X DoF when part of an IK", (void *)BONE_IK_NO_XDOF },
	{"lockYRot", (getter)PoseBone_getIKFlag, (setter)PoseBone_setIKFlag, 
		"disable Y DoF when part of an IK", (void *)BONE_IK_NO_YDOF },
	{"lockZRot", (getter)PoseBone_getIKFlag, (setter)PoseBone_setIKFlag,
		"disable Z DoF when part of an IK", (void *)BONE_IK_NO_ZDOF },
	{"layerMask", (getter)PoseBone_getLayerMask, (setter)PoseBone_setLayerMask, 
		"Layer bitmask", NULL },
	{NULL, NULL, NULL, NULL, NULL}
};

//------------------------tp_repr
//This is the string representation of the object
static PyObject *PoseBone_repr(BPyPoseBoneObject *self)
{
	return PyString_FromFormat( "[PoseBone \"%s\"]", self->posechannel->name); 
}
//------------------------tp_doc
//The __doc__ string for this object
static char BPyPoseBone_doc[] = "This object wraps a Blender PoseBone object.";

//------------------TYPE_OBECT DEFINITION--------------------------
PyTypeObject BPyPoseBone_Type = {
	PyObject_HEAD_INIT(NULL)   //tp_head
	0,										//tp_internal
	"PoseBone",							//tp_name
	sizeof(BPyPoseBoneObject),			//tp_basicsize
	0,										//tp_itemsize
	(destructor)PyObject_Del,	/* tp_dealloc */
	0,										//tp_print
	0,										//tp_getattr
	0,										//tp_setattr
	0,										//tp_compare
	(reprfunc)PoseBone_repr,		//tp_repr
	0,										//tp_as_number
	0,										//tp_as_sequence
	0,										//tp_as_mapping
	0,										//tp_hash
	0,										//tp_call
	0,										//tp_str
	0,										//tp_getattro
	0,										//tp_setattro
	0,										//tp_as_buffer
	Py_TPFLAGS_DEFAULT,         //tp_flags
	BPyPoseBone_doc,				//tp_doc
	0,										//tp_traverse
	0,										//tp_clear
	0,										//tp_richcompare
	0,										//tp_weaklistoffset
	0,										//tp_iter
	0,										//tp_iternext
	BPyPoseBone_methods,		//tp_methods
	0,										//tp_members
	BPyPoseBone_getset,			//tp_getset
	0,										//tp_base
	0,										//tp_dict
	0,										//tp_descr_get
	0,										//tp_descr_set
	0,										//tp_dictoffset
	0,										//tp_init
	0,										//tp_alloc
	0,										//tp_new
	0,										//tp_free
	0,										//tp_is_gc
	0,										//tp_bases
	0,										//tp_mro
	0,										//tp_cache
	0,										//tp_subclasses
	0,										//tp_weaklist
	0										//tp_del
};
//-------------------MODULE METHODS IMPLEMENTATION------------------------
//------------------VISIBLE PROTOTYPE IMPLEMENTATION-----------------------
//------------------------------Pose_CreatePyObject (internal)
//Returns a PyPose from a bPose - return PyNone if bPose is NULL
PyObject *Pose_CreatePyObject(BPyObject *bpyob)
{
	BPyPoseObject *py_pose = NULL;

	if (!bpyob->object->pose)
		Py_RETURN_NONE;
	
	py_pose =
		( BPyPoseObject * ) PyObject_NEW( BPyPoseObject, &BPyPose_Type );

	if (!py_pose)
		goto RuntimeError;

	py_pose->bpyob = bpyob;
	Py_INCREF(bpyob);

	//create armature.bones
	py_pose->Bones = (BPyPoseBoneDictObject*)PyPoseBonesDict_FromPyPose(py_pose);
	if (!py_pose->Bones)
		goto RuntimeError;

	return (PyObject*)py_pose;

RuntimeError:
	return EXPP_objError(PyExc_RuntimeError, "%s%s%s", 
		sPoseError, "Pose_CreatePyObject: ", "Internal Error Ocurred");
}
//------------------------------PyPoseBone_FromPosechannel (internal)
//Returns a PyPoseBone from a bPoseChannel - return PyNone if bPoseChannel is NULL
PyObject *PyPoseBone_FromPosechannel(bPoseChannel *pchan)
{
	BPyPoseBoneObject *py_posechannel = NULL;

	if (pchan){
		py_posechannel = (BPyPoseBoneObject*)BPyPoseBone_Type.tp_alloc(&BPyPoseBone_Type, 0);
		if (!py_posechannel)
			goto RuntimeError;
		py_posechannel->posechannel = pchan;
		return (PyObject*)py_posechannel;
	}else{
		Py_RETURN_NONE;
	}

RuntimeError:
	return EXPP_objError(PyExc_RuntimeError, "%s%s%s", 
		sPoseBoneError, "PyPoseBone_FromPosechannel: ", "Internal Error Ocurred");
}
//------------------------------Object_FromPoseChannel (internal)
//An ugly method for determining where the pchan chame from
Object *Object_FromPoseChannel(bPoseChannel *curr_pchan)
{
	int success = 0;
	Object *obj = NULL;
	bPoseChannel *pchan = NULL;
	for(obj = G.main->object.first; obj; obj = obj->id.next){
		if (obj->pose){
			for (pchan = obj->pose->chanbase.first; pchan; pchan = pchan->next){
				if (curr_pchan == pchan){
					success = 1;
					break;
				}
			}
			if (success)
				break;
		}
	}
	return obj;
}


//-------------------MODULE INITIALIZATION--------------------------------

PyObject *PoseType_Init(void)
{
	PyType_Ready( &BPyPose_Type );
	return (PyObject *) &BPyPose_Type;
}

PyObject *PoseBoneType_Init(void)
{
	PyType_Ready( &BPyPoseBone_Type );
	return (PyObject *) &BPyPoseBone_Type;
}

PyObject *PoseBoneDictType_Init(void)
{
	PyType_Ready( &BPyPoseBoneDict_Type );
	return (PyObject *) &BPyPoseBoneDict_Type;
}

void Pose_Init(void)
{
	PoseType_Init();
	PoseBoneType_Init();
	PoseBoneDictType_Init();
}
