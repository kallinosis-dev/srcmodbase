//========= Copyright � 1996-2009, Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
//=============================================================================//

#include "stdafx.h"
#include "Box3D.h"
#include "fgdlib/HelperInfo.h"
#include "materialsystem/imaterialsystem.h"
#include "materialsystem/IMesh.h"
#include "MapDoc.h"
#include "MapOccluder.h"
#include "MapView2D.h"
#include "Material.h"
#include "mathlib/MathLib.h"
#include "Render2D.h"
#include "Render3D.h"
#include "ToolManager.h"
#include "ToolSphere.h"
#include "..\FoW\FoW.h"

// memdbgon must be the last include file in a .cpp file!!!
#include <tier0/memdbgon.h>


IMPLEMENT_MAPCLASS(CMapOccluder)


//-----------------------------------------------------------------------------
// Purpose: Factory function. Used for creating a CMapOccluder helper from a
//			set of string parameters from the FGD file.
// Input  : pInfo - Pointer to helper info class which gives us information
//				about how to create the helper.
// Output : Returns a pointer to the helper, NULL if an error occurs.
//-----------------------------------------------------------------------------
CMapClass *CMapOccluder::Create(CHelperInfo *pHelperInfo, CMapEntity *pParent)
{
	CMapOccluder *pOccluder = new CMapOccluder;
	if (pOccluder != nullptr)
	{
		//
		// The first parameter should be the key name to represent. If it isn't
		// there we assume "radius".
		//
		const char *pszKeyName = pHelperInfo->GetParameter(0);
		if (pszKeyName != nullptr)
		{
			strcpy(pOccluder->m_szKeyName, pszKeyName);
		}
		else
		{
			strcpy(pOccluder->m_szKeyName, "radius");
		}

		//
		// Extract the line color from the parameter list.
		//
		unsigned char chRed = 255;
		unsigned char chGreen = 255;
		unsigned char chBlue = 255;

		const char *pszParam = pHelperInfo->GetParameter(1);
		if (pszParam != nullptr)
		{
			chRed = atoi(pszParam);
		}

		pszParam = pHelperInfo->GetParameter(2);
		if (pszParam != nullptr)
		{
			chGreen = atoi(pszParam);
		}

		pszParam = pHelperInfo->GetParameter(3);
		if (pszParam != nullptr)
		{
			chBlue = atoi(pszParam);
		}

		pOccluder->SetRenderColor(chRed, chGreen, chBlue);
	}

	return pOccluder;
}


//-----------------------------------------------------------------------------
// Purpose: Constructor.
//-----------------------------------------------------------------------------
CMapOccluder::CMapOccluder(bool AddToFoW) :
CMapSphere()
{
	m_szKeyName[0] = '\0';

	m_flRadius = 0;

	r = 255;
	g = 0;
	b = 0;

	SetVisible( true );
	SetVisible2D( true );

	m_FoWHandle = -1;
	if ( AddToFoW )
	{
		CFoW	*pFoW = CMapDoc::GetActiveMapDoc()->GetFoW();
		if ( pFoW )
		{
			m_FoWHandle = pFoW->AddOccluder( false );
		}
	}

	m_bWasSelected = false;
}


//-----------------------------------------------------------------------------
// Purpose: Destructor.
//-----------------------------------------------------------------------------
CMapOccluder::~CMapOccluder(void)
{
	if ( m_FoWHandle != -1 )
	{
		CFoW	*pFoW = CMapDoc::GetActiveMapDoc()->GetFoW();
		if ( pFoW != nullptr && m_FoWHandle != -1 )
		{
			pFoW->RemoveOccluder( m_FoWHandle );
		}
		m_FoWHandle = -1;
	}
}


//-----------------------------------------------------------------------------
// Purpose: 
// Input  : bFullUpdate - 
//-----------------------------------------------------------------------------
void CMapOccluder::CalcBounds(BOOL bFullUpdate)
{
	CMapClass::CalcBounds(bFullUpdate);

	Vector mins;
	Vector maxs;

	//
	// Pretend we're a point so that we don't change our parent entity bounds
	// in the 2D view.
	//
	m_Render2DBox.ResetBounds();
	m_Render2DBox.UpdateBounds(m_Origin);
	mins = m_Origin - Vector(m_flRadius, m_flRadius, m_flRadius);
	maxs = m_Origin + Vector(m_flRadius, m_flRadius, m_flRadius);
	m_Render2DBox.UpdateBounds(mins, maxs);

	//
	// Build our bounds for frustum culling in the 3D views.
	//
	m_CullBox.ResetBounds();
	mins = m_Origin - Vector(m_flRadius, m_flRadius, m_flRadius);
	maxs = m_Origin + Vector(m_flRadius, m_flRadius, m_flRadius);
	m_CullBox.UpdateBounds(mins, maxs);

	m_BoundingBox.ResetBounds(); // we don't want to use the bounds of the sphere for our bounding box
}


