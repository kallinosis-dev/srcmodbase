//===== Copyright � 1996-2005, Valve Corporation, All rights reserved. ======//
//
// Purpose: Model loading / unloading interface
//
// $NoKeywords: $
//===========================================================================//

#include "render_pch.h"
#include "Overlay.h"
#include "bspfile.h"
#include "modelloader.h"
#include "materialsystem/imesh.h"
#include "materialsystem/ivballoctracker.h"
#include "disp.h"
#include "collisionutils.h"
#include "tier0/vprof.h"
#include "render.h"
#include "r_decal.h"
#include "fmtstr.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

//-----------------------------------------------------------------------------
// Externs
//-----------------------------------------------------------------------------
int g_OverlayRenderFrameID;

//-----------------------------------------------------------------------------
// Convars
//-----------------------------------------------------------------------------
static ConVar r_renderoverlaybatch("r_renderoverlaybatch", "1", FCVAR_DEVELOPMENTONLY);
static ConVar r_renderoverlayfragment("r_renderoverlayfragment", "1");
static ConVar r_overlaywireframe( "r_overlaywireframe", "0" );
static ConVar r_overlayfadeenable( "r_overlayfadeenable", "0" );
static ConVar r_overlayfademin( "r_overlayfademin", "1750" );
static ConVar r_overlayfademax( "r_overlayfademax", "2000" );

//-----------------------------------------------------------------------------
// Structures used to represent the overlay
//-----------------------------------------------------------------------------
typedef unsigned short OverlayFragmentList_t;

enum
{
	OVERLAY_FRAGMENT_LIST_INVALID = (OverlayFragmentList_t)~0,
};

enum
{
	NUM_OVERLAY_TEXCOORDS = 2,
};

struct overlayvert_t
{
	Vector		pos;
	Vector		normal;
	Vector2D	texCoord[NUM_OVERLAY_TEXCOORDS];	// texcoord 0 = the mapped tex coord from worldcraft
													// texcoord 1 is used for alpha and maps the whole texture into the whole overlay
	Vector2D	tmpDispUV;

	float		lightCoord[2];
	int			packedColor;

	overlayvert_t()
	{
		pos.Init();
		normal.Init();
		texCoord[0].Init();
		texCoord[1].Init();
		tmpDispUV.Init();
		lightCoord[0] = lightCoord[1] = 0.0f;
		packedColor = 0xFFFFFFFF;
	}
};

struct moverlayfragment_t
{
	int						m_nRenderFrameID;	// So we only render a fragment once a frame!
	SurfaceHandle_t			m_SurfId;			// Surface Id
	int						m_iOverlay;			// Overlay Id
	OverlayFragmentHandle_t	m_hNextRender;
	unsigned short			m_nMaterialSortID;
	CUtlVector<overlayvert_t>	m_aPrimVerts;
	float					decalOffset;
	int						m_iMesh;			// Mesh Id
	int						m_nVertexBufferIndex;
	bool					m_bFaded;
};

// this draws in the unlit pass
#define FOVERLAY_DRAW_UNLIT		0x0001

struct moverlay_t
{
	int				m_nId;
	short			m_nTexInfo;
	short			m_nRenderOrder;	// 0 - MAX_OVERLAY_RENDER_ORDERS
	OverlayFragmentList_t m_hFirstFragment;
	unsigned short	m_nFlags;
	CUtlVector<SurfaceHandle_t>	m_aFaces;
	float			m_flU[2];
	float			m_flV[2];
	Vector			m_vecUVPoints[4];
	Vector			m_vecOrigin;
	Vector			m_vecBasis[3];		// 0 = u, 1 = v, 2 = normal
	void			*m_pBindProxy;		// client renderable for an overlay's material proxy to bind to
	float			m_flFadeDistMinSq;	// Distance from the overlay's origin at which we start fading (-1 = use max dist)
	float			m_flFadeDistMaxSq;	// Distance from the overlay's origin at which we fade out completely
	float			m_flInvFadeRangeSq;	// Precomputed 1.0f / ( m_flFadeDistMaxSq - m_flFadeDistMinSq )
	float			m_flRadius;			// max distance of an overlay vertex from the origin
	byte			m_nMinCPULevel;
	byte			m_nMaxCPULevel;
	byte			m_nMinGPULevel;
	byte			m_nMaxGPULevel;

	// TODO: Probably want to clear out some other stuff, but this is the only add for now!
	moverlay_t()
	{
		m_nMinCPULevel = 0;
		m_nMaxCPULevel = 0;
		m_nMinGPULevel = 0;
		m_nMaxGPULevel = 0;
	}

	bool ShouldDraw( int nCPULevel, int nGPULevel )
	{
		bool bDraw = true;
		if ( m_nMinCPULevel != 0 )
		{
			bDraw = nCPULevel >= ( m_nMinCPULevel - 1 );
		}

		if ( bDraw && m_nMaxCPULevel != 0 )
		{
			bDraw = nCPULevel <= ( m_nMaxCPULevel - 1 );
		}

		if ( bDraw && m_nMinGPULevel != 0 )
		{
			bDraw = nGPULevel >= ( m_nMinGPULevel - 1 );
		}

		if ( bDraw && m_nMaxGPULevel != 0 )
		{
			bDraw = nGPULevel <= ( m_nMaxGPULevel - 1 );
		}

		return bDraw;
	}
};

// Going away!
void Overlay_BuildBasisOrigin( Vector &vecBasisOrigin, SurfaceHandle_t surfID );
void Overlay_BuildBasis( const Vector &vecBasisNormal, Vector &vecBasisU, Vector &vecBasisV, bool bFlip );
void Overlay_OverlayUVToOverlayPlane( const Vector &vecBasisOrigin, const Vector &vecBasisU,
									  const Vector &vecBasisV, const Vector &vecUVPoint,
									  Vector &vecPlanePoint );
void Overlay_WorldToOverlayPlane( const Vector &vecBasisOrigin, const Vector &vecBasisNormal,
								  const Vector &vecWorldPoint, Vector &vecPlanePoint );
void Overlay_OverlayPlaneToWorld( const Vector &vecBasisNormal, SurfaceHandle_t surfID,
								  const Vector &vecPlanePoint, Vector &vecWorldPoint );
void Overlay_DispUVToWorld( CDispInfo *pDisp, CMeshReader *pReader, const Vector2D &vecUV, Vector &vecWorld, Vector &vecWorldNormal, moverlayfragment_t &surfaceFrag );


void Overlay_TriTLToBR( CDispInfo *pDisp, Vector &vecWorld, Vector &vecWorldNormal, float flU, float flV,
						int nSnapU, int nSnapV, int nWidth, int nHeight );
void Overlay_TriBLToTR( CDispInfo *pDisp, Vector &vecWorld, Vector &vecWorldNormal, float flU, float flV,
			            int nSnapU, int nSnapV, int nWidth, int nHeight );

//-----------------------------------------------------------------------------
// Overlay manager class
//-----------------------------------------------------------------------------
class COverlayMgr : public IOverlayMgr
{
public:
	typedef CUtlVector<moverlayfragment_t*> OverlayFragmentVector_t;



public:
	COverlayMgr();
	~COverlayMgr();

	// Implementation of IOverlayMgr interface
	virtual bool	LoadOverlays( );
	virtual void	UnloadOverlays( );

	virtual void	CreateFragments( void );
	virtual void	ReSortMaterials( void );
	virtual void	ClearRenderLists();
	virtual void	ClearRenderLists( int nSortGroup );
	virtual void	AddFragmentListToRenderList( int nSortGroup, OverlayFragmentHandle_t iFragment, bool bDisp );
	virtual void	RenderOverlays( IMatRenderContext *pRenderContext, int nSortGroup );
	virtual void	RenderAllUnlitOverlays( IMatRenderContext *pRenderContext, int nSortGroup );

	virtual void	SetOverlayBindProxy( int iOverlayID, void *pBindProxy );

	virtual void	UpdateOverlayRenderLevels( int nCPULevel, int nGPULevel );

private:

	void				RenderOverlaysBatch( IMatRenderContext *pRenderContext, int nSortGroup );

	// Create, destroy material sort order ids...
	int					GetMaterialSortID( IMaterial* pMaterial, int nLightmapPage );
	void				CleanupMaterial( unsigned short nSortOrder );

	moverlay_t			*GetOverlay( int iOverlay );
	moverlayfragment_t	*GetOverlayFragment( OverlayFragmentHandle_t iFragment );

	// Surfaces
	void				Surf_CreateFragments( moverlay_t *pOverlay, SurfaceHandle_t surfID );
	bool				Surf_PreClipFragment( moverlay_t *pOverlay, moverlayfragment_t &overlayFrag, SurfaceHandle_t surfID, moverlayfragment_t &surfaceFrag );
	void				Surf_PostClipFragment( moverlay_t *pOverlay, moverlayfragment_t &overlayFrag, SurfaceHandle_t surfID );
	void				Surf_ClipFragment( moverlay_t *pOverlay, moverlayfragment_t &overlayFrag, SurfaceHandle_t surfID, moverlayfragment_t &surfaceFrag );

	// Displacements
	void				Disp_CreateFragments( moverlay_t *pOverlay, SurfaceHandle_t surfID );
	bool				Disp_PreClipFragment( moverlay_t *pOverlay, OverlayFragmentVector_t &aDispFragments, SurfaceHandle_t surfID );
	void				Disp_PostClipFragment( CDispInfo *pDisp, CMeshReader *pReader, moverlay_t *pOverlay, OverlayFragmentVector_t &aDispFragments, SurfaceHandle_t surfID );
	void				Disp_ClipFragment( CDispInfo *pDisp, OverlayFragmentVector_t &aDispFragments );
	void				Disp_DoClip( CDispInfo *pDisp, OverlayFragmentVector_t &aCurrentFragments, cplane_t &clipPlane, 
									 float clipDistStart, int nInterval, int nLoopStart, int nLoopEnd, int nLoopInc );

	// Utility
	OverlayFragmentHandle_t AddFragmentToFragmentList( int nSize );
	OverlayFragmentHandle_t AddFragmentToFragmentList( moverlayfragment_t *pSrc );

	bool FadeOverlayFragmentGlobal( moverlayfragment_t *pFragment );
	bool FadeOverlayFragment( moverlay_t *pOverlay, moverlayfragment_t *pFragment );

	moverlayfragment_t *CreateTempFragment( int nSize );
	moverlayfragment_t *CopyTempFragment( moverlayfragment_t *pSrc );
	void				DestroyTempFragment( moverlayfragment_t *pFragment );

	void				BuildClipPlanes( SurfaceHandle_t surfID, moverlayfragment_t &surfaceFrag, const Vector &vecBasisNormal, CUtlVector<cplane_t> &m_ClipPlanes );
	void				DoClipFragment( moverlayfragment_t *pFragment, cplane_t *pClipPlane, moverlayfragment_t **ppFront, moverlayfragment_t **ppBack );

	void				InitTexCoords( moverlay_t *pOverlay, moverlayfragment_t &overlayFrag );

	void				BuildStaticBuffers();
	void				DestroyStaticBuffers();
	int					FindOrdAddMesh( IMaterial* pMaterial, int vertCount );

	void				DrawBatches( IMatRenderContext *pRenderContext, IMesh* pIndices, bool bWireframe );
	void				DrawFadedFragments( IMatRenderContext *pRenderContext, int nSortGroup, bool bWireframe );

private:
	enum
	{
		RENDER_QUEUE_INVALID = 0xFFFF
	};
	
	// Structures used to assign sort order handles
	struct RenderQueueInfo_t
	{
		OverlayFragmentHandle_t m_hFirstFragment;
		unsigned short m_nNextRenderQueue;	// Index of next queue that has stuff to render
		unsigned short m_nVertexCount;
		unsigned short m_nIndexCount;
	};

	struct RenderQueueHead_t
	{
		IMaterial *m_pMaterial;
		int m_nLightmapPage;

		RenderQueueInfo_t m_Queue[MAX_MAT_SORT_GROUPS];

		unsigned short m_nRefCount;
	};

	struct RenderBatch_t
	{
		int				m_iMesh;
		unsigned short	m_nFirstIndex;
		unsigned short	m_nNumIndex;
		IMaterial *		m_pMaterial;
		void *			m_pBindProxy;
		int				m_nLightmapPage;

		RenderBatch_t() : m_iMesh(0), m_nFirstIndex(0), m_nNumIndex(0), m_pMaterial(nullptr), m_pBindProxy(nullptr), m_nLightmapPage(0)
		{}
	};

	struct RenderMesh_t
	{
		IMesh*			m_pMesh;
		IMaterial *		m_pMaterial;
		int				m_nVertCount;
		VertexFormat_t	m_VertexFormat;
	};

	// First render queue to render
	unsigned short m_nFirstRenderQueue[MAX_MAT_SORT_GROUPS];

	// Used to assign sort order handles
	CUtlLinkedList<RenderQueueHead_t, unsigned short> m_RenderQueue;

	// All overlays
	CUtlVector<moverlay_t> m_aOverlays;

	// List of all overlay fragments. prev/next links point to the next fragment on a *surface*
	CUtlLinkedList< moverlayfragment_t, unsigned short, true > m_aFragments;

	// Used to find all fragments associated with a particular overlay
	CUtlLinkedList< OverlayFragmentHandle_t, unsigned short, true > m_OverlayFragments;

	// list of batches used to render overlays - static VB and dynamic IB
	CUtlVector<RenderBatch_t> m_RenderBatchList;

	// list of faded fragments - dynamic VB and dynamic IB
	CUtlVector<OverlayFragmentHandle_t> m_RenderFadedFragments;

	// List of static VB
	CUtlVector<RenderMesh_t> m_RenderMeshes;

	// Fade parameters.
	float m_flFadeMin2;
	float m_flFadeMax2;
	float m_flFadeDelta2;

	// Cache the gpu/cpu levels.
	int	  m_nCPULevel;
	int	  m_nGPULevel;
};


//-----------------------------------------------------------------------------
// Singleton accessor
//-----------------------------------------------------------------------------
static COverlayMgr g_OverlayMgr;
IOverlayMgr *OverlayMgr( void )
{
	return &g_OverlayMgr;
}

//-----------------------------------------------------------------------------
// Constructor
//-----------------------------------------------------------------------------
COverlayMgr::COverlayMgr()
{
	for ( int i = 0; i < MAX_MAT_SORT_GROUPS; ++i )
	{
		m_nFirstRenderQueue[i] = RENDER_QUEUE_INVALID;
	}

	m_flFadeMin2 = 0.0f;
	m_flFadeMax2 = 0.0f;
	m_flFadeDelta2 = 0.0f;

	m_nCPULevel = -1;
	m_nGPULevel = -1;
}

//-----------------------------------------------------------------------------
// Destructor
//-----------------------------------------------------------------------------
COverlayMgr::~COverlayMgr()
{
	UnloadOverlays();
}


//-----------------------------------------------------------------------------
// Returns a particular overlay
//-----------------------------------------------------------------------------
inline moverlay_t *COverlayMgr::GetOverlay( int iOverlay )
{
	return &m_aOverlays[iOverlay];
}

//-----------------------------------------------------------------------------
// Returns a particular overlay fragment
//-----------------------------------------------------------------------------
inline moverlayfragment_t *COverlayMgr::GetOverlayFragment( OverlayFragmentHandle_t iFragment )
{
	return &m_aFragments[iFragment];
}


//-----------------------------------------------------------------------------
// Cleanup overlays
//-----------------------------------------------------------------------------
void COverlayMgr::UnloadOverlays( )
{
	FOR_EACH_LL( m_RenderQueue, i )
	{
		m_RenderQueue[i].m_pMaterial->DecrementReferenceCount();
	}

	int nOverlayCount = m_aOverlays.Count();
	for ( int iOverlay = 0; iOverlay < nOverlayCount; ++iOverlay )
	{
		moverlay_t *pOverlay = &m_aOverlays.Element( iOverlay );
		int hFrag = pOverlay->m_hFirstFragment;
		while ( hFrag != OVERLAY_FRAGMENT_INVALID )
		{
			int iFrag = m_OverlayFragments[hFrag];
			m_aFragments.Free( iFrag );
			hFrag = m_OverlayFragments.Next( hFrag );
		}
	}

	m_aOverlays.Purge();
	m_aFragments.Purge();
	m_OverlayFragments.Purge();
	m_RenderQueue.Purge();

	DestroyStaticBuffers();

	for ( int i = 0; i < MAX_MAT_SORT_GROUPS; ++i )
	{
		m_nFirstRenderQueue[i] = RENDER_QUEUE_INVALID;
	}
}


