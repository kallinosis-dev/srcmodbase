//====== Copyright � 1996-2005, Valve Corporation, All rights reserved. =======
//
// Purpose: 
//
//=============================================================================

#include "toolutils/basepropertiescontainer.h"
#include "tier1/KeyValues.h"

CBasePropertiesContainer::CBasePropertiesContainer( vgui::Panel *parent, IDmNotify *pNotify, CDmeEditorTypeDictionary *pDict /*=NULL*/ )
	: BaseClass( parent, pNotify, nullptr, true, pDict )
{
	SetDropEnabled( true );
}

bool CBasePropertiesContainer::IsDroppable( CUtlVector< KeyValues * >& msglist )
{
	if ( msglist.Count() != 1 )
		return false;

	KeyValues *data = msglist[ 0 ];
	CDmElement *ptr = g_pDataModel->GetElement( DmElementHandle_t( data->GetInt( "dmeelement", DMELEMENT_HANDLE_INVALID .handle) ) );
	if ( !ptr )
		return false;

	if ( ptr == GetObject() )
		return false;

	return true;
}

void CBasePropertiesContainer::OnPanelDropped( CUtlVector< KeyValues * >& msglist )
{
	if ( msglist.Count() != 1 )
		return;

	KeyValues *data = msglist[ 0 ];
	CDmElement *ptr = g_pDataModel->GetElement( DmElementHandle_t( data->GetInt( "dmeelement", DMELEMENT_HANDLE_INVALID.handle ) ) );
	if ( !ptr )
		return;

	// Already browsing
	if ( ptr == GetObject() )
		return;

	SetObject( ptr );
}