//-----------------------------------------------------------------------------
// Purpose: 
// Output : CMapClass
//-----------------------------------------------------------------------------
CMapClass *CMapOccluder::Copy(bool bUpdateDependencies)
{
	CMapOccluder *pCopy = new CMapOccluder( false );

	if (pCopy != nullptr)
	{
		pCopy->CopyFrom(this, bUpdateDependencies);
	}

	return(pCopy);
}


//-----------------------------------------------------------------------------
// Purpose: Makes this an exact duplicate of pObject.
// Input  : pObject - Object to copy.
// Output : Returns this.
//-----------------------------------------------------------------------------
CMapClass *CMapOccluder::CopyFrom(CMapClass *pObject, bool bUpdateDependencies)
{
	Assert(pObject->IsMapClass(MAPCLASS_TYPE(CMapOccluder)));
	CMapOccluder *pFrom = (CMapOccluder *)pObject;

	CMapClass::CopyFrom(pObject, bUpdateDependencies);

	m_flRadius = pFrom->m_flRadius;
	strcpy(m_szKeyName, pFrom->m_szKeyName);

	return(this);
}


//-----------------------------------------------------------------------------
// Purpose: Gets the tool object for a given context data from HitTest2D.
//-----------------------------------------------------------------------------
CBaseTool *CMapOccluder::GetToolObject(int nHitData, bool bAttachObject)
{
	CToolSphere *pTool = (CToolSphere *)ToolManager()->GetToolForID(TOOL_SPHERE);

	if ( bAttachObject )
		pTool->Attach(this);

	return pTool;
}


//-----------------------------------------------------------------------------
// Purpose: 
// Input  : pView - 
//			point - point in client coordinates
// Output : 
//-----------------------------------------------------------------------------
bool CMapOccluder::HitTest2D(CMapView2D *pView, const Vector2D &point, HitInfo_t &HitData)
{
	if ( m_flRadius <= 0 )
	{
		return NULL;
	}

	Vector2D vecClientOrigin;
	pView->WorldToClient(vecClientOrigin, m_Origin);

	Vector vecRadius = m_Origin;
	vecRadius[pView->axHorz] += m_flRadius;

	Vector2D vecClientRadius;
	pView->WorldToClient(vecClientRadius, vecRadius);

	int nRadius = abs(vecClientRadius.x - vecClientOrigin.x);

	vecClientRadius.x = nRadius;
	vecClientRadius.y = nRadius;

	HitData.pObject = this;
	HitData.nDepth = 0; // handles have no depth
	HitData.uData = nRadius;

	Vector2D vecClientMin = vecClientOrigin - vecClientRadius;
	Vector2D vecClientMax = vecClientOrigin + vecClientRadius;

	//
	// Check the four resize handles.
	//
	Vector2D vecTemp(vecClientOrigin.x, vecClientMin.y - HANDLE_OFFSET);

	if (pView->CheckDistance(point, vecTemp, 6))
	{
		// Top handle
		SetCursor(AfxGetApp()->LoadStandardCursor(IDC_SIZENS));
		return true;
	}

	vecTemp.x = vecClientOrigin.x;
	vecTemp.y = vecClientMax.y + HANDLE_OFFSET;
	if (pView->CheckDistance(point, vecTemp, HANDLE_RADIUS))
	{
		// Bottom handle
		SetCursor(AfxGetApp()->LoadStandardCursor(IDC_SIZENS));
		return true;
	}

	vecTemp.x = vecClientMin.x - HANDLE_OFFSET;
	vecTemp.y = vecClientOrigin.y;
	if (pView->CheckDistance(point, vecTemp, HANDLE_RADIUS))
	{
		// Left handle
		SetCursor(AfxGetApp()->LoadStandardCursor(IDC_SIZEWE));
		return true;
	}

	vecTemp.x = vecClientMax.x + HANDLE_OFFSET;
	vecTemp.y = vecClientOrigin.y;
	if (pView->CheckDistance(point, vecTemp, HANDLE_RADIUS))
	{
		// Right handle
		SetCursor(AfxGetApp()->LoadStandardCursor(IDC_SIZEWE));
		return true;
	}

	HitData.pObject = nullptr;
	return false;
}


//-----------------------------------------------------------------------------
// Purpose: Notifies that this object's parent entity has had a key value change.
// Input  : szKey - The key that changed.
//			szValue - The new value of the key.
//-----------------------------------------------------------------------------
void CMapOccluder::OnParentKeyChanged(const char *szKey, const char *szValue)
{
	if (!stricmp(szKey, m_szKeyName))
	{
		m_flRadius = atof(szValue);
		PostUpdate(Notify_Changed);

		CFoW	*pFoW = CMapDoc::GetActiveMapDoc()->GetFoW();
		if ( pFoW )
		{
			pFoW->UpdateOccluderSize( m_FoWHandle, m_flRadius );
		}
	}
}