//-----------------------------------------------------------------------------
// Create, destroy material sort order ids...
//-----------------------------------------------------------------------------
int COverlayMgr::GetMaterialSortID( IMaterial* pMaterial, int nLightmapPage )
{
	// Search the sort order handles for an enumeration id match (means materials + lightmaps match)
	unsigned short i;
	for ( i = m_RenderQueue.Head(); i != m_RenderQueue.InvalidIndex();
		i = m_RenderQueue.Next(i) )
	{
		// Found a match, lets increment the refcount of this sort order id
		if ((m_RenderQueue[i].m_pMaterial == pMaterial) && (m_RenderQueue[i].m_nLightmapPage == nLightmapPage))
		{
			++m_RenderQueue[i].m_nRefCount;
			return i;
		}
	}

	// Didn't find it, lets assign a new sort order ID, with a refcount of 1
	i = m_RenderQueue.AddToTail();
	RenderQueueHead_t &renderQueue = m_RenderQueue[i];

	renderQueue.m_pMaterial = pMaterial;
	renderQueue.m_nLightmapPage = nLightmapPage;
	renderQueue.m_nRefCount = 1;

	for ( int j = 0; j < MAX_MAT_SORT_GROUPS; ++j )
	{
		RenderQueueInfo_t &info = renderQueue.m_Queue[j];

		info.m_hFirstFragment = OVERLAY_FRAGMENT_INVALID;
		info.m_nNextRenderQueue = RENDER_QUEUE_INVALID;
		info.m_nVertexCount = 0;
		info.m_nIndexCount = 0;
	}

	pMaterial->IncrementReferenceCount();

	return i;
}

void COverlayMgr::CleanupMaterial( unsigned short nSortOrder )
{
	RenderQueueHead_t &renderQueue = m_RenderQueue[nSortOrder];

#ifdef _DEBUG
	for ( int i = 0; i < MAX_MAT_SORT_GROUPS; ++i )
	{
		// Shouldn't be cleaning up while we've got a render list
		Assert( renderQueue.m_Queue[i].m_nVertexCount == 0 );
	}
#endif

	// Decrease the sort order reference count
	if (--renderQueue.m_nRefCount <= 0)
	{
		renderQueue.m_pMaterial->DecrementReferenceCount();

		// No one referencing the sort order number?
		// Then lets clean up the sort order id
		m_RenderQueue.Remove(nSortOrder);
	}
}


//-----------------------------------------------------------------------------
// Clears the render lists
//-----------------------------------------------------------------------------
void COverlayMgr::ClearRenderLists()
{
	for ( int i = 0; i < MAX_MAT_SORT_GROUPS; ++i )
	{
		ClearRenderLists( i );
	}

	if ( r_overlayfadeenable.GetBool() )
	{
		float flFadeMin = r_overlayfademin.GetFloat();
		float flFadeMax = r_overlayfademax.GetFloat();
		m_flFadeMin2 = flFadeMin * flFadeMin;
		m_flFadeMax2 = flFadeMax * flFadeMax;
		m_flFadeDelta2 = 1.0f / ( m_flFadeMax2 - m_flFadeMin2 );
	}
}


//-----------------------------------------------------------------------------
// Calculate the fade using the global convars.
//-----------------------------------------------------------------------------
bool COverlayMgr::FadeOverlayFragmentGlobal( moverlayfragment_t *pFragment )
{
	// Test the overlay distance and set alpha values.
	int iVert;
	bool bInRange = false;

	pFragment->m_bFaded = false;
	int nVertexCount = pFragment->m_aPrimVerts.Count();
	for ( iVert = 0; iVert < nVertexCount; ++iVert )
	{
		Vector vecSegment;
		VectorSubtract( CurrentViewOrigin(), pFragment->m_aPrimVerts.Element( iVert ).pos, vecSegment );
		float flLength2 = vecSegment.LengthSqr();

		// min dist of -1 means use max dist for fading
		if ( flLength2 < m_flFadeMin2 )
		{
			pFragment->m_aPrimVerts.Element( iVert ).packedColor = 0xFFFFFFFF;
			bInRange = true;
		}
		else if ( flLength2 > m_flFadeMax2 )
		{
			// Set vertex alpha to off.
			pFragment->m_aPrimVerts.Element( iVert ).packedColor = 0x00FFFFFF;
		}
		else
		{
			// Set the alpha based on distance inside of fadeMin and fadeMax
			float flAlpha = flLength2 - m_flFadeMin2;
			flAlpha *= m_flFadeDelta2;
			pFragment->m_aPrimVerts.Element( iVert ).packedColor = 0x00FFFFFF | (FastFToC(1.0f - flAlpha)<<24);

			pFragment->m_bFaded = true;
			bInRange = true;
		}
	}

	return bInRange;
}


//-----------------------------------------------------------------------------
// Calculate the fade using per-overlay fade distances.
//-----------------------------------------------------------------------------
bool COverlayMgr::FadeOverlayFragment( moverlay_t *pOverlay, moverlayfragment_t *pFragment )
{
	// min dist of -1 means use max dist for fading
	float flFadeDistMinSq = pOverlay->m_flFadeDistMinSq;
	float flFadeDistMaxSq = pOverlay->m_flFadeDistMaxSq;

	float flLength2 = pOverlay->m_vecOrigin.DistTo( CurrentViewOrigin() ) - pOverlay->m_flRadius;
	flLength2 *= flLength2;
	if ( flLength2 >= flFadeDistMaxSq )
		return false;

	int color = 0xFFFFFFFF;
	pFragment->m_bFaded = false;
	if ( ( flFadeDistMinSq >= 0 ) && ( flLength2 > flFadeDistMinSq ) )
	{
		float flAlpha = pOverlay->m_flInvFadeRangeSq * ( flFadeDistMaxSq - flLength2 );
		flAlpha = clamp( flAlpha, 0.0f, 1.0f );
		int alpha = FastFToC(flAlpha);
		color = 0x00FFFFFF | (alpha<<24);
		pFragment->m_bFaded = true;
	}

	int nVertexCount = pFragment->m_aPrimVerts.Count();
	for ( int iVert = 0; iVert < nVertexCount; ++iVert )
	{
		pFragment->m_aPrimVerts.Element( iVert ).packedColor = color;
	}

	return true;
}


//-----------------------------------------------------------------------------
// Adds the fragment list to the list of fragments to render when RenderOverlays is called
//-----------------------------------------------------------------------------
void COverlayMgr::AddFragmentListToRenderList( int nSortGroup, OverlayFragmentHandle_t iFragment, bool bDisp )
{
	OverlayFragmentHandle_t i;
	for ( i = iFragment; i != OVERLAY_FRAGMENT_INVALID; i = m_aFragments.Next(i) )
	{
		// Make sure we don't add the fragment twice...
		// FIXME: I currently have no way of ensuring a fragment doesn't end up in 2 sort groups
		// which would cause all manner of nastiness.
		moverlayfragment_t *pFragment = GetOverlayFragment(i);
		if ( !bDisp && pFragment->m_nRenderFrameID == g_OverlayRenderFrameID )
			continue;

		moverlay_t *pOverlay = &m_aOverlays[ pFragment->m_iOverlay ];
		if ( !pOverlay )
			continue;

		// Based on machine performance, should we be drawing this overlay?
		if ( !pOverlay->ShouldDraw( m_nCPULevel, m_nGPULevel ) )
			continue;

		// Triangle count too low? Skip it...
		int nVertexCount = pFragment->m_aPrimVerts.Count();
		if ( nVertexCount < 3 )
			continue;

		// See if we should fade the overlay.
		pFragment->m_bFaded = false;
		if ( r_overlayfadeenable.GetBool() )
		{
			// Fade using the convars that control distance.
			if ( !FadeOverlayFragmentGlobal( pFragment ) )
				continue;
		}
		else if ( pOverlay->m_flFadeDistMaxSq > 0 )
		{
			// Fade using per-overlay fade distances, configured by the level designer.
			if ( !FadeOverlayFragment( pOverlay, pFragment ) )
				continue;
		}
		
		// Update the frame count.
		pFragment->m_nRenderFrameID = g_OverlayRenderFrameID;

		// Determine the material associated with the fragment...
		int nMaterialSortID = pFragment->m_nMaterialSortID;

		// Insert the render queue into the list of render queues to render
		RenderQueueHead_t &renderQueue = m_RenderQueue[nMaterialSortID];
		RenderQueueInfo_t &info = renderQueue.m_Queue[nSortGroup];

		if ( info.m_hFirstFragment == OVERLAY_FRAGMENT_INVALID )
		{
			info.m_nNextRenderQueue = m_nFirstRenderQueue[nSortGroup];
			m_nFirstRenderQueue[nSortGroup] = nMaterialSortID;
		}

		// Add to list of fragments for this surface
		// NOTE: Render them in *reverse* order in which they appeared in the list
		// because they are stored in the list in *reverse* order in which they should be rendered.

		// Add the fragment to the bucket of fragments to render...
		pFragment->m_hNextRender = info.m_hFirstFragment;
		info.m_hFirstFragment = i;

		Assert( info.m_nVertexCount + nVertexCount < 65535 );
		info.m_nVertexCount += nVertexCount;
		info.m_nIndexCount += 3 * (nVertexCount - 2);
	}
}


//-----------------------------------------------------------------------------
// Renders all queued up overlays
//-----------------------------------------------------------------------------
void COverlayMgr::ClearRenderLists( int nSortGroup )
{
	g_OverlayRenderFrameID++;
	int nNextRenderQueue;
	for( int i = m_nFirstRenderQueue[nSortGroup]; i != RENDER_QUEUE_INVALID; i = nNextRenderQueue )
	{
 		RenderQueueInfo_t &renderQueue = m_RenderQueue[i].m_Queue[nSortGroup];
		nNextRenderQueue = renderQueue.m_nNextRenderQueue;

		// Clean up the render queue for next time...
		renderQueue.m_nVertexCount = 0;
		renderQueue.m_nIndexCount = 0;
		renderQueue.m_hFirstFragment = OVERLAY_FRAGMENT_INVALID;
		renderQueue.m_nNextRenderQueue = RENDER_QUEUE_INVALID;
	}

	m_nFirstRenderQueue[nSortGroup] = RENDER_QUEUE_INVALID;
}

//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
void COverlayMgr::DrawBatches( IMatRenderContext *pRenderContext, IMesh* pIndices, bool bWireframe )
{
	pRenderContext->BeginBatch( pIndices );
	for ( int i = 0; i < m_RenderBatchList.Count(); ++i )
	{
		RenderBatch_t& batch = m_RenderBatchList[i];

		if ( !bWireframe )
		{
			pRenderContext->BindBatch( m_RenderMeshes[batch.m_iMesh].m_pMesh, batch.m_pMaterial );
			pRenderContext->Bind( batch.m_pMaterial, batch.m_pBindProxy /*proxy*/ );
			pRenderContext->BindLightmapPage( batch.m_nLightmapPage );
		}
		else
		{
			pRenderContext->BindBatch( m_RenderMeshes[batch.m_iMesh].m_pMesh, g_materialWorldWireframe );
			pRenderContext->Bind( g_materialWorldWireframe, nullptr);
		}			
		pRenderContext->DrawBatch( MATERIAL_TRIANGLES, batch.m_nFirstIndex, batch.m_nNumIndex );
	}
	pRenderContext->EndBatch();

	m_RenderBatchList.RemoveAll();
}

//-----------------------------------------------------------------------------
// Draw fragment that are fading - Dynamic BV and dynamic IB
//-----------------------------------------------------------------------------
void COverlayMgr::DrawFadedFragments( IMatRenderContext *pRenderContext, int nSortGroup, bool bWireframe )
{
	IMesh *pMesh = nullptr;
	CMeshBuilder meshBuilder;
	CUtlVectorFixedGrowable<int,256> polyList;
	int nCurrVertexCount = 0;
	int nCurrIndexCount = 0;
	int nMaxIndices = pRenderContext->GetMaxIndicesToRender();
	IMaterial* pCurrentMaterial = nullptr;
	void* pCurrentBindProxy = nullptr;
	bool bLightmappedMaterial = false;
	for ( int iFragment = 0; iFragment < m_RenderFadedFragments.Count(); ++iFragment )
	{
		moverlayfragment_t *pFragment = &m_aFragments[m_RenderFadedFragments[iFragment]];
		moverlay_t *pOverlay = &m_aOverlays[pFragment->m_iOverlay];
		RenderQueueHead_t &renderQueueHead = m_RenderQueue[pFragment->m_nMaterialSortID];
		RenderQueueInfo_t &renderQueue = renderQueueHead.m_Queue[nSortGroup];

		int nMaxVertices = pRenderContext->GetMaxVerticesToRender( !bWireframe ? renderQueueHead.m_pMaterial : g_materialWorldWireframe );

		int nVertCount = pFragment->m_aPrimVerts.Count();
		int nIndexCount = 3 * ( nVertCount - 2 );

		if ( pMesh )
		{					  
			bool bFlush = false;

			// Would this cause an overflow? Flush!
			if ( ( ( nCurrVertexCount + nVertCount ) > nMaxVertices ) ||
				( ( nCurrIndexCount + nIndexCount ) > nMaxIndices ) )
			{
				bFlush = true;
			}

			// Changing material or bind proxy? Flush
			if ( ( renderQueueHead.m_pMaterial != pCurrentMaterial ) || ( pOverlay->m_pBindProxy != pCurrentBindProxy ) )
			{
				bFlush = true;
			}

			if ( bFlush )
			{
				CIndexBuilder &indexBuilder = meshBuilder;
				indexBuilder.FastPolygonList( 0, polyList.Base(), polyList.Count() );
				meshBuilder.End();
				pMesh->Draw();
				pMesh = nullptr;
				polyList.RemoveAll();
				nCurrIndexCount = nCurrVertexCount = 0;
			}
		}

		nCurrVertexCount += nVertCount;
		nCurrIndexCount += nIndexCount;

		const overlayvert_t *pVert = &(pFragment->m_aPrimVerts[0]);
		PREFETCH360(pVert,0);

		int iVert;
		if ( !pMesh )								// have we output any vertices yet? if first verts, init material and meshbuilder
		{
			if ( !bWireframe )
			{
				pRenderContext->Bind( renderQueueHead.m_pMaterial, pOverlay->m_pBindProxy /*proxy*/ );
				pRenderContext->BindLightmapPage( renderQueueHead.m_nLightmapPage );
				bLightmappedMaterial = renderQueueHead.m_pMaterial->GetPropertyFlag( MATERIAL_PROPERTY_NEEDS_LIGHTMAP ) ||
					renderQueueHead.m_pMaterial->GetPropertyFlag( MATERIAL_PROPERTY_NEEDS_BUMPED_LIGHTMAPS );
			}
			pCurrentMaterial = renderQueueHead.m_pMaterial;
			pCurrentBindProxy = pOverlay->m_pBindProxy;
			// Create the mesh/mesh builder.
			pMesh = pRenderContext->GetDynamicMesh();
			meshBuilder.Begin( pMesh, MATERIAL_TRIANGLES, MIN( renderQueue.m_nVertexCount, nMaxVertices ), 
				MIN( renderQueue.m_nIndexCount, nMaxIndices ) );
		}

		if ( bLightmappedMaterial )
		{
			float flOffset = pFragment->decalOffset;

			PREFETCH360(pVert+1,0);
			for ( iVert = 0; iVert < nVertCount; ++iVert, ++pVert )
			{
				PREFETCH360(pVert+2,0);
				meshBuilder.Position3fv( pVert->pos.Base() );
				meshBuilder.Normal3fv( pVert->normal.Base() );
				meshBuilder.Color4Packed( pVert->packedColor );
				meshBuilder.TexCoord2fv( 0, pVert->texCoord[0].Base() );
				meshBuilder.TexCoord2fv( 1, pVert->lightCoord );
				meshBuilder.TexCoord2f( 2, flOffset, 0 );
				meshBuilder.AdvanceVertexF<VTX_HAVEPOS|VTX_HAVENORMAL|VTX_HAVECOLOR, 3>();
			}
		}
		else
		{
			PREFETCH360(pVert+1,0);
			for ( iVert = 0; iVert < nVertCount; ++iVert, ++pVert )
			{
				PREFETCH360(pVert+2,0);
				meshBuilder.Position3fv( pVert->pos.Base() );
				meshBuilder.Normal3fv( pVert->normal.Base() );
				meshBuilder.Color4Packed( pVert->packedColor );
				meshBuilder.TexCoord2fv( 0, pVert->texCoord[0].Base() );
				meshBuilder.TexCoord2fv( 1, pVert->lightCoord );
				meshBuilder.TexCoord2fv( 2, pVert->texCoord[1].Base() );
				meshBuilder.AdvanceVertexF<VTX_HAVEPOS|VTX_HAVENORMAL|VTX_HAVECOLOR, 3>();
			}
		}
		polyList.AddToTail( nVertCount );
	}

	if (pMesh)
	{
		CIndexBuilder &indexBuilder = meshBuilder;
		indexBuilder.FastPolygonList( 0, polyList.Base(), polyList.Count() );
		meshBuilder.End();
		pMesh->Draw();
	}
	pMesh = nullptr;

	m_RenderFadedFragments.RemoveAll();
}

