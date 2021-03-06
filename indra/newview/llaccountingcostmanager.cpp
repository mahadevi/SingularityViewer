/** 
 * @file LLAccountingQuotaManager.cpp
 * @ Handles the setting and accessing for costs associated with mesh 
 *
 * $LicenseInfo:firstyear=2001&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2011, Linden Research, Inc.
 * 
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation;
 * version 2.1 of the License only.
 * 
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 * 
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 * 
 * Linden Research, Inc., 945 Battery Street, San Francisco, CA  94111  USA
 * $/LicenseInfo$
 */

#include "llviewerprecompiledheaders.h"
#include "llaccountingcostmanager.h"
#include "llagent.h"
#include "llcurl.h"
#include "llhttpclient.h"

class AIHTTPTimeoutPolicy;
extern AIHTTPTimeoutPolicy accountingCostResponder_timeout;

//===============================================================================
LLAccountingCostManager::LLAccountingCostManager()
{	
}
//===============================================================================
class LLAccountingCostResponder : public LLHTTPClient::ResponderWithResult
{
public:
	LLAccountingCostResponder( const LLSD& objectIDs )
	: mObjectIDs( objectIDs )
	{
	}
		
	void clearPendingRequests ( void )
	{
		for ( LLSD::array_iterator iter = mObjectIDs.beginArray(); iter != mObjectIDs.endArray(); ++iter )
		{
			LLAccountingCostManager::getInstance()->removePendingObject( iter->asUUID() );
		}
	}
	
	/*virtual*/ void error( U32 statusNum, const std::string& reason )
	{
		llwarns	<< "Transport error "<<reason<<llendl;	
		clearPendingRequests();
	}
	
	/*virtual*/ void result( const LLSD& content )
	{
		//Check for error
		if ( !content.isMap() || content.has("error") )
		{
			llwarns	<< "Error on fetched data"<< llendl;
			clearPendingRequests();
		}
		else if (content.has("selected"))
		{
			F32 physicsCost		= 0.0f;
			F32 networkCost		= 0.0f;
			F32 simulationCost	= 0.0f;

			//LLTransactionID transactionID;
				
			//transactionID	= content["selected"][i]["local_id"].asUUID();
			physicsCost		= content["selected"]["physics"].asReal();
			networkCost		= content["selected"]["streaming"].asReal();
			simulationCost	= content["selected"]["simulation"].asReal();
				
			SelectionCost selectionCost( /*transactionID,*/ physicsCost, networkCost, simulationCost );
					
		}

		clearPendingRequests();
	}
	
	/*virtual*/ AIHTTPTimeoutPolicy const& getHTTPTimeoutPolicy(void) const { return accountingCostResponder_timeout; }
	/*virtual*/ char const* getName(void) const { return "LLAccountingCostResponder"; }

private:
	//List of posted objects
	LLSD mObjectIDs;
};

//===============================================================================
void LLAccountingCostManager::fetchCosts( eSelectionType selectionType, const std::string& url )
{
	// Invoking system must have already determined capability availability
	if ( !url.empty() )
	{
		LLSD objectList;
		U32  objectIndex = 0;
		
		IDIt IDIter = mObjectList.begin();
		IDIt IDIterEnd = mObjectList.end();
		
		for ( ; IDIter != IDIterEnd; ++IDIter )
		{
			// Check to see if a request for this object has already been made.
			if ( mPendingObjectQuota.find( *IDIter ) ==	mPendingObjectQuota.end() )
			{
				mPendingObjectQuota.insert( *IDIter );
				objectList[objectIndex++] = *IDIter;
			}
		}
	
		mObjectList.clear();
		
		//Post results
		if ( objectList.size() > 0 )
		{
			std::string keystr;
			if ( selectionType == Roots ) 
			{ 
				keystr="selected_roots"; 
			}
			else
			if ( selectionType == Prims ) 
			{ 
				keystr="selected_prims";
			}
			else 
			{
				llinfos<<"Invalid selection type "<<llendl;
				mObjectList.clear();
				mPendingObjectQuota.clear();
				return;
			}
			
			LLSD dataToPost = LLSD::emptyMap();		
			dataToPost[keystr.c_str()] = objectList;

			LLHTTPClient::post( url, dataToPost, new LLAccountingCostResponder( objectList ));
		}
	}
	else
	{
		//url was empty - warn & continue
		llwarns<<"Supplied url is empty "<<llendl;
		mObjectList.clear();
		mPendingObjectQuota.clear();
	}
}
//===============================================================================
void LLAccountingCostManager::addObject( const LLUUID& objectID )
{
	mObjectList.insert( objectID );
}
//===============================================================================
void LLAccountingCostManager::removePendingObject( const LLUUID& objectID )
{
	mPendingObjectQuota.erase( objectID );
}
//===============================================================================