//-----------------------------------------------------------------------------
// Purpose: 
// Input  : 
// Output : 
//-----------------------------------------------------------------------------
void CMapOccluder::OnRemoveFromWorld(CMapWorld *pWorld, bool bNotifyChildren)
{
	CFoW	*pFoW = CMapDoc::GetActiveMapDoc()->GetFoW();

	if ( pFoW != nullptr && m_FoWHandle != -1 )
	{
		pFoW->RemoveOccluder( m_FoWHandle );
	}
	m_FoWHandle = -1;
}


//-----------------------------------------------------------------------------
// Purpose: 
// Input  : pRender - 
//-----------------------------------------------------------------------------
void CMapOccluder::Render2D(CRender2D *pRender)
{
	if ( m_flRadius > 0 )
	{
		pRender->SetDrawColor( 255, 0, 0 );

		Vector2D ptClientRadius;
		pRender->TransformNormal(ptClientRadius, Vector(m_flRadius,m_flRadius,m_flRadius) );

		int radius = ptClientRadius.x;

		pRender->DrawCircle( m_Origin, m_flRadius );

		bool bPopMode = pRender->BeginClientSpace();

		pRender->SetHandleStyle( HANDLE_RADIUS, CRender::HANDLE_SQUARE );
		pRender->SetHandleColor( 255,0,0 );
		if ( IsSelected() )
		{
			m_bWasSelected = true;

			//
			// Draw the four resize handles.
			//
			Vector2D offset;
			offset.x = 0;
			offset.y = -(radius + HANDLE_OFFSET);
			pRender->DrawHandle( m_Origin, &offset );

			offset.x = 0;
			offset.y = radius + HANDLE_OFFSET;
			pRender->DrawHandle( m_Origin, &offset );

			offset.x = -(radius + HANDLE_OFFSET);
			offset.y = 0;
			pRender->DrawHandle( m_Origin, &offset );

			offset.x = radius + HANDLE_OFFSET;
			offset.y = 0;
			pRender->DrawHandle( m_Origin, &offset );
		}

		pRender->DrawHandle( m_Origin, &vec2_origin );

		if ( bPopMode )
		{
			pRender->EndClientSpace();
		}

		CFoW	*pFoW = CMapDoc::GetActiveMapDoc()->GetFoW();
		if ( pFoW && m_bWasSelected )
		{
			float flVisibility = pFoW->GetLocationVisibilityDegree( 0, m_Origin, m_flRadius );
			Msg( "Visibility = %g\n", flVisibility );
			flVisibility = flVisibility;
		}
	}
}


//-----------------------------------------------------------------------------
// Purpose: Renders the wireframe sphere.
// Input  : pRender - Interface to renderer.
//-----------------------------------------------------------------------------
void CMapOccluder::Render3D(CRender3D *pRender)
{
	if ( m_flRadius > 0 )
	{
		pRender->RenderWireframeSphere(m_Origin, m_flRadius, 12, 12, 255, 0, 0);
	}
}


//-----------------------------------------------------------------------------
// Purpose: Overridden to chain down to our endpoints, which are not children.
//-----------------------------------------------------------------------------
void CMapOccluder::SetOrigin(Vector const& vecOrigin)
{
	BaseClass::SetOrigin(vecOrigin);

	CFoW	*pFoW = CMapDoc::GetActiveMapDoc()->GetFoW();
	if ( pFoW )
	{
		pFoW->UpdateOccluderLocation( m_FoWHandle, vecOrigin );
	}
}


//-----------------------------------------------------------------------------
// Purpose: Overridden to chain down to our endpoints, which are not children.
//-----------------------------------------------------------------------------
SelectionState_t CMapOccluder::SetSelectionState(SelectionState_t eSelectionState)
{
	SelectionState_t eState = BaseClass::SetSelectionState(eSelectionState);

	return eState;
}


//-----------------------------------------------------------------------------
// Purpose: Overridden to transform our endpoints.
//-----------------------------------------------------------------------------
void CMapOccluder::DoTransform(const VMatrix &matrix)
{
	BaseClass::DoTransform(matrix);

	CFoW	*pFoW = CMapDoc::GetActiveMapDoc()->GetFoW();
	if ( pFoW )
	{
		pFoW->UpdateOccluderLocation( m_FoWHandle, m_Origin );
	}
}


void CMapOccluder::SetRadius(float flRadius)
{
	__super::SetRadius( flRadius );

	CFoW	*pFoW = CMapDoc::GetActiveMapDoc()->GetFoW();
	if ( pFoW )
	{
		pFoW->UpdateOccluderSize( m_FoWHandle, m_flRadius );
	}
}