//-----------------------------------------------------------------------------
// Renders all queued up overlays (in batch)
// Static VB and dynamic IB for non fading fragments
// Dynamic VB and dynamic IB for fading fragments
//-----------------------------------------------------------------------------
void COverlayMgr::RenderOverlaysBatch( IMatRenderContext *pRenderContext, int nSortGroup )
{
	bool bWireframeFragments = ( r_overlaywireframe.GetInt() != 0 );
	if ( bWireframeFragments )
	{
		pRenderContext->Bind( g_materialWorldWireframe );
	}

	IMesh *pMesh = nullptr;
	CMeshBuilder meshBuilder;

	// Render sorted by material + lightmap...
	// Render them in order of their m_nRenderOrder parameter (set in the entity).
	int iCurrentRenderOrder = 0;
	int iHighestRenderOrder = 0;
	int nMaxIndices = pRenderContext->GetMaxIndicesToRender();
	int nCurrIndexCount = 0;
	RenderBatch_t currentBatch;
	while ( iCurrentRenderOrder <= iHighestRenderOrder )
	{
		// Draw fragment that are not fading first
		int nNextRenderQueue;
		for( int i = m_nFirstRenderQueue[nSortGroup]; i != RENDER_QUEUE_INVALID; i = nNextRenderQueue )
		{
			RenderQueueHead_t &renderQueueHead = m_RenderQueue[i];
			RenderQueueInfo_t &renderQueue = renderQueueHead.m_Queue[nSortGroup];
			nNextRenderQueue = renderQueue.m_nNextRenderQueue;

			Assert( renderQueue.m_nVertexCount > 0 );

			// Run this list for each bind proxy
			OverlayFragmentHandle_t hStartFragment = renderQueue.m_hFirstFragment;
			while ( hStartFragment != OVERLAY_FRAGMENT_INVALID )
			{
				void *pCurrentBindProxy = m_aOverlays[ m_aFragments[ hStartFragment ].m_iOverlay ].m_pBindProxy;
				int iCurrentMesh = m_aFragments[ hStartFragment ].m_iMesh;

				currentBatch.m_iMesh			= iCurrentMesh;
				currentBatch.m_pMaterial		= renderQueueHead.m_pMaterial;
				currentBatch.m_pBindProxy		= pCurrentBindProxy;
				currentBatch.m_nLightmapPage	= renderQueueHead.m_nLightmapPage;

				// We just need to make sure there's a unique sort ID for that. Then we bind once per queue
				OverlayFragmentHandle_t hFragment = hStartFragment;
				hStartFragment = OVERLAY_FRAGMENT_INVALID;

				for ( ; hFragment != OVERLAY_FRAGMENT_INVALID; hFragment = m_aFragments[hFragment].m_hNextRender )
				{
					moverlayfragment_t *pFragment = &m_aFragments[hFragment];
					moverlay_t *pOverlay = &m_aOverlays[pFragment->m_iOverlay];

					if ( pOverlay->m_pBindProxy != pCurrentBindProxy )
					{
						// This is from a different bind proxy
						if ( hStartFragment == OVERLAY_FRAGMENT_INVALID )
						{
							// Start at the first different bind proxy when we rerun the fragment list
							hStartFragment = hFragment;
						}
						continue;
					}

					// Only render the current render order.
					int iThisOverlayRenderOrder = pOverlay->m_nRenderOrder;
					iHighestRenderOrder = MAX( iThisOverlayRenderOrder, iHighestRenderOrder );
					if ( iThisOverlayRenderOrder != iCurrentRenderOrder )
						continue;

					// Keeps track of "fading fragments" (to be rendered later)
					if ( pFragment->m_bFaded )
					{
						m_RenderFadedFragments.AddToTail( hFragment );
						continue;
					}

					int nVertCount = pFragment->m_aPrimVerts.Count();
					int nIndexCount = 3 * ( nVertCount - 2 );

					if ( pFragment->m_iMesh != iCurrentMesh )
					{
						// Add batch
						if ( currentBatch.m_nNumIndex )
						{
							m_RenderBatchList.AddToTail( currentBatch );
							currentBatch.m_nFirstIndex += currentBatch.m_nNumIndex;
							currentBatch.m_nNumIndex = 0;
						}
						iCurrentMesh = pFragment->m_iMesh;
						currentBatch.m_iMesh = iCurrentMesh;
					}

					if ( pMesh )
					{					  
						// Would this cause an overflow? Flush!
						if ( ( nCurrIndexCount + nIndexCount ) > nMaxIndices )
						{
							meshBuilder.End();
							DrawBatches( pRenderContext, pMesh, bWireframeFragments );
							nCurrIndexCount = 0;
							pMesh = nullptr;
						}
					}

					if ( !pMesh )								// have we output any vertices yet? if first verts, init material and meshbuilder
					{
						if ( !bWireframeFragments )
						{
							pRenderContext->Bind( renderQueueHead.m_pMaterial, pOverlay->m_pBindProxy );
							pRenderContext->BindLightmapPage( renderQueueHead.m_nLightmapPage );
						}
						// Create the mesh/mesh builder.
						pMesh = pRenderContext->GetDynamicMesh(false);
						meshBuilder.Begin( pMesh, MATERIAL_TRIANGLES, 0, nMaxIndices );

						currentBatch.m_nFirstIndex = 0;
						currentBatch.m_nNumIndex = 0;
					}

					// Setup indices.
					int nTriCount = ( nVertCount - 2 );
					CIndexBuilder &indexBuilder = meshBuilder;
					indexBuilder.FastPolygon( pFragment->m_nVertexBufferIndex, nTriCount );

					nCurrIndexCount += nIndexCount;
					currentBatch.m_nNumIndex += nIndexCount;
				}

				// Add batch
				if ( currentBatch.m_nNumIndex )
				{
					m_RenderBatchList.AddToTail( currentBatch );
					currentBatch.m_nFirstIndex += currentBatch.m_nNumIndex;
					currentBatch.m_nNumIndex = 0;
				}

				// Draw fragment that are fading
				if ( m_RenderFadedFragments.Count() > 0 )
				{
					// Render non faded fragment first
					if ( pMesh )
					{					  
						meshBuilder.End();
						DrawBatches( pRenderContext, pMesh, bWireframeFragments );
						nCurrIndexCount = 0;
						pMesh = nullptr;
					}

					DrawFadedFragments( pRenderContext, nSortGroup, bWireframeFragments );
				}
			}
		}

		++iCurrentRenderOrder;
	}

	if ( pMesh )
	{
		meshBuilder.End();

		DrawBatches( pRenderContext, pMesh, bWireframeFragments );
	}

	m_RenderBatchList.RemoveAll();
	m_RenderFadedFragments.RemoveAll();
}

//-----------------------------------------------------------------------------
// Renders all queued up overlays
//-----------------------------------------------------------------------------
void COverlayMgr::RenderOverlays( IMatRenderContext *pRenderContext, int nSortGroup )
{
#ifndef DEDICATED
	VPROF_BUDGET( "COverlayMgr::RenderOverlays", VPROF_BUDGETGROUP_OVERLAYS );

	if ( r_renderoverlayfragment.GetInt() == 0 )
	{
		ClearRenderLists( nSortGroup );
		return;
	}

	if ( r_renderoverlaybatch.GetBool() )
	{
		RenderOverlaysBatch( pRenderContext, nSortGroup );
		return;
	}

	bool bWireframeFragments = ( r_overlaywireframe.GetInt() != 0 );
	if ( bWireframeFragments )
	{
		pRenderContext->Bind( g_materialWorldWireframe );
	}

	// Render sorted by material + lightmap...
	// Render them in order of their m_nRenderOrder parameter (set in the entity).
	int iCurrentRenderOrder = 0;
	int iHighestRenderOrder = 0;
	bool bLightmappedMaterial = false;
	int nMaxIndices = pRenderContext->GetMaxIndicesToRender();
	while ( iCurrentRenderOrder <= iHighestRenderOrder )
	{
		int nNextRenderQueue;
		for( int i = m_nFirstRenderQueue[nSortGroup]; i != RENDER_QUEUE_INVALID; i = nNextRenderQueue )
		{
			RenderQueueHead_t &renderQueueHead = m_RenderQueue[i];
 			RenderQueueInfo_t &renderQueue = renderQueueHead.m_Queue[nSortGroup];
			nNextRenderQueue = renderQueue.m_nNextRenderQueue;

			Assert( renderQueue.m_nVertexCount > 0 );

			int nMaxVertices = pRenderContext->GetMaxVerticesToRender( !bWireframeFragments ? renderQueueHead.m_pMaterial : g_materialWorldWireframe );

			// Run this list for each bind proxy
            OverlayFragmentHandle_t hStartFragment = renderQueue.m_hFirstFragment;
			while ( hStartFragment != OVERLAY_FRAGMENT_INVALID )
			{
				void *pCurrentBindProxy = m_aOverlays[ m_aFragments[ hStartFragment ].m_iOverlay ].m_pBindProxy;

				IMesh* pMesh = nullptr;								// only init when we actually have something
				CMeshBuilder meshBuilder;
				CUtlVectorFixedGrowable<int,256> polyList;
				int nCurrVertexCount = 0;
				int nCurrIndexCount = 0;
				bool bBoundMaterial = false;

				// We just need to make sure there's a unique sort ID for that. Then we bind once per queue
				OverlayFragmentHandle_t hFragment = hStartFragment;
				hStartFragment = OVERLAY_FRAGMENT_INVALID;

				for ( ; hFragment != OVERLAY_FRAGMENT_INVALID; hFragment = m_aFragments[hFragment].m_hNextRender )
				{
					moverlayfragment_t *pFragment = &m_aFragments[hFragment];
					moverlay_t *pOverlay = &m_aOverlays[pFragment->m_iOverlay];

					if ( pOverlay->m_pBindProxy != pCurrentBindProxy )
					{
						// This is from a different bind proxy
						if ( hStartFragment == OVERLAY_FRAGMENT_INVALID )
						{
							// Start at the first different bind proxy when we rerun the fragment list
							hStartFragment = hFragment;
						}
						continue;
					}

					// Only render the current render order.
					int iThisOverlayRenderOrder = pOverlay->m_nRenderOrder;
					iHighestRenderOrder = MAX( iThisOverlayRenderOrder, iHighestRenderOrder );
					if ( iThisOverlayRenderOrder != iCurrentRenderOrder )
						continue;

					int nVertCount = pFragment->m_aPrimVerts.Count();
					int nIndexCount = 3 * ( nVertCount - 2 );

					if ( pMesh )
					{					  
						// Would this cause an overflow? Flush!
						if ( ( ( nCurrVertexCount + nVertCount ) > nMaxVertices ) ||
							 ( ( nCurrIndexCount + nIndexCount ) > nMaxIndices ) )
						{
							CIndexBuilder &indexBuilder = meshBuilder;
							indexBuilder.FastPolygonList( 0, polyList.Base(), polyList.Count() );
							meshBuilder.End();
							pMesh->Draw();
							pMesh = nullptr;
							polyList.RemoveAll();
							nCurrIndexCount = nCurrVertexCount = 0;
						}
					}

					nCurrVertexCount += nVertCount;
					nCurrIndexCount += nIndexCount;

					const overlayvert_t *pVert = &(pFragment->m_aPrimVerts[0]);
					PREFETCH360(pVert,0);
								 
					int iVert;
					if ( !pMesh )								// have we output any vertices yet? if first verts, init material and meshbuilder
					{
						if ( !bWireframeFragments && !bBoundMaterial )
						{
							pRenderContext->Bind( renderQueueHead.m_pMaterial, pOverlay->m_pBindProxy /*proxy*/ );
							pRenderContext->BindLightmapPage( renderQueueHead.m_nLightmapPage );
							bLightmappedMaterial = renderQueueHead.m_pMaterial->GetPropertyFlag( MATERIAL_PROPERTY_NEEDS_LIGHTMAP ) ||
								renderQueueHead.m_pMaterial->GetPropertyFlag( MATERIAL_PROPERTY_NEEDS_BUMPED_LIGHTMAPS );
							bBoundMaterial = true;
						}
						// Create the mesh/mesh builder.
						pMesh = pRenderContext->GetDynamicMesh();
						meshBuilder.Begin( pMesh, MATERIAL_TRIANGLES, MIN( renderQueue.m_nVertexCount, nMaxVertices ), 
							MIN( renderQueue.m_nIndexCount, nMaxIndices ) );
					}

					if ( bLightmappedMaterial )
					{
						float flOffset = pFragment->decalOffset;

						PREFETCH360(pVert+1,0);
						for ( iVert = 0; iVert < nVertCount; ++iVert, ++pVert )
						{
							PREFETCH360(pVert+2,0);
							meshBuilder.Position3fv( pVert->pos.Base() );
							meshBuilder.Normal3fv( pVert->normal.Base() );
							meshBuilder.Color4Packed( pVert->packedColor );
							meshBuilder.TexCoord2fv( 0, pVert->texCoord[0].Base() );
							meshBuilder.TexCoord2fv( 1, pVert->lightCoord );
							meshBuilder.TexCoord2f( 2, flOffset, 0 );
							meshBuilder.AdvanceVertexF<VTX_HAVEPOS|VTX_HAVENORMAL|VTX_HAVECOLOR, 3>();
						}
					}
					else
					{
						PREFETCH360(pVert+1,0);
						for ( iVert = 0; iVert < nVertCount; ++iVert, ++pVert )
						{
							PREFETCH360(pVert+2,0);
							meshBuilder.Position3fv( pVert->pos.Base() );
							meshBuilder.Normal3fv( pVert->normal.Base() );
							meshBuilder.Color4Packed( pVert->packedColor );
							meshBuilder.TexCoord2fv( 0, pVert->texCoord[0].Base() );
							meshBuilder.TexCoord2fv( 1, pVert->lightCoord );
							meshBuilder.TexCoord2fv( 2, pVert->texCoord[1].Base() );
							meshBuilder.AdvanceVertexF<VTX_HAVEPOS|VTX_HAVENORMAL|VTX_HAVECOLOR, 3>();
						}
					}
					polyList.AddToTail( nVertCount );
				}

				if (pMesh)
				{
					CIndexBuilder &indexBuilder = meshBuilder;
					indexBuilder.FastPolygonList( 0, polyList.Base(), polyList.Count() );
					meshBuilder.End();
					pMesh->Draw();
				}
			}
		}
		
		++iCurrentRenderOrder;
	}
#endif
}

void COverlayMgr::RenderAllUnlitOverlays( IMatRenderContext *pRenderContext, int nSortGroup )
{
	for ( int i = 0; i < m_aOverlays.Count(); i++ )
	{
		if ( m_aOverlays[i].m_nFlags & FOVERLAY_DRAW_UNLIT )
		{
			for ( int hFrag = m_aOverlays[i].m_hFirstFragment; hFrag != OVERLAY_FRAGMENT_INVALID; hFrag = m_OverlayFragments.Next( hFrag ) )
			{
				AddFragmentListToRenderList( nSortGroup, m_OverlayFragments[ hFrag ], false );
			}
		}
	}
	RenderOverlays( pRenderContext, nSortGroup );
	ClearRenderLists( nSortGroup );
}


