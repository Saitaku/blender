/**
 * $Id$
 * ***** BEGIN GPL/BL DUAL LICENSE BLOCK *****
 *
 * The contents of this file may be used under the terms of either the GNU
 * General Public License Version 2 or later (the "GPL", see
 * http://www.gnu.org/licenses/gpl.html ), or the Blender License 1.0 or
 * later (the "BL", see http://www.blender.org/BL/ ) which has to be
 * bought from the Blender Foundation to become active, in which case the
 * above mentioned GPL option does not apply.
 *
 * The Original Code is Copyright (C) 2002 by NaN Holding BV.
 * All rights reserved.
 *
 * The Original Code is: all of this file.
 *
 * Contributor(s): none yet.
 *
 * ***** END GPL/BL DUAL LICENSE BLOCK *****
 */

/**

 * $Id$
 * Copyright (C) 2001 NaN Technologies B.V.
 * @author	Maarten Gribnau
 * @date	March 31, 2001
 */

#ifndef _H_ACT_TESTACTION
#define _H_ACT_TESTACTION

#include "ACT_Action.h"

#include <iostream>

class TestAction : public ACT_Action
{
public:
	TestAction(const STR_String& name) : ACT_Action(name) {}
	virtual ~TestAction() { cout << m_name.Ptr() << ": deleted\n"; }
protected:
	virtual void doApply() { cout << m_name.Ptr() << ": applied\n"; }
	virtual void doUndo() { cout << m_name.Ptr() << ": undone\n"; }
};

#endif // _H_ACT_TESTACTION