void COverlayMgr::SetOverlayBindProxy( int iOverlayID, void *pBindProxy )
{
	moverlay_t *pOverlay = GetOverlay( iOverlayID );
	if ( pOverlay )
		pOverlay->m_pBindProxy = pBindProxy;
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
bool COverlayMgr::Surf_PreClipFragment( moverlay_t *pOverlay, moverlayfragment_t &overlayFrag, 
								        SurfaceHandle_t surfID, moverlayfragment_t &surfaceFrag )
{
	MEM_ALLOC_CREDIT();
	// Convert the overlay uv points to overlay plane points.
	overlayFrag.m_aPrimVerts.SetCount( 4 );
	for( int iVert = 0; iVert < 4; ++iVert )
	{
		Overlay_OverlayUVToOverlayPlane( pOverlay->m_vecOrigin, pOverlay->m_vecBasis[0], 
			                             pOverlay->m_vecBasis[1], pOverlay->m_vecUVPoints[iVert],
										 overlayFrag.m_aPrimVerts[iVert].pos );
	}

	// Overlay texture coordinates.
	InitTexCoords( pOverlay, overlayFrag );

	// Surface
	int nVertCount = surfaceFrag.m_aPrimVerts.Count();
	for ( int iVert = 0; iVert < nVertCount; ++iVert )
	{
		// Position.
		Overlay_WorldToOverlayPlane( pOverlay->m_vecOrigin, pOverlay->m_vecBasis[2], 
			                         surfaceFrag.m_aPrimVerts[iVert].pos, surfaceFrag.m_aPrimVerts[iVert].pos );
	}

	return true;
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void COverlayMgr::Surf_PostClipFragment( moverlay_t *pOverlay, moverlayfragment_t &overlayFrag,
								         SurfaceHandle_t surfID )
{
#ifndef DEDICATED
	// Get fragment vertex count.
	int nVertCount = overlayFrag.m_aPrimVerts.Count();

	if ( nVertCount == 0 )
		return;

	// Create fragment.
	OverlayFragmentHandle_t hFragment = AddFragmentToFragmentList( nVertCount );
	moverlayfragment_t *pFragment = GetOverlayFragment( hFragment );

	// Get surface context.
	SurfaceCtx_t ctx;
	SurfSetupSurfaceContext( ctx, surfID );	

	pFragment->m_iOverlay = pOverlay->m_nId;
	pFragment->m_SurfId = surfID;
	pFragment->decalOffset = ComputeDecalLightmapOffset( surfID );

	const Vector &vNormal = MSurf_Plane( surfID ).normal;

	moverlayfragment_t origOverlay;
	origOverlay.m_aPrimVerts.SetSize( 4 );
	for ( int iPoint = 0; iPoint < 4; ++iPoint )
	{
		Overlay_OverlayUVToOverlayPlane( pOverlay->m_vecOrigin, pOverlay->m_vecBasis[0], 
			                             pOverlay->m_vecBasis[1], pOverlay->m_vecUVPoints[iPoint],
										 origOverlay.m_aPrimVerts[iPoint].pos );
	}
	InitTexCoords( pOverlay, origOverlay );

	for ( int iVert = 0; iVert < nVertCount; ++iVert )
	{
		Vector2D vecUV;
		PointInQuadToBarycentric( origOverlay.m_aPrimVerts[0].pos, 
								  origOverlay.m_aPrimVerts[3].pos, 
			                      origOverlay.m_aPrimVerts[2].pos, 
								  origOverlay.m_aPrimVerts[1].pos,
								  overlayFrag.m_aPrimVerts[iVert].pos, vecUV );

		Overlay_OverlayPlaneToWorld( pOverlay->m_vecBasis[2], surfID, 
			                         overlayFrag.m_aPrimVerts[iVert].pos,
			                         pFragment->m_aPrimVerts[iVert].pos );

		// Texture coordinates.
		Vector2D vecTexCoord;
		for ( int iTexCoord=0; iTexCoord < NUM_OVERLAY_TEXCOORDS; iTexCoord++ )
		{
			TexCoordInQuadFromBarycentric( origOverlay.m_aPrimVerts[0].texCoord[iTexCoord], origOverlay.m_aPrimVerts[3].texCoord[iTexCoord],
										origOverlay.m_aPrimVerts[2].texCoord[iTexCoord], origOverlay.m_aPrimVerts[1].texCoord[iTexCoord],
										vecUV, vecTexCoord );

			pFragment->m_aPrimVerts[iVert].texCoord[iTexCoord][0] = vecTexCoord.x;
			pFragment->m_aPrimVerts[iVert].texCoord[iTexCoord][1] = vecTexCoord.y;
		}

		// Normals : FIXME this isn't an interpolated normal.
		pFragment->m_aPrimVerts[iVert].normal = vNormal; 
		
		// Lightmap coordinates.
		Vector2D uv;
		SurfComputeLightmapCoordinate( ctx, surfID, pFragment->m_aPrimVerts[iVert].pos, uv );
		pFragment->m_aPrimVerts[iVert].lightCoord[0] = uv.x;
		pFragment->m_aPrimVerts[iVert].lightCoord[1] = uv.y;

		// Push -just- off the surface to avoid z-clipping errors.
		pFragment->m_aPrimVerts[iVert].pos += vNormal * OVERLAY_AVOID_FLICKER_NORMAL_OFFSET;
	}

	// Create the sort ID for this fragment
	const MaterialSystem_SortInfo_t &sortInfo = materialSortInfoArray[MSurf_MaterialSortID( surfID )];
	mtexinfo_t *pTexInfo = &host_state.worldbrush->texinfo[pOverlay->m_nTexInfo];
	pFragment->m_nMaterialSortID = GetMaterialSortID( pTexInfo->material, sortInfo.lightmapPageID );

	// Add to list of fragments for this overlay
	MEM_ALLOC_CREDIT();
	OverlayFragmentList_t i = m_OverlayFragments.Alloc( true );
	m_OverlayFragments[i] = hFragment;
	m_OverlayFragments.LinkBefore( pOverlay->m_hFirstFragment, i );
	pOverlay->m_hFirstFragment = i;

	// Add to list of fragments for this surface
	// NOTE: Store them in *reverse* order so that when we pull them off for
	// rendering, we can do *that* in reverse order too? Reduces the amount of iteration necessary
	// Therefore, we need to add to the head of the list
	m_aFragments.LinkBefore( MSurf_OverlayFragmentList( surfID ), hFragment );
	MSurf_OverlayFragmentList( surfID ) = hFragment;
#endif // !DEDICATED
}


//-----------------------------------------------------------------------------
// Clips an overlay to a surface
//-----------------------------------------------------------------------------
void COverlayMgr::Surf_ClipFragment( moverlay_t *pOverlay, moverlayfragment_t &overlayFrag, 
								     SurfaceHandle_t surfID, moverlayfragment_t &surfaceFrag )
{
	MEM_ALLOC_CREDIT();
	// Create the clip planes.
	CUtlVector<cplane_t> m_ClipPlanes;
	BuildClipPlanes( surfID, surfaceFrag, pOverlay->m_vecBasis[2], m_ClipPlanes );

	// Copy the overlay fragment (initial clipped fragment).
	moverlayfragment_t *pClippedFrag = CopyTempFragment( &overlayFrag );

	for( int iPlane = 0; iPlane < m_ClipPlanes.Count(); ++iPlane )
	{
		moverlayfragment_t *pFront = nullptr, *pBack = nullptr;
		DoClipFragment( pClippedFrag, &m_ClipPlanes[iPlane], &pFront, &pBack );
		DestroyTempFragment( pClippedFrag );
		pClippedFrag = nullptr;
		
		// Keep the backside and continue clipping.
		if ( pBack )
		{
			pClippedFrag = pBack;
		}

		if ( pFront )
		{
			DestroyTempFragment( pFront );
		}
	}

	m_ClipPlanes.Purge();

	// Copy the clipped polygon back to the overlay frag.
	overlayFrag.m_aPrimVerts.RemoveAll();
	if ( pClippedFrag )
	{
		overlayFrag.m_aPrimVerts.SetCount( pClippedFrag->m_aPrimVerts.Count() );
		for ( int iVert = 0; iVert < pClippedFrag->m_aPrimVerts.Count(); ++iVert )
		{
			overlayFrag.m_aPrimVerts[iVert].pos = pClippedFrag->m_aPrimVerts[iVert].pos;
			memcpy( overlayFrag.m_aPrimVerts[iVert].texCoord, pClippedFrag->m_aPrimVerts[iVert].texCoord, sizeof( overlayFrag.m_aPrimVerts[iVert].texCoord ) );
		}
	}

	DestroyTempFragment( pClippedFrag );
}


//-----------------------------------------------------------------------------
// Creates overlay fragments for a particular surface
//-----------------------------------------------------------------------------
void COverlayMgr::Surf_CreateFragments( moverlay_t *pOverlay, SurfaceHandle_t surfID )
{
	moverlayfragment_t overlayFrag, surfaceFrag;

	// The faces get fan tesselated into triangles when rendered - do the same to
	// create the fragments!
	int iFirstVert = MSurf_FirstVertIndex( surfID );
	
	int nSurfTriangleCount = MSurf_VertCount( surfID ) - 2;
	for( int iTri = 0; iTri < nSurfTriangleCount; ++iTri )
	{
		// 3 Points in a triangle.
		surfaceFrag.m_aPrimVerts.SetCount( 3 );
		
		int iVert = host_state.worldbrush->vertindices[(iFirstVert)];
		mvertex_t *pVert = &host_state.worldbrush->vertexes[iVert];
		surfaceFrag.m_aPrimVerts[0].pos = pVert->position;
		
		iVert = host_state.worldbrush->vertindices[(iFirstVert+iTri+1)];
		pVert = &host_state.worldbrush->vertexes[iVert];
		surfaceFrag.m_aPrimVerts[1].pos = pVert->position;
		
		iVert = host_state.worldbrush->vertindices[(iFirstVert+iTri+2)];
		pVert = &host_state.worldbrush->vertexes[iVert];
		surfaceFrag.m_aPrimVerts[2].pos = pVert->position;

		if ( TriangleArea( surfaceFrag.m_aPrimVerts[0].pos, surfaceFrag.m_aPrimVerts[1].pos, surfaceFrag.m_aPrimVerts[2].pos ) > 1.0f )
		{
			if ( Surf_PreClipFragment( pOverlay, overlayFrag, surfID, surfaceFrag ) )
			{
				Surf_ClipFragment( pOverlay, overlayFrag, surfID, surfaceFrag );
				Surf_PostClipFragment( pOverlay, overlayFrag, surfID );
			}
		}
		
		// Clean up!
		surfaceFrag.m_aPrimVerts.RemoveAll();
		overlayFrag.m_aPrimVerts.RemoveAll();
	}
}


//-----------------------------------------------------------------------------
// Creates fragments from the overlays loaded in from file
//-----------------------------------------------------------------------------
void COverlayMgr::CreateFragments( void )
{
	int nOverlayCount = m_aOverlays.Count();
	for ( int iOverlay = 0; iOverlay < nOverlayCount; ++iOverlay )
	{
		moverlay_t *pOverlay = &m_aOverlays.Element( iOverlay );
		int nFaceCount = pOverlay->m_aFaces.Count();
		if ( nFaceCount == 0 )
			continue;

		// Build the overlay basis.
		bool bFlip = ( pOverlay->m_vecUVPoints[3].z == 1.0f );
		pOverlay->m_vecUVPoints[3].z = 0.0f;
		Overlay_BuildBasis( pOverlay->m_vecBasis[2], pOverlay->m_vecBasis[0], pOverlay->m_vecBasis[1], bFlip );

		// Clip against each face in the face list.
		for( int iFace = 0; iFace < nFaceCount; ++iFace )
		{
			SurfaceHandle_t surfID = pOverlay->m_aFaces[iFace];
		
			if ( SurfaceHasDispInfo( surfID ) )
			{
				Disp_CreateFragments( pOverlay, surfID );
			}
			else
			{
				Surf_CreateFragments( pOverlay, surfID );
			}
		}
	}

	// Radius computation (for fades)
	for ( int iOverlay = 0; iOverlay < nOverlayCount; ++iOverlay )
	{
		float flRadiusSq = 0.0f;
		moverlay_t *pOverlay = &m_aOverlays.Element( iOverlay );
		for ( int hFrag = pOverlay->m_hFirstFragment; hFrag != OVERLAY_FRAGMENT_INVALID; hFrag = m_OverlayFragments.Next( hFrag ) )
		{
			int iFrag = m_OverlayFragments[hFrag];
			moverlayfragment_t *pFrag = &m_aFragments[iFrag];
			int nVertCount = pFrag->m_aPrimVerts.Count();
			for ( int iVert = 0; iVert < nVertCount; ++iVert )
			{
				overlayvert_t *pVert = &pFrag->m_aPrimVerts[iVert];
				float flDistSq = pVert->pos.DistToSqr( pOverlay->m_vecOrigin );
				flRadiusSq = MAX( flRadiusSq, flDistSq );
			}
		}
		pOverlay->m_flRadius = sqrt( flRadiusSq );
	}

	// Overlay checking!
	for ( int iOverlay = 0; iOverlay < nOverlayCount; ++iOverlay )
	{
		moverlay_t *pOverlay = &m_aOverlays.Element( iOverlay );
		mtexinfo_t *pTexInfo = &host_state.worldbrush->texinfo[pOverlay->m_nTexInfo];
		const char *pShaderName = pTexInfo->material->GetShaderName();
		if ( !V_stricmp( pShaderName, "UnlitGeneric") || !V_stricmp( pShaderName, "DecalModulate") )
		{
			pOverlay->m_nFlags = FOVERLAY_DRAW_UNLIT;
		}
		else
		{
			pOverlay->m_nFlags = 0;
		}

		int hFrag = pOverlay->m_hFirstFragment;
		while ( hFrag != OVERLAY_FRAGMENT_INVALID )
		{
			int iFrag = m_OverlayFragments[hFrag];
			moverlayfragment_t *pFrag = &m_aFragments[iFrag];

			if ( !IsFinite( pFrag->decalOffset ) )
			{
				Assert( 0 );
				DevMsg( 1, "Bad overlay decal offset - %d with material '%s'\n", iOverlay,
					( pTexInfo && pTexInfo->material ) ? pTexInfo->material->GetName() : ""	);
			}

			int nVertCount = pFrag->m_aPrimVerts.Count();
			for ( int iVert = 0; iVert < nVertCount; ++iVert )
			{
				overlayvert_t *pVert = &pFrag->m_aPrimVerts[iVert];
				if ( !pVert->pos.IsValid() )
				{
					Assert( 0 );
					DevMsg( 1, "Bad overlay vert - %d at (%f, %f, %f) with material '%s'\n", iOverlay,
						pOverlay->m_vecOrigin.x, pOverlay->m_vecOrigin.y, pOverlay->m_vecOrigin.z,
						( pTexInfo && pTexInfo->material ) ? pTexInfo->material->GetName() : ""	);
				}

				if ( !pVert->normal.IsValid() )
				{
					Assert( 0 );
					DevMsg( 1, "Bad overlay normal - %d at (%f, %f, %f) with material '%s'\n", iOverlay,
						pOverlay->m_vecOrigin.x, pOverlay->m_vecOrigin.y, pOverlay->m_vecOrigin.z,
						( pTexInfo && pTexInfo->material ) ? pTexInfo->material->GetName() : ""	);
				}

				if ( !pVert->texCoord[0].IsValid() || !pVert->texCoord[1].IsValid() )
				{
					Assert( 0 );
					DevMsg( 1, "Bad overlay texture coords - %d at (%f, %f, %f) with material '%s'\n", iOverlay,
						pOverlay->m_vecOrigin.x, pOverlay->m_vecOrigin.y, pOverlay->m_vecOrigin.z,
						( pTexInfo && pTexInfo->material ) ? pTexInfo->material->GetName() : ""	);
				}
			}
			hFrag = m_OverlayFragments.Next( hFrag );
		}
	}

	BuildStaticBuffers();
}

//-----------------------------------------------------------------------------
// COverlayMgr::BuildStaticBuffers
//-----------------------------------------------------------------------------
void COverlayMgr::BuildStaticBuffers()
{
	int nOverlayCount = m_aOverlays.Count();

	for ( int iOverlay = 0; iOverlay < nOverlayCount; ++iOverlay )
	{
		moverlay_t *pOverlay = &m_aOverlays.Element( iOverlay );
		int hFrag = pOverlay->m_hFirstFragment;
		while ( hFrag != OVERLAY_FRAGMENT_INVALID )
		{
			int iFrag = m_OverlayFragments[hFrag];
			moverlayfragment_t* pFragment = &m_aFragments[iFrag];

			// Get material associated with the fragment
			IMaterial* pMaterial = m_RenderQueue[pFragment->m_nMaterialSortID].m_pMaterial;

			pFragment->m_iMesh = FindOrdAddMesh( pMaterial, pFragment->m_aPrimVerts.Count() );

			hFrag = m_OverlayFragments.Next( hFrag );
		}
	}

	CMatRenderContextPtr pRenderContext( materials );

	for ( int iMesh = 0; iMesh < m_RenderMeshes.Count(); ++iMesh )
	{
		Assert( m_RenderMeshes[iMesh].m_nVertCount > 0 );
		if ( g_VBAllocTracker )
			g_VBAllocTracker->TrackMeshAllocations( "OverlayMeshCreate" );
		m_RenderMeshes[iMesh].m_pMesh = pRenderContext->CreateStaticMesh( m_RenderMeshes[iMesh].m_VertexFormat, TEXTURE_GROUP_STATIC_VERTEX_BUFFER_OTHER, m_RenderMeshes[iMesh].m_pMaterial );
		int vertBufferIndex = 0;
		// NOTE: Index count is zero because this will be a static vertex buffer!!!
		CMeshBuilder meshBuilder;
		meshBuilder.Begin( m_RenderMeshes[iMesh].m_pMesh, MATERIAL_TRIANGLES, m_RenderMeshes[iMesh].m_nVertCount, 0 );

		for ( int iOverlay = 0; iOverlay < nOverlayCount; ++iOverlay )
		{
			moverlay_t *pOverlay = &m_aOverlays.Element( iOverlay );
			int hFrag = pOverlay->m_hFirstFragment;
			while ( hFrag != OVERLAY_FRAGMENT_INVALID )
			{
				int iFrag = m_OverlayFragments[hFrag];
				moverlayfragment_t* pFragment = &m_aFragments[iFrag];
				int meshId = pFragment->m_iMesh;
				if ( meshId == iMesh )
				{
					IMaterial* pMaterial = m_RenderQueue[pFragment->m_nMaterialSortID].m_pMaterial;
					bool bLightmappedMaterial = pMaterial->GetPropertyFlag( MATERIAL_PROPERTY_NEEDS_LIGHTMAP ) ||
						pMaterial->GetPropertyFlag( MATERIAL_PROPERTY_NEEDS_BUMPED_LIGHTMAPS );
					int nVertCount = pFragment->m_aPrimVerts.Count();
					const overlayvert_t *pVert = &(pFragment->m_aPrimVerts[0]);

					if ( bLightmappedMaterial )
					{
						float flOffset = pFragment->decalOffset;

						for ( int iVert = 0; iVert < nVertCount; ++iVert, ++pVert )
						{
							meshBuilder.Position3fv( pVert->pos.Base() );
							meshBuilder.Normal3fv( pVert->normal.Base() );
							meshBuilder.Color4Packed( 0xFFFFFFFF );
							meshBuilder.TexCoord2fv( 0, pVert->texCoord[0].Base() );
							meshBuilder.TexCoord2fv( 1, pVert->lightCoord );
							meshBuilder.TexCoord2f( 2, flOffset, 0 );
							meshBuilder.AdvanceVertexF<VTX_HAVEPOS|VTX_HAVENORMAL|VTX_HAVECOLOR, 3>();
						}
					}
					else
					{
						for ( int iVert = 0; iVert < nVertCount; ++iVert, ++pVert )
						{
							meshBuilder.Position3fv( pVert->pos.Base() );
							meshBuilder.Normal3fv( pVert->normal.Base() );
							meshBuilder.Color4Packed( 0xFFFFFFFF );
							meshBuilder.TexCoord2fv( 0, pVert->texCoord[0].Base() );
							meshBuilder.TexCoord2fv( 1, pVert->lightCoord );
							meshBuilder.TexCoord2fv( 2, pVert->texCoord[1].Base() );
							meshBuilder.AdvanceVertexF<VTX_HAVEPOS|VTX_HAVENORMAL|VTX_HAVECOLOR, 3>();
						}
					}

					pFragment->m_nVertexBufferIndex = vertBufferIndex;
					vertBufferIndex += nVertCount;
				}

				hFrag = m_OverlayFragments.Next( hFrag );
			}
		}

		meshBuilder.End();
		Assert(vertBufferIndex == m_RenderMeshes[iMesh].m_nVertCount);
		if ( g_VBAllocTracker )
			g_VBAllocTracker->TrackMeshAllocations(nullptr);
	}
}

//-----------------------------------------------------------------------------
// COverlayMgr::DestroyStaticBuffers
//-----------------------------------------------------------------------------
void COverlayMgr::DestroyStaticBuffers()
{
	// Destroy static vertex buffers
	if ( m_RenderMeshes.Count() > 0 )
    {
            CMatRenderContextPtr pRenderContext( materials );
        for ( int i = 0; i < m_RenderMeshes.Count(); i++ )
        {
            pRenderContext->DestroyStaticMesh( m_RenderMeshes[i].m_pMesh );
        }
    }
	m_RenderMeshes.Purge();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
int COverlayMgr::FindOrdAddMesh( IMaterial* pMaterial, int vertCount )
{
	VertexFormat_t format = ( pMaterial->GetVertexFormat() & ~VERTEX_FORMAT_COMPRESSED );
	CMatRenderContextPtr pRenderContext( materials );
	int nMaxVertices = pRenderContext->GetMaxVerticesToRender( pMaterial );

	for ( int i = 0; i < m_RenderMeshes.Count(); i++ )
	{
		if ( m_RenderMeshes[i].m_VertexFormat != format )
			continue;

		if ( m_RenderMeshes[i].m_nVertCount + vertCount > nMaxVertices )
			continue;

		m_RenderMeshes[i].m_nVertCount += vertCount;
		return i;
	}

	int index = m_RenderMeshes.AddToTail();
	m_RenderMeshes[index].m_nVertCount = vertCount;
	m_RenderMeshes[index].m_VertexFormat = format;
	m_RenderMeshes[index].m_pMaterial = pMaterial;

	return index;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void COverlayMgr::UpdateOverlayRenderLevels( int nCPULevel, int nGPULevel )
{
	m_nCPULevel = nCPULevel;
	m_nGPULevel = nGPULevel;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void COverlayMgr::ReSortMaterials( void )
{
#ifndef DEDICATED
	// Clear the old render queue.
	m_RenderQueue.Purge();
	for ( int iSort = 0; iSort < MAX_MAT_SORT_GROUPS; ++iSort )
	{
		m_nFirstRenderQueue[iSort] = RENDER_QUEUE_INVALID;
	}

	// Update all fragments.
	if ( host_state.worldbrush )
	{
		int nOverlayCount = m_aOverlays.Count();
		for (int iOverlay = 0; iOverlay < nOverlayCount; ++iOverlay)
		{
			moverlay_t *pOverlay = &m_aOverlays.Element( iOverlay );
			if (!pOverlay)
				continue;

			mtexinfo_t *pTexInfo = &host_state.worldbrush->texinfo[pOverlay->m_nTexInfo];
			if (!pTexInfo)
				continue;

			int hFrag = pOverlay->m_hFirstFragment;
			while (hFrag != OVERLAY_FRAGMENT_INVALID)
			{
				int iFrag = m_OverlayFragments[hFrag];
				moverlayfragment_t *pFrag = &m_aFragments[iFrag];
				if (pFrag)
				{
					const MaterialSystem_SortInfo_t &sortInfo = materialSortInfoArray[MSurf_MaterialSortID( pFrag->m_SurfId )];
					pFrag->m_nMaterialSortID = GetMaterialSortID( pTexInfo->material, sortInfo.lightmapPageID );

					// Get surface context.
					SurfaceCtx_t ctx;
					SurfSetupSurfaceContext( ctx, pFrag->m_SurfId );

					if ( SurfaceHasDispInfo( pFrag->m_SurfId ) )
					{

						IDispInfo *pIDisp = MSurf_DispInfo( pFrag->m_SurfId );
						CDispInfo *pDisp = static_cast<CDispInfo*>(pIDisp);
						CMeshReader *pReader;

						if ( pDisp )
						{
							 pReader = &pDisp->m_MeshReader;

							 Vector2D lightCoords[4];
							 int nInterval = pDisp->GetSideLength();

							 pReader->TexCoord2f( 0, DISP_LMCOORDS_STAGE, lightCoords[0].x, lightCoords[0].y );
							 pReader->TexCoord2f( nInterval - 1, DISP_LMCOORDS_STAGE, lightCoords[1].x, lightCoords[1].y );
							 pReader->TexCoord2f( (nInterval * nInterval) - 1, DISP_LMCOORDS_STAGE, lightCoords[2].x, lightCoords[2].y );
							 pReader->TexCoord2f( nInterval * (nInterval - 1), DISP_LMCOORDS_STAGE, lightCoords[3].x, lightCoords[3].y );

							 int nVertCount = pFrag->m_aPrimVerts.Count();
							 for ( int iVert = 0; iVert < nVertCount; ++iVert )
							 {
								 // Lightmap coordinates.
								 Vector2D uv;
								 TexCoordInQuadFromBarycentric( lightCoords[0], lightCoords[1], lightCoords[2], lightCoords[3], pFrag->m_aPrimVerts[iVert].tmpDispUV, uv );
								 pFrag->m_aPrimVerts[iVert].lightCoord[0] = uv.x;
								 pFrag->m_aPrimVerts[iVert].lightCoord[1] = uv.y;
							 }
						}
					}
					else
					{
						int nVertCount = pFrag->m_aPrimVerts.Count();
						for ( int iVert = 0; iVert < nVertCount; ++iVert )
						{
							const Vector originalPos = pFrag->m_aPrimVerts[iVert].pos - (pFrag->m_aPrimVerts[iVert].normal * OVERLAY_AVOID_FLICKER_NORMAL_OFFSET);
							// Lightmap coordinates.
							Vector2D uv;
							SurfComputeLightmapCoordinate( ctx, pFrag->m_SurfId, originalPos, uv );

							pFrag->m_aPrimVerts[iVert].lightCoord[0] = uv.x;
							pFrag->m_aPrimVerts[iVert].lightCoord[1] = uv.y;
						}
					}
				}
				hFrag = m_OverlayFragments.Next( hFrag );
			}
		}
	}

	// Rebuild static buffers since geometry has changes
	DestroyStaticBuffers();
	BuildStaticBuffers();

#endif // !DEDICATED
}

//-----------------------------------------------------------------------------
// Loads overlays from the lump
//-----------------------------------------------------------------------------
bool COverlayMgr::LoadOverlays( )
{
	CMapLoadHelper lh( LUMP_OVERLAYS );
	CMapLoadHelper lh2( LUMP_WATEROVERLAYS );
	CMapLoadHelper lhOverlayFades( LUMP_OVERLAY_FADES );
	CMapLoadHelper lhOverlaySystemLevel( LUMP_OVERLAY_SYSTEM_LEVELS );

	doverlay_t *pOverlayIn;
	dwateroverlay_t	*pWaterOverlayIn;

	pOverlayIn = ( doverlay_t* )lh.LumpBase();
	if ( lh.LumpSize() % sizeof( doverlay_t ) )
		return false;

	pWaterOverlayIn = ( dwateroverlay_t* )lh2.LumpBase();
	if ( lh2.LumpSize() % sizeof( dwateroverlay_t ) )
		return false;
		
	// Fade distances are in a parallel lump
	doverlayfade_t *pOverlayFadesIn = (doverlayfade_t *)lhOverlayFades.LumpBase();
	if ( lhOverlayFades.LumpSize() % sizeof( doverlayfade_t ) )
		return false;

	// System levels are in yet another parallel lump
	doverlaysystemlevel_t *pOverlaySystemLevelIn = (doverlaysystemlevel_t *)lhOverlaySystemLevel.LumpBase();
	if ( lhOverlaySystemLevel.LumpSize() % sizeof( doverlaysystemlevel_t ) )
		return false;

	int nOverlayCount = lh.LumpSize() / sizeof( doverlay_t );
	int nWaterOverlayCount = lh2.LumpSize() / sizeof( dwateroverlay_t );

	// Memory allocation!
	m_aOverlays.SetSize( nOverlayCount + nWaterOverlayCount );

	for ( int iOverlay = 0; iOverlay < nOverlayCount; ++iOverlay, ++pOverlayIn )
	{
		moverlay_t *pOverlayOut = &m_aOverlays.Element( iOverlay );

		// Use the system if it exists - not all maps were recompiled.
		if ( pOverlaySystemLevelIn )
		{
			pOverlayOut->m_nMinCPULevel = pOverlaySystemLevelIn->nMinCPULevel;
			pOverlayOut->m_nMaxCPULevel = pOverlaySystemLevelIn->nMaxCPULevel;
			pOverlayOut->m_nMinGPULevel = pOverlaySystemLevelIn->nMinGPULevel;
			pOverlayOut->m_nMaxGPULevel = pOverlaySystemLevelIn->nMaxGPULevel;
			pOverlaySystemLevelIn++;
		}

		pOverlayOut->m_nId = iOverlay;
		pOverlayOut->m_nTexInfo = pOverlayIn->nTexInfo;
		pOverlayOut->m_nRenderOrder = pOverlayIn->GetRenderOrder();
		pOverlayOut->m_nFlags = 0;

		if ( pOverlayOut->m_nRenderOrder >= OVERLAY_NUM_RENDER_ORDERS )
			Error( "COverlayMgr::LoadOverlays: invalid render order (%d) for an overlay.", pOverlayOut->m_nRenderOrder );

		pOverlayOut->m_flU[0] = pOverlayIn->flU[0];
		pOverlayOut->m_flU[1] = pOverlayIn->flU[1];
		pOverlayOut->m_flV[0] = pOverlayIn->flV[0];
		pOverlayOut->m_flV[1] = pOverlayIn->flV[1];

		if ( pOverlayFadesIn )
		{
			pOverlayOut->m_flFadeDistMinSq = pOverlayFadesIn->flFadeDistMinSq;
			pOverlayOut->m_flFadeDistMaxSq = pOverlayFadesIn->flFadeDistMaxSq;
			pOverlayOut->m_flInvFadeRangeSq = 1.0f / ( pOverlayFadesIn->flFadeDistMaxSq - pOverlayFadesIn->flFadeDistMinSq );
			
			pOverlayFadesIn++;
		}
		else
		{
			pOverlayOut->m_flFadeDistMinSq = -1.0f;
			pOverlayOut->m_flFadeDistMaxSq = 0;
			pOverlayOut->m_flInvFadeRangeSq = 1.0f;
		}
		
		VectorCopy( pOverlayIn->vecOrigin, pOverlayOut->m_vecOrigin );

		VectorCopy( pOverlayIn->vecUVPoints[0], pOverlayOut->m_vecUVPoints[0] );
		VectorCopy( pOverlayIn->vecUVPoints[1], pOverlayOut->m_vecUVPoints[1] );
		VectorCopy( pOverlayIn->vecUVPoints[2], pOverlayOut->m_vecUVPoints[2] );
		VectorCopy( pOverlayIn->vecUVPoints[3], pOverlayOut->m_vecUVPoints[3] );

		VectorCopy( pOverlayIn->vecBasisNormal, pOverlayOut->m_vecBasis[2] );

		// Basis U is encoded in the z components of the UVPoints 0, 1, 2
		pOverlayOut->m_vecBasis[0].x = pOverlayOut->m_vecUVPoints[0].z;
		pOverlayOut->m_vecBasis[0].y = pOverlayOut->m_vecUVPoints[1].z;
		pOverlayOut->m_vecBasis[0].z = pOverlayOut->m_vecUVPoints[2].z;

		if ( pOverlayOut->m_vecBasis[0].x == 0.0f && pOverlayOut->m_vecBasis[0].y == 0.0f && pOverlayOut->m_vecBasis[0].z == 0.0f )
		{
			Warning( "Bad overlay basis at (%f %f %f)!\n", pOverlayOut->m_vecOrigin.x, pOverlayOut->m_vecOrigin.y, pOverlayOut->m_vecOrigin.z );
		}

		CrossProduct( pOverlayOut->m_vecBasis[2], pOverlayOut->m_vecBasis[0], pOverlayOut->m_vecBasis[1] );
		VectorNormalize( pOverlayOut->m_vecBasis[1] );

		pOverlayOut->m_vecUVPoints[0].z = 0.0f;
		pOverlayOut->m_vecUVPoints[1].z = 0.0f;
		pOverlayOut->m_vecUVPoints[2].z = 0.0f;

		pOverlayOut->m_aFaces.SetSize( pOverlayIn->GetFaceCount() );
		for( int iFace = 0; iFace < pOverlayIn->GetFaceCount(); ++iFace )
		{
			pOverlayOut->m_aFaces[iFace] = SurfaceHandleFromIndex( pOverlayIn->aFaces[iFace], lh.GetMap() );
		}

		pOverlayOut->m_hFirstFragment = OVERLAY_FRAGMENT_LIST_INVALID;
		pOverlayOut->m_pBindProxy = nullptr;
	}

	for( int iWaterOverlay = 0; iWaterOverlay < nWaterOverlayCount; ++iWaterOverlay, ++pWaterOverlayIn )
	{
		moverlay_t *pOverlayOut = &m_aOverlays.Element( nOverlayCount + iWaterOverlay );

		pOverlayOut->m_nId = nOverlayCount + iWaterOverlay;
		pOverlayOut->m_nTexInfo = pWaterOverlayIn->nTexInfo;
		pOverlayOut->m_nRenderOrder = pWaterOverlayIn->GetRenderOrder();
		pOverlayOut->m_nFlags = 0;
		if ( pOverlayOut->m_nRenderOrder >= OVERLAY_NUM_RENDER_ORDERS )
			Error( "COverlayMgr::LoadOverlays: invalid render order (%d) for an overlay.", pOverlayOut->m_nRenderOrder );

		pOverlayOut->m_flU[0] = pWaterOverlayIn->flU[0];
		pOverlayOut->m_flU[1] = pWaterOverlayIn->flU[1];
		pOverlayOut->m_flV[0] = pWaterOverlayIn->flV[0];
		pOverlayOut->m_flV[1] = pWaterOverlayIn->flV[1];

		VectorCopy( pWaterOverlayIn->vecOrigin, pOverlayOut->m_vecOrigin );

		VectorCopy( pWaterOverlayIn->vecUVPoints[0], pOverlayOut->m_vecUVPoints[0] );
		VectorCopy( pWaterOverlayIn->vecUVPoints[1], pOverlayOut->m_vecUVPoints[1] );
		VectorCopy( pWaterOverlayIn->vecUVPoints[2], pOverlayOut->m_vecUVPoints[2] );
		VectorCopy( pWaterOverlayIn->vecUVPoints[3], pOverlayOut->m_vecUVPoints[3] );

		VectorCopy( pWaterOverlayIn->vecBasisNormal, pOverlayOut->m_vecBasis[2] );

		// Basis U is encoded in the z components of the UVPoints 0, 1, 2
		pOverlayOut->m_vecBasis[0].x = pOverlayOut->m_vecUVPoints[0].z;
		pOverlayOut->m_vecBasis[0].y = pOverlayOut->m_vecUVPoints[1].z;
		pOverlayOut->m_vecBasis[0].z = pOverlayOut->m_vecUVPoints[2].z;

		if ( pOverlayOut->m_vecBasis[0].x == 0.0f && pOverlayOut->m_vecBasis[0].y == 0.0f && pOverlayOut->m_vecBasis[0].z == 0.0f )
		{
			Warning( "Bad overlay basis at (%f %f %f)!\n", pOverlayOut->m_vecOrigin.x, pOverlayOut->m_vecOrigin.y, pOverlayOut->m_vecOrigin.z );
		}

		CrossProduct( pOverlayOut->m_vecBasis[2], pOverlayOut->m_vecBasis[0], pOverlayOut->m_vecBasis[1] );
		VectorNormalize( pOverlayOut->m_vecBasis[1] );

		pOverlayOut->m_vecUVPoints[0].z = 0.0f;
		pOverlayOut->m_vecUVPoints[1].z = 0.0f;
		pOverlayOut->m_vecUVPoints[2].z = 0.0f;

		pOverlayOut->m_aFaces.SetSize( pWaterOverlayIn->GetFaceCount() );
		for( int iFace = 0; iFace < pWaterOverlayIn->GetFaceCount(); ++iFace )
		{
			pOverlayOut->m_aFaces[iFace] = SurfaceHandleFromIndex( pWaterOverlayIn->aFaces[iFace], lh2.GetMap() );
		}

		pOverlayOut->m_hFirstFragment = OVERLAY_FRAGMENT_LIST_INVALID;
		pOverlayOut->m_pBindProxy = nullptr;
	}

	return true;
}


//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void COverlayMgr::Disp_CreateFragments( moverlay_t *pOverlay, SurfaceHandle_t surfID )
{
	OverlayFragmentVector_t aDispFragments;

	if ( Disp_PreClipFragment( pOverlay, aDispFragments, surfID ) )
	{
		IDispInfo *pIDisp = MSurf_DispInfo( surfID );
		CDispInfo *pDisp = static_cast<CDispInfo*>( pIDisp );
		if ( pDisp )
		{
			Disp_ClipFragment( pDisp, aDispFragments );
			Disp_PostClipFragment( pDisp, &pDisp->m_MeshReader, pOverlay, aDispFragments, surfID );
		}
	}

	for ( int i = aDispFragments.Count(); --i >= 0; )
	{
		DestroyTempFragment( aDispFragments[i] );
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool COverlayMgr::Disp_PreClipFragment( moverlay_t *pOverlay, OverlayFragmentVector_t &aDispFragments, 
								        SurfaceHandle_t surfID )
{
	MEM_ALLOC_CREDIT();

	// The faces are not tesselated when they are displaced faces.
	int iFirstVert = MSurf_FirstVertIndex( surfID );

	// Displaced faces are quads.
	moverlayfragment_t surfaceFrag;
	surfaceFrag.m_aPrimVerts.SetCount( 4 );
	for( int iVert = 0; iVert < 4; ++iVert )
	{
		int iVertex = host_state.worldbrush->vertindices[(iFirstVert+iVert)];
		mvertex_t *pVert = &host_state.worldbrush->vertexes[iVertex];
		surfaceFrag.m_aPrimVerts[iVert].pos = pVert->position;
	}

	// Setup the base fragment to be clipped by the base surface previous to the
	// displaced surface.
	moverlayfragment_t overlayFrag;
	if ( !Surf_PreClipFragment( pOverlay, overlayFrag, surfID, surfaceFrag ) )
		return false;

	Surf_ClipFragment( pOverlay, overlayFrag, surfID, surfaceFrag );

	// Get fragment vertex count.
	int nVertCount = overlayFrag.m_aPrimVerts.Count();
	if ( nVertCount == 0 )
		return false;

	// Setup
	moverlayfragment_t *pFragment = CopyTempFragment( &overlayFrag );
	aDispFragments.AddToTail( pFragment );

	IDispInfo *pIDispInfo = MSurf_DispInfo( surfID );
	CDispInfo *pDispInfo = static_cast<CDispInfo*>( pIDispInfo );
	int iPointStart = pDispInfo->m_iPointStart;

	Vector2D vecTmpUV;
	for ( int iVert = 0; iVert < nVertCount; ++iVert )
	{
		PointInQuadToBarycentric( surfaceFrag.m_aPrimVerts[iPointStart].pos,
								  surfaceFrag.m_aPrimVerts[(iPointStart+3)%4].pos,
								  surfaceFrag.m_aPrimVerts[(iPointStart+2)%4].pos,
								  surfaceFrag.m_aPrimVerts[(iPointStart+1)%4].pos,
								  overlayFrag.m_aPrimVerts[iVert].pos,
								  vecTmpUV );
		if ( !vecTmpUV.IsValid() )
		{
			mtexinfo_t *pTexInfo = &host_state.worldbrush->texinfo[pOverlay->m_nTexInfo];
			DevWarning( 1, "Bad overlay geometry at %s with material '%s'\n", VecToString(pOverlay->m_vecOrigin), 
				( pTexInfo && pTexInfo->material ) ? pTexInfo->material->GetName() : ""	);
			return false;
		}

		pFragment->m_aPrimVerts[iVert].pos.x = vecTmpUV.x;
		pFragment->m_aPrimVerts[iVert].pos.y = vecTmpUV.y;
		pFragment->m_aPrimVerts[iVert].pos.z = 0.0f;
	}
	
	return true;
}


//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void COverlayMgr::Disp_PostClipFragment( CDispInfo *pDisp, CMeshReader *pReader, moverlay_t *pOverlay, 
										 OverlayFragmentVector_t &aDispFragments, SurfaceHandle_t surfID )
{
#ifndef DEDICATED
	if ( aDispFragments.Count() == 0 )
		return;

	// Get surface context.
	SurfaceCtx_t ctx;
	SurfSetupSurfaceContext( ctx, surfID );	

	// The faces are not tesselated when they are displaced faces.
	int iFirstVert = MSurf_FirstVertIndex( surfID );

	// Displaced faces are quads.
	moverlayfragment_t surfaceFrag;
	surfaceFrag.m_aPrimVerts.SetCount( 4 );
	for( int iVert = 0; iVert < 4; ++iVert )
	{
		int iVertex = host_state.worldbrush->vertindices[(iFirstVert+iVert)];
		mvertex_t *pVert = &host_state.worldbrush->vertexes[iVertex];
		surfaceFrag.m_aPrimVerts[iVert].pos = pVert->position;
	}

	Vector2D lightCoords[4];
	int nInterval = pDisp->GetSideLength();
	
	pReader->TexCoord2f( 0, DISP_LMCOORDS_STAGE, lightCoords[0].x, lightCoords[0].y );
	pReader->TexCoord2f( nInterval - 1, DISP_LMCOORDS_STAGE, lightCoords[1].x, lightCoords[1].y );
	pReader->TexCoord2f( ( nInterval * nInterval ) - 1, DISP_LMCOORDS_STAGE, lightCoords[2].x, lightCoords[2].y );
	pReader->TexCoord2f( nInterval * ( nInterval - 1 ), DISP_LMCOORDS_STAGE, lightCoords[3].x, lightCoords[3].y );

	// Get the number of displacement fragments.
	int nFragCount = aDispFragments.Count();
	for ( int iFrag = 0; iFrag < nFragCount; ++iFrag )
	{
		moverlayfragment_t *pDispFragment = aDispFragments[iFrag];
		if ( !pDispFragment )
			continue;

		int nVertCount = pDispFragment->m_aPrimVerts.Count();
		if ( nVertCount < 3 )
			continue;

		// Create fragment.
		OverlayFragmentHandle_t hFragment = AddFragmentToFragmentList( nVertCount );
		moverlayfragment_t *pFragment = GetOverlayFragment( hFragment );

		pFragment->m_iOverlay = pOverlay->m_nId;
		pFragment->m_SurfId = surfID;
		pFragment->decalOffset = ComputeDecalLightmapOffset( surfID );
		
		Vector2D vecTmpUV;
		Vector	 vecTmp;
		for ( int iVert = 0; iVert < nVertCount; ++iVert )
		{
			vecTmpUV.x = pDispFragment->m_aPrimVerts[iVert].pos.x;
			vecTmpUV.y = pDispFragment->m_aPrimVerts[iVert].pos.y;

			vecTmpUV.x = clamp( vecTmpUV.x, 0.0f, 1.0f );
			vecTmpUV.y = clamp( vecTmpUV.y, 0.0f, 1.0f );

			Overlay_DispUVToWorld( pDisp, pReader, vecTmpUV, pFragment->m_aPrimVerts[iVert].pos, pFragment->m_aPrimVerts[iVert].normal, surfaceFrag );

			// Texture coordinates.
			pFragment->m_aPrimVerts[iVert].texCoord[0] = pDispFragment->m_aPrimVerts[iVert].texCoord[0];
			pFragment->m_aPrimVerts[iVert].texCoord[1] = pDispFragment->m_aPrimVerts[iVert].texCoord[1];

			// cache for use in ReSortMaterials
			pFragment->m_aPrimVerts[iVert].tmpDispUV = vecTmpUV;

			// Lightmap coordinates.
			Vector2D uv;
			TexCoordInQuadFromBarycentric( lightCoords[0], lightCoords[1], lightCoords[2], lightCoords[3], vecTmpUV, uv ); 
			pFragment->m_aPrimVerts[iVert].lightCoord[0] = uv.x;
			pFragment->m_aPrimVerts[iVert].lightCoord[1] = uv.y;
		}

		// Create the sort ID for this fragment
		const MaterialSystem_SortInfo_t &sortInfo = materialSortInfoArray[MSurf_MaterialSortID( surfID )];
		mtexinfo_t *pTexInfo = &host_state.worldbrush->texinfo[pOverlay->m_nTexInfo];
		pFragment->m_nMaterialSortID = GetMaterialSortID( pTexInfo->material, sortInfo.lightmapPageID );

		// Add to list of fragments for this overlay
		MEM_ALLOC_CREDIT();

		OverlayFragmentList_t i = m_OverlayFragments.Alloc( true );
		m_OverlayFragments[i] = hFragment;
		m_OverlayFragments.LinkBefore( pOverlay->m_hFirstFragment, i );
		pOverlay->m_hFirstFragment = i;

		// Add to list of fragments for this surface
		// NOTE: Store them in *reverse* order so that when we pull them off for
		// rendering, we can do *that* in reverse order too? Reduces the amount of iteration necessary
		// Therefore, we need to add to the head of the list
		m_aFragments.LinkBefore( MSurf_OverlayFragmentList( surfID ), hFragment );
		MSurf_OverlayFragmentList( surfID ) = hFragment;
	}
#endif // !DEDICATED
}


//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void COverlayMgr::Disp_ClipFragment( CDispInfo *pDisp, OverlayFragmentVector_t &aDispFragments )
{
	cplane_t	clipPlane;

	// Cache the displacement interval.
	const CPowerInfo *pPowerInfo = pDisp->GetPowerInfo();
	int nInterval = ( 1 << pPowerInfo->GetPower() );

	// Displacement-space clipping in V.
	clipPlane.normal.Init( 1.0f, 0.0f, 0.0f );
	Disp_DoClip( pDisp, aDispFragments, clipPlane, 1.0f, nInterval, 1, nInterval, 1 );

	// Displacement-space clipping in U.
	clipPlane.normal.Init( 0.0f, 1.0f, 0.0f );
	Disp_DoClip( pDisp, aDispFragments, clipPlane, 1.0f, nInterval, 1, nInterval, 1 );

	// Displacement-space clipping UV from top-left to bottom-right.
	clipPlane.normal.Init( 0.707f, 0.707f, 0.0f );  // 45 degrees
	Disp_DoClip( pDisp, aDispFragments, clipPlane, 0.707f, nInterval, 2, ( nInterval * 2 - 1 ), 2 );

	// Displacement-space clipping UV from bottom-left to top-right.
	clipPlane.normal.Init( -0.707f, 0.707f, 0.0f );  // 135 degrees
	Disp_DoClip( pDisp, aDispFragments, clipPlane, 0.707f, nInterval, -( nInterval - 2 ), ( nInterval - 1 ), 2 );
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void COverlayMgr::Disp_DoClip( CDispInfo *pDisp, OverlayFragmentVector_t &aDispFragments, cplane_t &clipPlane, 
							   float clipDistStart, int nInterval, 
							   int nLoopStart, int nLoopEnd, int nLoopInc )
{
	// Setup interval information.
	float flInterval = static_cast<float>( nInterval );
	float flOOInterval = 1.0f / flInterval;

	// Holds the current set of clipped faces.
	OverlayFragmentVector_t aClippedFragments;

	for ( int iInterval = nLoopStart; iInterval < nLoopEnd; iInterval += nLoopInc )
	{
		// Copy the current list to clipped face list.
		aClippedFragments.CopyArray( aDispFragments.Base(), aDispFragments.Count() );
		aDispFragments.Purge();

		// Clip in V.
		int nFragCount = aClippedFragments.Count();
		for ( int iFrag = 0; iFrag < nFragCount; iFrag++ )
		{
			moverlayfragment_t *pClipFrag = aClippedFragments[iFrag];
			if ( pClipFrag )
			{
				moverlayfragment_t *pFront = nullptr, *pBack = nullptr;

				clipPlane.dist = clipDistStart * ( ( float )iInterval * flOOInterval );
				DoClipFragment( pClipFrag, &clipPlane, &pFront, &pBack );
				DestroyTempFragment( pClipFrag );
				pClipFrag = nullptr;

				if ( pFront )
				{
					aDispFragments.AddToTail( pFront );
				}

				if ( pBack )
				{
					aDispFragments.AddToTail( pBack );
				}
			}
		}
	}
}


//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void COverlayMgr::InitTexCoords( moverlay_t *pOverlay, moverlayfragment_t &overlayFrag )
{
	// Overlay texture coordinates.
	overlayFrag.m_aPrimVerts[0].texCoord[0].Init( pOverlay->m_flU[0], pOverlay->m_flV[0] );
	overlayFrag.m_aPrimVerts[1].texCoord[0].Init( pOverlay->m_flU[0], pOverlay->m_flV[1] );
	overlayFrag.m_aPrimVerts[2].texCoord[0].Init( pOverlay->m_flU[1], pOverlay->m_flV[1] );
	overlayFrag.m_aPrimVerts[3].texCoord[0].Init( pOverlay->m_flU[1], pOverlay->m_flV[0] );

	overlayFrag.m_aPrimVerts[0].texCoord[1].Init( 0, 0 );
	overlayFrag.m_aPrimVerts[1].texCoord[1].Init( 0, 1 );
	overlayFrag.m_aPrimVerts[2].texCoord[1].Init( 1, 1 );
	overlayFrag.m_aPrimVerts[3].texCoord[1].Init( 1, 0 );
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void COverlayMgr::DoClipFragment( moverlayfragment_t *pFragment, cplane_t *pClipPlane,
								  moverlayfragment_t **ppFront, moverlayfragment_t **ppBack )
{
	const float OVERLAY_EPSILON	= 0.0001f;

	// Verify.
	if ( !pFragment )
		return;

	float	flDists[128];
	int		nSides[128];
	int		nSideCounts[3];

	//
	// Determine "sidedness" of all the polygon points.
	//
	nSideCounts[0] = nSideCounts[1] = nSideCounts[2] = 0;
	int iVert = 0;
	for ( ; iVert < pFragment->m_aPrimVerts.Count(); ++iVert )
	{
		flDists[iVert] = pClipPlane->normal.Dot( pFragment->m_aPrimVerts[iVert].pos ) - pClipPlane->dist;

		if ( flDists[iVert] > OVERLAY_EPSILON )
		{
			nSides[iVert] = SIDE_FRONT;
		}
		else if ( flDists[iVert] < -OVERLAY_EPSILON )
		{
			nSides[iVert] = SIDE_BACK;
		}
		else
		{
			nSides[iVert] = SIDE_ON;
		}

		nSideCounts[nSides[iVert]]++;
	}

	// Wrap around (close the polygon).
	nSides[iVert] = nSides[0];
	flDists[iVert] =  flDists[0];

	// All points in back - no split (copy face to back).
	if( !nSideCounts[SIDE_FRONT] )
	{
		*ppBack = CopyTempFragment( pFragment );
		return;
	}

	// All points in front - no split (copy face to front).
	if( !nSideCounts[SIDE_BACK] )
	{
		*ppFront = CopyTempFragment( pFragment );
		return;
	}

	// Build new front and back faces.
	// NOTE: Gotta create them first
	moverlayfragment_t *pFront = CreateTempFragment( 0 );
	moverlayfragment_t *pBack = CreateTempFragment( 0 );
	if ( !pFront || !pBack )
	{
		DestroyTempFragment( pFront );
		DestroyTempFragment( pBack );
		return;
	}

	MEM_ALLOC_CREDIT();

	int nVertCount = pFragment->m_aPrimVerts.Count();
	for ( iVert = 0; iVert < nVertCount; ++iVert )
	{
		// "On" clip plane.
		if ( nSides[iVert] == SIDE_ON )
		{
			pFront->m_aPrimVerts.AddToTail( pFragment->m_aPrimVerts[iVert] );
			pBack->m_aPrimVerts.AddToTail( pFragment->m_aPrimVerts[iVert] );
			continue;
		}

		// "In back" of clip plane.
		if ( nSides[iVert] == SIDE_BACK )
		{
			pBack->m_aPrimVerts.AddToTail( pFragment->m_aPrimVerts[iVert] );
		}

		// "In front" of clip plane.
		if ( nSides[iVert] == SIDE_FRONT )
		{
			pFront->m_aPrimVerts.AddToTail( pFragment->m_aPrimVerts[iVert] );
		}

		if ( nSides[iVert+1] == SIDE_ON || nSides[iVert+1] == nSides[iVert] )
			continue;

		// Split!
		float fraction = flDists[iVert] / ( flDists[iVert] - flDists[iVert+1] );

		overlayvert_t vert;
		vert.pos = pFragment->m_aPrimVerts[iVert].pos + fraction * ( pFragment->m_aPrimVerts[(iVert+1)%nVertCount].pos - 
			                                                         pFragment->m_aPrimVerts[iVert].pos );
		for ( int iTexCoord=0; iTexCoord < NUM_OVERLAY_TEXCOORDS; iTexCoord++ )
		{
			vert.texCoord[iTexCoord][0] = pFragment->m_aPrimVerts[iVert].texCoord[iTexCoord][0] + fraction * ( pFragment->m_aPrimVerts[(iVert+1)%nVertCount].texCoord[iTexCoord][0] - 
				                                                                         pFragment->m_aPrimVerts[iVert].texCoord[iTexCoord][0] );
			vert.texCoord[iTexCoord][1] = pFragment->m_aPrimVerts[iVert].texCoord[iTexCoord][1] + fraction * ( pFragment->m_aPrimVerts[(iVert+1)%nVertCount].texCoord[iTexCoord][1] - 
																						pFragment->m_aPrimVerts[iVert].texCoord[iTexCoord][1] );
		}

		pFront->m_aPrimVerts.AddToTail( vert );
		pBack->m_aPrimVerts.AddToTail( vert );
	}

	*ppFront = pFront;
	*ppBack = pBack;
}


//-----------------------------------------------------------------------------
// Copies a fragment into the main fragment list
//-----------------------------------------------------------------------------
OverlayFragmentHandle_t COverlayMgr::AddFragmentToFragmentList( int nSize )
{
	MEM_ALLOC_CREDIT();

	// Add to list of fragments.
	int iFragment = m_aFragments.Alloc( true );

	moverlayfragment_t &frag = m_aFragments[iFragment];

	frag.m_SurfId = SURFACE_HANDLE_INVALID;
	frag.m_iOverlay = -1;
	frag.m_nRenderFrameID = -1;
	frag.m_nMaterialSortID = 0xFFFF;
	frag.m_hNextRender = OVERLAY_FRAGMENT_INVALID;

	if ( nSize > 0 )
	{
		frag.m_aPrimVerts.SetSize( nSize );
	}

	return iFragment;
}


//-----------------------------------------------------------------------------
// Copies a fragment into the main fragment list
//-----------------------------------------------------------------------------
OverlayFragmentHandle_t COverlayMgr::AddFragmentToFragmentList( moverlayfragment_t *pSrc )
{
	MEM_ALLOC_CREDIT();

	// Add to list of fragments.
	int iFragment = m_aFragments.Alloc( true );

	moverlayfragment_t &frag = m_aFragments[iFragment];

	frag.m_SurfId = pSrc->m_SurfId;
	frag.m_iOverlay = pSrc->m_iOverlay;
	frag.decalOffset = pSrc->decalOffset;
	frag.m_aPrimVerts.CopyArray( pSrc->m_aPrimVerts.Base(), pSrc->m_aPrimVerts.Count() );

	return iFragment;
}


//-----------------------------------------------------------------------------
// Temp fragments for clipping algorithms
//-----------------------------------------------------------------------------
moverlayfragment_t *COverlayMgr::CreateTempFragment( int nSize )
{
	MEM_ALLOC_CREDIT();
	moverlayfragment_t *pDst =  new moverlayfragment_t;
	if ( pDst )
	{
		pDst->m_SurfId = SURFACE_HANDLE_INVALID;
		pDst->m_iOverlay = -1;
		if ( nSize > 0 )
		{
			pDst->m_aPrimVerts.SetSize( nSize );
		}
	}

	return pDst;
}


//-----------------------------------------------------------------------------
// Temp fragments for clipping algorithms
//-----------------------------------------------------------------------------
moverlayfragment_t *COverlayMgr::CopyTempFragment( moverlayfragment_t *pSrc )
{
	MEM_ALLOC_CREDIT();
	moverlayfragment_t *pDst =  new moverlayfragment_t;
	if ( pDst )
	{
		pDst->m_SurfId = pSrc->m_SurfId;
		pDst->m_iOverlay = pSrc->m_iOverlay;
		pDst->m_aPrimVerts.CopyArray( pSrc->m_aPrimVerts.Base(), pSrc->m_aPrimVerts.Count() );
	}

	return pDst;
}

//-----------------------------------------------------------------------------
// Temp fragments for clipping algorithms
//-----------------------------------------------------------------------------
void COverlayMgr::DestroyTempFragment( moverlayfragment_t *pFragment )
{
	if ( pFragment )
	{
		delete pFragment;
	}
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void COverlayMgr::BuildClipPlanes( SurfaceHandle_t surfID, moverlayfragment_t &surfaceFrag, 
								   const Vector &vecBasisNormal, 
								   CUtlVector<cplane_t> &m_ClipPlanes )
{
	int nVertCount = surfaceFrag.m_aPrimVerts.Count();
	for ( int iVert = 0; iVert < nVertCount; ++iVert )
	{
		Vector vecEdge;
		vecEdge = surfaceFrag.m_aPrimVerts[(iVert+1)%nVertCount].pos - surfaceFrag.m_aPrimVerts[iVert].pos;
		VectorNormalize( vecEdge );

		int iPlane = m_ClipPlanes.AddToTail();
		cplane_t *pPlane = &m_ClipPlanes[iPlane];

		pPlane->normal = vecBasisNormal.Cross( vecEdge );
		pPlane->dist = pPlane->normal.Dot( surfaceFrag.m_aPrimVerts[iVert].pos );
		pPlane->type = 3;

		// Check normal facing.
		float flDistance = pPlane->normal.Dot( surfaceFrag.m_aPrimVerts[(iVert+2)%nVertCount].pos ) - pPlane->dist;
		if ( flDistance > 0.0 )
		{
			// Flip
			pPlane->normal.Negate();
			pPlane->dist = -pPlane->dist;
		}
	}
}

//=============================================================================
//
// Code below this line will get moved out into common code!!!!!!!!!!!!!
//

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void Overlay_BuildBasisOrigin( Vector &vecBasisOrigin, SurfaceHandle_t surfID )
{
	cplane_t surfacePlane = MSurf_Plane( surfID );
	VectorNormalize( surfacePlane.normal );

	// Get the distance from entity origin to face plane.
	float flDist = surfacePlane.normal.Dot( vecBasisOrigin ) - surfacePlane.dist;
	
	// Move the basis origin to the position of the entity projected into the face plane.
	vecBasisOrigin = vecBasisOrigin - ( flDist * surfacePlane.normal );
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
bool Overlay_IsBasisFlipped( int *pFlip, int iAxis, int iComponent )
{
	if ( iAxis < 0 || iAxis > 2 || iComponent < 0 || iComponent > 2 )
		return false;
	
	int nValue = ( 1 << iComponent );
	return ( ( pFlip[iAxis] & nValue ) != 0 );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void Overlay_BuildBasis( const Vector &vecBasisNormal, Vector &vecBasisU, Vector &vecBasisV, bool bFlip )
{
	// Verify incoming data.
	Assert( vecBasisNormal.IsValid() );
	if ( !vecBasisNormal.IsValid() )	
		return;

	vecBasisV = vecBasisNormal.Cross( vecBasisU );

	if ( bFlip )
	{
		vecBasisV.Negate();
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void Overlay_TriTLToBR( 
	CDispInfo *pDisp, 
	CMeshReader *pReader,
	Vector &vecWorld,
	Vector &vecWorldNormal,
	float flU, 
	float flV,
	int nWidth, 
	const Vector &vecIntersectPoint )
{
	const float TRIEDGE_EPSILON = 0.000001f;

	int nHeight = nWidth;

	int nSnapU = static_cast<int>( flU );
	int nSnapV = static_cast<int>( flV );
	int nNextU = nSnapU + 1;
	int nNextV = nSnapV + 1;
	if ( nNextU == nWidth)	 { --nNextU; }
	if ( nNextV == nHeight ) { --nNextV; }

	float flFracU = flU - static_cast<float>( nSnapU );
	float flFracV = flV - static_cast<float>( nSnapV );

	Vector vecVerts[3], vecFlatVerts[3], vecNormals[3];
	if( ( flFracU + flFracV ) >= ( 1.0f + TRIEDGE_EPSILON ) )
	{
		int nIndices[3];
		nIndices[0] = nNextV * nWidth + nSnapU;
		nIndices[1] = nNextV * nWidth + nNextU;
		nIndices[2] = nSnapV * nWidth + nNextU;

		for( int iVert = 0; iVert < 3; ++iVert )
		{
			vecVerts[iVert] = GetOverlayPos( pReader, nIndices[iVert] );
			vecFlatVerts[iVert] = pDisp->GetFlatVert( nIndices[iVert] );
			pReader->Normal(  nIndices[iVert], vecNormals[iVert] );
		}

		if ( nSnapU == nNextU )
		{
			if ( nSnapV == nNextV )
			{
				vecWorld = vecVerts[0];
				vecWorldNormal = vecNormals[0];
			}
			else
			{
				float flFrac = ( vecIntersectPoint - vecFlatVerts[0] ).Length() / ( vecFlatVerts[2] - vecFlatVerts[0] ).Length();
				vecWorld = vecVerts[0] + ( flFrac * ( vecVerts[2] - vecVerts[0] ) );
				vecWorldNormal = vecNormals[0] + ( flFrac * ( vecNormals[2] - vecNormals[0] ) );
			}
		}
		else if ( nSnapV == nNextV )
		{
			if ( nSnapU == nNextU )
			{
				vecWorld = vecVerts[0];
				vecWorldNormal = vecNormals[0];
			}
			else
			{
				float flFrac = ( vecIntersectPoint - vecFlatVerts[0] ).Length() / ( vecFlatVerts[2] - vecFlatVerts[0] ).Length();
				vecWorld = vecVerts[0] + ( flFrac * ( vecVerts[2] - vecVerts[0] ) );
				vecWorldNormal = vecNormals[0] + ( flFrac * ( vecNormals[2] - vecNormals[0] ) );
			}
		}
		else
		{
			float flCfs[3];
			if ( CalcBarycentricCooefs( vecFlatVerts[0], vecFlatVerts[1], vecFlatVerts[2], vecIntersectPoint, flCfs[0], flCfs[1], flCfs[2] ) )
			{
				vecWorld = ( vecVerts[0] * flCfs[0] ) + ( vecVerts[1] * flCfs[1] ) + ( vecVerts[2] * flCfs[2] );
				vecWorldNormal = ( vecNormals[0] * flCfs[0] ) + ( vecNormals[1] * flCfs[1] ) + ( vecNormals[2] * flCfs[2] );
			}
			else
			{
				int nIndices[3];
				nIndices[0] = nSnapV * nWidth + nSnapU;
				nIndices[1] = nNextV * nWidth + nSnapU;
				nIndices[2] = nSnapV * nWidth + nNextU;
				
				for( int iVert = 0; iVert < 3; ++iVert )
				{
					vecVerts[iVert] = GetOverlayPos( pReader, nIndices[iVert] );
					vecFlatVerts[iVert] = pDisp->GetFlatVert( nIndices[iVert] );
					pReader->Normal(  nIndices[iVert], vecNormals[iVert] );
				}
				
				if ( nSnapU == nNextU )
				{	
					if ( nSnapV == nNextV )
					{
						vecWorld = vecVerts[0];
						vecWorldNormal = vecNormals[0];
					}
					else
					{
						float flFrac = ( vecIntersectPoint - vecFlatVerts[0] ).Length() / ( vecFlatVerts[1] - vecFlatVerts[0] ).Length();
						vecWorld = vecVerts[0] + ( flFrac * ( vecVerts[1] - vecVerts[0] ) );
						vecWorldNormal = vecNormals[0] + ( flFrac * ( vecNormals[1] - vecNormals[0] ) );
					}
				}
				else if ( nSnapV == nNextV )
				{
					if ( nSnapU == nNextU )
					{
						vecWorld = vecVerts[0];
						vecWorldNormal = vecNormals[0];
					}
					else
					{
						float flFrac = ( vecIntersectPoint - vecFlatVerts[0] ).Length() / ( vecFlatVerts[2] - vecFlatVerts[0] ).Length();
						vecWorld = vecVerts[0] + ( flFrac * ( vecVerts[2] - vecVerts[0] ) );
						vecWorldNormal = vecNormals[0] + ( flFrac * ( vecNormals[2] - vecNormals[0] ) );
					}
				}
				else
				{
					float flCfs[3];
					CalcBarycentricCooefs( vecFlatVerts[0], vecFlatVerts[1], vecFlatVerts[2], vecIntersectPoint, flCfs[0], flCfs[1], flCfs[2] );
					vecWorld = ( vecVerts[0] * flCfs[0] ) + ( vecVerts[1] * flCfs[1] ) + ( vecVerts[2] * flCfs[2] );
					vecWorldNormal = ( vecNormals[0] * flCfs[0] ) + ( vecNormals[1] * flCfs[1] ) + ( vecNormals[2] * flCfs[2] );
				}
			}
		}
	}
	else
	{
		int nIndices[3];
		nIndices[0] = nSnapV * nWidth + nSnapU;
		nIndices[1] = nNextV * nWidth + nSnapU;
		nIndices[2] = nSnapV * nWidth + nNextU;

		for( int iVert = 0; iVert < 3; ++iVert )
		{
			vecVerts[iVert] = GetOverlayPos( pReader, nIndices[iVert] );
			vecFlatVerts[iVert] = pDisp->GetFlatVert( nIndices[iVert] );
			pReader->Normal(  nIndices[iVert], vecNormals[iVert] );
		}

		if ( nSnapU == nNextU )
		{
			if ( nSnapV == nNextV )
			{
				vecWorld = vecVerts[0];
				vecWorldNormal = vecNormals[0];
			}
			else
			{
				float flFrac = ( vecIntersectPoint - vecFlatVerts[0] ).Length() / ( vecFlatVerts[1] - vecFlatVerts[0] ).Length();
				vecWorld = vecVerts[0] + ( flFrac * ( vecVerts[1] - vecVerts[0] ) );
				vecWorldNormal = vecNormals[0] + ( flFrac * ( vecNormals[1] - vecNormals[0] ) );
			}
		}
		else if ( nSnapV == nNextV )
		{
			if ( nSnapU == nNextU )
			{
				vecWorld = vecVerts[0];
				vecWorldNormal = vecNormals[0];
			}
			else
			{
				float flFrac = ( vecIntersectPoint - vecFlatVerts[0] ).Length() / ( vecFlatVerts[2] - vecFlatVerts[0] ).Length();
				vecWorld = vecVerts[0] + ( flFrac * ( vecVerts[2] - vecVerts[0] ) );
				vecWorldNormal = vecNormals[0] + ( flFrac * ( vecNormals[2] - vecNormals[0] ) );
			}
		}
		else
		{
			float flCfs[3];
			if ( CalcBarycentricCooefs( vecFlatVerts[0], vecFlatVerts[1], vecFlatVerts[2], vecIntersectPoint, flCfs[0], flCfs[1], flCfs[2] ) )
			{
				vecWorld = ( vecVerts[0] * flCfs[0] ) + ( vecVerts[1] * flCfs[1] ) + ( vecVerts[2] * flCfs[2] );
				vecWorldNormal = ( vecNormals[0] * flCfs[0] ) + ( vecNormals[1] * flCfs[1] ) + ( vecNormals[2] * flCfs[2] );
			}
			else
			{
				int nIndices[3];
				nIndices[0] = nNextV * nWidth + nSnapU;
				nIndices[1] = nNextV * nWidth + nNextU;
				nIndices[2] = nSnapV * nWidth + nNextU;
				
				for( int iVert = 0; iVert < 3; ++iVert )
				{
					vecVerts[iVert] = GetOverlayPos( pReader, nIndices[iVert] );
					vecFlatVerts[iVert] = pDisp->GetFlatVert( nIndices[iVert] );
					pReader->Normal(  nIndices[iVert], vecNormals[iVert] );
				}
				
				if ( nSnapU == nNextU )
				{
					if ( nSnapV == nNextV )
					{
						vecWorld = vecVerts[0];
						vecWorldNormal = vecNormals[0];
					}
					else
					{
						float flFrac = ( vecIntersectPoint - vecFlatVerts[0] ).Length() / ( vecFlatVerts[2] - vecFlatVerts[0] ).Length();
						vecWorld = vecVerts[0] + ( flFrac * ( vecVerts[2] - vecVerts[0] ) );
						vecWorldNormal = vecNormals[0] + ( flFrac * ( vecNormals[2] - vecNormals[0] ) );
					}
				}
				else if ( nSnapV == nNextV )
				{
					if ( nSnapU == nNextU )
					{
						vecWorld = vecVerts[0];
						vecWorldNormal = vecNormals[0];
					}
					else
					{
						float flFrac = ( vecIntersectPoint - vecFlatVerts[0] ).Length() / ( vecFlatVerts[2] - vecFlatVerts[0] ).Length();
						vecWorld = vecVerts[0] + ( flFrac * ( vecVerts[2] - vecVerts[0] ) );
						vecWorldNormal = vecNormals[0] + ( flFrac * ( vecNormals[2] - vecNormals[0] ) );
					}
				}
				else
				{
					float flCfs[3];
					CalcBarycentricCooefs( vecFlatVerts[0], vecFlatVerts[1], vecFlatVerts[2], vecIntersectPoint, flCfs[0], flCfs[1], flCfs[2] );
					vecWorld = ( vecVerts[0] * flCfs[0] ) + ( vecVerts[1] * flCfs[1] ) + ( vecVerts[2] * flCfs[2] );
					vecWorldNormal = ( vecNormals[0] * flCfs[0] ) + ( vecNormals[1] * flCfs[1] ) + ( vecNormals[2] * flCfs[2] );
				}
			}
		}
	}
	vecWorldNormal.NormalizeInPlace();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void Overlay_TriBLToTR( 
	CDispInfo *pDisp, 
	CMeshReader *pReader,
	Vector &vecWorld, 
	Vector &vecWorldNormal, 
	float flU, 
	float flV,
	int nWidth, 
	const Vector &vecIntersectPoint )
{
	int nHeight = nWidth;

	int nSnapU = static_cast<int>( flU );
	int nSnapV = static_cast<int>( flV );
	int nNextU = nSnapU + 1;
	int nNextV = nSnapV + 1;
	if ( nNextU == nWidth)	 { --nNextU; }
	if ( nNextV == nHeight ) { --nNextV; }

	float flFracU = flU - static_cast<float>( nSnapU );
	float flFracV = flV - static_cast<float>( nSnapV );

	// The fractions are not correct all the time - but they are a good first guess!
	Vector vecVerts[3], vecFlatVerts[3], vecNormals[3];
	if( flFracU < flFracV )
	{
		int nIndices[3];
		nIndices[0] = nSnapV * nWidth + nSnapU;
		nIndices[1] = nNextV * nWidth + nSnapU;
		nIndices[2] = nNextV * nWidth + nNextU;
		
		for( int iVert = 0; iVert < 3; ++iVert )
		{
			vecVerts[iVert] = GetOverlayPos( pReader, nIndices[iVert] );
			vecFlatVerts[iVert] = pDisp->GetFlatVert( nIndices[iVert] );
			pReader->Normal( nIndices[iVert], vecNormals[iVert] );
		}
		
		if ( nSnapU == nNextU )
		{
			if ( nSnapV == nNextV )
			{
				vecWorld = vecVerts[0];
				vecWorldNormal = vecNormals[0];
			}
			else
			{
				float flFrac = ( vecIntersectPoint - vecFlatVerts[0] ).Length() / ( vecFlatVerts[2] - vecFlatVerts[0] ).Length();
				vecWorld = vecVerts[0] + ( flFrac * ( vecVerts[2] - vecVerts[0] ) );
				vecWorldNormal = vecNormals[0] + ( flFrac * ( vecNormals[2] - vecNormals[0] ) );
			}
		}
		else if ( nSnapV == nNextV )
		{
			if ( nSnapU == nNextU )
			{
				vecWorld = vecVerts[0];
				vecWorldNormal = vecNormals[0];
			}
			else
			{
				float flFrac = ( vecIntersectPoint - vecFlatVerts[0] ).Length() / ( vecFlatVerts[2] - vecFlatVerts[0] ).Length();
				vecWorld = vecVerts[0] + ( flFrac * ( vecVerts[2] - vecVerts[0] ) );
				vecWorldNormal = vecNormals[0] + ( flFrac * ( vecNormals[2] - vecNormals[0] ) );
			}
		}
		else
		{
			float flCfs[3];
			if ( CalcBarycentricCooefs( vecFlatVerts[0], vecFlatVerts[1], vecFlatVerts[2], vecIntersectPoint, flCfs[0], flCfs[1], flCfs[2] ) )
			{
				vecWorld = ( vecVerts[0] * flCfs[0] ) + ( vecVerts[1] * flCfs[1] ) + ( vecVerts[2] * flCfs[2] );
				vecWorldNormal = ( vecNormals[0] * flCfs[0] ) + ( vecNormals[1] * flCfs[1] ) + ( vecNormals[2] * flCfs[2] );
			}
			else
			{
				int nIndices[3];
				nIndices[0] = nSnapV * nWidth + nSnapU;
				nIndices[1] = nNextV * nWidth + nNextU;
				nIndices[2] = nSnapV * nWidth + nNextU;
				
				for( int iVert = 0; iVert < 3; ++iVert )
				{
					vecVerts[iVert] = GetOverlayPos( pReader, nIndices[iVert] );
					vecFlatVerts[iVert] = pDisp->GetFlatVert( nIndices[iVert] );
					pReader->Normal( nIndices[iVert], vecNormals[iVert] );
				}
				
				if ( nSnapU == nNextU )
				{
					if ( nSnapV == nNextV )
					{
						vecWorld = vecVerts[0];
						vecWorldNormal = vecNormals[0];
					}
					else
					{
						float flFrac = ( vecIntersectPoint - vecFlatVerts[0] ).Length() / ( vecFlatVerts[1] - vecFlatVerts[0] ).Length();
						vecWorld = vecVerts[0] + ( flFrac * ( vecVerts[1] - vecVerts[0] ) );
						vecWorldNormal = vecNormals[0] + ( flFrac * ( vecNormals[1] - vecNormals[0] ) );
					}
				}
				else if ( nSnapV == nNextV )
				{
					if ( nSnapU == nNextU )
					{
						vecWorld = vecVerts[0];
						vecWorldNormal = vecNormals[0];
					}
					else
					{
						float flFrac = ( vecIntersectPoint - vecFlatVerts[0] ).Length() / ( vecFlatVerts[2] - vecFlatVerts[0] ).Length();
						vecWorld = vecVerts[0] + ( flFrac * ( vecVerts[2] - vecVerts[0] ) );
						vecWorldNormal = vecNormals[0] + ( flFrac * ( vecNormals[2] - vecNormals[0] ) );
					}
				}
				else
				{
					float flCfs[3];
					CalcBarycentricCooefs( vecFlatVerts[0], vecFlatVerts[1], vecFlatVerts[2], vecIntersectPoint, flCfs[0], flCfs[1], flCfs[2] );
					vecWorld = ( vecVerts[0] * flCfs[0] ) + ( vecVerts[1] * flCfs[1] ) + ( vecVerts[2] * flCfs[2] );
					vecWorldNormal = ( vecNormals[0] * flCfs[0] ) + ( vecNormals[1] * flCfs[1] ) + ( vecNormals[2] * flCfs[2] );
				}
			}
		}
	}
	else
	{
		int nIndices[3];
		nIndices[0] = nSnapV * nWidth + nSnapU;
		nIndices[1] = nNextV * nWidth + nNextU;
		nIndices[2] = nSnapV * nWidth + nNextU;
		
		for( int iVert = 0; iVert < 3; ++iVert )
		{
			vecVerts[iVert] = GetOverlayPos( pReader, nIndices[iVert] );
			vecFlatVerts[iVert] = pDisp->GetFlatVert( nIndices[iVert] );
			pReader->Normal( nIndices[iVert], vecNormals[iVert] );
		}
		
		if ( nSnapU == nNextU )
		{
			if ( nSnapV == nNextV )
			{
				vecWorld = vecVerts[0];
				vecWorldNormal = vecNormals[0];
			}
			else
			{
				float flFrac = ( vecIntersectPoint - vecFlatVerts[0] ).Length() / ( vecFlatVerts[1] - vecFlatVerts[0] ).Length();
				vecWorld = vecVerts[0] + ( flFrac * ( vecVerts[1] - vecVerts[0] ) );
				vecWorldNormal = vecNormals[0] + ( flFrac * ( vecNormals[1] - vecNormals[0] ) );
			}
		}
		else if ( nSnapV == nNextV )
		{
			if ( nSnapU == nNextU )
			{
				vecWorld = vecVerts[0];
				vecWorldNormal = vecNormals[0];
			}
			else
			{
				float flFrac = ( vecIntersectPoint - vecFlatVerts[0] ).Length() / ( vecFlatVerts[2] - vecFlatVerts[0] ).Length();
				vecWorld = vecVerts[0] + ( flFrac * ( vecVerts[2] - vecVerts[0] ) );
				vecWorldNormal = vecNormals[0] + ( flFrac * ( vecNormals[2] - vecNormals[0] ) );
			}
		}
		else
		{
			float flCfs[3];
			if ( CalcBarycentricCooefs( vecFlatVerts[0], vecFlatVerts[1], vecFlatVerts[2], vecIntersectPoint, flCfs[0], flCfs[1], flCfs[2] ) )
			{
				vecWorld = ( vecVerts[0] * flCfs[0] ) + ( vecVerts[1] * flCfs[1] ) + ( vecVerts[2] * flCfs[2] );
				vecWorldNormal = ( vecNormals[0] * flCfs[0] ) + ( vecNormals[1] * flCfs[1] ) + ( vecNormals[2] * flCfs[2] );
			}
			else
			{
				int nIndices[3];
				nIndices[0] = nSnapV * nWidth + nSnapU;
				nIndices[1] = nNextV * nWidth + nSnapU;
				nIndices[2] = nNextV * nWidth + nNextU;
				
				for( int iVert = 0; iVert < 3; ++iVert )
				{
					vecVerts[iVert] = GetOverlayPos( pReader, nIndices[iVert] );
					vecFlatVerts[iVert] = pDisp->GetFlatVert( nIndices[iVert] );
					pReader->Normal( nIndices[iVert], vecNormals[iVert] );
				}
				
				if ( nSnapU == nNextU )
				{
					if ( nSnapV == nNextV )
					{
						vecWorld = vecVerts[0];
						vecWorldNormal = vecNormals[0];
					}
					else
					{
						float flFrac = ( vecIntersectPoint - vecFlatVerts[0] ).Length() / ( vecFlatVerts[2] - vecFlatVerts[0] ).Length();
						vecWorld = vecVerts[0] + ( flFrac * ( vecVerts[2] - vecVerts[0] ) );
						vecWorldNormal = vecNormals[0] + ( flFrac * ( vecNormals[2] - vecNormals[0] ) );
					}
				}
				else if ( nSnapV == nNextV )
				{
					if ( nSnapU == nNextU )
					{
						vecWorld = vecVerts[0];
						vecWorldNormal = vecNormals[0];
					}
					else
					{
						float flFrac = ( vecIntersectPoint - vecFlatVerts[0] ).Length() / ( vecFlatVerts[2] - vecFlatVerts[0] ).Length();
						vecWorld = vecVerts[0] + ( flFrac * ( vecVerts[2] - vecVerts[0] ) );
						vecWorldNormal = vecNormals[0] + ( flFrac * ( vecNormals[2] - vecNormals[0] ) );
					}
				}
				else
				{
					float flCfs[3];
					CalcBarycentricCooefs( vecFlatVerts[0], vecFlatVerts[1], vecFlatVerts[2], vecIntersectPoint, flCfs[0], flCfs[1], flCfs[2] );
					vecWorld = ( vecVerts[0] * flCfs[0] ) + ( vecVerts[1] * flCfs[1] ) + ( vecVerts[2] * flCfs[2] );
					vecWorldNormal = ( vecNormals[0] * flCfs[0] ) + ( vecNormals[1] * flCfs[1] ) + ( vecNormals[2] * flCfs[2] );
				}
			}
		}
	}
	vecWorldNormal.NormalizeInPlace();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void Overlay_DispUVToWorld( CDispInfo *pDisp, CMeshReader *pReader, const Vector2D &vecUV, Vector &vecWorld, Vector &vecWorldNormal, moverlayfragment_t &surfaceFrag )
{
	// Get the displacement power.
	const CPowerInfo *pPowerInfo = pDisp->GetPowerInfo();
	int nWidth = ( ( 1 << pPowerInfo->GetPower() ) + 1 );
	int nHeight = nWidth;

	Vector vecIntersectPoint;
	PointInQuadFromBarycentric( surfaceFrag.m_aPrimVerts[(0+pDisp->m_iPointStart)%4].pos, 
		                        surfaceFrag.m_aPrimVerts[(3+pDisp->m_iPointStart)%4].pos,
		                        surfaceFrag.m_aPrimVerts[(2+pDisp->m_iPointStart)%4].pos, 
								surfaceFrag.m_aPrimVerts[(1+pDisp->m_iPointStart)%4].pos,
								vecUV, vecIntersectPoint );

	// Scale the U, V coordinates to the displacement grid size.
	float flU = vecUV.x * static_cast<float>( nWidth - 1.000001f );
	float flV = vecUV.y * static_cast<float>( nHeight - 1.000001f );

	// Find the base U, V.
	int nSnapU = static_cast<int>( flU );
	int nSnapV = static_cast<int>( flV );

	// Use this to get the triangle orientation.
	bool bOdd = ( ( ( nSnapV * nWidth ) + nSnapU ) % 2 == 1 );

	// Top Left to Bottom Right
	if( bOdd )
	{
		Overlay_TriTLToBR( pDisp, pReader, vecWorld, vecWorldNormal, flU, flV, nWidth, vecIntersectPoint );
	}
	// Bottom Left to Top Right
	else
	{
		Overlay_TriBLToTR( pDisp, pReader, vecWorld, vecWorldNormal, flU, flV, nWidth, vecIntersectPoint );
	}
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void Overlay_OverlayUVToOverlayPlane( const Vector &vecBasisOrigin, const Vector &vecBasisU,
									  const Vector &vecBasisV, const Vector &vecUVPoint,
									  Vector &vecPlanePoint )
{
	vecPlanePoint = ( vecUVPoint.x * vecBasisU ) + ( vecUVPoint.y * vecBasisV );
	vecPlanePoint += vecBasisOrigin;
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void Overlay_WorldToOverlayPlane( const Vector &vecBasisOrigin, const Vector &vecBasisNormal,
								  const Vector &vecWorldPoint, Vector &vecPlanePoint )
{
	Vector vecDelta = vecWorldPoint - vecBasisOrigin;
	float flDistance = vecBasisNormal.Dot( vecDelta );
	vecPlanePoint = vecWorldPoint - ( flDistance * vecBasisNormal );
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void Overlay_OverlayPlaneToWorld( const Vector &vecBasisNormal, SurfaceHandle_t surfID,
								  const Vector &vecPlanePoint, Vector &vecWorldPoint )
{
	cplane_t surfacePlane = MSurf_Plane( surfID );
	VectorNormalize( surfacePlane.normal );
	float flDistanceToSurface = surfacePlane.normal.Dot( vecPlanePoint ) - surfacePlane.dist;

	float flDenom = surfacePlane.normal.Dot( vecBasisNormal );
	float flDistance;
	if( flDenom != 0.0f )
	{
		flDistance = ( 1.0f / flDenom ) * flDistanceToSurface;
	}
	else
	{
		flDistance = flDistanceToSurface;
	}

	vecWorldPoint = vecPlanePoint - ( vecBasisNormal * flDistance );
}
